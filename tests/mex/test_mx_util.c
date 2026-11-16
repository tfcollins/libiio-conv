/* SPDX-License-Identifier: MIT */
/* The argument and return helpers of mx_util.h, each through a command of a local group
 * that calls one helper and returns what it produced. */
#include "mex_test.h"

#include <limits.h>
#include <math.h>

/* ------------------------------------------------------------------ the local command group */

static char oplog[64];
static char attr_obj[] = "attr";

static void log_destroy(void *ptr, void *aux)
{
	(void)aux;
	strncat(oplog, ptr, sizeof(oplog) - strlen(oplog) - 1);
}

static int arg_type(struct gw_call *c, int k, enum gw_type *type)
{
	long long v;
	int ret = gw_arg_int(c, k, 0, GW_T_COUNT, &v);

	*type = (enum gw_type)v;
	return ret;
}

static int u_given(struct gw_call *c)
{
	return gw_ret_bool(c, 0, gw_arg_given(c, 0));
}

static int u_id(struct gw_call *c)
{
	gw_id id = 99;
	int ret = gw_arg_id(c, 0, &id);

	return ret ? ret : gw_ret_uint64(c, 0, id);
}

static int handle_cmd(struct gw_call *c, bool zero_ok)
{
	char canary[1];
	void *p = canary;
	enum gw_type type;
	gw_id id = 99;
	int ret = arg_type(c, 1, &type);

	if (ret)
		return ret;
	ret = zero_ok ? gw_arg_handle0(c, 0, type, &p, &id) : gw_arg_handle(c, 0, type, &p, &id);
	if (ret)
		return ret;
	ret = gw_ret_uint64(c, 0, (uint64_t)(uintptr_t)p);
	return ret ? ret : gw_ret_uint64(c, 1, id);
}

static int u_handle(struct gw_call *c)
{
	return handle_cmd(c, false);
}

static int u_handle0(struct gw_call *c)
{
	return handle_cmd(c, true);
}

/* Both result pointers may be NULL. */
static int u_handle_null(struct gw_call *c)
{
	enum gw_type type;
	int ret = arg_type(c, 1, &type);

	if (!ret)
		ret = gw_arg_handle(c, 0, type, NULL, NULL);
	if (!ret)
		ret = gw_arg_handle0(c, 0, type, NULL, NULL);
	return ret;
}

static int u_destroy(struct gw_call *c)
{
	enum gw_type type;
	int ret = arg_type(c, 1, &type);

	return ret ? ret : gw_arg_destroy(c, 0, type);
}

static int u_str(struct gw_call *c)
{
	char *s = NULL;
	int ret = gw_arg_str(c, 0, &s);

	if (ret)
		return ret;
	if (!s)
		return -EFAULT;
	ret = gw_ret_str(c, 0, s);
	mxFree(s);
	return ret;
}

static int u_str0(struct gw_call *c)
{
	char canary[1];
	char *s = canary;
	int ret = gw_arg_str0(c, 0, &s);

	if (ret)
		return ret;
	ret = gw_ret_str(c, 0, s);
	mxFree(s);
	return ret;
}

static int u_bool(struct gw_call *c)
{
	bool v = false;
	int ret = gw_arg_bool(c, 0, &v);

	return ret ? ret : gw_ret_bool(c, 0, v);
}

static int u_integer(struct gw_call *c)
{
	long long v = 77, lo, hi;
	int ret = gw_arg_int(c, 1, LLONG_MIN, LLONG_MAX, &lo);

	if (!ret)
		ret = gw_arg_int(c, 2, LLONG_MIN, LLONG_MAX, &hi);
	if (!ret)
		ret = gw_arg_int(c, 0, lo, hi, &v);
	return ret ? ret : gw_ret_int64(c, 0, v);
}

static int u_index(struct gw_call *c)
{
	unsigned int v = 77;
	int ret = gw_arg_index(c, 0, &v);

	return ret ? ret : gw_ret_double(c, 0, v);
}

static int u_double(struct gw_call *c)
{
	double v = 77;
	int ret = gw_arg_double(c, 0, &v);

	return ret ? ret : gw_ret_double(c, 0, v);
}

static int u_bytes(struct gw_call *c)
{
	const uint8_t *p = NULL;
	size_t n = 77;
	unsigned int sum = 0;
	int ret = gw_arg_bytes(c, 0, &p, &n);

	if (ret)
		return ret;
	if (!p)
		return -EFAULT;
	for (size_t i = 0; i < n; i++)       /* every byte is readable (ASan) */
		sum += p[i];
	ret = gw_ret_uint64(c, 0, (uint64_t)(uintptr_t)p);
	if (!ret)
		ret = gw_ret_double(c, 1, (double)n);
	return ret ? ret : gw_ret_double(c, 2, sum);
}

/* [is NULL, [log_level stderr_level timestamp_level timeout_ms flags out_is_null err_is_null reserved_is_zero]] */
static int u_params(struct gw_call *c)
{
	struct iio_context_params storage;
	const struct iio_context_params *params = &storage;
	double v[8] = { 0 };
	int ret;

	memset(&storage, 0xff, sizeof(storage));
	ret = gw_arg_params(c, 0, &storage, &params);
	if (ret)
		return ret;
	if (params && params != &storage)
		return -EFAULT;
	if (params) {
		v[0] = params->log_level;
		v[1] = params->stderr_level;
		v[2] = params->timestamp_level;
		v[3] = params->timeout_ms;
		v[4] = params->flags;
		v[5] = params->out == NULL;
		v[6] = params->err == NULL;
		v[7] = 1;
		for (size_t i = 0; i < sizeof(params->__rsrv); i++)
			if (params->__rsrv[i])
				v[7] = 0;
	}
	ret = gw_ret_bool(c, 0, params == NULL);
	return ret ? ret : gw_ret_doubles(c, 1, v, 8);
}

enum {
	R_STR_NULL, R_STR_EMPTY, R_STR_ABC, R_STR_UTF8, R_ID, R_ID_SET, R_IDS_0, R_IDS_3, R_INT64,
	R_UINT64, R_TRUE, R_FALSE, R_DOUBLE, R_DOUBLES, R_DOUBLES_0, R_BYTES, R_BYTES_0, R_STRS,
	R_STRS_0, R_EMPTY, R_TWICE, R_COUNT
};

