/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "attr". The first slice (Task 8a) implements attr_read_raw;
 * the other entries are still the stubs of Task 4b. Task 13 replaces gw_stub by the command
 * functions; no entry is added, removed or renamed. */
#include "mx_util.h"

#define READ_FIRST 32768u                   /* first buffer size */
#define READ_LIMIT (16u * 1024u * 1024u)    /* no retry with a larger buffer from here on */

/* char = attr_read_raw(attr [, as_bytes = false]); with as_bytes: 1-by-N uint8.
 * char: the text up to the first NUL. Bytes: exactly the bytes libiio counted (device and
 * channel attributes include the terminating NUL, context attributes do not). */
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
	n = (size_t)got < len ? (size_t)got : len;
	if (as_bytes) {
		ret = gw_ret_bytes(c, 0, buf, n);
	} else {
		buf[n < len ? n : len - 1] = '\0';
		ret = gw_ret_str(c, 0, buf);
	}
	mxFree(buf);
	return ret;
}

static const struct gw_cmd cmds[] = {
	GW_CMD("attr_get_name", gw_stub, 1, 1, 1),
	GW_CMD("attr_get_filename", gw_stub, 1, 1, 1),
	GW_CMD("attr_get_static_value", gw_stub, 1, 1, 1),
	GW_CMD("attr_read_raw", attr_read_raw, 1, 2, 1),
	GW_CMD("attr_write_raw", gw_stub, 2, 2, 1),
	GW_CMD("attr_write_string", gw_stub, 2, 2, 1),
	GW_CMD("attr_read_bool", gw_stub, 1, 1, 1),
	GW_CMD("attr_read_longlong", gw_stub, 1, 1, 1),
	GW_CMD("attr_read_double", gw_stub, 1, 1, 1),
	GW_CMD("attr_write_bool", gw_stub, 2, 2, 0),
	GW_CMD("attr_write_longlong", gw_stub, 2, 2, 0),
	GW_CMD("attr_write_double", gw_stub, 2, 3, 0),
	GW_CMD("attr_get_range", gw_stub, 1, 1, 1),
	GW_CMD("attr_get_available", gw_stub, 1, 1, 1),
};

const struct gw_group gw_group_attr = GW_GROUP("attr", cmds);
