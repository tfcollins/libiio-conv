/* SPDX-License-Identifier: MIT */
/* Self-test of the mock MATLAB runtime (tests/mxmock): what mock_call() reports, what the
 * mock counts, and which misuse of the MEX API it turns into an abort. */
#include "mex.h"
#include "mxmock.h"
#undef printf
#include "test_util.h"

#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

static int exits;                 /* calls of exit_fn() */
static bool entry_plhs_nonnull;   /* plhs[0] as the "ok" command found it */

static void exit_fn(void)
{
	exits++;
}

/* The command is compared without mxArrayToUTF8String, so that a command makes exactly the
 * mx allocations its description says. */
static bool is_cmd(const mxArray *a, const char *name)
{
	size_t n = strlen(name);
	const mxChar *d;

	if (!mxIsChar(a) || mxGetNumberOfElements(a) != n)
		return false;
	d = mxGetData(a);
	for (size_t i = 0; i < n; i++)
		if (d[i] != (mxChar)name[i])
			return false;
	return true;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	const mxArray *cmd;
	mxArray *a, *b, *c;
	void *p, *q;
	char *s;

	(void)nlhs;
	if (nrhs < 1)
		mexErrMsgIdAndTxt("iio:ENOSYS", "no command");
	cmd = prhs[0];

	if (is_cmd(cmd, "ok")) {
		entry_plhs_nonnull = plhs[0] != NULL;
		plhs[0] = mxCreateDoubleScalar(42.0);
	} else if (is_cmd(cmd, "nothing")) {
		;
	} else if (is_cmd(cmd, "raise")) {
		a = mxCreateDoubleScalar(1.0);
		p = mxMalloc(8);
		(void)a;
		(void)p;
		mexErrMsgIdAndTxt("iio:EINVAL", "bad %d", 7);
	} else if (is_cmd(cmd, "out_then_raise")) {
		plhs[0] = mxCreateDoubleScalar(1.0);
		mexErrMsgIdAndTxt("iio:EBUSY", "busy");
	} else if (is_cmd(cmd, "leak")) {
		plhs[0] = mxCreateDoubleScalar(5.0);
		a = mxCreateDoubleScalar(6.0);
		p = mxMalloc(8);
		(void)a;
		(void)p;
	} else if (is_cmd(cmd, "warn")) {
		mexWarnMsgIdAndTxt("iio:EAGAIN", "careful %d", 3);
		mexPrintf("hello %s %d\n", "mock", 12);
	} else if (is_cmd(cmd, "lock")) {
		mexLock();
	} else if (is_cmd(cmd, "unlock")) {
		mexUnlock();
	} else if (is_cmd(cmd, "atexit")) {
		mexAtExit(exit_fn);
	} else if (is_cmd(cmd, "islocked")) {
		plhs[0] = mxCreateLogicalScalar(mexIsLocked());
	} else if (is_cmd(cmd, "three")) {
		a = mxCreateDoubleScalar(1.0);
		b = mxCreateString("two");
		c = mxCreateNumericMatrix(1, 3, mxUINT64_CLASS, mxREAL);
		mxDestroyArray(a);
		mxDestroyArray(b);
		plhs[0] = c;
	} else if (is_cmd(cmd, "allocs")) {       /* five mx allocations, one of each kind */
		p = mxMalloc(24);
		q = mxCalloc(3, 8);
		a = mxDuplicateArray(prhs[1]);
		s = mxArrayToUTF8String(cmd);
		plhs[0] = mxCreateLogicalScalar(strcmp(s, "allocs") == 0);
		mxFree(s);
		mxDestroyArray(a);
		mxFree(q);
		mxFree(p);
	} else if (is_cmd(cmd, "cell")) {         /* a container with two members is one result */
		c = mxCreateCellMatrix(1, 2);
		mxSetCell(c, 0, mxCreateString("a"));
		mxSetCell(c, 1, mxCreateDoubleScalar(2.0));
		plhs[0] = c;
	} else if (is_cmd(cmd, "bad_id")) {
		mexErrMsgIdAndTxt("iio:22", "identifier component starts with a digit");
	} else if (is_cmd(cmd, "destroy_input")) {
		mxDestroyArray((mxArray *)prhs[0]);
	} else if (is_cmd(cmd, "setcell_input")) {
		c = mxCreateCellMatrix(1, 1);
		mxSetCell(c, 0, (mxArray *)prhs[1]);
		mxDestroyArray(c);
	} else if (is_cmd(cmd, "ret_input")) {
		plhs[0] = (mxArray *)prhs[1];
	} else if (is_cmd(cmd, "ret_twice")) {
		a = mxCreateDoubleScalar(1.0);
		plhs[0] = a;
		plhs[1] = a;
	} else if (is_cmd(cmd, "ret_first_only")) {
		plhs[0] = mxCreateDoubleScalar(1.0);
	} else {
		mexErrMsgIdAndTxt("iio:ENOSYS", "unknown test command");
	}
}

