/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "attr": the seven kinds of libiio attribute through one handle type. */
#include "mx_util.h"

#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define READ_FIRST 32768u                   /* first buffer size */
#define READ_LIMIT (16u * 1024u * 1024u)    /* no retry with a larger buffer from here on */

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
 * char: the text up to the first NUL. Bytes: exactly the bytes libiio counted (device and
 * channel attributes include the terminating NUL, context attributes do not).
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
		return (int)got;
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

/* n = attr_write_raw(attr, uint8 vector): the bytes of the input as they are, no copy, no strlen */
static int attr_write_raw(struct gw_call *c)
{
	const struct iio_attr *attr;
	const uint8_t *data;
	ssize_t written;
	size_t n;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bytes(c, 1, &data, &n);    /* never NULL, also when n is 0 */
	if (ret)
		return ret;
	written = iio_attr_write_raw(attr, data, n);
	if (written < 0)
		return (int)written;
	return gw_ret_double(c, 0, (double)written);
}

/* n = attr_write_string(attr, char): libiio sends the terminating NUL too and counts it */
static int attr_write_string(struct gw_call *c)
{
	const struct iio_attr *attr;
	ssize_t written;
	char *s;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &s);
	if (ret)
		return ret;
	written = iio_attr_write_string(attr, s);
	mxFree(s);
	if (written < 0)
		return (int)written;
	return gw_ret_double(c, 0, (double)written);
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
	bool v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bool(c, 1, &v);
	if (ret)
		return ret;
	ret = iio_attr_write_bool(attr, v);
	return ret < 0 ? ret : 0;
}

/* attr_write_longlong(attr, integer): the whole long long range; int64 and uint64 inputs are read exactly */
static int attr_write_longlong(struct gw_call *c)
{
	const struct iio_attr *attr;
	long long v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 1, LLONG_MIN, LLONG_MAX, &v);
	if (ret)
		return ret;
	ret = iio_attr_write_longlong(attr, v);
	return ret < 0 ? ret : 0;
}

/* The shortest fixed-notation text that strtod() reads back as v, at most 40 decimals; else
 * "%.17g". Fixed notation because the kernel's IIO fixed-point parser takes no exponent. The
 * numeric locale is "C" while the text is made, whatever MATLAB's is. v is finite. */
static int exact_text(double v, char *text, size_t len)
{
	locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0), old;
	int decimals;

	if (c_locale == (locale_t)0)
		return -ENOMEM;
	old = uselocale(c_locale);
	for (decimals = 0; decimals <= 40; decimals++) {
		snprintf(text, len, "%.*f", decimals, v);
		if (strtod(text, NULL) == v)
			break;
	}
	if (decimals > 40)
		snprintf(text, len, "%.17g", v);
	uselocale(old);
	freelocale(c_locale);
	return 0;
}

/* attr_write_double(attr, double [, exact = true]). exact: the text of exact_text() through
 * iio_attr_write_string. Not exact: iio_attr_write_double, which is libiio's "%f" (six decimals). */
static int attr_write_double(struct gw_call *c)
{
	const struct iio_attr *attr;
	char text[512];             /* DBL_MAX with 40 decimals is 351 characters */
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
	if (gw_arg_given(c, 2)) {
		ret = gw_arg_bool(c, 2, &exact);
		if (ret)
			return ret;
	}
	if (!isfinite(v))
		return gw_fail(c, -EINVAL, "the value must be finite");
	if (!exact) {
		ret = iio_attr_write_double(attr, v);
		return ret < 0 ? ret : 0;
	}
	ret = exact_text(v, text, sizeof(text));
	if (ret)
		return gw_fail(c, ret, "no \"C\" locale");
	written = iio_attr_write_string(attr, text);
	return written < 0 ? (int)written : 0;
}

/* [min step max] = attr_get_range(attr) */
static int attr_get_range(struct gw_call *c)
{
	const struct iio_attr *attr;
	double v[3] = { 0, 0, 0 };
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_ATTR, &attr, NULL);
	if (ret)
		return ret;
	ret = iio_attr_get_range(attr, &v[0], &v[1], &v[2]);
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
