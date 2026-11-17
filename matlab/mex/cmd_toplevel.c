/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "toplevel": error text, backend queries, context scan. */
#include "mx_util.h"

#include <limits.h>

/* char = strerror(err): libiio's text, "Invalid argument (-22)". INT_MIN is refused: libiio
 * negates the value. */
static int cmd_strerror(struct gw_call *c)
{
	char buf[256];
	long long err;
	int ret;

	ret = gw_arg_int(c, 0, -INT_MAX, INT_MAX, &err);
	if (ret)
		return ret;
	buf[0] = '\0';
	iio_strerror((int)err, buf, sizeof(buf));
	return gw_ret_str(c, 0, buf);
}

/* tf = has_backend(params, backend) */
static int has_backend(struct gw_call *c)
{
	struct iio_context_params storage;
	const struct iio_context_params *params;
	char *backend;
	bool found;
	int ret;

	ret = gw_arg_params(c, 0, &storage, &params);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &backend);
	if (ret)
		return ret;
	found = iio_has_backend(params, backend);
	mxFree(backend);
	return gw_ret_bool(c, 0, found);
}

/* One element of a list of text: element i of obj, NULL when there is none. */
typedef const char *(*text_at)(const void *obj, size_t i);

/* Output k = 1-by-n cell of char rows, element i = get(obj, i) (NULL reads as ''). Nothing
 * when k > 0 and k >= nlhs. The cell is in plhs before its elements are made, so a failed
 * allocation leaves nothing behind that MATLAB does not own. */
static int ret_text_list(struct gw_call *c, int k, text_at get, const void *obj, size_t n)
{
	mxArray *cell;

	if (k > 0 && k >= c->nlhs)
		return 0;
	cell = mxCreateCellMatrix(1, n);
	if (!cell)
		return gw_fail(c, -ENOMEM, "out of memory for output %d", k + 1);
	c->plhs[k] = cell;
	for (size_t i = 0; i < n; i++) {
		const char *s = get(obj, i);
		mxArray *e = mxCreateString(s ? s : "");

		if (!e)
			return gw_fail(c, -ENOMEM, "out of memory for output %d", k + 1);
		mxSetCell(cell, i, e);
	}
	return 0;
}

static const char *backend_at(const void *obj, size_t i)
{
	(void)obj;
	return iio_get_builtin_backend((unsigned int)i);
}

/* cell = get_builtin_backends(): list form, libiio order */
static int get_builtin_backends(struct gw_call *c)
{
	return ret_text_list(c, 0, backend_at, NULL, iio_get_builtin_backends_count());
}

static void scan_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_scan_destroy(ptr);
}

/* scan = scan(params, backends or []): [] is C NULL, libiio's default list of backends. */
static int scan(struct gw_call *c)
{
	static const struct gw_ops ops = { NULL, scan_destroy_op };
	struct iio_context_params storage;
	const struct iio_context_params *params;
	struct gw_c_numeric loc;
	struct iio_scan *s;
	char *backends = NULL;
	gw_id id;
	int ret;

	ret = gw_arg_params(c, 0, &storage, &params);
	if (ret)
		return ret;
	ret = gw_arg_str0(c, 1, &backends);
	if (ret)
		return ret;
	/* Result first: no mx allocation may follow the creation of the scan. */
	ret = gw_ret_id(c, 0, 0);
	if (ret) {
		mxFree(backends);
		return ret;
	}
	/* a backend's scan may read a context description as iio_create_context does (strtof) */
	ret = gw_c_numeric_enter(&loc);
	if (ret) {
		mxFree(backends);
		return gw_fail(c, ret, "no \"C\" numeric locale");
	}
	s = iio_scan(params, backends);
	gw_c_numeric_leave(&loc);
	mxFree(backends);
	ret = iio_err(s);              /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = gw_handle_add(GW_T_SCAN, s, 0, true, &ops, NULL);
	if (!id) {
		iio_scan_destroy(s);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* scan_destroy(id) */
static int scan_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_SCAN);
}

static const char *uri_at(const void *obj, size_t i)
{
	return iio_scan_get_uri(obj, i);
}

static const char *description_at(const void *obj, size_t i)
{
	return iio_scan_get_description(obj, i);
}

/* [uris, descriptions] = scan_get_results(scan): list form, each a 1-by-N cell of char */
static int scan_get_results(struct gw_call *c)
{
	const struct iio_scan *s;
	size_t n;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_SCAN, &s, NULL);
	if (ret)
		return ret;
	n = iio_scan_get_results_count(s);
	ret = ret_text_list(c, 0, uri_at, s, n);
	if (ret)
		return ret;
	return ret_text_list(c, 1, description_at, s, n);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("strerror", cmd_strerror, 1, 1, 1),
	GW_CMD("has_backend", has_backend, 2, 2, 1),
	GW_CMD("get_builtin_backends", get_builtin_backends, 0, 0, 1),
	GW_CMD("scan", scan, 2, 2, 1),
	GW_CMD("scan_destroy", scan_destroy, 1, 1, 0),
	GW_CMD("scan_get_results", scan_get_results, 1, 1, 2),
};

const struct gw_group gw_group_toplevel = GW_GROUP("toplevel", cmds);
