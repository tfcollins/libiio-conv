/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "test": the stub table of Task 4b. Task 10 replaces gw_stub
 * by the command functions; no entry is added, removed or renamed. */
#include "mx_util.h"

#ifdef IIO_MEX_TEST_HOOKS

static const struct gw_cmd cmds[] = {
	GW_XCMD("test_create_fake_context", gw_stub, 1, 1, 1),
	GW_XCMD("test_fake_last_timeout", gw_stub, 0, 0, 1),
	GW_XCMD("test_fake_last_write", gw_stub, 0, 0, 1),
	GW_XCMD("test_fake_set", gw_stub, 2, 2, 0),
	GW_XCMD("test_fake_log", gw_stub, 0, 0, 1),
};

const struct gw_group gw_group_test = GW_GROUP("test", cmds);

#endif /* IIO_MEX_TEST_HOOKS */
