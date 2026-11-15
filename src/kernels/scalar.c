/* SPDX-License-Identifier: MIT */
/* Portable reference backend: handles every supported conversion. */
#define KT_NAME(x) x##_scalar
#include "kernel_tmpl.h"

static conv_rd_fn pick_rd(const struct chan_plan *p)
{
	return p->pair ? NULL : rd_generic_scalar;
}

static conv_wr_fn pick_wr(const struct chan_plan *p)
{
	return p->pair ? NULL : wr_generic_scalar;
}

const struct conv_backend conv_backend_scalar = {
	.name = "scalar",
	.pick_rd = pick_rd,
	.pick_wr = pick_wr,
};
