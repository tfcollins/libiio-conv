/* SPDX-License-Identifier: MIT */
/*
 * SIMD kernels for 8- and 16-bit storage and int16 / float / double host
 * arrays, written once over a small per-ISA vocabulary.
 *
 * The including file provides, for a vector type sw_t holding SK 16-bit
 * lanes:
 *
 *   S_NAME(x)                       name mangling
 *   SK                              lanes per vector (even)
 *   sw_load16 / sw_load8            SK contiguous words / zero-extended bytes
 *   sw_gather16 / sw_gather8        one every step bytes
 *   sw_gather_pair16                SK/2 pairs of adjacent words, every step
 *   sw_store16 / sw_store8          contiguous stores (store8: low bytes)
 *   sw_scatter16 / sw_scatter8 / sw_scatter_pair16
 *   sw_bswap, sw_srl, sw_sll, sw_and, sw_xor, sw_sub, sw_set1
 *   sw_min_u, sw_min_s, sw_max_s
 *   sw_to_f32 / sw_to_f64(w, sgn, a, b, dst)     lanes -> value * a + b
 *   sw_from_f32 / sw_from_f64(src, b, inv_a, lo, hi)
 *                                   (x - b) * inv_a, NaN -> 0, clamp, round
 *                                   to nearest even; low 16 bits per lane
 *
 * and includes kernel_tmpl.h with KT_NAME(x) = S_NAME(x), which supplies the
 * generic kernels used for the tails. Loads and stores never touch bytes
 * outside the samples being converted.
 */

enum { S_NAME(M_CONTIG), S_NAME(M_GATHER), S_NAME(M_GATHER_PAIR) };

#define S_INLINE static inline __attribute__((always_inline))

/* Converts as many whole vectors as fit in n and returns how many units of n
 * were consumed. In pair mode one unit is an I/Q pair, otherwise one word. */
S_INLINE size_t S_NAME(rd_core)(const uint8_t *src, size_t step, uint8_t *dst, size_t n,
		const struct chan_plan *p, int mode, enum iio_conv_type type)
{
	const size_t lb = p->length / 8;
	const size_t tsize = type == IIO_CONV_F64 ? 8 : type == IIO_CONV_F32 ? 4 : 2;
	const size_t per = mode == S_NAME(M_GATHER_PAIR) ? SK / 2 : SK;
	const size_t in_adv = mode == S_NAME(M_CONTIG) ? lb : step;
	const size_t out_adv = mode == S_NAME(M_GATHER_PAIR) ? 2 * tsize : tsize;
	const int shift = (int)p->shift;
	const bool sgn = p->is_signed, swap = p->swap;
	const bool sat_u = p->saturate && !sgn;
	const sw_t mask = sw_set1((uint16_t)p->mask);
	const sw_t sbit = sw_set1((uint16_t)(1u << (p->bits - 1)));
	const sw_t i16max = sw_set1(0x7FFF);
	const float a32 = (float)p->a, b32 = (float)p->b;
	const double a64 = p->a, b64 = p->b;
	size_t i;

	for (i = 0; i + per <= n; i += per) {
		const uint8_t *s = src + i * in_adv;
		uint8_t *d = dst + i * out_adv;
		sw_t w;

		if (lb == 2) {
			if (mode == S_NAME(M_CONTIG))
				w = sw_load16(s);
			else if (mode == S_NAME(M_GATHER))
				w = sw_gather16(s, step);
			else
				w = sw_gather_pair16(s, step);
			if (swap)
				w = sw_bswap(w);
		} else {
			w = mode == S_NAME(M_CONTIG) ? sw_load8(s) : sw_gather8(s, step);
		}

		w = sw_and(sw_srl(w, shift), mask);
		if (sgn)
			w = sw_sub(sw_xor(w, sbit), sbit);

		if (type == IIO_CONV_F32) {
			sw_to_f32(w, sgn, a32, b32, d);
		} else if (type == IIO_CONV_F64) {
			sw_to_f64(w, sgn, a64, b64, d);
		} else {
			if (sat_u)
				w = sw_min_u(w, i16max);
			sw_store16(d, w);
		}
	}

	return i;
}

