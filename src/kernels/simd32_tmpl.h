/* SPDX-License-Identifier: MIT */
/*
 * SIMD kernels for 32-bit storage and int32 / float / double host arrays.
 *
 * The including file provides, for a vector type sd_t holding SD 32-bit
 * lanes:
 *
 *   S_NAME(x), SD
 *   sd_load / sd_store               SD contiguous 32-bit words
 *   sd_gather(base, idx)             idx: sd_t of byte offsets
 *   sd_scatter(base, idx, w)
 *   sd_iota                          lanes 0, 1, 2, ...
 *   sd_bswap, sd_srl, sd_sll, sd_and, sd_xor, sd_sub, sd_add, sd_mullo, sd_set1
 *   sd_min_u, sd_min_s, sd_max_s
 *   sd_to_f32 / sd_to_f64(w, a, b, dst)      signed lanes -> value * a + b
 *   sd_from_f32(src, b, inv_a, lo, hi, hi_int)
 *   sd_from_f64(src, b, inv_a, lo, hi)
 *                                    (x - b) * inv_a, NaN -> 0, clamp, round
 *                                    to nearest even, as int32 lanes
 *
 * Unsigned channels with all 32 bits valid are left to the generic kernels
 * for float hosts, since their values do not fit a signed lane.
 */

enum { S_NAME(D_CONTIG), S_NAME(D_GATHER), S_NAME(D_GATHER_PAIR) };

#ifndef S_INLINE
#define S_INLINE static inline __attribute__((always_inline))
#endif

/* Byte offsets of the SD lanes: one word every step bytes, or pairs of
 * adjacent words every step bytes. Kept in a register: a 64-byte stack array
 * here gets an aligned load that the AddressSanitizer fake stack does not
 * honour (GCC PR 110027). */
S_INLINE sd_t S_NAME(d_index)(size_t step, int mode)
{
	const sd_t iota = sd_iota(), vstep = sd_set1((uint32_t)step);

	if (mode == S_NAME(D_GATHER_PAIR))
		return sd_add(sd_mullo(sd_srl(iota, 1), vstep),
				sd_sll(sd_and(iota, sd_set1(1)), 2));
	return sd_mullo(iota, vstep);
}

S_INLINE size_t S_NAME(rd32_core)(const uint8_t *src, size_t step, uint8_t *dst, size_t n,
		const struct chan_plan *p, int mode, enum iio_conv_type type)
{
	const size_t tsize = type == IIO_CONV_F64 ? 8 : 4;
	const size_t per = mode == S_NAME(D_GATHER_PAIR) ? SD / 2 : SD;
	const size_t in_adv = mode == S_NAME(D_CONTIG) ? 4 : step;
	const size_t out_adv = mode == S_NAME(D_GATHER_PAIR) ? 2 * tsize : tsize;
	const int shift = (int)p->shift;
	const bool sgn = p->is_signed, swap = p->swap;
	const bool sat_u = p->saturate && !sgn;
	const sd_t mask = sd_set1((uint32_t)p->mask);
	const sd_t sbit = sd_set1(UINT32_C(1) << (p->bits - 1));
	const sd_t i32max = sd_set1(0x7FFFFFFF);
	const float a32 = (float)p->a, b32 = (float)p->b;
	const double a64 = p->a, b64 = p->b;
	const sd_t idx = S_NAME(d_index)(step, mode);
	size_t i;

	for (i = 0; i + per <= n; i += per) {
		const uint8_t *s = src + i * in_adv;
		uint8_t *d = dst + i * out_adv;
		sd_t w = mode == S_NAME(D_CONTIG) ? sd_load(s) : sd_gather(s, idx);

		if (swap)
			w = sd_bswap(w);
		w = sd_and(sd_srl(w, shift), mask);
		if (sgn)
			w = sd_sub(sd_xor(w, sbit), sbit);

		if (type == IIO_CONV_F32) {
			sd_to_f32(w, a32, b32, d);
		} else if (type == IIO_CONV_F64) {
			sd_to_f64(w, a64, b64, d);
		} else {
			if (sat_u)
				w = sd_min_u(w, i32max);
			sd_store(d, w);
		}
	}

	return i;
}

S_INLINE size_t S_NAME(wr32_core)(uint8_t *dst, size_t step, const uint8_t *src, size_t n,
		const struct chan_plan *p, int mode, enum iio_conv_type type)
{
	const size_t tsize = type == IIO_CONV_F64 ? 8 : 4;
	const size_t per = mode == S_NAME(D_GATHER_PAIR) ? SD / 2 : SD;
	const size_t st_adv = mode == S_NAME(D_CONTIG) ? 4 : step;
	const size_t host_adv = mode == S_NAME(D_GATHER_PAIR) ? 2 * tsize : tsize;
	const int shift = (int)p->shift;
	const bool swap = p->swap, sat = p->saturate;
	const int32_t hi_int = p->hi > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)p->hi;
	const sd_t mask = sd_set1((uint32_t)p->mask);
	const sd_t clo = sd_set1((uint32_t)(int32_t)p->lo);
	const sd_t chi = sd_set1((uint32_t)hi_int);
	const float b32 = (float)p->b, inv32 = (float)p->inv_a;
	const float lo32 = (float)p->lo, hi32 = (float)p->hi;
	const double b64 = p->b, inv64 = p->inv_a;
	const double lo64 = (double)p->lo, hi64 = (double)p->hi;
	const sd_t idx = S_NAME(d_index)(step, mode);
	size_t i;

	for (i = 0; i + per <= n; i += per) {
		const uint8_t *h = src + i * host_adv;
		uint8_t *d = dst + i * st_adv;
		sd_t w;

		if (type == IIO_CONV_F32) {
			w = sd_from_f32(h, b32, inv32, lo32, hi32, hi_int);
		} else if (type == IIO_CONV_F64) {
			w = sd_from_f64(h, b64, inv64, lo64, hi64);
		} else {
			w = sd_load(h);
			if (sat)
				w = sd_max_s(sd_min_s(w, chi), clo);
		}

		w = sd_sll(sd_and(w, mask), shift);
		if (swap)
			w = sd_bswap(w);
		if (mode == S_NAME(D_CONTIG))
			sd_store(d, w);
		else
			sd_scatter(d, idx, w);
	}

	return i;
}

