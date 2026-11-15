/* SPDX-License-Identifier: MIT */
#include "../internal.h"

static void rd_todo(const uint8_t *src, size_t step, uint8_t *dst, size_t n,
		const struct chan_plan *p)
{
	(void)src; (void)step; (void)dst; (void)n; (void)p;
}

static void wr_todo(uint8_t *dst, size_t step, const uint8_t *src, size_t n,
		const struct chan_plan *p)
{
	(void)src; (void)step; (void)dst; (void)n; (void)p;
}

static conv_rd_fn pick_rd(const struct chan_plan *p)
{
	return p->pair ? NULL : rd_todo;
}

static conv_wr_fn pick_wr(const struct chan_plan *p)
{
	return p->pair ? NULL : wr_todo;
}

const struct conv_backend conv_backend_scalar = {
	.name = "scalar",
	.pick_rd = pick_rd,
	.pick_wr = pick_wr,
};