/* u_ret(which, k): the result helper `which` on output k. With k > 0, output 0 is 7. */
static int u_ret(struct gw_call *c)
{
	static const gw_id ids[3] = { 1, 2, UINT64_MAX };
	static const double dbl[3] = { 1, 2, 3 };
	static char x[] = "x", yz[] = "yz";
	char *const list[2] = { x, yz };
	long long which, kk;
	int ret = gw_arg_int(c, 0, 0, R_COUNT - 1, &which);
	int k;

	if (!ret)
		ret = gw_arg_int(c, 1, 0, 3, &kk);
	if (ret)
		return ret;
	k = (int)kk;
	if (k > 0) {
		ret = gw_ret_double(c, 0, 7);
		if (ret)
			return ret;
	}
	switch (which) {
	case R_STR_NULL:  return gw_ret_str(c, k, NULL);
	case R_STR_EMPTY: return gw_ret_str(c, k, "");
	case R_STR_ABC:   return gw_ret_str(c, k, "abc");
	case R_STR_UTF8:  return gw_ret_str(c, k, "\xC2\xB5V");
	case R_ID:        return gw_ret_id(c, k, 5);
	case R_ID_SET:
		ret = gw_ret_id(c, k, 0);
		gw_set_id(c, k, UINT64_MAX);
		return ret;
	case R_IDS_0:     return gw_ret_ids(c, k, NULL, 0);
	case R_IDS_3:     return gw_ret_ids(c, k, ids, 3);
	case R_INT64:     return gw_ret_int64(c, k, INT64_MIN);
	case R_UINT64:    return gw_ret_uint64(c, k, UINT64_MAX);
	case R_TRUE:      return gw_ret_bool(c, k, true);
	case R_FALSE:     return gw_ret_bool(c, k, false);
	case R_DOUBLE:    return gw_ret_double(c, k, 2.5);
	case R_DOUBLES:   return gw_ret_doubles(c, k, dbl, 3);
	case R_DOUBLES_0: return gw_ret_doubles(c, k, NULL, 0);
	case R_BYTES:     return gw_ret_bytes(c, k, "a\0b", 3);
	case R_BYTES_0:   return gw_ret_bytes(c, k, NULL, 0);
	case R_STRS:      return gw_ret_strs(c, k, list, 2);
	case R_STRS_0:    return gw_ret_strs(c, k, NULL, 0);
	case R_EMPTY:     return gw_ret_empty(c, k);
	case R_TWICE:     /* the second result replaces the first; nothing is left behind */
		ret = gw_ret_strs(c, k, list, 2);
		return ret ? ret : gw_ret_double(c, k, 9);
	default:          return -EINVAL;
	}
}

/* u_borrowed(have_ptr, owner): the id of the attribute object as a borrowed child. */
static int u_borrowed(struct gw_call *c)
{
	bool have;
	gw_id owner;
	int ret = gw_arg_bool(c, 0, &have);

	if (!ret)
		ret = gw_arg_id(c, 1, &owner);
	if (ret)
		return ret;
	if (c->nlhs == 2) {
		ret = gw_ret_double(c, 0, 7);
		return ret ? ret : gw_ret_borrowed(c, 1, GW_T_ATTR, have ? attr_obj : NULL, owner);
	}
	ret = gw_ret_borrowed(c, 0, GW_T_ATTR, have ? attr_obj : NULL, owner);
	/* an output nobody asked for: nothing is created and nothing is registered */
	return ret ? ret : gw_ret_borrowed(c, 1, GW_T_CHANNEL, oplog, owner);
}

static const struct gw_cmd local_cmds[] = {
	GW_XCMD("u_given", u_given, 0, 1, 1),
	GW_XCMD("u_id", u_id, 0, 1, 1),
	GW_XCMD("u_handle", u_handle, 2, 2, 2),
	GW_XCMD("u_handle0", u_handle0, 2, 2, 2),
	GW_XCMD("u_handle_null", u_handle_null, 2, 2, 0),
	GW_XCMD("u_destroy", u_destroy, 2, 2, 0),
	GW_XCMD("u_str", u_str, 0, 1, 1),
	GW_XCMD("u_str0", u_str0, 0, 1, 1),
	GW_XCMD("u_bool", u_bool, 0, 1, 1),
	GW_XCMD("u_int", u_integer, 3, 3, 1),
	GW_XCMD("u_index", u_index, 0, 1, 1),
	GW_XCMD("u_double", u_double, 0, 1, 1),
	GW_XCMD("u_bytes", u_bytes, 0, 1, 3),
	GW_XCMD("u_params", u_params, 0, 1, 2),
	GW_XCMD("u_ret", u_ret, 2, 2, 2),
	GW_XCMD("u_borrowed", u_borrowed, 2, 2, 2),
};

static const struct gw_group gw_group_local = GW_GROUP("local", local_cmds);

MT_GROUPS(&gw_group_local);

/* ------------------------------------------------------------------ helpers */

static mxArray *num(mxClassID cls, const void *value)
{
	mxArray *a = mxCreateNumericMatrix(1, 1, cls, mxREAL);

	memcpy(mxGetData(a), value, mxGetElementSize(a));
	return a;
}

static mxArray *i8(int8_t v) { return num(mxINT8_CLASS, &v); }
static mxArray *u8(uint8_t v) { return num(mxUINT8_CLASS, &v); }
static mxArray *i16(int16_t v) { return num(mxINT16_CLASS, &v); }
static mxArray *u16(uint16_t v) { return num(mxUINT16_CLASS, &v); }
static mxArray *i32(int32_t v) { return num(mxINT32_CLASS, &v); }
static mxArray *u32(uint32_t v) { return num(mxUINT32_CLASS, &v); }
static mxArray *sgl(float v) { return num(mxSINGLE_CLASS, &v); }

static mxArray *matrix(mxClassID cls, mxComplexity cplx, size_t m, size_t n)
{
	return mxCreateNumericMatrix(m, n, cls, cplx);
}

static mxArray *lo5(void) { return mt_i64(-5); }
static mxArray *himax(void) { return mt_i64(LLONG_MAX); }

