/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "block": the stub table of Task 4b. Task 17 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("block_destroy", gw_stub, 1, 1, 0),
	GW_CMD("block_first", gw_stub, 2, 2, 1),
	GW_CMD("block_enqueue", gw_stub, 3, 3, 0),
	GW_CMD("block_dequeue", gw_stub, 2, 2, 1),
	GW_CMD("block_get_buffer_stream", gw_stub, 1, 1, 1),
	GW_CMD("block_get_dmabuf_fd", gw_stub, 1, 1, 1),
	GW_CMD("block_disable_cpu_access", gw_stub, 2, 2, 0),
	GW_CMD("channel_read", gw_stub, 3, 3, 1),
	GW_CMD("channel_write", gw_stub, 4, 4, 1),
	GW_XCMD("block_size", gw_stub, 1, 1, 1),
	GW_XCMD("block_get_bytes", gw_stub, 1, 3, 1),
	GW_XCMD("block_set_bytes", gw_stub, 2, 3, 1),
	GW_XCMD("block_read", gw_stub, 2, 2, 1),
	GW_XCMD("block_write", gw_stub, 3, 3, 1),
};

const struct gw_group gw_group_block = GW_GROUP("block", cmds);
