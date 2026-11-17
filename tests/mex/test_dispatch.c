/* SPDX-License-Identifier: MIT */
/* The dispatcher of the iio_mex gateway: command lookup, argument and output counts, the
 * single error exit and its identifiers, the lock, "result first" and parking under an
 * allocation failure, mex_shutdown and MATLAB exit. */
#include "mex_test.h"

#include <limits.h>

/* ------------------------------------------------------------------ the local command group */

static char oplog[256];          /* what the ops and the park free function did, in order */

static void log_add(const char *s)
{
	strncat(oplog, s, sizeof(oplog) - strlen(oplog) - 1);
}

static char objs[16];            /* the "libiio objects" of t_add: object i is &objs[i] */
static unsigned int n_objs;

static void obj_pre(void *ptr, void *aux)
{
	char s[8];

	(void)aux;
	snprintf(s, sizeof(s), "p%d", (int)((char *)ptr - objs));
	log_add(s);
}

static void obj_destroy(void *ptr, void *aux)
{
	char s[8];

	(void)aux;
	snprintf(s, sizeof(s), "d%d", (int)((char *)ptr - objs));
	log_add(s);
}

static int t_ok(struct gw_call *c)
{
	return gw_ret_double(c, 0, 42);
}

static int t_two(struct gw_call *c)
{
	int ret = gw_ret_double(c, 0, 1);

	return ret ? ret : gw_ret_double(c, 1, 2);
}

static int t_errno(struct gw_call *c)
{
	long long v;
	int ret = gw_arg_int(c, 0, INT_MIN, INT_MAX, &v);

	return ret ? ret : -(int)v;
}

static int t_msg(struct gw_call *c)
{
	return gw_fail(c, -EIO, "disk %d", 3);
}

/* A message longer than err_msg: cut to 255 characters, never past the end. */
static int t_msg_long(struct gw_call *c)
{
	return gw_fail(c, -EIO, "%0300d", 7);
}

static int t_out_then_fail(struct gw_call *c)
{
	int ret = gw_ret_double(c, 0, 5);

	return ret ? ret : -EBUSY;
}

static int t_forget(struct gw_call *c)
{
	(void)c;
	return 0;
}

/* Result first: the id array exists before the object does. */
static int t_add(struct gw_call *c)
{
	static const struct gw_ops ops = { obj_pre, obj_destroy };
	gw_id id;
	int ret = gw_ret_id(c, 0, 0);

	if (ret)
		return ret;
	if (n_objs >= sizeof(objs))
		return -ENOSPC;
	id = gw_handle_add(GW_T_CONTEXT, &objs[n_objs], 0, true, &ops, NULL);
	if (!id)
		return -ENOMEM;
	n_objs++;
	gw_set_id(c, 0, id);
	return 0;
}

static int park_rets[8];         /* what each gw_park of the last t_park returned */
static char park_log_after[8][sizeof(oplog)];   /* the log right after each of them */
static bool park_size_ok = true;

static void park_free(void *p, size_t n)
{
	if (n != 7)
		park_size_ok = false;
	free(p);
	log_add("f");
}

/* t_park([count = 1]): parks count malloc blocks, then creates its result (count 6: fails). */
static int t_park(struct gw_call *c)
{
	long long count = 1;
	int ret;

	if (gw_arg_given(c, 0)) {
		ret = gw_arg_int(c, 0, 1, 8, &count);
		if (ret)
			return ret;
	}
	for (int i = 0; i < count; i++) {
		void *p = malloc(7);

		if (!p)
			return -ENOMEM;
		park_rets[i] = gw_park(p, 7, park_free);
		snprintf(park_log_after[i], sizeof(park_log_after[i]), "%s", oplog);
	}
	if (count == 6)
		return -EDOM;        /* a command that fails after it parked something */
	return gw_ret_double(c, 0, (double)count);
}

static int t_del(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_CONTEXT);
}

static int t_range(struct gw_call *c)
{
	return gw_ret_double(c, 0, c->nrhs);
}

/* The longest name the dispatcher looks up: 63 characters. */
#define NAME63 "t_a_command_name_of_exactly_sixty_three_characters_0123456789_a"