static bool is_class(const mxArray *a, mxClassID cls, size_t m, size_t n)
{
	return a && mxGetClassID(a) == cls && mxGetM(a) == m && mxGetN(a) == n &&
		mxGetNumberOfDimensions(a) == 2 && !mxIsComplex(a) && !mxIsSparse(a);
}

static bool name_is(int err, const char *want)
{
	const char *got = gw_errno_name(err);

	return want ? got && !strcmp(got, want) : got == NULL;
}

static bool msg_has(const char *text)
{
	return strstr(mock_err_msg(), text) != NULL;
}

static void test_errno_names(void)
{
	static const char *const more =
		"ESRCH EINTR E2BIG EFAULT EEXIST ENOTDIR EISDIR EMFILE EROFS EDOM ENAMETOOLONG "
		"ENODATA ETIME EPROTO EBADMSG EMSGSIZE EPROTOTYPE EADDRINUSE EAFNOSUPPORT ENETDOWN "
		"ENETUNREACH ECONNABORTED ECONNRESET ENOBUFS EISCONN ENOTCONN ESHUTDOWN EHOSTDOWN "
		"EALREADY EINPROGRESS ECANCELED EREMOTEIO EILSEQ ";
	static const int more_values[] = {
		ESRCH, EINTR, E2BIG, EFAULT, EEXIST, ENOTDIR, EISDIR, EMFILE, EROFS, EDOM,
		ENAMETOOLONG, ENODATA, ETIME, EPROTO, EBADMSG, EMSGSIZE, EPROTOTYPE, EADDRINUSE,
		EAFNOSUPPORT, ENETDOWN, ENETUNREACH, ECONNABORTED, ECONNRESET, ENOBUFS, EISCONN,
		ENOTCONN, ESHUTDOWN, EHOSTDOWN, EALREADY, EINPROGRESS, ECANCELED, EREMOTEIO, EILSEQ,
	};
	const char *p = more;

	CHECK(name_is(-22, "EINVAL"));
	CHECK(name_is(22, "EINVAL"));
	CHECK(name_is(-110, "ETIMEDOUT"));
	CHECK(name_is(-95, "ENOTSUP"));
	CHECK(name_is(-11, "EAGAIN"));
	CHECK(name_is(-2, "ENOENT"));
	CHECK(name_is(-13, "EACCES"));
	CHECK(name_is(-1, "EPERM"));
	CHECK(name_is(-16, "EBUSY"));
	CHECK(name_is(-19, "ENODEV"));
	CHECK(name_is(-6, "ENXIO"));
	CHECK(name_is(-28, "ENOSPC"));
	CHECK(name_is(-27, "EFBIG"));
	CHECK(name_is(-34, "ERANGE"));
	CHECK(name_is(-32, "EPIPE"));
	CHECK(name_is(-111, "ECONNREFUSED"));
	CHECK(name_is(-113, "EHOSTUNREACH"));
	CHECK(name_is(-9, "EBADF"));
	CHECK(name_is(-12, "ENOMEM"));
	CHECK(name_is(-5, "EIO"));
	CHECK(name_is(-75, "EOVERFLOW"));
	CHECK(name_is(-38, "ENOSYS"));
	CHECK(name_is(0, NULL));
	CHECK(name_is(-99999, NULL));
	CHECK(name_is(99999, NULL));
	CHECK(name_is(524, NULL));
	CHECK(name_is(INT_MIN, NULL));
	CHECK(name_is(INT_MAX, NULL));

	for (size_t i = 0; i < sizeof(more_values) / sizeof(more_values[0]); i++) {
		size_t len = strcspn(p, " ");
		const char *got = gw_errno_name(-more_values[i]);

		CHECK(got && strlen(got) == len && !strncmp(got, p, len));
		CHECK(gw_errno_name(more_values[i]) == got);
		p += len + 1;
	}
	CHECK(*p == '\0');
	/* every name can be part of a MATLAB message identifier */
	for (int e = 1; e < 200; e++) {
		const char *s = gw_errno_name(e);

		CHECK(!s || (s[0] == 'E' && strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == strlen(s)));
	}
}

static void test_given(void)
{
	CHECK(mt_bool_of("u_given", 0) == false);
	CHECK(mt_bool_of("u_given", 1, mt_empty()) == false);
	CHECK(mt_bool_of("u_given", 1, mt_str("")) == true);
	CHECK(mt_bool_of("u_given", 1, mt_dbl(0)) == true);
	CHECK(mt_bool_of("u_given", 1, mt_bytes(NULL, 0)) == true);
	CHECK(mt_bool_of("u_given", 1, matrix(mxDOUBLE_CLASS, mxREAL, 1, 0)) == true);
	CHECK(mt_bool_of("u_given", 1, matrix(mxDOUBLE_CLASS, mxREAL, 0, 1)) == true);
	CHECK(mt_bool_of("u_given", 1, matrix(mxSINGLE_CLASS, mxREAL, 0, 0)) == true);
	CHECK(mt_bool_of("u_given", 1, mxCreateCellMatrix(0, 0)) == true);
	CHECK(mt_bool_of("u_given", 1, mock_string_object()) == true);
}

static void test_id(void)
{
	CHECK(mt_u64_of("u_id", 1, mt_u64(5)) == 5);
	CHECK(mt_u64_of("u_id", 1, mt_u64(0)) == 0);
	CHECK(mt_u64_of("u_id", 1, mt_u64(UINT64_MAX)) == UINT64_MAX);
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mt_dbl(5)));
	CHECK(msg_has("u_id: argument 1: ") && msg_has("uint64") && msg_has("1x1 double"));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, matrix(mxUINT64_CLASS, mxREAL, 1, 2)));
	CHECK(msg_has("1x2 uint64"));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, matrix(mxUINT64_CLASS, mxCOMPLEX, 1, 1)));
	CHECK(msg_has("complex"));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, matrix(mxUINT64_CLASS, mxREAL, 0, 0)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mt_i64(5)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mt_str("5")));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mt_bool(true)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mxCreateCellMatrix(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mock_sparse_double(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 1, mock_nd_array(mxUINT64_CLASS, mxREAL, 1, 1, 2)));
	CHECK(mt_raises("iio:EINVAL", "u_id", 0));      /* the helper is safe without the argument */
	CHECK(msg_has("argument 1"));
}

