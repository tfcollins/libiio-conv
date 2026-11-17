/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "attr": the seven kinds of libiio attribute through one handle type. */
#include "mx_util.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define READ_FIRST 32768u                   /* first buffer size */
#define READ_LIMIT (16u * 1024u * 1024u)    /* no retry with a larger buffer from here on */

/* The error of a negative ssize_t result of libiio. A backend's value outside the errno range
 * (-4294967296 would be 0 as an int: "success" without a result) is an I/O error. */
static int io_err(ssize_t v)
{
	return v < -4095 ? -EIO : (int)v;
}

/* gw_c_numeric_enter() with the error text. Every write is bracketed with it as well as the
 * range parser: the emu backend checks a written value against the attribute's "_available"
 * range with sscanf("%lf"), so under a decimal comma it refused 0.75 for a range [0 0.25 1]. */
static int c_numeric(struct gw_call *c, struct gw_c_numeric *loc)
{
	int ret = gw_c_numeric_enter(loc);

	return ret ? gw_fail(c, ret, "no \"C\" numeric locale") : 0;
}

static int attr_get_name(struct gw_call *c)
{
	const struct iio_attr *attr;
	int ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_attr_get_name(attr));
}

static int attr_get_filename(struct gw_call *c)
{
	const struct iio_attr *attr;
	int ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_attr_get_filename(attr));
}

/* char = attr_get_static_value(attr); [] when the attribute has none (only context attributes do) */
static int attr_get_static_value(struct gw_call *c)
{
	const struct iio_attr *attr;
	int ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_attr_get_static_value(attr));
}

/* char = attr_read_raw(attr [, as_bytes = false]); with as_bytes: 1-by-N uint8.
 * char: the text up to the first NUL, read as UTF-8. Text that is not UTF-8 (a Latin-1 degree
 * sign, binary data) is NOT preserved: MATLAB puts U+FFFD where a byte sequence is invalid.
 * Bytes: exactly the bytes libiio counted (device and channel attributes include the
 * terminating NUL, context attributes do not); this form is exact for any value.
 *
 * The buffer starts at READ_FIRST bytes and grows by four while libiio says the value did not
 * fit and the buffer is below READ_LIMIT. "Did not fit" is -EFBIG (local backend) or a return
 * value >= the buffer size (emu, context attributes, the binary iiod protocol). emu returns
 * exactly the buffer size both for a truncated value and for one of size - 1 characters, so
 * such a value is read once more. A value that fits no buffer is -EFBIG on every backend;
 * with the last buffer at 32 MiB that also refuses a value of exactly 32 MiB - 1 characters.
 *
 * Limit, not handled: the legacy (text) iiod protocol reports a too-small buffer as -EIO
 * (iiod_client_attr_read), which cannot be told from a real I/O error and is therefore not
 * retried: over such a server a value of READ_FIRST - 1 bytes or more is iio:EIO. */
static int attr_read_raw(struct gw_call *c)
{
	const struct iio_attr *attr;
	bool as_bytes = false;
	size_t len = READ_FIRST, n;
	ssize_t got;
	char *buf;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	if (gw_arg_given(c, 1)) {
		ret = gw_arg_bool(c, 1, &as_bytes);
		if (ret)
			return ret;
	}
	for (;;) {
		buf = mxMalloc(len);
		got = iio_attr_read_raw(attr, buf, len);
		if (!(got == -EFBIG || (got >= 0 && (size_t)got >= len)) || len >= READ_LIMIT)
			break;
		mxFree(buf);
		len *= 4;
	}
	if (got < 0) {
		mxFree(buf);
		return io_err(got);
	}
	n = (size_t)got;
	if (n >= len) {
		mxFree(buf);
		return gw_fail(c, -EFBIG, "the value does not fit a buffer of %zu bytes", len);
	}
	if (as_bytes) {
		ret = gw_ret_bytes(c, 0, buf, n);
	} else {
		buf[n] = '\0';
		ret = gw_ret_str(c, 0, buf);
	}
	mxFree(buf);
	return ret;
}

