/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "device": the stub table of Task 4b. Task 14 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("device_get_context", gw_stub, 1, 1, 1),
	GW_CMD("device_get_id", gw_stub, 1, 1, 1),
	GW_CMD("device_get_name", gw_stub, 1, 1, 1),
	GW_CMD("device_get_label", gw_stub, 1, 1, 1),
	GW_CMD("device_get_channels", gw_stub, 1, 1, 1),
	GW_CMD("device_get_attrs", gw_stub, 1, 1, 1),
	GW_CMD("device_get_buffers", gw_stub, 1, 1, 1),
	GW_CMD("device_find_channel", gw_stub, 3, 3, 1),
	GW_CMD("device_find_attr", gw_stub, 2, 2, 1),
	GW_CMD("device_is_trigger", gw_stub, 1, 1, 1),
	GW_CMD("device_is_hwmon", gw_stub, 1, 1, 1),
};

const struct gw_group gw_group_device = GW_GROUP("device", cmds);
