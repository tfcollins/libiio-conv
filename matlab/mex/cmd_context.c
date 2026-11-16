/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "context". */
#include "mx_util.h"

#include <limits.h>

static void context_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_context_destroy(ptr);
}

/* ctx = create_context(params, uri or []) */
static int create_context(struct gw_call *c)
{
	static const struct gw_ops ops = { NULL, context_destroy_op };
	struct iio_context_params storage;
	const struct iio_context_params *params;
	struct iio_context *ctx;
	char *uri = NULL;
	gw_id id;
	int ret;

	ret = gw_arg_params(c, 0, &storage, &params);
	if (ret)
		return ret;
	ret = gw_arg_str0(c, 1, &uri);
	if (ret)
		return ret;
	/* Result first: no mx allocation may follow the creation of the context. */
	ret = gw_ret_id(c, 0, 0);
	if (ret) {
		mxFree(uri);
		return ret;
	}
	ctx = iio_create_context(params, uri);
	mxFree(uri);
	ret = iio_err(ctx);            /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = gw_handle_add(GW_T_CONTEXT, ctx, 0, true, &ops, NULL);
	if (!id) {
		iio_context_destroy(ctx);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* context_destroy(id): the context's descendants and borrowed ids go first (handle table). */
static int context_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_CONTEXT);
}

/* attr or 0 = context_find_attr(ctx, name) */
static int context_find_attr(struct gw_call *c)
{
	const struct iio_context *ctx;
	const struct iio_attr *attr;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	attr = iio_context_find_attr(ctx, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

/* n = context_get_version_major(ctx or 0), _minor, _patch; char = context_get_version_tag(ctx or 0).
 * Id 0 passes NULL: the version of the library itself. */
static int context_get_version_major(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle0(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_double(c, 0, iio_context_get_version_major(ctx));
}

static int context_get_version_minor(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle0(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_double(c, 0, iio_context_get_version_minor(ctx));
}

static int context_get_version_patch(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle0(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_double(c, 0, iio_context_get_version_patch(ctx));
}

static int context_get_version_tag(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle0(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_context_get_version_tag(ctx));
}

static void free_xml(void *p, size_t n)
{
	(void)n;
	iio_free(p);
}

/* char = context_get_xml(ctx). libiio returns the text from malloc: it is parked, so the
 * dispatcher frees it however the call ends; this command never frees it. */
static int context_get_xml(struct gw_call *c)
{
	const struct iio_context *ctx;
	char *xml;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);
	if (ret)
		return ret;
	xml = iio_context_get_xml(ctx);
	ret = iio_err(xml);            /* error-encoded, never NULL */
	if (ret)
		return ret;
	ret = gw_park(xml, 0, free_xml);
	if (ret)
		return gw_fail(c, ret, "no room to park the XML text");
	return gw_ret_str(c, 0, xml);
}

static int context_get_name(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_context_get_name(ctx));
}

static int context_get_description(struct gw_call *c)
{
	const struct iio_context *ctx;
	int ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_context_get_description(ctx));
}

/* List form. The result array is created first (zeroed) and the ids are written into it, so
 * nothing is allocated after the first entry was registered. */
static int context_get_attrs(struct gw_call *c)
{
	const struct iio_context *ctx;
	unsigned int n;
	gw_id id, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, &id);
	if (ret)
		return ret;
	n = iio_context_get_attrs_count(ctx);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_ATTR, iio_context_get_attr(ctx, i), id, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register attribute %u", i);
	}
	return 0;
}

static int context_get_devices(struct gw_call *c)
{
	const struct iio_context *ctx;
	unsigned int n;
	gw_id id, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, &id);
	if (ret)
		return ret;
	n = iio_context_get_devices_count(ctx);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_DEVICE, iio_context_get_device(ctx, i), id, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register device %u", i);
	}
	return 0;
}

/* dev or 0 = context_find_device(ctx, name): name is an id, a label or a name */
static int context_find_device(struct gw_call *c)
{
	const struct iio_context *ctx;
	const struct iio_device *dev;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	dev = iio_context_find_device(ctx, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_DEVICE, dev, id);
}

/* context_set_timeout(ctx, timeout_ms): of the whole context. 0 = the backend's default,
 * -1 = none, INT_MIN = non-blocking; any other negative value is libiio's -EINVAL. */
static int context_set_timeout(struct gw_call *c)
{
	struct iio_context *ctx;
	long long ms;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 1, INT_MIN, INT_MAX, &ms);
	if (ret)
		return ret;
	ret = iio_context_set_timeout(ctx, (int)ms);
	return ret < 0 ? ret : 0;
}

/* struct = context_get_params(ctx): a copy; the streams out and err are not exposed */
static int context_get_params(struct gw_call *c)
{
	static const char *const names[] = { "log_level", "stderr_level", "timestamp_level", "timeout_ms", "flags" };
	const struct iio_context_params *p;
	const struct iio_context *ctx;
	double values[5];
	mxArray *s;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);
	if (ret)
		return ret;
	p = iio_context_get_params(ctx);
	values[0] = (double)p->log_level;
	values[1] = (double)p->stderr_level;
	values[2] = (double)p->timestamp_level;
	values[3] = (double)p->timeout_ms;
	values[4] = (double)p->flags;
	s = mxCreateStructMatrix(1, 1, 5, (const char **)names);
	if (!s)
		return gw_fail(c, -ENOMEM, "out of memory for the result");
	c->plhs[0] = s;
	for (int i = 0; i < 5; i++) {
		mxArray *v = mxCreateDoubleScalar(values[i]);

		if (!v)
			return gw_fail(c, -ENOMEM, "out of memory for the result");
		mxSetFieldByNumber(s, 0, i, v);
	}
	return 0;
}

static int context_ping(struct gw_call *c)
{
	struct iio_context *ctx;
	int ret = gw_arg_handle(c, 0, GW_T_CONTEXT, &ctx, NULL);

	if (ret)
		return ret;
	ret = iio_context_ping(ctx);
	return ret < 0 ? ret : 0;
}

static const struct gw_cmd cmds[] = {
	GW_CMD("create_context", create_context, 2, 2, 1),
	GW_CMD("context_destroy", context_destroy, 1, 1, 0),
	GW_CMD("context_get_version_major", context_get_version_major, 1, 1, 1),
	GW_CMD("context_get_version_minor", context_get_version_minor, 1, 1, 1),
	GW_CMD("context_get_version_patch", context_get_version_patch, 1, 1, 1),
	GW_CMD("context_get_version_tag", context_get_version_tag, 1, 1, 1),
	GW_CMD("context_get_xml", context_get_xml, 1, 1, 1),
	GW_CMD("context_get_name", context_get_name, 1, 1, 1),
	GW_CMD("context_get_description", context_get_description, 1, 1, 1),
	GW_CMD("context_get_attrs", context_get_attrs, 1, 1, 1),
	GW_CMD("context_find_attr", context_find_attr, 2, 2, 1),
	GW_CMD("context_get_devices", context_get_devices, 1, 1, 1),
	GW_CMD("context_find_device", context_find_device, 2, 2, 1),
	GW_CMD("context_set_timeout", context_set_timeout, 2, 2, 0),
	GW_CMD("context_get_params", context_get_params, 1, 1, 1),
	GW_CMD("context_ping", context_ping, 1, 1, 0),
};

const struct gw_group gw_group_context = GW_GROUP("context", cmds);