static void test_handles(void)
{
	static char ctx_obj[] = "C", dev_obj[] = "D", dev2_obj[] = "E", m1_obj[] = "M", m2_obj[] = "V";
	static const struct gw_ops ops = { NULL, log_destroy };
	mxArray *out[2] = { NULL, NULL };
	gw_id ctx, dev, dev2, m_owned, m_view, a, a2;
	size_t count, live;

	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);    /* the gateway has set up its table */
	ctx = gw_handle_add(GW_T_CONTEXT, ctx_obj, 0, true, &ops, NULL);
	CHECK(ctx != 0);
	CHECK(gw_handle_borrow(GW_T_DEVICE, dev_obj, ctx, &dev) == 0 && dev != 0);
	CHECK(gw_handle_borrow(GW_T_DEVICE, dev2_obj, ctx, &dev2) == 0 && dev2 != 0);

	/* gw_arg_handle */
	CHECK(mt_call(2, out, "u_handle", 2, mt_u64(dev), mt_dbl(GW_T_DEVICE)) == 0);
	CHECK(is_class(out[0], mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(out[0]) == (uintptr_t)dev_obj);
	CHECK(is_class(out[1], mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(out[1]) == dev);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	CHECK(mt_u64_of("u_handle", 2, mt_u64(ctx), mt_dbl(GW_T_CONTEXT)) == (uintptr_t)ctx_obj);
	mt_ok("u_handle_null", 2, mt_u64(dev), mt_dbl(GW_T_DEVICE));

	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(0), mt_dbl(GW_T_DEVICE)));
	CHECK(msg_has("invalid or destroyed device handle") && msg_has("argument 1"));
	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(0x1234567800000042), mt_dbl(GW_T_DEVICE)));
	CHECK(msg_has("invalid or destroyed device handle"));
	gw_handle_destroy(dev2);
	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(dev2), mt_dbl(GW_T_DEVICE)));
	CHECK(msg_has("invalid or destroyed device handle"));
	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(ctx), mt_dbl(GW_T_DEVICE)));
	CHECK(msg_has("expected a device handle, got a context"));
	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(dev), mt_dbl(GW_T_ATTR)));
	CHECK(msg_has("expected an attr handle, got a device"));
	CHECK(mt_raises("iio:EBADF", "u_handle", 2, mt_u64(dev), mt_dbl(GW_T_EVENT_STREAM)));
	CHECK(msg_has("expected an event stream handle, got a device"));
	CHECK(mt_raises("iio:EINVAL", "u_handle", 2, mt_dbl((double)dev), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EINVAL", "u_handle", 2, mt_empty(), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EINVAL", "u_handle", 2, mock_string_object(), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EBADF", "u_handle_null", 2, mt_u64(0), mt_dbl(GW_T_DEVICE)));

	/* gw_arg_handle0 */
	CHECK(mt_call(2, out, "u_handle0", 2, mt_u64(0), mt_dbl(GW_T_DEVICE)) == 0);
	CHECK(is_class(out[0], mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(out[0]) == 0);
	CHECK(is_class(out[1], mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(out[1]) == 0);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	CHECK(mt_call(2, out, "u_handle0", 2, mt_u64(dev), mt_dbl(GW_T_DEVICE)) == 0);
	CHECK(out[0] && *(uint64_t *)mxGetData(out[0]) == (uintptr_t)dev_obj);
	CHECK(out[1] && *(uint64_t *)mxGetData(out[1]) == dev);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	CHECK(mt_raises("iio:EBADF", "u_handle0", 2, mt_u64(dev2), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EBADF", "u_handle0", 2, mt_u64(ctx), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EINVAL", "u_handle0", 2, mt_dbl(0), mt_dbl(GW_T_DEVICE)));
	CHECK(mt_raises("iio:EINVAL", "u_handle0", 2, mt_empty(), mt_dbl(GW_T_DEVICE)));

	/* gw_ret_borrowed: a child of the owner's root, one id per pointer */
	count = gw_handle_count();
	a = mt_u64_of("u_borrowed", 2, mt_bool(true), mt_u64(dev));
	CHECK(a != 0 && gw_handle_count() == count + 1);
	CHECK(gw_handle_type(a) == GW_T_ATTR && !gw_handle_owned(a));
	CHECK(gw_handle_parent(a) == ctx);
	CHECK(gw_handle_ptr(a, GW_T_ATTR) == attr_obj);
	CHECK(mt_u64_of("u_borrowed", 2, mt_bool(true), mt_u64(dev)) == a);
	CHECK(mt_u64_of("u_borrowed", 2, mt_bool(true), mt_u64(ctx)) == a);
	CHECK(mt_u64_of("u_borrowed", 2, mt_bool(true), mt_u64(a)) == a);
	CHECK(gw_handle_count() == count + 1);
	CHECK(mt_u64_of("u_borrowed", 2, mt_bool(false), mt_u64(dev)) == 0);
	CHECK(mt_u64_of("u_borrowed", 2, mt_bool(false), mt_u64(0)) == 0);
	CHECK(gw_handle_count() == count + 1);
	gw_handle_destroy(a);
	CHECK(mt_call(2, out, "u_borrowed", 2, mt_bool(true), mt_u64(dev)) == 0);   /* as output 1 */
	a2 = out[1] && is_class(out[1], mxUINT64_CLASS, 1, 1) ? *(uint64_t *)mxGetData(out[1]) : 0;
	CHECK(a2 != 0 && a2 != a && gw_handle_parent(a2) == ctx);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	gw_handle_destroy(a2);
	CHECK(gw_handle_count() == count);
	CHECK(mt_raises("iio:EBADF", "u_borrowed", 2, mt_bool(true), mt_u64(dev2)));   /* stale owner */
	CHECK(mt_raises("iio:EBADF", "u_borrowed", 2, mt_bool(true), mt_u64(0)));
	CHECK(gw_handle_count() == count);
	/* out of memory: no entry without the array that carries its id */
	live = mock_live();
	mock_fail_alloc(1);
	CHECK(mt_call(1, out, "u_borrowed", 2, mt_bool(true), mt_u64(dev)) == 1);
	CHECK(mock_alloc_failed());
	CHECK(gw_handle_count() == count && gw_handle_find(GW_T_ATTR, attr_obj) == 0);
	CHECK(mt_oom_sweep(1, "u_borrowed", 2, mt_bool(true), mt_u64(dev)) == 1);
	CHECK(gw_handle_count() == count + 1);
	gw_handle_destroy(gw_handle_find(GW_T_ATTR, attr_obj));
	CHECK(gw_handle_count() == count && mock_live() == live);

	/* gw_arg_destroy */
	m_owned = gw_handle_add(GW_T_MASK, m1_obj, dev, true, &ops, NULL);
	CHECK(m_owned != 0);
	CHECK(gw_handle_borrow(GW_T_MASK, m2_obj, ctx, &m_view) == 0 && m_view != 0);
	count = gw_handle_count();
	oplog[0] = '\0';
	CHECK(mt_raises("iio:EBADF", "u_destroy", 2, mt_u64(ctx), mt_dbl(GW_T_MASK)));
	CHECK(msg_has("expected a channels mask handle, got a context"));
	CHECK(mt_raises("iio:EBADF", "u_destroy", 2, mt_u64(m_owned), mt_dbl(GW_T_BLOCK)));
	CHECK(mt_raises("iio:EINVAL", "u_destroy", 2, mt_dbl(0), mt_dbl(GW_T_MASK)));
	CHECK(mt_raises("iio:EINVAL", "u_destroy", 2, mt_empty(), mt_dbl(GW_T_MASK)));
	CHECK(gw_handle_count() == count && !strcmp(oplog, ""));
	mt_ok("u_destroy", 2, mt_u64(m_view), mt_dbl(GW_T_MASK));       /* borrowed: left alone */
	CHECK(gw_handle_count() == count && gw_handle_ptr(m_view, GW_T_MASK) == m2_obj);
	mt_ok("u_destroy", 2, mt_u64(m_owned), mt_dbl(GW_T_MASK));
	CHECK(gw_handle_count() == count - 1 && !strcmp(oplog, "M"));
	CHECK(gw_handle_type(m_owned) == GW_T_NONE);
	mt_ok("u_destroy", 2, mt_u64(m_owned), mt_dbl(GW_T_MASK));      /* stale */
	mt_ok("u_destroy", 2, mt_u64(0), mt_dbl(GW_T_MASK));
	mt_ok("u_destroy", 2, mt_u64(dev2), mt_dbl(GW_T_MASK));         /* stale, of another type */
	mt_ok("u_destroy", 2, mt_u64(UINT64_MAX), mt_dbl(GW_T_MASK));
	CHECK(gw_handle_count() == count - 1 && !strcmp(oplog, "M"));
	CHECK(mock_is_locked());

	mt_ok("u_destroy", 2, mt_u64(ctx), mt_dbl(GW_T_CONTEXT));       /* with everything below it */
	CHECK(!strcmp(oplog, "MC"));
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());
}

static void test_str(void)
{
	static const mxChar nul_text[3] = { 'a', 0, 'b' };
	mxArray *out[1] = { NULL };
	mxArray *a;
	const char *s;

	s = mt_str_of("u_str", 1, mt_str("abc"));
	CHECK(s && !strcmp(s, "abc"));
	CHECK(mt_call(1, out, "u_str", 1, mt_str("")) == 0);
	CHECK(is_class(out[0], mxCHAR_CLASS, 0, 0));
	mxDestroyArray(out[0]);
	s = mt_str_of("u_str", 1, mt_str("\xC2\xB5V"));
	CHECK(s && !strcmp(s, "\xC2\xB5V"));
	CHECK(mt_raises("iio:EINVAL", "u_str", 1, mt_dbl(1)));
	CHECK(msg_has("u_str: argument 1: ") && msg_has("1x1 double"));
	CHECK(mt_raises("iio:EINVAL", "u_str", 1, mxCreateCellMatrix(1, 1)));
	CHECK(msg_has("1x1 cell"));
	CHECK(mt_raises("iio:EINVAL", "u_str", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "u_str", 1, mt_bytes("abc", 3)));
	CHECK(mt_raises("iio:EINVAL", "u_str", 1, mock_string_object()));
	CHECK(msg_has("string"));
	CHECK(mt_raises("iio:EINVAL", "u_str", 0));
	/* text with an embedded NUL ends there, as a C string does */
	a = mxCreateString("a?b");
	memcpy(mxGetData(a), nul_text, sizeof(nul_text));
	s = mt_str_of("u_str", 1, a);
	CHECK(s && !strcmp(s, "a"));

	CHECK(mt_call(1, out, "u_str0", 1, mt_empty()) == 0);
	CHECK(is_class(out[0], mxDOUBLE_CLASS, 0, 0));
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "u_str0", 0) == 0);
	CHECK(is_class(out[0], mxDOUBLE_CLASS, 0, 0));
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "u_str0", 1, mt_str("")) == 0);
	CHECK(is_class(out[0], mxCHAR_CLASS, 0, 0));
	mxDestroyArray(out[0]);
	s = mt_str_of("u_str0", 1, mt_str("abc"));
	CHECK(s && !strcmp(s, "abc"));
	CHECK(mt_raises("iio:EINVAL", "u_str0", 1, mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "u_str0", 1, matrix(mxDOUBLE_CLASS, mxREAL, 1, 0)));
	CHECK(mt_raises("iio:EINVAL", "u_str0", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "u_str0", 1, mxCreateCellMatrix(0, 0)));

	/* the copy is released whichever allocation fails */
	CHECK(mt_oom_sweep(1, "u_str", 1, mt_str("abc")) == 2);
}