/* mock_call(nlhs, out, {cmd, 3.0}); the inputs are destroyed afterwards. */
static int call(int nlhs, mxArray *out[], const char *cmd)
{
	mxArray *in[2] = { mxCreateString(cmd), mxCreateDoubleScalar(3.0) };
	const mxArray *prhs[2] = { in[0], in[1] };
	int rc = mock_call(nlhs, out, 2, prhs);

	mxDestroyArray(in[0]);
	mxDestroyArray(in[1]);
	return rc;
}

/* How a forked child that runs fn(arg) ended: 0 = it returned, 1 = killed by SIGABRT,
 * 2 = any other abnormal end (a sanitizer report, another signal). */
static int child_fate(void (*fn)(const char *), const char *arg)
{
	int status = 0;
	pid_t pid;

	fflush(NULL);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		struct rlimit no_core = { 0, 0 };

		setrlimit(RLIMIT_CORE, &no_core);
		/* The limit does not stop a core_pattern that is a pipe (apport, systemd-coredump),
		 * and dumping a sanitized process there takes about a second per abort. */
		prctl(PR_SET_DUMPABLE, 0);
		if (!freopen("/dev/null", "w", stderr))
			_exit(0);
		fn(arg);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid)
		return 0;
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)
		return 1;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 2;
}

static bool aborts(void (*fn)(const char *), const char *arg)
{
	return child_fate(fn, arg) == 1;
}

static void in_call(const char *cmd)
{
	mxArray *out[2];

	call(strncmp(cmd, "ret_", 4) == 0 && strcmp(cmd, "ret_input") != 0 ? 2 : 1, out, cmd);
}

static void fine(const char *arg)
{
	(void)arg;
	mxDestroyArray(mxCreateDoubleScalar(1.0));
}

static void raise_outside_call(const char *arg)
{
	(void)arg;
	mexErrMsgIdAndTxt("iio:EINVAL", "nobody is listening");
}

static void two_cells(const char *arg)
{
	mxArray *c1 = mxCreateCellMatrix(1, 1), *c2 = mxCreateCellMatrix(1, 1);
	mxArray *v = mxCreateDoubleScalar(1.0);

	(void)arg;
	mxSetCell(c1, 0, v);
	mxSetCell(c2, 0, v);
}

static void use_destroyed(const char *arg)
{
	mxArray *a = mxCreateDoubleScalar(1.0);

	(void)arg;
	mxDestroyArray(a);
	(void)mxGetM(a);
}

static void destroy_twice(const char *arg)
{
	mxArray *a = mxCreateDoubleScalar(1.0);

	(void)arg;
	mxDestroyArray(a);
	mxDestroyArray(a);
}

static void null_array(const char *arg)
{
	(void)arg;
	(void)mxIsChar(NULL);
}

static void foreign_array(const char *arg)
{
	static uint64_t not_an_array[32];

	(void)arg;
	(void)mxGetM((const mxArray *)not_an_array);
}

static void cell_beyond_end(const char *arg)
{
	mxArray *c = mxCreateCellMatrix(1, 2);

	(void)arg;
	(void)mxGetCell(c, 2);
}

static void bad_field_number(const char *arg)
{
	const char *names[] = { "a", "b" };
	mxArray *s = mxCreateStructMatrix(1, 1, 2, names);

	(void)arg;
	mxSetFieldByNumber(s, 0, 2, mxCreateDoubleScalar(1.0));
}

static void bad_field_name(const char *arg)
{
	const char *names[] = { "in_voltage0-voltage1_scale" };

	(void)arg;
	(void)mxCreateStructMatrix(1, 1, 1, names);
}

static void logicals_of_double(const char *arg)
{
	mxArray *a = mxCreateDoubleScalar(1.0);

	(void)arg;
	(void)mxGetLogicals(a);
}

static void destroy_member(const char *arg)
{
	mxArray *c = mxCreateCellMatrix(1, 1);
	mxArray *v = mxCreateDoubleScalar(1.0);

	(void)arg;
	mxSetCell(c, 0, v);
	mxDestroyArray(v);
}

