/* SPDX-License-Identifier: MIT */
/*
 * Generic strided kernels for every supported storage length and host type.
 *
 * Include with KT_NAME(x) defined to give the functions a per-backend name.
 * Provides:
 *   static void KT_NAME(rd_generic)(src, step, dst, n, plan)
 *   static void KT_NAME(wr_generic)(dst, step, src, n, plan)
 *
 * This file defines the reference behaviour; every SIMD kernel must produce
 * exactly the same bytes. The translation unit must be compiled with
 * -ffp-contract=off so that raw * a + b is never fused.
 */
#include "../internal.h"

#include <math.h>
#include <string.h>

#ifndef KT_NAME
#error "define KT_NAME before including kernel_tmpl.h"
#endif

#ifndef KT_COMMON
#define KT_COMMON

#define kt_bswap8(x) (x)
#define kt_bswap16(x) __builtin_bswap16(x)
#define kt_bswap32(x) __builtin_bswap32(x)
#define kt_bswap64(x) __builtin_bswap64(x)

/* Clamp a value, given both as signed (v) and unsigned (u) 64-bit, into
 * [lo, hi]. Returns the two's complement bits of the result. */
static inline uint64_t kt_sat(bool src_signed, int64_t v, uint64_t u, int64_t lo, uint64_t hi)
{
	if (src_signed) {
		if (v < lo)
			return (uint64_t)lo;
		if (v >= 0 && (uint64_t)v > hi)
			return hi;
		return (uint64_t)v;
	}
	return u > hi ? hi : u;
}

static inline void kt_type_range(enum iio_conv_type type, bool *is_signed, int64_t *lo,
		uint64_t *hi)
{
	switch (type) {
	case IIO_CONV_I8:  *is_signed = true;  *lo = INT8_MIN;  *hi = INT8_MAX;   break;
	case IIO_CONV_U8:  *is_signed = false; *lo = 0;         *hi = UINT8_MAX;  break;
	case IIO_CONV_I16: *is_signed = true;  *lo = INT16_MIN; *hi = INT16_MAX;  break;
	case IIO_CONV_U16: *is_signed = false; *lo = 0;         *hi = UINT16_MAX; break;
	case IIO_CONV_I32: *is_signed = true;  *lo = INT32_MIN; *hi = INT32_MAX;  break;
	case IIO_CONV_U32: *is_signed = false; *lo = 0;         *hi = UINT32_MAX; break;
	case IIO_CONV_I64: *is_signed = true;  *lo = INT64_MIN; *hi = INT64_MAX;  break;
	default:           *is_signed = false; *lo = 0;         *hi = UINT64_MAX; break;
	}
}

/* Storage word -> value, as u (zero extended) and v (sign extended when the
 * channel is signed). */
#define KT_LOAD(UT, BSWAP) \
	UT w_; \
	memcpy(&w_, src, sizeof(w_)); \
	if (swap) \
		w_ = (UT)BSWAP(w_); \
	const uint64_t u = ((uint64_t)w_ >> shift) & mask; \
	const int64_t v = (int64_t)((u ^ sbit) - sbit)

#define KT_RD_INT(UT, BSWAP, CT) \
	for (i = 0; i < n; i++, src += step, dst += ds) { \
		KT_LOAD(UT, BSWAP); \
		CT o = (CT)(sat ? kt_sat(sgn, v, u, tlo, thi) : (uint64_t)v); \
		memcpy(dst, &o, sizeof(o)); \
	}

#define KT_RD_FLT(UT, BSWAP, CT) \
	{ \
		const CT a = (CT)p->a, b = (CT)p->b; \
		for (i = 0; i < n; i++, src += step, dst += ds) { \
			KT_LOAD(UT, BSWAP); \
			CT o = sgn ? (CT)v : (CT)u; \
			o = o * a; \
			o = o + b; \
			memcpy(dst, &o, sizeof(o)); \
		} \
	}

/* Raw value bits r -> storage word */
#define KT_STORE(UT, BSWAP) \
	UT w_ = (UT)((r & mask) << shift); \
	if (swap) \
		w_ = (UT)BSWAP(w_); \
	memcpy(dst, &w_, sizeof(w_))

#define KT_WR_INT(UT, BSWAP, CT) \
	for (i = 0; i < n; i++, dst += step, src += ds) { \
		CT x; \
		memcpy(&x, src, sizeof(x)); \
		uint64_t r = sat ? kt_sat(tsgn, (int64_t)x, (uint64_t)x, p->lo, p->hi) : (uint64_t)x; \
		KT_STORE(UT, BSWAP); \
	}

