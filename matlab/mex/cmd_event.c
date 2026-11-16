/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "event": the stub table of Task 4b. Task 19 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("device_create_event_stream", gw_stub, 1, 1, 1),
	GW_CMD("event_stream_destroy", gw_stub, 1, 1, 0),
	GW_CMD("event_stream_read", gw_stub, 2, 2, 3),
	GW_CMD("event_get_type", gw_stub, 1, 1, 2),
	GW_CMD("event_get_direction", gw_stub, 1, 1, 2),
	GW_CMD("event_get_channel", gw_stub, 3, 3, 1),
};

const struct gw_group gw_group_event = GW_GROUP("event", cmds);
