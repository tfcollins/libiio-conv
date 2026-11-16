/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "context". The first slice (Task 8a) implements create_context,
 * context_destroy and context_find_attr; the other entries are still the stubs of Task 4b.
 * Task 12 replaces gw_stub by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

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

static const struct gw_cmd cmds[] = {
	GW_CMD("create_context", create_context, 2, 2, 1),
	GW_CMD("context_destroy", context_destroy, 1, 1, 0),
	GW_CMD("context_get_version_major", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_minor", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_patch", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_tag", gw_stub, 1, 1, 1),
	GW_CMD("context_get_xml", gw_stub, 1, 1, 1),
	GW_CMD("context_get_name", gw_stub, 1, 1, 1),
	GW_CMD("context_get_description", gw_stub, 1, 1, 1),
	GW_CMD("context_get_attrs", gw_stub, 1, 1, 1),
	GW_CMD("context_find_attr", context_find_attr, 2, 2, 1),
	GW_CMD("context_get_devices", gw_stub, 1, 1, 1),
	GW_CMD("context_find_device", gw_stub, 2, 2, 1),
	GW_CMD("context_set_timeout", gw_stub, 2, 2, 0),
	GW_CMD("context_get_params", gw_stub, 1, 1, 1),
	GW_CMD("context_ping", gw_stub, 1, 1, 0),
};

const struct gw_group gw_group_context = GW_GROUP("context", cmds);