#define S_DEFINE_SINGLE32(SUFFIX, TYPE) \
static void S_NAME(rd32_##SUFFIX)(const uint8_t *src, size_t step, uint8_t *dst, size_t n, \
		const struct chan_plan *p) \
{ \
	int mode = step == 4 ? S_NAME(D_CONTIG) : S_NAME(D_GATHER); \
	size_t i = S_NAME(rd32_core)(src, step, dst, n, p, mode, TYPE); \
\
	S_NAME(rd_generic)(src + i * step, step, dst + i * p->dst_stride, n - i, p); \
} \
\
static void S_NAME(wr32_##SUFFIX)(uint8_t *dst, size_t step, const uint8_t *src, size_t n, \
		const struct chan_plan *p) \
{ \
	int mode = step == 4 ? S_NAME(D_CONTIG) : S_NAME(D_GATHER); \
	size_t i = S_NAME(wr32_core)(dst, step, src, n, p, mode, TYPE); \
\
	S_NAME(wr_generic)(dst + i * step, step, src + i * p->dst_stride, n - i, p); \
}

#define S_DEFINE_PAIR32(SUFFIX, TYPE) \
static void S_NAME(rd32_pair_##SUFFIX)(const uint8_t *src, size_t step, uint8_t *dst, \
		size_t n, const struct chan_plan *p) \
{ \
	const size_t half = p->dst_stride / 2; \
	size_t i; \
\
	if (step == 8) \
		i = S_NAME(rd32_core)(src, 4, dst, 2 * n, p, S_NAME(D_CONTIG), TYPE) / 2; \
	else \
		i = S_NAME(rd32_core)(src, step, dst, n, p, S_NAME(D_GATHER_PAIR), TYPE); \
\
	src += i * step; \
	dst += i * p->dst_stride; \
	S_NAME(rd_generic)(src, step, dst, n - i, p); \
	S_NAME(rd_generic)(src + 4, step, dst + half, n - i, p); \
} \
\
static void S_NAME(wr32_pair_##SUFFIX)(uint8_t *dst, size_t step, const uint8_t *src, \
		size_t n, const struct chan_plan *p) \
{ \
	const size_t half = p->dst_stride / 2; \
	size_t i; \
\
	if (step == 8) \
		i = S_NAME(wr32_core)(dst, 4, src, 2 * n, p, S_NAME(D_CONTIG), TYPE) / 2; \
	else \
		i = S_NAME(wr32_core)(dst, step, src, n, p, S_NAME(D_GATHER_PAIR), TYPE); \
\
	dst += i * step; \
	src += i * p->dst_stride; \
	S_NAME(wr_generic)(dst, step, src, n - i, p); \
	S_NAME(wr_generic)(dst + 4, step, src + half, n - i, p); \
}

S_DEFINE_SINGLE32(i32, IIO_CONV_I32)
S_DEFINE_SINGLE32(f32, IIO_CONV_F32)
S_DEFINE_SINGLE32(f64, IIO_CONV_F64)
S_DEFINE_PAIR32(i32, IIO_CONV_I32)
S_DEFINE_PAIR32(f32, IIO_CONV_F32)
S_DEFINE_PAIR32(f64, IIO_CONV_F64)

static bool S_NAME(simd32_ok)(const struct chan_plan *p)
{
	size_t tsize = conv_type_size(p->type);

	if (p->length != 32)
		return false;
	if (p->type != IIO_CONV_I32 && !p->is_signed && p->bits == 32)
		return false;
	return p->dst_stride == (p->pair ? 2 * tsize : tsize);
}

static conv_rd_fn S_NAME(pick_rd32)(const struct chan_plan *p)
{
	if (!S_NAME(simd32_ok)(p))
		return NULL;
	switch (p->type) {
	case IIO_CONV_I32:
		return p->pair ? S_NAME(rd32_pair_i32) : S_NAME(rd32_i32);
	case IIO_CONV_F32:
		return p->pair ? S_NAME(rd32_pair_f32) : S_NAME(rd32_f32);
	case IIO_CONV_F64:
		return p->pair ? S_NAME(rd32_pair_f64) : S_NAME(rd32_f64);
	default:
		return NULL;
	}
}

static conv_wr_fn S_NAME(pick_wr32)(const struct chan_plan *p)
{
	if (!S_NAME(simd32_ok)(p))
		return NULL;
	switch (p->type) {
	case IIO_CONV_I32:
		return p->pair ? S_NAME(wr32_pair_i32) : S_NAME(wr32_i32);
	case IIO_CONV_F32:
		return p->pair ? S_NAME(wr32_pair_f32) : S_NAME(wr32_f32);
	case IIO_CONV_F64:
		return p->pair ? S_NAME(wr32_pair_f64) : S_NAME(wr32_f64);
	default:
		return NULL;
	}
}