static void test_bool(void)
{
	CHECK(mt_bool_of("u_bool", 1, mt_bool(true)) == true);
	CHECK(mt_bool_of("u_bool", 1, mt_bool(false)) == false);
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mt_dbl(1)));
	CHECK(msg_has("argument 1") && msg_has("logical"));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mxCreateLogicalMatrix(1, 2)));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mxCreateLogicalMatrix(0, 0)));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, u8(1)));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mt_str("1")));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "u_bool", 0));
}

static void test_int(void)
{
	mxArray *a;

	CHECK(mt_i64_of("u_int", 3, mt_dbl(3), lo5(), himax()) == 3);
	CHECK(mt_i64_of("u_int", 3, i8(-5), lo5(), himax()) == -5);
	CHECK(mt_i64_of("u_int", 3, mt_i64(9007199254740993), lo5(), himax()) == 9007199254740993);
	CHECK(mt_i64_of("u_int", 3, mt_u64(9223372036854775807), lo5(), himax()) == LLONG_MAX);
	CHECK(mt_i64_of("u_int", 3, mt_dbl(2450000000.0), lo5(), himax()) == 2450000000);
	CHECK(mt_i64_of("u_int", 3, mt_dbl(-0.0), lo5(), himax()) == 0);
	CHECK(mt_i64_of("u_int", 3, sgl(-4), lo5(), himax()) == -4);
	CHECK(mt_i64_of("u_int", 3, u8(255), lo5(), himax()) == 255);
	CHECK(mt_i64_of("u_int", 3, i16(-5), lo5(), himax()) == -5);
	CHECK(mt_i64_of("u_int", 3, u16(65535), lo5(), himax()) == 65535);
	CHECK(mt_i64_of("u_int", 3, i32(INT32_MAX), lo5(), himax()) == INT32_MAX);
	CHECK(mt_i64_of("u_int", 3, u32(UINT32_MAX), lo5(), himax()) == UINT32_MAX);
	CHECK(mt_i64_of("u_int", 3, mt_i64(LLONG_MIN), mt_i64(LLONG_MIN), himax()) == LLONG_MIN);
	CHECK(mt_i64_of("u_int", 3, mt_dbl(-9223372036854775808.0), mt_i64(LLONG_MIN), himax()) == LLONG_MIN);
	CHECK(mt_i64_of("u_int", 3, mt_dbl(9223372036854774784.0), lo5(), himax()) == 9223372036854774784);
	CHECK(mt_i64_of("u_int", 3, mt_dbl(7), mt_dbl(7), mt_dbl(7)) == 7);

	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_u64(9223372036854775808u), lo5(), himax()));
	CHECK(msg_has("u_int: argument 1: ") && msg_has("uint64"));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_u64(UINT64_MAX), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(2.5), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(-6), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, i8(-6), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(NAN), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(INFINITY), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(-INFINITY), mt_i64(LLONG_MIN), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(9223372036854775808.0), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(-9223372036854777856.0), mt_i64(LLONG_MIN), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(1e300), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, sgl(0.5f), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(8), mt_dbl(7), mt_dbl(7)));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_bool(true), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_str("3"), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, matrix(mxDOUBLE_CLASS, mxCOMPLEX, 1, 1), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, matrix(mxDOUBLE_CLASS, mxREAL, 1, 2), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, matrix(mxINT64_CLASS, mxCOMPLEX, 1, 1), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_empty(), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mock_sparse_double(1, 1), lo5(), himax()));
	CHECK(msg_has("sparse"));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mock_string_object(), lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mxCreateCellMatrix(1, 1), lo5(), himax()));
	a = mxCreateStructMatrix(1, 1, 0, NULL);
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, a, lo5(), himax()));
	CHECK(mt_raises("iio:EINVAL", "u_int", 3, mt_dbl(3), mt_str("x"), himax()));
	CHECK(msg_has("u_int: argument 2: "));

	CHECK(mt_dbl_of("u_index", 1, mt_dbl(4294967295.0)) == 4294967295.0);
	CHECK(mt_dbl_of("u_index", 1, mt_dbl(0)) == 0);
	CHECK(mt_dbl_of("u_index", 1, u32(UINT32_MAX)) == 4294967295.0);
	CHECK(mt_dbl_of("u_index", 1, mt_u64(17)) == 17);
	CHECK(mt_raises("iio:EINVAL", "u_index", 1, mt_dbl(-1)));
	CHECK(mt_raises("iio:EINVAL", "u_index", 1, mt_dbl(4294967296.0)));
	CHECK(mt_raises("iio:EINVAL", "u_index", 1, mt_i64(-1)));
	CHECK(mt_raises("iio:EINVAL", "u_index", 1, mt_dbl(0.5)));
	CHECK(mt_raises("iio:EINVAL", "u_index", 1, mt_bool(true)));
	CHECK(mt_raises("iio:EINVAL", "u_index", 0));
}