S_INLINE size_t S_NAME(wr_core)(uint8_t *dst, size_t step, const uint8_t *src, size_t n,
		const struct chan_plan *p, int mode, enum iio_conv_type type)
{
	const size_t lb = p->length / 8;
	const size_t tsize = type == IIO_CONV_F64 ? 8 : type == IIO_CONV_F32 ? 4 : 2;
	const size_t per = mode == S_NAME(M_GATHER_PAIR) ? SK / 2 : SK;
	const size_t st_adv = mode == S_NAME(M_CONTIG) ? lb : step;
	const size_t host_adv = mode == S_NAME(M_GATHER_PAIR) ? 2 * tsize : tsize;
	const int shift = (int)p->shift;
	const bool swap = p->swap, sat = p->saturate;
	const sw_t mask = sw_set1((uint16_t)p->mask);
	/* int16 host values clamped to the part of the channel range they can reach */
	const sw_t clo = sw_set1((uint16_t)(int16_t)p->lo);
	const sw_t chi = sw_set1((uint16_t)(p->hi > 0x7FFF ? 0x7FFF : p->hi));
	const float b32 = (float)p->b, inv32 = (float)p->inv_a;
	const float lo32 = (float)p->lo, hi32 = (float)p->hi;
	const double b64 = p->b, inv64 = p->inv_a;
	const double lo64 = (double)p->lo, hi64 = (double)p->hi;
	size_t i;

	for (i = 0; i + per <= n; i += per) {
		const uint8_t *h = src + i * host_adv;
		uint8_t *d = dst + i * st_adv;
		sw_t w;

		if (type == IIO_CONV_F32) {
			w = sw_from_f32(h, b32, inv32, lo32, hi32);
		} else if (type == IIO_CONV_F64) {
			w = sw_from_f64(h, b64, inv64, lo64, hi64);
		} else {
			w = sw_load16(h);
			if (sat)
				w = sw_max_s(sw_min_s(w, chi), clo);
		}

		w = sw_sll(sw_and(w, mask), shift);

		if (lb == 2) {
			if (swap)
				w = sw_bswap(w);
			if (mode == S_NAME(M_CONTIG))
				sw_store16(d, w);
			else if (mode == S_NAME(M_GATHER))
				sw_scatter16(d, step, w);
			else
				sw_scatter_pair16(d, step, w);
		} else {
			if (mode == S_NAME(M_CONTIG))
				sw_store8(d, w);
			else
				sw_scatter8(d, step, w);
		}
	}

	return i;
}

/* One channel into a contiguous host column */
#define S_DEFINE_SINGLE(SUFFIX, TYPE) \
static void S_NAME(rd_##SUFFIX)(const uint8_t *src, size_t step, uint8_t *dst, size_t n, \
		const struct chan_plan *p) \
{ \
	int mode = step == p->length / 8 ? S_NAME(M_CONTIG) : S_NAME(M_GATHER); \
	size_t i = S_NAME(rd_core)(src, step, dst, n, p, mode, TYPE); \
\
	S_NAME(rd_generic)(src + i * step, step, dst + i * p->dst_stride, n - i, p); \
} \
\
static void S_NAME(wr_##SUFFIX)(uint8_t *dst, size_t step, const uint8_t *src, size_t n, \
		const struct chan_plan *p) \
{ \
	int mode = step == p->length / 8 ? S_NAME(M_CONTIG) : S_NAME(M_GATHER); \
	size_t i = S_NAME(wr_core)(dst, step, src, n, p, mode, TYPE); \
\
	S_NAME(wr_generic)(dst + i * step, step, src + i * p->dst_stride, n - i, p); \
}