/* n = attr_write_raw(attr, uint8 vector): the bytes of the input as they are, no copy, no strlen.
 * Result first: when the allocation of the count fails, nothing was written to the device. */
static int attr_write_raw(struct gw_call *c)
{
	const struct iio_attr *attr;
	const uint8_t *data;
	struct gw_c_numeric loc;
	ssize_t written;
	size_t n;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bytes(c, 1, &data, &n);    /* never NULL, also when n is 0 */
	if (ret)
		return ret;
	ret = gw_ret_double(c, 0, 0);
	if (!ret)
		ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	written = iio_attr_write_raw(attr, data, n);
	gw_c_numeric_leave(&loc);
	if (written < 0)
		return io_err(written);
	gw_set_double(c, 0, (double)written);
	return 0;
}

/* n = attr_write_string(attr, char): libiio sends the terminating NUL too and counts it.
 * Result first, as attr_write_raw. Text with an embedded NUL is -EINVAL (gw_arg_str). */
static int attr_write_string(struct gw_call *c)
{
	const struct iio_attr *attr;
	struct gw_c_numeric loc;
	ssize_t written;
	char *s;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &s);
	if (ret)
		return ret;
	ret = gw_ret_double(c, 0, 0);
	if (!ret)
		ret = c_numeric(c, &loc);
	if (ret) {
		mxFree(s);
		return ret;
	}
	written = iio_attr_write_string(attr, s);
	gw_c_numeric_leave(&loc);
	mxFree(s);
	if (written < 0)
		return io_err(written);
	gw_set_double(c, 0, (double)written);
	return 0;
}

static int attr_read_bool(struct gw_call *c)
{
	const struct iio_attr *attr;
	bool v = false;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = iio_attr_read_bool(attr, &v);
	return ret < 0 ? ret : gw_ret_bool(c, 0, v);
}

/* int64 = attr_read_longlong(attr): never through double */
static int attr_read_longlong(struct gw_call *c)
{
	const struct iio_attr *attr;
	long long v = 0;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = iio_attr_read_longlong(attr, &v);
	return ret < 0 ? ret : gw_ret_int64(c, 0, (int64_t)v);
}

static int attr_read_double(struct gw_call *c)
{
	const struct iio_attr *attr;
	double v = 0;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = iio_attr_read_double(attr, &v);
	return ret < 0 ? ret : gw_ret_double(c, 0, v);
}

static int attr_write_bool(struct gw_call *c)
{
	const struct iio_attr *attr;
	struct gw_c_numeric loc;
	bool v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bool(c, 1, &v);
	if (!ret)
		ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	ret = iio_attr_write_bool(attr, v);
	gw_c_numeric_leave(&loc);
	return ret < 0 ? ret : 0;
}

/* attr_write_longlong(attr, integer): the whole long long range; int64 and uint64 inputs are read exactly */
static int attr_write_longlong(struct gw_call *c)
{
	const struct iio_attr *attr;
	struct gw_c_numeric loc;
	long long v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 1, LLONG_MIN, LLONG_MAX, &v);
	if (!ret)
		ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	ret = iio_attr_write_longlong(attr, v);
	gw_c_numeric_leave(&loc);
	return ret < 0 ? ret : 0;
}

/* The shortest fixed-notation text that strtod() reads back as v, at most 40 decimals; else
 * "%.17g". Fixed notation because the kernel's IIO fixed-point parser takes no exponent: a
 * value that needs more than 40 decimals (1.5e-40; most values below about 1e-24) is
 * sent with an exponent and a kernel driver is expected to refuse it. Every digit is sent and
 * that parser truncates where libiio's "%f" rounds to six decimals: 2.9999999999999996 is
 * "2.9999999999999996" here and "3.000000" with exact = false. The caller holds the "C" numeric
 * locale (c_numeric), whatever MATLAB's is. v is finite. */
static void exact_text(double v, char *text, size_t len)
{
	int decimals;

	for (decimals = 0; decimals <= 40; decimals++) {
		snprintf(text, len, "%.*f", decimals, v);
		if (strtod(text, NULL) == v)
			return;
	}
	snprintf(text, len, "%.17g", v);
}

