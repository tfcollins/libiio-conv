/* SPDX-License-Identifier: MIT */
#ifndef IIO_CONV_INTERNAL_H
#define IIO_CONV_INTERNAL_H

#include <iio/iio-conv.h>
#include <stdint.h>

struct chan_plan {
	unsigned int length, bits, shift;
	bool is_signed, swap, saturate, affine;
	/* this channel and the next form an adjacent I/Q pair that one kernel
	 * call may convert together (see plan.c) */
	bool pair;
	size_t src_off, dst_off, dst_stride;
	double a, b, inv_a;
	enum iio_conv_type type;
	uint64_t mask; /* (1 << bits) - 1 */
	int64_t lo;    /* channel value range */
	uint64_t hi;
};

/* src and dst already point at the channel's first sample / element.
 * The host pointer advances by p->dst_stride, the storage pointer by step. */
typedef void (*conv_rd_fn)(const uint8_t *src, size_t step, uint8_t *dst,
		size_t n, const struct chan_plan *p);
typedef void (*conv_wr_fn)(uint8_t *dst, size_t step, const uint8_t *src,
		size_t n, const struct chan_plan *p);

struct conv_backend {
	const char *name;
	/* Return NULL when there is no fast path. With p->pair set, return an
	 * I/Q pair kernel or NULL. */
	conv_rd_fn (*pick_rd)(const struct chan_plan *p);
	conv_wr_fn (*pick_wr)(const struct chan_plan *p);
};

extern const struct conv_backend conv_backend_scalar; /* never returns NULL */
extern const struct conv_backend conv_backend_sse41;
extern const struct conv_backend conv_backend_avx2;
extern const struct conv_backend conv_backend_avx512;
extern const struct conv_backend conv_backend_neon;

/* Best backend for this CPU, honouring IIO_CONV_FORCE. Cached. */
const struct conv_backend *conv_dispatch(void);
/* Backend by name, NULL if not compiled in or not supported by the CPU */
const struct conv_backend *conv_backend_by_name(const char *name);

struct iio_conv {
	unsigned int nb_chans, nb_cols;
	size_t step, host_step, elem_size;
	bool columns;
	struct chan_plan *chans;
	conv_rd_fn *rd; /* one per channel, never NULL */
	conv_wr_fn *wr;
	/* rd_pair[2k] != NULL: converts channels 2k and 2k+1 in one pass.
	 * Called with channel 2k's plan and pointers; Q lives length/8 bytes
	 * after I in storage and one host element after I in the host array. */
	conv_rd_fn *rd_pair;
	conv_wr_fn *wr_pair;
};

size_t conv_type_size(enum iio_conv_type type);

/* Test hooks */
const struct chan_plan *iio_conv_test_get_plan(const struct iio_conv *conv,
		unsigned int chan);
int iio_conv_create_raw_backend(const char *backend,
		const struct iio_conv_chan *chans, unsigned int nb_chans,
		size_t step, const struct iio_conv_spec *spec,
		struct iio_conv **conv);

#endif /* IIO_CONV_INTERNAL_H */
