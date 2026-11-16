/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "lowlevel": the stub table of Task 4b. Task 20 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("device_get_sample_size", gw_stub, 2, 2, 1),
	GW_CMD("device_get_trigger", gw_stub, 1, 1, 1),
	GW_CMD("device_set_trigger", gw_stub, 2, 2, 0),
	GW_CMD("device_reg_read", gw_stub, 2, 2, 1),
	GW_CMD("device_reg_write", gw_stub, 3, 3, 0),
	GW_CMD("device_get_debug_attrs", gw_stub, 1, 1, 1),
	GW_CMD("device_get_event_attrs", gw_stub, 1, 1, 1),
	GW_CMD("device_find_debug_attr", gw_stub, 2, 2, 1),
	GW_CMD("device_find_event_attr", gw_stub, 2, 2, 1),
};

const struct gw_group gw_group_lowlevel = GW_GROUP("lowlevel", cmds);
