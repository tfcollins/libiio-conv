/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "channel": the stub table of Task 4b. Task 15 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("channel_get_device", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_id", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_name", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_label", gw_stub, 1, 1, 1),
	GW_CMD("channel_is_output", gw_stub, 1, 1, 1),
	GW_CMD("channel_is_scan_element", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_index", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_type", gw_stub, 1, 1, 2),
	GW_CMD("channel_get_modifier", gw_stub, 1, 1, 2),
	GW_CMD("hwmon_channel_get_type", gw_stub, 1, 1, 2),
	GW_CMD("channel_get_attrs", gw_stub, 1, 1, 1),
	GW_CMD("channel_get_event_attrs", gw_stub, 1, 1, 1),
	GW_CMD("channel_find_attr", gw_stub, 2, 2, 1),
	GW_CMD("channel_find_event_attr", gw_stub, 2, 2, 1),
	GW_CMD("channel_get_data_format", gw_stub, 1, 1, 1),
	GW_CMD("channel_refresh_format", gw_stub, 1, 1, 0),
	GW_CMD("channel_convert", gw_stub, 2, 2, 1),
	GW_CMD("channel_convert_inverse", gw_stub, 2, 2, 1),
	GW_CMD("channel_enable", gw_stub, 2, 2, 0),
	GW_CMD("channel_disable", gw_stub, 2, 2, 0),
	GW_CMD("channel_is_enabled", gw_stub, 2, 2, 1),
	GW_CMD("create_channels_mask", gw_stub, 1, 1, 1),
	GW_CMD("channels_mask_destroy", gw_stub, 1, 1, 0),
	GW_XCMD("channels_mask_get", gw_stub, 1, 1, 1),
};

const struct gw_group gw_group_channel = GW_GROUP("channel", cmds);