static void free_malloc_pointer(const char *arg)
{
	(void)arg;
	mxFree(malloc(8));
}

static void free_twice(const char *arg)
{
	void *p = mxMalloc(8);

	(void)arg;
	mxFree(p);
	mxFree(p);
}

static void free_array_data(const char *arg)
{
	mxArray *a = mxCreateDoubleScalar(1.0);

	(void)arg;
	mxFree(mxGetData(a));
}

static void alloc_outside_call(const char *arg)
{
	(void)arg;
	mock_fail_alloc(1);
	mxDestroyArray(mxCreateDoubleScalar(1.0));   /* outside a call: not counted, see below */
	_exit(0);
}

static void test_ok_raise_leak(void)
{
	mxArray *out[2] = { NULL, NULL };
	size_t live = mock_live(), freed = mock_autofreed();

	/* "ok": a normal return; the output belongs to the test. */
	CHECK(call(1, out, "ok") == 0);
	CHECK(strcmp(mock_err_id(), "") == 0);
	CHECK(strcmp(mock_err_msg(), "") == 0);
	CHECK(!mock_alloc_failed());
	CHECK(entry_plhs_nonnull);
	CHECK(out[0] != NULL && mxGetScalar(out[0]) == 42.0);
	CHECK(mock_live() == live + 1);
	CHECK(mock_autofreed() == freed);
	mxDestroyArray(out[0]);
	CHECK(mock_live() == live);

	/* nlhs == 0: output 0 is optional, and is taken when set. */
	out[0] = NULL;
	CHECK(call(0, out, "ok") == 0);
	CHECK(out[0] != NULL && mxGetScalar(out[0]) == 42.0);
	mxDestroyArray(out[0]);
	out[0] = (mxArray *)&live;
	CHECK(call(0, out, "nothing") == 0);
	CHECK(out[0] == NULL);
	CHECK(mock_live() == live);
	CHECK(mock_autofreed() == freed);

	/* "raise": one array and one mxMalloc block are left to MATLAB's cleanup. */
	out[0] = (mxArray *)&live;
	CHECK(call(1, out, "raise") == 1);
	CHECK(strcmp(mock_err_id(), "iio:EINVAL") == 0);
	CHECK(strcmp(mock_err_msg(), "bad 7") == 0);
	CHECK(!mock_alloc_failed());
	CHECK(out[0] == NULL);
	CHECK(mock_autofreed() == freed + 2);
	CHECK(mock_live() == live);
	freed = mock_autofreed();

	/* An output that was already set when the error is raised is cleaned up too. */
	out[0] = out[1] = (mxArray *)&live;
	CHECK(call(2, out, "out_then_raise") == 1);
	CHECK(strcmp(mock_err_id(), "iio:EBUSY") == 0);
	CHECK(strcmp(mock_err_msg(), "busy") == 0);
	CHECK(out[0] == NULL && out[1] == NULL);
	CHECK(mock_autofreed() == freed + 1);
	CHECK(mock_live() == live);
	freed = mock_autofreed();

	/* The error state does not outlive the next call. */
	CHECK(call(1, out, "ok") == 0);
	CHECK(strcmp(mock_err_id(), "") == 0);
	CHECK(strcmp(mock_err_msg(), "") == 0);
	mxDestroyArray(out[0]);

	/* "leak": a normal return that leaves one array and one block behind. */
	CHECK(call(1, out, "leak") == 0);
	CHECK(mock_autofreed() == freed + 2);
	CHECK(out[0] != NULL && mxGetScalar(out[0]) == 5.0);
	CHECK(mock_live() == live + 1);
	mxDestroyArray(out[0]);
	CHECK(mock_live() == live);
	freed = mock_autofreed();

	/* A container counts once; destroying it destroys its members. */
	CHECK(call(1, out, "cell") == 0);
	CHECK(mock_autofreed() == freed);
	CHECK(mock_live() == live + 1);
	CHECK(mxIsCell(out[0]) && mxIsChar(mxGetCell(out[0], 0)));
	CHECK(mxGetScalar(mxGetCell(out[0], 1)) == 2.0);
	mxDestroyArray(out[0]);
	CHECK(mock_live() == live);

	/* An unknown command of this test's own mexFunction. */
	CHECK(call(1, out, "no_such") == 1);
	CHECK(strcmp(mock_err_id(), "iio:ENOSYS") == 0);
	CHECK(mock_autofreed() == freed);
}