static void test_double(void)
{
	CHECK(mt_dbl_of("u_double", 1, mt_dbl(2.5)) == 2.5);
	CHECK(mt_dbl_of("u_double", 1, sgl(2.5f)) == 2.5);
	CHECK(mt_dbl_of("u_double", 1, i32(2)) == 2);
	CHECK(mt_dbl_of("u_double", 1, mt_i64(-3)) == -3);
	CHECK(mt_dbl_of("u_double", 1, mt_u64(UINT64_MAX)) == 18446744073709551616.0);
	CHECK(mt_dbl_of("u_double", 1, u8(200)) == 200);
	CHECK(isinf(mt_dbl_of("u_double", 1, mt_dbl(INFINITY))));
	CHECK(isnan(mt_dbl_of("u_double", 1, mt_dbl(NAN))));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mt_str("2")));
	CHECK(msg_has("u_double: argument 1: ") && msg_has("1x1 char"));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mt_bool(true)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, matrix(mxDOUBLE_CLASS, mxCOMPLEX, 1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, matrix(mxDOUBLE_CLASS, mxREAL, 2, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mock_sparse_double(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mxCreateCellMatrix(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 1, mock_nd_array(mxDOUBLE_CLASS, mxREAL, 1, 1, 2)));
	CHECK(mt_raises("iio:EINVAL", "u_double", 0));
}

static void bytes_case(mxArray *a, double want_n, bool want_data_ptr)
{
	uintptr_t data = (uintptr_t)mxGetData(a);
	mxArray *out[3] = { NULL, NULL, NULL };

	CHECK(mt_call(3, out, "u_bytes", 1, a) == 0);
	if (!out[0] || !out[1] || !out[2]) {
		CHECK(!"u_bytes failed");
		return;
	}
	CHECK(*(uint64_t *)mxGetData(out[0]) != 0);
	if (want_data_ptr)
		CHECK(*(uint64_t *)mxGetData(out[0]) == data);
	else
		CHECK(data == 0);
	CHECK(mxGetScalar(out[1]) == want_n);
	CHECK(mxGetScalar(out[2]) == (want_n ? 1 + 2 + 3 + 4 + 250 : 0));
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mxDestroyArray(out[2]);
}

static void test_bytes(void)
{
	static const uint8_t five[5] = { 1, 2, 3, 4, 250 };
	mxArray *a;

	bytes_case(mt_bytes(five, 5), 5, true);
	a = matrix(mxUINT8_CLASS, mxREAL, 5, 1);
	memcpy(mxGetData(a), five, 5);
	bytes_case(a, 5, true);
	bytes_case(matrix(mxUINT8_CLASS, mxREAL, 0, 0), 0, false);
	bytes_case(matrix(mxUINT8_CLASS, mxREAL, 1, 0), 0, false);
	bytes_case(matrix(mxUINT8_CLASS, mxREAL, 0, 1), 0, false);
	bytes_case(mt_bytes(NULL, 0), 0, false);

	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, matrix(mxDOUBLE_CLASS, mxREAL, 1, 5)));
	CHECK(msg_has("u_bytes: argument 1: ") && msg_has("uint8") && msg_has("1x5 double"));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, matrix(mxUINT8_CLASS, mxREAL, 2, 2)));
	CHECK(msg_has("2x2 uint8"));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, matrix(mxINT8_CLASS, mxREAL, 1, 5)));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, matrix(mxUINT8_CLASS, mxCOMPLEX, 1, 5)));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mock_nd_array(mxUINT8_CLASS, mxREAL, 1, 5, 2)));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mock_nd_array(mxUINT8_CLASS, mxREAL, 1, 1, 5)));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mt_str("abc")));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mock_sparse_double(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "u_bytes", 0));
}