/* attr_write_double(attr, double [, exact = true]). exact: the text of exact_text() through
 * iio_attr_write_string. Not exact: iio_attr_write_double, which is libiio's "%f" (six decimals).
 * The value is a double or a single: an integer class is -EINVAL, because int64(2^53 + 1) would
 * become another number on the way through double (attr_write_longlong is exact). */
static int attr_write_double(struct gw_call *c)
{
	const struct iio_attr *attr;
	char text[512];             /* DBL_MAX with 40 decimals is 351 characters */
	struct gw_c_numeric loc;
	bool exact = true;
	ssize_t written;
	double v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_double(c, 1, &v);
	if (ret)
		return ret;
	if (mxGetClassID(c->prhs[1]) != mxDOUBLE_CLASS && mxGetClassID(c->prhs[1]) != mxSINGLE_CLASS)
		return gw_fail(c, -EINVAL, "argument 2: expected a double or single scalar, got %s; "
			"integers are written with attr_write_longlong", mxGetClassName(c->prhs[1]));
	if (gw_arg_given(c, 2)) {
		ret = gw_arg_bool(c, 2, &exact);
		if (ret)
			return ret;
	}
	if (!isfinite(v))
		return gw_fail(c, -EINVAL, "the value must be finite");
	ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	if (exact) {
		exact_text(v, text, sizeof(text));
		written = iio_attr_write_string(attr, text);
		ret = written < 0 ? io_err(written) : 0;
	} else {
		ret = iio_attr_write_double(attr, v);
	}
	gw_c_numeric_leave(&loc);
	return ret < 0 ? ret : 0;
}

/* [min step max] = attr_get_range(attr). libiio parses the text with sscanf("%lf"). */
static int attr_get_range(struct gw_call *c)
{
	const struct iio_attr *attr;
	double v[3] = { 0, 0, 0 };
	struct gw_c_numeric loc;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	ret = iio_attr_get_range(attr, &v[0], &v[1], &v[2]);
	gw_c_numeric_leave(&loc);
	return ret < 0 ? ret : gw_ret_doubles(c, 0, v, 3);
}

static void free_available(void *p, size_t n)
{
	iio_available_list_free(p, n);
}

/* cell = attr_get_available(attr). libiio returns the list from malloc: it is parked, so the
 * dispatcher frees it however the call ends; this command never frees it. */
static int attr_get_available(struct gw_call *c)
{
	const struct iio_attr *attr;
	char **list = NULL;
	size_t count = 0;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = iio_attr_get_available(attr, &list, &count);
	if (ret < 0)
		return ret;
	ret = gw_park(list, count, free_available);
	if (ret)
		return gw_fail(c, ret, "no room to park the list");
	return gw_ret_strs(c, 0, list, count);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("attr_get_name", attr_get_name, 1, 1, 1),
	GW_CMD("attr_get_filename", attr_get_filename, 1, 1, 1),
	GW_CMD("attr_get_static_value", attr_get_static_value, 1, 1, 1),
	GW_CMD("attr_read_raw", attr_read_raw, 1, 2, 1),
	GW_CMD("attr_write_raw", attr_write_raw, 2, 2, 1),
	GW_CMD("attr_write_string", attr_write_string, 2, 2, 1),
	GW_CMD("attr_read_bool", attr_read_bool, 1, 1, 1),
	GW_CMD("attr_read_longlong", attr_read_longlong, 1, 1, 1),
	GW_CMD("attr_read_double", attr_read_double, 1, 1, 1),
	GW_CMD("attr_write_bool", attr_write_bool, 2, 2, 0),
	GW_CMD("attr_write_longlong", attr_write_longlong, 2, 2, 0),
	GW_CMD("attr_write_double", attr_write_double, 2, 3, 0),
	GW_CMD("attr_get_range", attr_get_range, 1, 1, 1),
	GW_CMD("attr_get_available", attr_get_available, 1, 1, 1),
};

const struct gw_group gw_group_attr = GW_GROUP("attr", cmds);
