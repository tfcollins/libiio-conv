/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "context": the stub table of Task 4b. Task 12 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

static const struct gw_cmd cmds[] = {
	GW_CMD("create_context", gw_stub, 2, 2, 1),
	GW_CMD("context_destroy", gw_stub, 1, 1, 0),
	GW_CMD("context_get_version_major", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_minor", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_patch", gw_stub, 1, 1, 1),
	GW_CMD("context_get_version_tag", gw_stub, 1, 1, 1),
	GW_CMD("context_get_xml", gw_stub, 1, 1, 1),
	GW_CMD("context_get_name", gw_stub, 1, 1, 1),
	GW_CMD("context_get_description", gw_stub, 1, 1, 1),
	GW_CMD("context_get_attrs", gw_stub, 1, 1, 1),
	GW_CMD("context_find_attr", gw_stub, 2, 2, 1),
	GW_CMD("context_get_devices", gw_stub, 1, 1, 1),
	GW_CMD("context_find_device", gw_stub, 2, 2, 1),
	GW_CMD("context_set_timeout", gw_stub, 2, 2, 0),
	GW_CMD("context_get_params", gw_stub, 1, 1, 1),
	GW_CMD("context_ping", gw_stub, 1, 1, 0),
};

const struct gw_group gw_group_context = GW_GROUP("context", cmds);