static mxArray *params_struct(size_t m, size_t n, int nfields, const char **names, const double *values)
{
	mxArray *s = mxCreateStructMatrix(m, n, nfields, names);

	for (size_t e = 0; e < m * n; e++)
		for (int i = 0; i < nfields; i++)
			mxSetFieldByNumber(s, e, i, mxCreateDoubleScalar(values[i]));
	return s;
}

static bool params_are(mxArray *arg, const double want[5])
{
	mxArray *out[2] = { NULL, NULL };
	bool ok;

	if (mt_call(2, out, "u_params", 1, arg) != 0)
		return false;
	ok = is_class(out[0], mxLOGICAL_CLASS, 1, 1) && !mxIsLogicalScalarTrue(out[0]) &&
		is_class(out[1], mxDOUBLE_CLASS, 1, 8);
	if (ok) {
		const double *v = mxGetData(out[1]);

		ok = !memcmp(v, want, 5 * sizeof(double)) && v[5] == 1 && v[6] == 1 && v[7] == 1;
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	return ok;
}

static void test_params(void)
{
	const char *three[] = { "timeout_ms", "log_level", "flags" };
	const char *all[] = { "log_level", "stderr_level", "timestamp_level", "timeout_ms", "flags" };
	const char *bogus[] = { "timeout_ms", "bogus" };
	const char *one[] = { "timeout_ms" };
	const double v3[] = { 123, 1, 1 };
	const double vall[] = { 4, 3, 2, -1, 1 };
	const double vmin[] = { 0, 0, 0, INT_MIN, INT_MAX };
	const double want3[5] = { 1, 0, 0, 123, 1 };
	const double zero[5] = { 0, 0, 0, 0, 0 };
	const double v15[] = { 1.5 }, vbig[] = { 2147483648.0 }, vnan[] = { NAN };
	mxArray *out[2] = { NULL, NULL };
	mxArray *s;

	CHECK(mt_call(2, out, "u_params", 1, mt_empty()) == 0);
	CHECK(is_class(out[0], mxLOGICAL_CLASS, 1, 1) && mxIsLogicalScalarTrue(out[0]));
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	CHECK(mt_bool_of("u_params", 0) == true);      /* absent */

	CHECK(params_are(params_struct(1, 1, 3, three, v3), want3));
	CHECK(params_are(params_struct(1, 1, 5, all, vall), vall));
	CHECK(params_are(params_struct(1, 1, 5, all, vmin), vmin));
	CHECK(params_are(mxCreateStructMatrix(1, 1, 0, NULL), zero));
	s = mxCreateStructMatrix(1, 1, 1, one);        /* integer classes are fine */
	mxSetFieldByNumber(s, 0, 0, i32(-1));
	CHECK(params_are(s, (const double[5]){ 0, 0, 0, -1, 0 }));

	CHECK(mt_raises("iio:EINVAL", "u_params", 1, params_struct(1, 1, 2, bogus, v3)));
	CHECK(msg_has("u_params: argument 1: ") && msg_has("bogus"));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, params_struct(1, 1, 1, one, v15)));
	CHECK(msg_has("timeout_ms"));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, params_struct(1, 1, 1, one, vbig)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, params_struct(1, 1, 1, one, vnan)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, params_struct(1, 2, 3, three, v3)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mxCreateStructMatrix(0, 0, 1, one)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mxCreateStructMatrix(1, 1, 1, one)));  /* field is [] */
	s = mxCreateStructMatrix(1, 1, 1, one);
	mxSetFieldByNumber(s, 0, 0, mt_str("5"));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, s));
	s = mxCreateStructMatrix(1, 1, 1, one);
	mxSetFieldByNumber(s, 0, 0, mock_string_object());
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, s));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mt_dbl(5)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mt_str("timeout_ms")));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mxCreateCellMatrix(1, 1)));
	CHECK(mt_raises("iio:EINVAL", "u_params", 1, mock_string_object()));
}

static mxArray *ret(int which)
{
	mxArray *out[1] = { NULL };

	if (mt_call(1, out, "u_ret", 2, mt_dbl(which), mt_dbl(0)) != 0) {
		fprintf(stderr, "u_ret(%d): %s: %s\n", which, mock_err_id(), mock_err_msg());
		CHECK(!"u_ret raised");
	}
	return out[0];
}

