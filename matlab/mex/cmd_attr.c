/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "attr": the stub table of Task 4b. Task 13 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("attr_get_name", gw_stub, 1, 1, 1),
	GW_CMD("attr_get_filename", gw_stub, 1, 1, 1),
	GW_CMD("attr_get_static_value", gw_stub, 1, 1, 1),
	GW_CMD("attr_read_raw", gw_stub, 1, 2, 1),
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
