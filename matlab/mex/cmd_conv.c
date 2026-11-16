/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "conv": the stub table of Task 4b. Task 21 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_XCMD("conv_stream_open", gw_stub, 3, 3, 1),
	GW_XCMD("conv_stream_read", gw_stub, 1, 1, 2),
	GW_XCMD("conv_stream_write", gw_stub, 2, 2, 1),
	GW_XCMD("conv_stream_flush", gw_stub, 1, 1, 1),
	GW_XCMD("conv_stream_close", gw_stub, 1, 1, 0),
};

const struct gw_group gw_group_conv = GW_GROUP("conv", cmds);
