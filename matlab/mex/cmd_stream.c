/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "stream": the stub table of Task 4b. Task 18 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("buffer_create_stream", gw_stub, 4, 4, 1),
	GW_CMD("stream_get_next_block", gw_stub, 1, 1, 1),
	GW_CMD("stream_cancel", gw_stub, 1, 1, 0),
	GW_CMD("stream_destroy", gw_stub, 1, 1, 0),
};

const struct gw_group gw_group_stream = GW_GROUP("stream", cmds);