static void test_results(void)
{
	mxArray *out[2] = { NULL, NULL };
	mxArray *a, *e;
	char *s;
	size_t live = mock_live();

	a = ret(R_STR_NULL);
	CHECK(is_class(a, mxDOUBLE_CLASS, 0, 0));
	mxDestroyArray(a);
	a = ret(R_STR_EMPTY);
	CHECK(is_class(a, mxCHAR_CLASS, 0, 0));
	mxDestroyArray(a);
	a = ret(R_STR_ABC);
	CHECK(is_class(a, mxCHAR_CLASS, 1, 3));
	CHECK(a && !memcmp(mxGetData(a), (const mxChar[3]){ 'a', 'b', 'c' }, 3 * sizeof(mxChar)));
	mxDestroyArray(a);
	a = ret(R_STR_UTF8);
	CHECK(is_class(a, mxCHAR_CLASS, 1, 2));
	CHECK(a && !memcmp(mxGetData(a), (const mxChar[2]){ 0xB5, 'V' }, 2 * sizeof(mxChar)));
	mxDestroyArray(a);

	a = ret(R_ID);
	CHECK(is_class(a, mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(a) == 5);
	mxDestroyArray(a);
	a = ret(R_ID_SET);
	CHECK(is_class(a, mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(a) == UINT64_MAX);
	mxDestroyArray(a);
	a = ret(R_IDS_0);
	CHECK(is_class(a, mxUINT64_CLASS, 1, 0));
	mxDestroyArray(a);
	a = ret(R_IDS_3);
	CHECK(is_class(a, mxUINT64_CLASS, 1, 3));
	CHECK(a && !memcmp(mxGetData(a), (const uint64_t[3]){ 1, 2, UINT64_MAX }, 24));
	mxDestroyArray(a);
	a = ret(R_INT64);
	CHECK(is_class(a, mxINT64_CLASS, 1, 1) && *(int64_t *)mxGetData(a) == INT64_MIN);
	mxDestroyArray(a);
	a = ret(R_UINT64);
	CHECK(is_class(a, mxUINT64_CLASS, 1, 1) && *(uint64_t *)mxGetData(a) == UINT64_MAX);
	mxDestroyArray(a);
	a = ret(R_TRUE);
	CHECK(is_class(a, mxLOGICAL_CLASS, 1, 1) && mxIsLogicalScalarTrue(a));
	mxDestroyArray(a);
	a = ret(R_FALSE);
	CHECK(is_class(a, mxLOGICAL_CLASS, 1, 1) && !mxIsLogicalScalarTrue(a));
	mxDestroyArray(a);
	a = ret(R_DOUBLE);
	CHECK(is_class(a, mxDOUBLE_CLASS, 1, 1) && mxGetScalar(a) == 2.5);
	mxDestroyArray(a);
	a = ret(R_DOUBLES);
	CHECK(is_class(a, mxDOUBLE_CLASS, 1, 3));
	CHECK(a && !memcmp(mxGetData(a), (const double[3]){ 1, 2, 3 }, 24));
	mxDestroyArray(a);
	a = ret(R_DOUBLES_0);
	CHECK(is_class(a, mxDOUBLE_CLASS, 1, 0));
	mxDestroyArray(a);
	a = ret(R_BYTES);
	CHECK(is_class(a, mxUINT8_CLASS, 1, 3));
	CHECK(a && !memcmp(mxGetData(a), (const uint8_t[3]){ 97, 0, 98 }, 3));
	mxDestroyArray(a);
	a = ret(R_BYTES_0);
	CHECK(is_class(a, mxUINT8_CLASS, 1, 0));
	mxDestroyArray(a);

	a = ret(R_STRS);
	CHECK(is_class(a, mxCELL_CLASS, 1, 2));
	e = a ? mxGetCell(a, 0) : NULL;
	CHECK(is_class(e, mxCHAR_CLASS, 1, 1));
	s = e ? mxArrayToUTF8String(e) : NULL;
	CHECK(s && !strcmp(s, "x"));
	mxFree(s);
	e = a ? mxGetCell(a, 1) : NULL;
	CHECK(is_class(e, mxCHAR_CLASS, 1, 2));
	s = e ? mxArrayToUTF8String(e) : NULL;
	CHECK(s && !strcmp(s, "yz"));
	mxFree(s);
	mxDestroyArray(a);
	a = ret(R_STRS_0);
	CHECK(is_class(a, mxCELL_CLASS, 1, 0));
	mxDestroyArray(a);
	a = ret(R_EMPTY);
	CHECK(is_class(a, mxDOUBLE_CLASS, 0, 0));
	mxDestroyArray(a);
	a = ret(R_TWICE);
	CHECK(is_class(a, mxDOUBLE_CLASS, 1, 1) && mxGetScalar(a) == 9);
	mxDestroyArray(a);
	CHECK(mock_live() == live);
	CHECK(mock_autofreed() == 0);

	for (int which = 0; which < R_COUNT; which++) {
		/* output 1 was not asked for: nothing is created */
		CHECK(mt_call(1, out, "u_ret", 2, mt_dbl(which), mt_dbl(1)) == 0);
		CHECK(mock_live() == live + 1);
		CHECK(is_class(out[0], mxDOUBLE_CLASS, 1, 1) && mxGetScalar(out[0]) == 7);
		mxDestroyArray(out[0]);
		/* nor with no output asked for at all, but output 0 always is */
		CHECK(mt_call(0, out, "u_ret", 2, mt_dbl(which), mt_dbl(1)) == 0);
		CHECK(mock_live() == live + 1);
		mxDestroyArray(out[0]);
		CHECK(mt_call(0, out, "u_ret", 2, mt_dbl(which), mt_dbl(0)) == 0);
		CHECK(mock_live() == live + 1 && out[0] != NULL);
		mxDestroyArray(out[0]);
		/* asked for: created */
		CHECK(mt_call(2, out, "u_ret", 2, mt_dbl(which), mt_dbl(1)) == 0);
		CHECK(mock_live() == live + 2 && out[1] != NULL);
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
		/* whichever allocation fails, nothing stays behind */
		CHECK(mt_oom_sweep(1, "u_ret", 2, mt_dbl(which), mt_dbl(0)) >= 1);
		CHECK(mock_live() == live);
	}
	CHECK(mt_oom_sweep(1, "u_ret", 2, mt_dbl(R_STRS), mt_dbl(0)) == 3);
	CHECK(mock_autofreed() == 0);
}

int main(void)
{
	test_errno_names();
	test_given();
	test_id();
	test_handles();
	test_str();
	test_bool();
	test_int();
	test_double();
	test_bytes();
	test_params();
	test_results();
	CHECK(mock_warn_count() == 0);
	MT_MAIN_RETURN();
}