static const struct gw_cmd local_cmds[] = {
	GW_XCMD("t_ok", t_ok, 0, 0, 1),
	GW_XCMD("t", t_ok, 0, 0, 1),      /* what a column 't_ok' would read as, taken by its columns */
	GW_XCMD("t_two", t_two, 0, 0, 2),
	GW_XCMD("t_errno", t_errno, 1, 1, 0),
	GW_XCMD("t_msg", t_msg, 0, 0, 0),
	GW_XCMD("t_msg_long", t_msg_long, 0, 0, 0),
	GW_XCMD("t_out_then_fail", t_out_then_fail, 0, 0, 1),
	GW_XCMD("t_forget", t_forget, 0, 0, 1),
	GW_XCMD("t_add", t_add, 0, 0, 1),
	GW_XCMD("t_park", t_park, 0, 1, 1),
	GW_XCMD("t_del", t_del, 1, 1, 0),
	GW_XCMD("t_range", t_range, 1, 2, 1),
	GW_XCMD(NAME63, t_ok, 0, 0, 1),
};

static const struct gw_group gw_group_local = GW_GROUP("local", local_cmds);

MT_GROUPS(&gw_group_local);

/* ------------------------------------------------------------------ helpers */

static bool is_double(const mxArray *a, double want)
{
	return a && mxGetClassID(a) == mxDOUBLE_CLASS && mxIsScalar(a) && !mxIsComplex(a) &&
		mxGetScalar(a) == want;
}

static bool raised(const char *id, const char *msg)
{
	return !strcmp(mock_err_id(), id) && (!msg || !strcmp(mock_err_msg(), msg));
}

static void test_lookup(void)
{
	mxArray *out[2] = { NULL, NULL };
	const mxArray *in[1];
	mxArray *d = mxCreateDoubleScalar(1);
	mxArray *cell = mxCreateCellMatrix(1, 1);
	mxArray *text = mxCreateNumericMatrix(2, 2, mxUINT8_CLASS, mxREAL);
	mxArray *obj = mock_string_object();

	/* no argument at all */
	CHECK(mock_call(0, out, 0, NULL) == 1);
	CHECK(raised("iio:ENOSYS", NULL));
	CHECK(out[0] == NULL);

	/* a first argument that is not text */
	in[0] = d;
	CHECK(mock_call(0, out, 1, in) == 1);
	CHECK(raised("iio:ENOSYS", NULL));
	in[0] = cell;
	CHECK(mock_call(0, out, 1, in) == 1);
	CHECK(raised("iio:ENOSYS", NULL));
	in[0] = text;
	CHECK(mock_call(0, out, 1, in) == 1);
	CHECK(raised("iio:ENOSYS", NULL));
	in[0] = obj;
	CHECK(mock_call(1, out, 1, in) == 1);
	CHECK(raised("iio:ENOSYS", NULL));
	mxDestroyArray(d);
	mxDestroyArray(cell);
	mxDestroyArray(text);
	mxDestroyArray(obj);

	/* text that is not a row is not a name, even where its first row or its elements
	 * spell a registered one */
	CHECK(mt_dbl_of("t", 0) == 42);
	{
		mxArray *shapes[] = {
			mock_char_array(4, 1, 1, "t_ok"), mock_char_array(1, 2, 2, "t_ok"),
			mock_char_array(2, 2, 1, "t_ok"), mock_char_array(16, 1, 1, "mex_handle_count"),
			mock_char_array(1, 8, 2, "mex_handle_count"), mock_char_array(0, 4, 1, ""),
		};

		for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
			in[0] = shapes[i];
			CHECK(mock_call(1, out, 1, in) == 1);
			CHECK(raised("iio:ENOSYS", NULL));
			CHECK(strstr(mock_err_msg(), "iio_mex: the first argument must be a command name") != NULL);
			mxDestroyArray(shapes[i]);
		}
	}

	/* names that are not registered: the match is exact and case-sensitive */
	CHECK(mt_raises("iio:ENOSYS", "no_such", 0));
	CHECK(!strcmp(mock_err_msg(), "no_such: unknown command"));
	CHECK(mt_raises("iio:ENOSYS", "T_OK", 0));
	CHECK(mt_raises("iio:ENOSYS", "t_o", 0));
	CHECK(mt_raises("iio:ENOSYS", "t_ok ", 0));
	CHECK(mt_raises("iio:ENOSYS", "", 0));
	CHECK(mt_raises("iio:ENOSYS", "t_\xC2\xB5", 0));
	CHECK(mt_raises("iio:ENOSYS",
		"a_command_name_that_is_far_longer_than_any_registered_one_and_than_the_"
		"buffer_the_dispatcher_copies_names_into_0123456789_0123456789_0123456789", 0));
	CHECK(mt_raises("iio:ENOSYS", "no_such", 3, mt_dbl(1), mt_str("x"), mt_empty()));
	/* a longer name is not cut down to a registered one */
	CHECK(strlen(NAME63) == 63);
	CHECK(mt_dbl_of(NAME63, 0) == 42);
	CHECK(mt_raises("iio:ENOSYS", NAME63 "c", 0));
	CHECK(strstr(mock_err_msg(), ": unknown command") != NULL);
	CHECK(mt_raises("iio:ENOSYS", NAME63 "cdefghijklmnopqrstuvwxyz", 0));
	CHECK(mock_live() == 0);
}

