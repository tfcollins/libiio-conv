/* SPDX-License-Identifier: MIT */
/* AArch64 NEON backend: 8 lanes of 16 bits per step. */
#include <arm_neon.h>
#include <stdint.h>
#include <string.h>

#define S_NAME(x) x##_neon
#define KT_NAME(x) x##_neon
#include "kernel_tmpl.h"

#define SK 8
typedef uint16x8_t sw_t;

#define sw_load16(s) vreinterpretq_u16_u8(vld1q_u8(s))
#define sw_load8(s) vmovl_u8(vld1_u8(s))
#define sw_store16(d, w) vst1q_u8(d, vreinterpretq_u8_u16(w))
#define sw_store8(d, w) vst1_u8(d, vmovn_u16(w))
#define sw_bswap(w) vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(w)))
#define sw_srl(w, n) vshlq_u16(w, vdupq_n_s16((int16_t)-(n)))
#define sw_sll(w, n) vshlq_u16(w, vdupq_n_s16((int16_t)(n)))
#define sw_and(a, b) vandq_u16(a, b)
#define sw_xor(a, b) veorq_u16(a, b)
#define sw_sub(a, b) vsubq_u16(a, b)
#define sw_set1(v) vdupq_n_u16((uint16_t)(v))
#define sw_min_u(a, b) vminq_u16(a, b)
#define sw_min_s(a, b) \
	vreinterpretq_u16_s16(vminq_s16(vreinterpretq_s16_u16(a), vreinterpretq_s16_u16(b)))
#define sw_max_s(a, b) \
	vreinterpretq_u16_s16(vmaxq_s16(vreinterpretq_s16_u16(a), vreinterpretq_s16_u16(b)))

/* Strided access goes through a small array; each element is read or written
 * with its exact width so nothing outside the channel is touched. */
static inline uint16x8_t sw_gather16(const uint8_t *s, size_t step)
{
	uint16_t t[8];
	unsigned int k;

	for (k = 0; k < 8; k++)
		memcpy(&t[k], s + k * step, sizeof(t[k]));
	return vld1q_u16(t);
}

static inline uint16x8_t sw_gather8(const uint8_t *s, size_t step)
{
	uint16_t t[8];
	unsigned int k;

	for (k = 0; k < 8; k++)
		t[k] = s[k * step];
	return vld1q_u16(t);
}

static inline uint16x8_t sw_gather_pair16(const uint8_t *s, size_t step)
{
	uint32_t t[4];
	unsigned int k;

	for (k = 0; k < 4; k++)
		memcpy(&t[k], s + k * step, sizeof(t[k]));
	return vreinterpretq_u16_u32(vld1q_u32(t));
}

static inline void sw_scatter16(uint8_t *d, size_t step, uint16x8_t w)
{
	uint16_t t[8];
	unsigned int k;

	vst1q_u16(t, w);
	for (k = 0; k < 8; k++)
		memcpy(d + k * step, &t[k], sizeof(t[k]));
}

static inline void sw_scatter8(uint8_t *d, size_t step, uint16x8_t w)
{
	uint16_t t[8];
	unsigned int k;

	vst1q_u16(t, w);
	for (k = 0; k < 8; k++)
		d[k * step] = (uint8_t)t[k];
}

static inline void sw_scatter_pair16(uint8_t *d, size_t step, uint16x8_t w)
{
	uint32_t t[4];
	unsigned int k;

	vst1q_u32(t, vreinterpretq_u32_u16(w));
	for (k = 0; k < 4; k++)
		memcpy(d + k * step, &t[k], sizeof(t[k]));
}

static inline int32x4_t n_widen_lo(uint16x8_t w, bool sgn)
{
	return sgn ? vmovl_s16(vget_low_s16(vreinterpretq_s16_u16(w))) :
		vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(w)));
}

static inline int32x4_t n_widen_hi(uint16x8_t w, bool sgn)
{
	return sgn ? vmovl_s16(vget_high_s16(vreinterpretq_s16_u16(w))) :
		vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(w)));
}