static void test_warn_print(void)
{
	mxArray *out[1];
	unsigned int warns = mock_warn_count();

	CHECK(strcmp(mock_warn_id(), "") == 0);
	CHECK(strcmp(mock_output(), "") == 0);
	CHECK(call(0, out, "warn") == 0);
	CHECK(mock_warn_count() == warns + 1);
	CHECK(strcmp(mock_warn_id(), "iio:EAGAIN") == 0);
	CHECK(strstr(mock_output(), "hello mock 12\n") != NULL);
	CHECK(strcmp(mock_err_id(), "") == 0);       /* a warning is not an error */
	CHECK(call(0, out, "warn") == 0);
	CHECK(mock_warn_count() == warns + 2);
	CHECK(strcmp(mock_output(), "hello mock 12\nhello mock 12\n") == 0);
}

static void test_lock_exit(void)
{
	mxArray *out[1];

	exits = 0;
	CHECK(!mock_is_locked());
	CHECK(call(0, out, "atexit") == 0);
	CHECK(call(0, out, "lock") == 0);
	CHECK(call(0, out, "lock") == 0);
	CHECK(mock_is_locked());
	CHECK(call(1, out, "islocked") == 0);
	CHECK(mxIsLogicalScalarTrue(out[0]));
	mxDestroyArray(out[0]);

	mock_clear_mex();                 /* locked: nothing happens */
	CHECK(exits == 0);
	CHECK(mock_is_locked());

	CHECK(call(0, out, "unlock") == 0);
	CHECK(mock_is_locked());          /* locked twice, unlocked once */
	mock_clear_mex();
	CHECK(exits == 0);
	CHECK(call(0, out, "unlock") == 0);
	CHECK(!mock_is_locked());
	CHECK(call(1, out, "islocked") == 0);
	CHECK(!mxIsLogicalScalarTrue(out[0]));
	mxDestroyArray(out[0]);
	CHECK(exits == 0);

	mock_clear_mex();                 /* unlocked: the exit function runs once */
	CHECK(exits == 1);
	mock_clear_mex();                 /* and was forgotten */
	CHECK(exits == 1);

	/* MATLAB exit runs the exit function even when the MEX is locked. */
	CHECK(call(0, out, "atexit") == 0);
	CHECK(call(0, out, "lock") == 0);
	CHECK(mock_is_locked());
	mock_exit_matlab();
	CHECK(exits == 2);
	CHECK(!mock_is_locked());
	mock_exit_matlab();               /* forgotten here too */
	CHECK(exits == 2);
	mock_clear_mex();
	CHECK(exits == 2);

	/* mexLock, mexUnlock and mexAtExit also work outside a call (a test may call gateway
	 * functions directly). */
	mexAtExit(exit_fn);
	mexLock();
	CHECK(mock_is_locked() && mexIsLocked());
	mexUnlock();
	CHECK(!mock_is_locked());
	mock_exit_matlab();
	CHECK(exits == 3);
}

static void test_data(void)
{
	mxArray *a = mxCreateUninitNumericMatrix(3, 5, mxINT16_CLASS, mxCOMPLEX);
	const uint8_t *d = mxGetData(a);
	bool all = true;

	for (size_t i = 0; i < 3 * 5 * 4; i++)
		all = all && d[i] == 0xA5;
	CHECK(all);
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(3, 5, mxINT16_CLASS, mxCOMPLEX);
	d = mxGetData(a);
	all = true;
	for (size_t i = 0; i < 3 * 5 * 4; i++)
		all = all && d[i] == 0;
	CHECK(all);
	mxDestroyArray(a);
}

static void test_aborts(void)
{
	CHECK(child_fate(fine, NULL) == 0);          /* the helper itself */

	CHECK(aborts(in_call, "bad_id"));
	CHECK(aborts(raise_outside_call, NULL));
	CHECK(aborts(in_call, "destroy_input"));
	CHECK(aborts(two_cells, NULL));
	CHECK(aborts(in_call, "setcell_input"));
	CHECK(aborts(use_destroyed, NULL));
	CHECK(aborts(destroy_twice, NULL));
	CHECK(aborts(null_array, NULL));
	CHECK(aborts(foreign_array, NULL));
	CHECK(aborts(in_call, "ret_input"));
	CHECK(aborts(in_call, "ret_twice"));
	CHECK(aborts(in_call, "ret_first_only"));
	CHECK(aborts(cell_beyond_end, NULL));
	CHECK(aborts(bad_field_number, NULL));
	CHECK(aborts(bad_field_name, NULL));
	CHECK(aborts(logicals_of_double, NULL));
	CHECK(aborts(destroy_member, NULL));
	/* Under ASan the sanitizer may get there first: any abnormal end counts. */
	CHECK(child_fate(free_malloc_pointer, NULL) != 0);
	CHECK(child_fate(free_twice, NULL) != 0);
	CHECK(child_fate(free_array_data, NULL) != 0);
}