static void test_counts(void)
{
	mxArray *out[3] = { NULL, NULL, NULL };
	size_t live;

	CHECK(mt_raises("iio:EINVAL", "t_range", 0));
	CHECK(strstr(mock_err_msg(), "t_range: ") == mock_err_msg());
	CHECK(mt_raises("iio:EINVAL", "t_range", 3, mt_dbl(1), mt_dbl(2), mt_dbl(3)));
	CHECK(mt_dbl_of("t_range", 1, mt_dbl(1)) == 1);
	CHECK(mt_dbl_of("t_range", 2, mt_dbl(1), mt_dbl(2)) == 2);
	CHECK(mt_raises("iio:EINVAL", "t_ok", 1, mt_dbl(1)));

	CHECK(mt_call(2, out, "t_ok", 0) == 1);
	CHECK(raised("iio:EINVAL", NULL));
	CHECK(out[0] == NULL && out[1] == NULL);
	CHECK(mt_call(1, out, "t_errno", 1, mt_dbl(0)) == 1);   /* max_lhs is 0 */
	CHECK(raised("iio:EINVAL", NULL));

	CHECK(mt_call(0, out, "t_ok", 0) == 0);
	CHECK(is_double(out[0], 42));
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "t_ok", 0) == 0);
	CHECK(is_double(out[0], 42));
	mxDestroyArray(out[0]);

	live = mock_live();
	CHECK(mt_call(1, out, "t_two", 0) == 0);
	CHECK(is_double(out[0], 1));
	CHECK(mock_live() == live + 1);
	mxDestroyArray(out[0]);
	CHECK(mt_call(2, out, "t_two", 0) == 0);
	CHECK(is_double(out[0], 1) && is_double(out[1], 2));
	CHECK(mock_live() == live + 2);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	CHECK(mt_call(0, out, "t_two", 0) == 0);
	CHECK(is_double(out[0], 1));
	mxDestroyArray(out[0]);

	/* a command without outputs leaves output 0 alone */
	CHECK(mt_call(0, out, "t_errno", 1, mt_dbl(0)) == 0);
	CHECK(out[0] == NULL);
	CHECK(mock_live() == live);
}

static void test_errors(void)
{
	mxArray *out[1] = { NULL };
	size_t live = mock_live();

	CHECK(mt_raises("iio:EINVAL", "t_errno", 1, mt_dbl(22)));
	CHECK(!strcmp(mock_err_msg(), "t_errno: Invalid argument (-22)"));
	CHECK(mt_raises("iio:ETIMEDOUT", "t_errno", 1, mt_dbl(110)));
	CHECK(mt_raises("iio:ENOTSUP", "t_errno", 1, mt_dbl(95)));
	CHECK(mt_raises("iio:EAGAIN", "t_errno", 1, mt_dbl(11)));
	CHECK(mt_raises("iio:ENOSYS", "t_errno", 1, mt_dbl(38)));
	CHECK(mt_raises("iio:E524", "t_errno", 1, mt_dbl(524)));
	CHECK(strstr(mock_err_msg(), "t_errno: ") == mock_err_msg());
	CHECK(mt_raises("iio:EBADF", "t_errno", 1, mt_dbl(9)));
	CHECK(mt_raises("iio:EPERM", "t_errno", 1, mt_dbl(1)));
	CHECK(mt_raises("iio:E2147483647", "t_errno", 1, mt_dbl(INT_MAX)));
	/* a command that returns a positive number has failed too */
	CHECK(mt_raises("iio:EINVAL", "t_errno", 1, mt_dbl(-22)));
	CHECK(!strcmp(mock_err_msg(), "t_errno: Invalid argument (-22)"));

	CHECK(mt_raises("iio:EIO", "t_msg", 0));
	CHECK(!strcmp(mock_err_msg(), "t_msg: disk 3"));
	/* The message of one call does not leak into the next, and a command that sets none
	 * gets the text of its errno, whatever the stack held (the mock fills it with 0xA5). */
	CHECK(mt_raises("iio:EIO", "t_errno", 1, mt_dbl(5)));
	CHECK(!strcmp(mock_err_msg(), "t_errno: Input/output error (-5)"));
	CHECK(mt_raises("iio:EIO", "t_msg_long", 0));
	CHECK(strlen(mock_err_msg()) == strlen("t_msg_long: ") + 255);
	CHECK(mt_raises("iio:ENOENT", "t_errno", 1, mt_dbl(2)));
	CHECK(!strcmp(mock_err_msg(), "t_errno: No such file or directory (-2)"));

	CHECK(mt_call(1, out, "t_out_then_fail", 0) == 1);
	CHECK(raised("iio:EBUSY", NULL));
	CHECK(out[0] == NULL);
	CHECK(mock_live() == live);
	CHECK(mt_call(0, out, "t_out_then_fail", 0) == 1);
	CHECK(raised("iio:EBUSY", NULL));
	CHECK(mock_live() == live);

	CHECK(mt_call(1, out, "t_forget", 0) == 1);
	CHECK(raised("iio:EFAULT", NULL));
	CHECK(strstr(mock_err_msg(), "t_forget: ") == mock_err_msg());
	/* nobody asked for the output: nothing is missing */
	CHECK(mt_call(0, out, "t_forget", 0) == 0);
	CHECK(out[0] == NULL);
	CHECK(mock_autofreed() == 0);
}

