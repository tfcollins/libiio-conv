/* SPDX-License-Identifier: MIT */
/* Every command group of the iio_mex MEX file. A gateway test defines its own gw_groups[]
 * (MT_GROUPS) with the groups it links, so this file is not part of the test runtime. */
#include "mx_util.h"

const struct gw_group *const gw_groups[] = {
	&gw_group_mex,
	&gw_group_toplevel,
	&gw_group_context,
	&gw_group_attr,
	&gw_group_device,
	&gw_group_channel,
	&gw_group_buffer,
	&gw_group_block,
	&gw_group_stream,
	&gw_group_event,
	&gw_group_lowlevel,
	&gw_group_conv,
#ifdef IIO_MEX_TEST_HOOKS
	&gw_group_test,
#endif
	NULL
};