/* Nearest (ties to even) under the default rounding mode, NaN -> 0, and
 * always saturate to the channel range. */
#define KT_WR_FLT(UT, BSWAP, CT, RINT) \
	{ \
		const CT b = (CT)p->b, inv_a = (CT)p->inv_a; \
		const CT flo = (CT)p->lo, fhi = (CT)p->hi; \
		for (i = 0; i < n; i++, dst += step, src += ds) { \
			CT x, t; \
			uint64_t r; \
			memcpy(&x, src, sizeof(x)); \
			t = x - b; \
			t = t * inv_a; \
			if (t != t) { \
				r = 0; \
			} else { \
				t = RINT(t); \
				if (t <= flo) \
					r = (uint64_t)p->lo; \
				else if (t >= fhi) \
					r = p->hi; \
				else \
					r = sgn ? (uint64_t)(int64_t)t : (uint64_t)t; \
			} \
			KT_STORE(UT, BSWAP); \
		} \
	}

#define KT_SWITCH(M, UT, BSWAP) \
	switch (p->type) { \
	case IIO_CONV_I8:  M##_INT(UT, BSWAP, int8_t) break; \
	case IIO_CONV_U8:  M##_INT(UT, BSWAP, uint8_t) break; \
	case IIO_CONV_I16: M##_INT(UT, BSWAP, int16_t) break; \
	case IIO_CONV_U16: M##_INT(UT, BSWAP, uint16_t) break; \
	case IIO_CONV_I32: M##_INT(UT, BSWAP, int32_t) break; \
	case IIO_CONV_U32: M##_INT(UT, BSWAP, uint32_t) break; \
	case IIO_CONV_I64: M##_INT(UT, BSWAP, int64_t) break; \
	case IIO_CONV_U64: M##_INT(UT, BSWAP, uint64_t) break; \
	case IIO_CONV_F32: M##_F32(UT, BSWAP) break; \
	case IIO_CONV_F64: M##_F64(UT, BSWAP) break; \
	}

#define KT_RD_F32(UT, BSWAP) KT_RD_FLT(UT, BSWAP, float)
#define KT_RD_F64(UT, BSWAP) KT_RD_FLT(UT, BSWAP, double)
#define KT_WR_F32(UT, BSWAP) KT_WR_FLT(UT, BSWAP, float, rintf)
#define KT_WR_F64(UT, BSWAP) KT_WR_FLT(UT, BSWAP, double, rint)

#endif /* KT_COMMON */

static void KT_NAME(rd_generic)(const uint8_t *src, size_t step, uint8_t *dst, size_t n,
		const struct chan_plan *p)
{
	const unsigned int shift = p->shift;
	const uint64_t mask = p->mask;
	const bool sgn = p->is_signed, swap = p->swap, sat = p->saturate;
	const uint64_t sbit = sgn ? UINT64_C(1) << (p->bits - 1) : 0;
	const size_t ds = p->dst_stride;
	bool tsgn;
	int64_t tlo;
	uint64_t thi;
	size_t i;

	kt_type_range(p->type, &tsgn, &tlo, &thi);
	(void)tsgn;

	switch (p->length) {
	case 8:
		KT_SWITCH(KT_RD, uint8_t, kt_bswap8)
		break;
	case 16:
		KT_SWITCH(KT_RD, uint16_t, kt_bswap16)
		break;
	case 32:
		KT_SWITCH(KT_RD, uint32_t, kt_bswap32)
		break;
	case 64:
		KT_SWITCH(KT_RD, uint64_t, kt_bswap64)
		break;
	}
}

static void KT_NAME(wr_generic)(uint8_t *dst, size_t step, const uint8_t *src, size_t n,
		const struct chan_plan *p)
{
	const unsigned int shift = p->shift;
	const uint64_t mask = p->mask;
	const bool sgn = p->is_signed, swap = p->swap, sat = p->saturate;
	const size_t ds = p->dst_stride;
	bool tsgn;
	int64_t tlo;
	uint64_t thi;
	size_t i;

	kt_type_range(p->type, &tsgn, &tlo, &thi);
	(void)tlo;
	(void)thi;
	(void)sgn;

	switch (p->length) {
	case 8:
		KT_SWITCH(KT_WR, uint8_t, kt_bswap8)
		break;
	case 16:
		KT_SWITCH(KT_WR, uint16_t, kt_bswap16)
		break;
	case 32:
		KT_SWITCH(KT_WR, uint32_t, kt_bswap32)
		break;
	case 64:
		KT_SWITCH(KT_WR, uint64_t, kt_bswap64)
		break;
	}
}
