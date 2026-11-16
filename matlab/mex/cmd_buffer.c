/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "buffer": the stub table of Task 4b. Task 16 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("buffer_get_device", gw_stub, 1, 1, 1),
	GW_CMD("buffer_is_output", gw_stub, 1, 1, 1),
	GW_CMD("buffer_get_attrs", gw_stub, 1, 1, 1),
	GW_CMD("buffer_find_attr", gw_stub, 2, 2, 1),
	GW_CMD("buffer_get_scan_elements", gw_stub, 1, 1, 1),
	GW_CMD("buffer_open", gw_stub, 2, 2, 1),
	GW_CMD("buffer_close", gw_stub, 1, 1, 0),
	GW_CMD("buffer_stream_start", gw_stub, 1, 1, 0),
	GW_CMD("buffer_stream_stop", gw_stub, 1, 1, 0),
	GW_CMD("buffer_stream_cancel", gw_stub, 1, 1, 0),
	GW_CMD("buffer_stream_get_channels_mask", gw_stub, 1, 1, 1),
	GW_CMD("buffer_stream_create_block", gw_stub, 2, 2, 1),
	GW_XCMD("buffer_stream_is_started", gw_stub, 1, 1, 1),
};

const struct gw_group gw_group_buffer = GW_GROUP("buffer", cmds);