/* Adjacent I and Q into / from one interleaved complex column. When the two
 * channels fill the whole sample this is one contiguous run of 2n words. */
#define S_DEFINE_PAIR(SUFFIX, TYPE) \
static void S_NAME(rd_pair_##SUFFIX)(const uint8_t *src, size_t step, uint8_t *dst, size_t n, \
		const struct chan_plan *p) \
{ \
	const size_t lb = p->length / 8, half = p->dst_stride / 2; \
	size_t i = 0; \
\
	if (step == 2 * lb) \
		i = S_NAME(rd_core)(src, lb, dst, 2 * n, p, S_NAME(M_CONTIG), TYPE) / 2; \
	else if (lb == 2) \
		i = S_NAME(rd_core)(src, step, dst, n, p, S_NAME(M_GATHER_PAIR), TYPE); \
\
	src += i * step; \
	dst += i * p->dst_stride; \
	S_NAME(rd_generic)(src, step, dst, n - i, p); \
	S_NAME(rd_generic)(src + lb, step, dst + half, n - i, p); \
} \
\
static void S_NAME(wr_pair_##SUFFIX)(uint8_t *dst, size_t step, const uint8_t *src, size_t n, \
		const struct chan_plan *p) \
{ \
	const size_t lb = p->length / 8, half = p->dst_stride / 2; \
	size_t i = 0; \
\
	if (step == 2 * lb) \
		i = S_NAME(wr_core)(dst, lb, src, 2 * n, p, S_NAME(M_CONTIG), TYPE) / 2; \
	else if (lb == 2) \
		i = S_NAME(wr_core)(dst, step, src, n, p, S_NAME(M_GATHER_PAIR), TYPE); \
\
	dst += i * step; \
	src += i * p->dst_stride; \
	S_NAME(wr_generic)(dst, step, src, n - i, p); \
	S_NAME(wr_generic)(dst + lb, step, src + half, n - i, p); \
}

S_DEFINE_SINGLE(i16, IIO_CONV_I16)
S_DEFINE_SINGLE(f32, IIO_CONV_F32)
S_DEFINE_SINGLE(f64, IIO_CONV_F64)
S_DEFINE_PAIR(i16, IIO_CONV_I16)
S_DEFINE_PAIR(f32, IIO_CONV_F32)
S_DEFINE_PAIR(f64, IIO_CONV_F64)

/* The host side must be contiguous: one element per sample for a single
 * channel, one complex element per sample for a pair. */
static bool S_NAME(simd16_ok)(const struct chan_plan *p)
{
	size_t tsize = conv_type_size(p->type);

	if (p->length != 16 && p->length != 8)
		return false;
	return p->dst_stride == (p->pair ? 2 * tsize : tsize);
}

static conv_rd_fn S_NAME(pick_rd16)(const struct chan_plan *p)
{
	if (!S_NAME(simd16_ok)(p))
		return NULL;
	switch (p->type) {
	case IIO_CONV_I16:
		return p->pair ? S_NAME(rd_pair_i16) : S_NAME(rd_i16);
	case IIO_CONV_F32:
		return p->pair ? S_NAME(rd_pair_f32) : S_NAME(rd_f32);
	case IIO_CONV_F64:
		return p->pair ? S_NAME(rd_pair_f64) : S_NAME(rd_f64);
	default:
		return NULL;
	}
}

static conv_wr_fn S_NAME(pick_wr16)(const struct chan_plan *p)
{
	if (!S_NAME(simd16_ok)(p))
		return NULL;
	switch (p->type) {
	case IIO_CONV_I16:
		return p->pair ? S_NAME(wr_pair_i16) : S_NAME(wr_i16);
	case IIO_CONV_F32:
		return p->pair ? S_NAME(wr_pair_f32) : S_NAME(wr_f32);
	case IIO_CONV_F64:
		return p->pair ? S_NAME(wr_pair_f64) : S_NAME(wr_f64);
	default:
		return NULL;
	}
}