static void test_lock(void)
{
	static char other[] = "scan";
	uint64_t id, id2;
	gw_id scan;

	CHECK(!mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_raises("iio:EINVAL", "mex_handle_count", 1, mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "mex_shutdown", 1, mt_dbl(1)));
	CHECK(!mock_is_locked());

	oplog[0] = '\0';
	id = mt_u64_of("t_add", 0);
	CHECK(id != 0);
	CHECK(mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	CHECK(gw_handle_type(id) == GW_T_CONTEXT && gw_handle_owned(id));
	mock_clear_mex();                      /* "clear mex" does nothing to a locked MEX file */
	CHECK(mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	CHECK(!strcmp(oplog, ""));

	/* a second handle does not lock a second time: one unlock is enough later */
	id2 = mt_u64_of("t_add", 0);
	CHECK(id2 != 0 && id2 != id);
	mt_ok("t_del", 1, mt_u64(id2));
	CHECK(!strcmp(oplog, "p1d1"));
	CHECK(mock_is_locked());

	mt_ok("t_del", 1, mt_u64(id));
	CHECK(!strcmp(oplog, "p1d1p0d0"));
	CHECK(!mock_is_locked());
	CHECK(gw_handle_count() == 0);
	mt_ok("t_del", 1, mt_u64(id));         /* stale */
	mt_ok("t_del", 1, mt_u64(0));
	mt_ok("t_del", 1, mt_u64(UINT64_MAX)); /* never existed */
	CHECK(!strcmp(oplog, "p1d1p0d0"));
	CHECK(!mock_is_locked());
	CHECK(mt_raises("iio:EINVAL", "t_del", 1, mt_dbl(0)));

	/* a live id of another type */
	scan = gw_handle_add(GW_T_SCAN, other, 0, true, NULL, NULL);
	CHECK(scan != 0);
	CHECK(mt_raises("iio:EBADF", "t_del", 1, mt_u64(scan)));
	CHECK(strstr(mock_err_msg(), "expected a context handle, got a scan") != NULL);
	CHECK(gw_handle_count() == 1);
	CHECK(mock_is_locked());               /* the failed call saw the handle */
	gw_handle_destroy(scan);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());
}

static void test_oom_and_parking(void)
{
	mxArray *out[1] = { NULL };
	size_t count = gw_handle_count();
	unsigned int objs_before = n_objs;

	/* result first: the allocation fails before the object exists */
	oplog[0] = '\0';
	mock_fail_alloc(1);
	CHECK(mt_call(1, out, "t_add", 0) == 1);
	CHECK(mock_alloc_failed());
	CHECK(!strcmp(mock_err_id(), "MATLAB:nomem"));
	CHECK(out[0] == NULL);
	CHECK(gw_handle_count() == count);
	CHECK(n_objs == objs_before);
	CHECK(!strcmp(oplog, ""));
	CHECK(!mock_is_locked());

	/* parked memory is released when the call ends ... */
	CHECK(mt_dbl_of("t_park", 0) == 1);
	CHECK(!mock_alloc_failed());
	CHECK(park_rets[0] == 0);
	CHECK(!strcmp(park_log_after[0], ""));     /* not before the command returned */
	CHECK(!strcmp(oplog, "f"));

	/* ... and at the start of the next call when this one ended non-locally */
	mock_fail_alloc(1);
	CHECK(mt_call(1, out, "t_park", 0) == 1);
	CHECK(mock_alloc_failed());
	CHECK(!strcmp(oplog, "f"));
	CHECK(mt_dbl_of("t_park", 0) == 1);
	CHECK(!strcmp(park_log_after[0], "ff"));   /* before the next command runs */
	CHECK(!strcmp(oplog, "fff"));
	CHECK(mt_dbl_of("t_ok", 0) == 42);
	CHECK(!strcmp(oplog, "fff"));

	/* ... or at MATLAB exit, when no call follows */
	mock_fail_alloc(1);
	CHECK(mt_call(1, out, "t_park", 1, mt_dbl(3)) == 1);
	CHECK(mock_alloc_failed());
	CHECK(!strcmp(oplog, "fff"));
	mock_exit_matlab();
	CHECK(!strcmp(oplog, "ffffff"));
	CHECK(mt_dbl_of("t_ok", 0) == 42);
	CHECK(!strcmp(oplog, "ffffff"));

	/* a command that fails still has its parked memory released */
	oplog[0] = '\0';
	CHECK(mt_raises("iio:EDOM", "t_park", 1, mt_dbl(6)));
	CHECK(park_rets[4] == -ENOMEM && park_rets[5] == -ENOMEM);
	CHECK(!strcmp(oplog, "ffffff"));

	/* four slots: the fifth pointer is freed at once and reported */
	oplog[0] = '\0';
	CHECK(mt_dbl_of("t_park", 1, mt_dbl(5)) == 5);
	CHECK(park_rets[0] == 0 && park_rets[1] == 0 && park_rets[2] == 0 && park_rets[3] == 0);
	CHECK(park_rets[4] == -ENOMEM);
	CHECK(!strcmp(park_log_after[3], ""));
	CHECK(!strcmp(park_log_after[4], "f"));
	CHECK(!strcmp(oplog, "fffff"));
	/* the slots are free again in the next call */
	oplog[0] = '\0';
	CHECK(mt_dbl_of("t_park", 1, mt_dbl(4)) == 4);
	CHECK(park_rets[3] == 0);
	CHECK(!strcmp(oplog, "ffff"));
	CHECK(park_size_ok);
	CHECK(!mock_is_locked());
}

static void test_shutdown_and_exit(void)
{
	uint64_t a, b, c, d;
	char want[32];

	oplog[0] = '\0';
	a = mt_u64_of("t_add", 0);
	b = mt_u64_of("t_add", 0);
	CHECK(a != 0 && b != 0 && a != b);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 2);
	CHECK(mock_is_locked());
	mt_ok("mex_shutdown", 0);
	snprintf(want, sizeof(want), "p%ud%up%ud%u", n_objs - 1, n_objs - 1, n_objs - 2, n_objs - 2);
	CHECK(!strcmp(oplog, want));           /* newest first */
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());
	mt_ok("mex_shutdown", 0);              /* nothing left: nothing happens */
	CHECK(!strcmp(oplog, want));

	/* MATLAB exit with a root alive */
	oplog[0] = '\0';
	c = mt_u64_of("t_add", 0);
	CHECK(c != 0 && c != a && c != b);
	CHECK(mock_is_locked());
	mock_exit_matlab();
	snprintf(want, sizeof(want), "p%ud%u", n_objs - 1, n_objs - 1);
	CHECK(!strcmp(oplog, want));
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());

	/* the gateway starts again, with ids of its own */
	d = mt_u64_of("t_add", 0);
	CHECK(d != 0 && d != a && d != b && d != c);
	CHECK(mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	oplog[0] = '\0';
	mt_ok("t_del", 1, mt_u64(c));          /* an id of the time before the exit */
	mt_ok("t_del", 1, mt_u64(a));
	CHECK(!strcmp(oplog, ""));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	mt_ok("t_del", 1, mt_u64(d));
	snprintf(want, sizeof(want), "p%ud%u", n_objs - 1, n_objs - 1);
	CHECK(!strcmp(oplog, want));
	CHECK(!mock_is_locked());

	/* "clear mex" of an unlocked gateway runs the exit function; the next call starts over */
	mock_clear_mex();
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_dbl_of("t_ok", 0) == 42);
}

int main(void)
{
	CHECK(!mock_is_locked());
	test_lookup();
	test_counts();
	test_errors();
	test_lock();
	test_oom_and_parking();
	test_shutdown_and_exit();
	CHECK(mock_warn_count() == 0);
	CHECK(!strcmp(mock_output(), ""));
	MT_MAIN_RETURN();
}