static inline void sw_to_f32(uint16x8_t w, bool sgn, float a, float b, uint8_t *dst)
{
	const float32x4_t va = vdupq_n_f32(a), vb = vdupq_n_f32(b);
	float32x4_t lo = vcvtq_f32_s32(n_widen_lo(w, sgn));
	float32x4_t hi = vcvtq_f32_s32(n_widen_hi(w, sgn));

	lo = vaddq_f32(vmulq_f32(lo, va), vb);
	hi = vaddq_f32(vmulq_f32(hi, va), vb);
	vst1q_u8(dst, vreinterpretq_u8_f32(lo));
	vst1q_u8(dst + 16, vreinterpretq_u8_f32(hi));
}

static inline void n_store_f64(int32x2_t i, float64x2_t va, float64x2_t vb, uint8_t *dst)
{
	float64x2_t f = vcvtq_f64_s64(vmovl_s32(i));

	f = vaddq_f64(vmulq_f64(f, va), vb);
	vst1q_u8(dst, vreinterpretq_u8_f64(f));
}

static inline void sw_to_f64(uint16x8_t w, bool sgn, double a, double b, uint8_t *dst)
{
	const float64x2_t va = vdupq_n_f64(a), vb = vdupq_n_f64(b);
	int32x4_t lo = n_widen_lo(w, sgn), hi = n_widen_hi(w, sgn);

	n_store_f64(vget_low_s32(lo), va, vb, dst);
	n_store_f64(vget_high_s32(lo), va, vb, dst + 16);
	n_store_f64(vget_low_s32(hi), va, vb, dst + 32);
	n_store_f64(vget_high_s32(hi), va, vb, dst + 48);
}

static inline uint16x4_t n_f32_to_u16(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	float32x4_t t = vreinterpretq_f32_u8(vld1q_u8(src));

	t = vmulq_f32(vsubq_f32(t, vdupq_n_f32(b)), vdupq_n_f32(inv_a));
	t = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(t), vceqq_f32(t, t))); /* NaN -> 0 */
	t = vminq_f32(vmaxq_f32(t, vdupq_n_f32(lo)), vdupq_n_f32(hi));
	return vmovn_u32(vreinterpretq_u32_s32(vcvtnq_s32_f32(t)));
}

static inline uint16x8_t sw_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	return vcombine_u16(n_f32_to_u16(src, b, inv_a, lo, hi),
			n_f32_to_u16(src + 16, b, inv_a, lo, hi));
}

static inline int32x2_t n_f64_to_i32(const uint8_t *src, double b, double inv_a, double lo,
		double hi)
{
	float64x2_t t = vreinterpretq_f64_u8(vld1q_u8(src));

	t = vmulq_f64(vsubq_f64(t, vdupq_n_f64(b)), vdupq_n_f64(inv_a));
	t = vreinterpretq_f64_u64(vandq_u64(vreinterpretq_u64_f64(t), vceqq_f64(t, t)));
	t = vminq_f64(vmaxq_f64(t, vdupq_n_f64(lo)), vdupq_n_f64(hi));
	return vmovn_s64(vcvtnq_s64_f64(t));
}

static inline uint16x8_t sw_from_f64(const uint8_t *src, double b, double inv_a, double lo,
		double hi)
{
	int32x4_t i0 = vcombine_s32(n_f64_to_i32(src, b, inv_a, lo, hi),
			n_f64_to_i32(src + 16, b, inv_a, lo, hi));
	int32x4_t i1 = vcombine_s32(n_f64_to_i32(src + 32, b, inv_a, lo, hi),
			n_f64_to_i32(src + 48, b, inv_a, lo, hi));

	return vcombine_u16(vmovn_u32(vreinterpretq_u32_s32(i0)),
			vmovn_u32(vreinterpretq_u32_s32(i1)));
}

#include "simd16_tmpl.h"

const struct conv_backend conv_backend_neon = {
	.name = "neon",
	.pick_rd = pick_rd16_neon,
	.pick_wr = pick_wr16_neon,
};
