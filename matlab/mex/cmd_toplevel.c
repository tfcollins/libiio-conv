/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "toplevel": the stub table of Task 4b. Task 11 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("strerror", gw_stub, 1, 1, 1),
	GW_CMD("has_backend", gw_stub, 2, 2, 1),
	GW_CMD("get_builtin_backends", gw_stub, 0, 0, 1),
	GW_CMD("scan", gw_stub, 2, 2, 1),
	GW_CMD("scan_destroy", gw_stub, 1, 1, 0),
	GW_CMD("scan_get_results", gw_stub, 1, 1, 2),
};

const struct gw_group gw_group_toplevel = GW_GROUP("toplevel", cmds);