static void test_alloc_failure(void)
{
	mxArray *out[1] = { NULL };
	size_t live = mock_live(), freed = mock_autofreed();
	int failed = 0;

	mock_fail_alloc(2);
	CHECK(call(1, out, "three") == 1);
	CHECK(strcmp(mock_err_id(), "MATLAB:nomem") == 0);
	CHECK(mock_alloc_failed());
	CHECK(out[0] == NULL);
	CHECK(mock_autofreed() == freed);
	CHECK(mock_live() == live);

	CHECK(call(1, out, "three") == 0);            /* it fired once */
	CHECK(!mock_alloc_failed());
	CHECK(strcmp(mock_err_id(), "") == 0);
	CHECK(mxGetN(out[0]) == 3);
	CHECK(mock_autofreed() == freed);
	CHECK(mock_live() == live + 1);
	mxDestroyArray(out[0]);

	/* Allocations made outside a call (a test building its inputs) are not counted. */
	mock_fail_alloc(1);
	CHECK(child_fate(alloc_outside_call, NULL) == 0);
	CHECK(call(1, out, "three") == 1);
	CHECK(mock_alloc_failed());

	/* 0 switches it off. */
	mock_fail_alloc(3);
	mock_fail_alloc(0);
	CHECK(call(1, out, "three") == 0);
	CHECK(!mock_alloc_failed());
	mxDestroyArray(out[0]);

	/* Every kind of mx allocation counts: mxMalloc, mxCalloc, mxDuplicateArray,
	 * mxArrayToUTF8String, mxCreate*. This is the sweep the gateway tests run. */
	for (unsigned int nth = 1; nth < 20; nth++) {
		mock_fail_alloc(nth);
		if (call(1, out, "allocs") == 0)
			break;
		CHECK(mock_alloc_failed());
		CHECK(out[0] == NULL);
		failed++;
	}
	CHECK(failed == 5);
	CHECK(!mock_alloc_failed());
	CHECK(out[0] != NULL && mxIsLogicalScalarTrue(out[0]));
	mxDestroyArray(out[0]);
	mock_fail_alloc(0);
	CHECK(mock_autofreed() == freed);
	CHECK(mock_live() == live);
}

static void test_special_arrays(void)
{
	size_t live = mock_live();
	mxArray *a = mock_string_object();

	CHECK(!mxIsChar(a));
	CHECK(!mxIsNumeric(a));
	CHECK(strcmp(mxGetClassName(a), "string") == 0);
	CHECK(mxIsClass(a, "string"));
	CHECK(mxArrayToUTF8String(a) == NULL);
	CHECK(mxGetData(a) == NULL);
	CHECK(mock_live() == live + 1);
	mxDestroyArray(a);

	a = mock_sparse_double(2, 2);
	CHECK(mxIsSparse(a));
	CHECK(mxGetClassID(a) == mxDOUBLE_CLASS);
	CHECK(mxGetM(a) == 2 && mxGetN(a) == 2);
	mxDestroyArray(a);

	a = mock_nd_array(mxINT16_CLASS, mxCOMPLEX, 4, 1, 2);
	CHECK(mxGetNumberOfDimensions(a) == 3);
	CHECK(mxGetM(a) == 4);
	CHECK(mxGetN(a) == 2);
	CHECK(mxGetNumberOfElements(a) == 8);
	CHECK(mxIsComplex(a));
	CHECK(mxGetClassID(a) == mxINT16_CLASS);
	CHECK(mxGetData(a) != NULL);
	CHECK(((int16_t *)mxGetData(a))[15] == 0);
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(4, 2, mxINT16_CLASS, mxCOMPLEX);
	CHECK(mxGetNumberOfDimensions(a) == 2);
	mxDestroyArray(a);
	CHECK(mock_live() == live);
}

int main(void)
{
	test_warn_print();
	test_ok_raise_leak();
	test_lock_exit();
	test_data();
	test_aborts();
	test_alloc_failure();
	test_special_arrays();
	CHECK(mock_live() == 0);
	TEST_MAIN_RETURN();
}
