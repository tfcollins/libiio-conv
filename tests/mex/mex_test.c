/* SPDX-License-Identifier: MIT */
/* Gateway test harness (mex_test.h).
 *
 * CHECK() of test_util.h counts in a variable of the test's own source file, which this file
 * cannot reach. A failure found here (a shorthand whose command raised or returned another
 * class) is therefore printed, counted here, and turned into exit status 1 by an atexit
 * handler, whatever main() returned. */
#include "mex_test.h"

#include <langinfo.h>
#include <locale.h>
#include <stdarg.h>

#define MT_MAX_ARGS 15

static unsigned int harness_failures;

static void fail_at_exit(void)
{
	if (!harness_failures)
		return;
	fprintf(stderr, "%u harness check(s) failed\n", harness_failures);
	fflush(NULL);
	_exit(1);
}

static void harness_fail(const char *cmd, const char *fmt, ...) __attribute__((format(__printf__, 2, 3)));

static void harness_fail(const char *cmd, const char *fmt, ...)
{
	va_list ap;

	(void)test_failures;        /* the counter of test_util.h in this file: not the test's */
	if (!harness_failures++)
		atexit(fail_at_exit);
	if (harness_failures > 20)
		return;
	fprintf(stderr, "mex_test: %s: ", cmd);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ------------------------------------------------------------------ fixture */

int mt_fixture_setup(struct mt_fixture *fx, const char *src_xml)
{
	int ret;

	memset(fx, 0, sizeof(*fx));
	ret = fixture_setup(src_xml, fx->dir);
	if (ret) {
		fx->dir[0] = '\0';
		return ret;
	}
	snprintf(fx->xml, sizeof(fx->xml), "%s/iio_fixture.xml", fx->dir);
	snprintf(fx->uri, sizeof(fx->uri), "emu:%s", fx->xml);
	snprintf(fx->xml_uri, sizeof(fx->xml_uri), "xml:%s", fx->xml);
	return 0;
}

void mt_fixture_teardown(struct mt_fixture *fx)
{
	if (fx->dir[0])
		fixture_cleanup(fx->dir);
}

/* ------------------------------------------------------------------ argument makers */

static mxArray *scalar(mxClassID cls, const void *value, size_t size)
{
	mxArray *a = mxCreateNumericMatrix(1, 1, cls, mxREAL);

	memcpy(mxGetData(a), value, size);
	return a;
}

mxArray *mt_str(const char *s)
{
	return mxCreateString(s);
}

mxArray *mt_u64(uint64_t v)
{
	return scalar(mxUINT64_CLASS, &v, sizeof(v));
}

mxArray *mt_i64(int64_t v)
{
	return scalar(mxINT64_CLASS, &v, sizeof(v));
}

mxArray *mt_dbl(double v)
{
	return mxCreateDoubleScalar(v);
}

mxArray *mt_bool(bool v)
{
	return mxCreateLogicalScalar(v);
}

mxArray *mt_bytes(const void *p, size_t n)
{
	mxArray *a = mxCreateNumericMatrix(1, n, mxUINT8_CLASS, mxREAL);

	if (n)
		memcpy(mxGetData(a), p, n);
	return a;
}

mxArray *mt_empty(void)
{
	return mxCreateDoubleMatrix(0, 0, mxREAL);
}

/* ------------------------------------------------------------------ calls */

/* prhs = { command name, the nargs arrays of ap }; the count, or -1 after reporting. */
static int collect(const mxArray *prhs[1 + MT_MAX_ARGS], const char *cmd, int nargs, va_list ap)
{
	if (nargs < 0 || nargs > MT_MAX_ARGS) {
		harness_fail(cmd, "%d arguments (at most %d)", nargs, MT_MAX_ARGS);
		return -1;
	}
	for (int i = 0; i < nargs; i++)
		prhs[1 + i] = va_arg(ap, mxArray *);
	prhs[0] = mxCreateString(cmd);
	return 1 + nargs;
}

static void release(const mxArray *prhs[], int nrhs)
{
	for (int i = 0; i < nrhs; i++)
		mxDestroyArray((mxArray *)prhs[i]);
}

static int vcall(int nlhs, mxArray *out[], const char *cmd, int nargs, va_list ap)
{
	const mxArray *prhs[1 + MT_MAX_ARGS];
	int nrhs = collect(prhs, cmd, nargs, ap);
	int ret;

	if (nrhs < 0) {
		for (int k = 0; k < (nlhs > 1 ? nlhs : 1); k++)
			out[k] = NULL;
		return 1;
	}
	ret = mock_call(nlhs, out, nrhs, prhs);
	release(prhs, nrhs);
	return ret;
}

int mt_call(int nlhs, mxArray *out[], const char *cmd, int nargs, ...)
{
	va_list ap;
	int ret;

	va_start(ap, nargs);
	ret = vcall(nlhs, out, cmd, nargs, ap);
	va_end(ap);
	return ret;
}

/* One output of class cls (or anything when cls is mxUNKNOWN_CLASS); NULL after reporting. */
static mxArray *one(mxClassID cls, bool scalar_only, const char *cmd, int nargs, va_list ap)
{
	mxArray *out[1] = { NULL };

	if (vcall(1, out, cmd, nargs, ap)) {
		harness_fail(cmd, "raised %s: %s", mock_err_id(), mock_err_msg());
		return NULL;
	}
	if (cls != mxUNKNOWN_CLASS && (mxGetClassID(out[0]) != cls || mxIsComplex(out[0]) ||
	    (scalar_only && !mxIsScalar(out[0])))) {
		harness_fail(cmd, "returned a %zux%zu %s", mxGetM(out[0]), mxGetN(out[0]),
			mxGetClassName(out[0]));
		mxDestroyArray(out[0]);
		return NULL;
	}
	return out[0];
}

uint64_t mt_u64_of(const char *cmd, int nargs, ...)
{
	uint64_t v = 0;
	mxArray *a;
	va_list ap;

	va_start(ap, nargs);
	a = one(mxUINT64_CLASS, true, cmd, nargs, ap);
	va_end(ap);
	if (a) {
		v = *(const uint64_t *)mxGetData(a);
		mxDestroyArray(a);
	}
	return v;
}

int64_t mt_i64_of(const char *cmd, int nargs, ...)
{
	int64_t v = 0;
	mxArray *a;
	va_list ap;

	va_start(ap, nargs);
	a = one(mxINT64_CLASS, true, cmd, nargs, ap);
	va_end(ap);
	if (a) {
		v = *(const int64_t *)mxGetData(a);
		mxDestroyArray(a);
	}
	return v;
}

double mt_dbl_of(const char *cmd, int nargs, ...)
{
	double v = 0;
	mxArray *a;
	va_list ap;

	va_start(ap, nargs);
	a = one(mxDOUBLE_CLASS, true, cmd, nargs, ap);
	va_end(ap);
	if (a) {
		v = *(const double *)mxGetData(a);
		mxDestroyArray(a);
	}
	return v;
}

bool mt_bool_of(const char *cmd, int nargs, ...)
{
	bool v = false;
	mxArray *a;
	va_list ap;

	va_start(ap, nargs);
	a = one(mxLOGICAL_CLASS, true, cmd, nargs, ap);
	va_end(ap);
	if (a) {
		v = mxIsLogicalScalarTrue(a);
		mxDestroyArray(a);
	}
	return v;
}

const char *mt_str_of(const char *cmd, int nargs, ...)
{
	static char buf[65536];
	const char *ret = NULL;
	mxArray *a;
	va_list ap;
	char *s;

	va_start(ap, nargs);
	a = one(mxUNKNOWN_CLASS, false, cmd, nargs, ap);
	va_end(ap);
	if (!a)
		return NULL;
	if (mxIsChar(a)) {
		s = mxArrayToUTF8String(a);
		if (!s || strlen(s) >= sizeof(buf)) {
			harness_fail(cmd, "returned text that does not fit mt_str_of");
		} else {
			strcpy(buf, s);
			ret = buf;
		}
		mxFree(s);
	} else if (mxGetClassID(a) != mxDOUBLE_CLASS || mxGetM(a) != 0 || mxGetN(a) != 0) {
		/* [] is the gateway's NULL; anything else is not text */
		harness_fail(cmd, "returned a %zux%zu %s", mxGetM(a), mxGetN(a), mxGetClassName(a));
	}
	mxDestroyArray(a);
	return ret;
}

void mt_ok(const char *cmd, int nargs, ...)
{
	mxArray *out[1] = { NULL };
	va_list ap;

	va_start(ap, nargs);
	if (vcall(0, out, cmd, nargs, ap))
		harness_fail(cmd, "raised %s: %s", mock_err_id(), mock_err_msg());
	va_end(ap);
	if (out[0])
		mxDestroyArray(out[0]);
}

bool mt_raises(const char *id, const char *cmd, int nargs, ...)
{
	mxArray *out[1] = { NULL };
	va_list ap;
	int raised;

	va_start(ap, nargs);
	raised = vcall(0, out, cmd, nargs, ap);
	va_end(ap);
	if (out[0])
		mxDestroyArray(out[0]);
	if (raised && !strcmp(mock_err_id(), id))
		return true;
	if (raised)
		fprintf(stderr, "mex_test: %s: raised %s, not %s: %s\n", cmd, mock_err_id(), id, mock_err_msg());
	else
		fprintf(stderr, "mex_test: %s: did not raise %s\n", cmd, id);
	return false;
}

int mt_oom_sweep(int nlhs, const char *cmd, int nargs, ...)
{
	enum { MAX_OUT = 16, MAX_ATTEMPTS = 100000 };
	const mxArray *prhs[1 + MT_MAX_ARGS];
	mxArray *out[MAX_OUT];
	int failed = 0;
	va_list ap;
	int nrhs;

	va_start(ap, nargs);
	nrhs = collect(prhs, cmd, nargs, ap);
	va_end(ap);
	if (nrhs < 0)
		return 0;
	if (nlhs < 0 || nlhs > MAX_OUT) {
		harness_fail(cmd, "%d outputs (at most %d)", nlhs, MAX_OUT);
		release(prhs, nrhs);
		return 0;
	}
	for (;;) {
		int raised;

		mock_fail_alloc((unsigned int)failed + 1);
		raised = mock_call(nlhs, out, nrhs, prhs);
		mock_fail_alloc(0);
		if (!raised || !mock_alloc_failed()) {
			if (!raised)
				for (int k = 0; k < (nlhs > 1 ? nlhs : 1); k++)
					if (out[k])
						mxDestroyArray(out[k]);
			break;
		}
		if (++failed >= MAX_ATTEMPTS) {
			harness_fail(cmd, "still out of memory after %d attempts", failed);
			break;
		}
	}
	release(prhs, nrhs);
	return failed;
}

/* ------------------------------------------------------------------ registry */

void mt_check_arities(const struct gw_group *g, const struct mt_arity *want, size_t n)
{
	if (g->nb_cmds != n)
		harness_fail(g->name, "%zu registered commands, the reference has %zu", g->nb_cmds, n);
	for (size_t i = 0; i < n && i < g->nb_cmds; i++) {
		const struct gw_cmd *cmd = &g->cmds[i];

		if (strcmp(cmd->name, want[i].name)) {
			harness_fail(g->name, "command %zu is %s, the reference has %s", i, cmd->name, want[i].name);
			continue;
		}
		if (cmd->min_rhs != want[i].min_rhs || cmd->max_rhs != want[i].max_rhs ||
		    cmd->max_lhs != want[i].max_lhs)
			harness_fail(cmd->name, "registered with %d, %d, %d; the reference has %d, %d, %d",
				cmd->min_rhs, cmd->max_rhs, cmd->max_lhs,
				want[i].min_rhs, want[i].max_rhs, want[i].max_lhs);
	}
}

/* ------------------------------------------------------------------ objects */

static void context_destroy(void *ptr, void *aux)
{
	(void)aux;
	iio_context_destroy(ptr);
}

gw_id mt_open_context(const char *uri)
{
	static const struct gw_ops ops = { NULL, context_destroy };
	struct iio_context *ctx;
	gw_id id;

	/* The gateway sets its handle table up in its first call (and again in the first call
	 * after MATLAB's exit handler ran): an entry added before that would be forgotten. */
	mt_ok("mex_handle_count", 0);
	ctx = iio_create_context(NULL, uri);
	if (iio_err(ctx))
		return 0;
	id = gw_handle_add(GW_T_CONTEXT, ctx, 0, true, &ops, NULL);
	if (!id) {
		iio_context_destroy(ctx);
		return 0;
	}
	mt_ok("mex_handle_count", 0);    /* the gateway sees the handle and locks the MEX file */
	return id;
}

const char *mt_comma_locale(void)
{
	static const char *const candidates[] = { "de_DE.UTF-8", "en_DK.UTF-8", "fr_FR.UTF-8",
		"nl_NL.UTF-8", "es_ES.UTF-8", "it_IT.UTF-8", "pt_BR.UTF-8", "ru_RU.UTF-8" };
	static const char *found;
	static bool searched;
	const char *opt_out = getenv("IIO_TEST_NO_COMMA_LOCALE");

	if (searched)
		return found;
	searched = true;
	for (size_t i = 0; !found && i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		locale_t l = newlocale(LC_NUMERIC_MASK, candidates[i], (locale_t)0);

		if (l == (locale_t)0)
			continue;
		if (!strcmp(nl_langinfo_l(RADIXCHAR, l), ","))
			found = candidates[i];
		freelocale(l);
	}
	if (found)
		return found;
	if (opt_out && !strcmp(opt_out, "1"))
		fprintf(stderr, "mex_test: IIO_TEST_NO_COMMA_LOCALE=1 and no locale with a decimal comma: "
			"the numeric-locale checks do not run\n");
	else
		harness_fail("mt_comma_locale", "no installed locale has a decimal comma (tried de_DE.UTF-8, "
			"en_DK.UTF-8, ...): run `sudo locale-gen de_DE.UTF-8`, or set "
			"IIO_TEST_NO_COMMA_LOCALE=1 to leave the numeric-locale checks out");
	return NULL;
}

bool mt_comma_begin(void)
{
	const char *name = mt_comma_locale();

	if (!name)
		return false;
	if (uselocale((locale_t)0) != LC_GLOBAL_LOCALE)
		harness_fail("mt_comma_begin", "the thread does not use the global locale");
	if (!setlocale(LC_NUMERIC, name) || strcmp(nl_langinfo(RADIXCHAR), ","))
		harness_fail("mt_comma_begin", "setlocale(LC_NUMERIC, \"%s\") gave no decimal comma", name);
	return true;
}

void mt_comma_end(void)
{
	if (uselocale((locale_t)0) != LC_GLOBAL_LOCALE) {
		harness_fail("mt_comma_end", "a command left the thread in a locale of its own");
		uselocale(LC_GLOBAL_LOCALE);
	}
	if (strcmp(nl_langinfo(RADIXCHAR), ","))
		harness_fail("mt_comma_end", "the decimal separator of the process is no longer \",\"");
	setlocale(LC_NUMERIC, "C");
}
