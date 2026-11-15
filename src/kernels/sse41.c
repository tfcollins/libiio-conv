/* SPDX-License-Identifier: MIT */
/* SSE4.1 backend: 8 lanes of 16 bits per step, 128-bit float vectors. */
#include "x86_common.h"

#define S_NAME(x) x##_sse41
#define KT_NAME(x) x##_sse41
#include "kernel_tmpl.h"

#define SK 8
typedef __m128i sw_t;

#define sw_load16(s) _mm_loadu_si128((const __m128i *)(s))
#define sw_load8(s) x_load8(s)
#define sw_gather16(s, step) x_gather16(s, step)
#define sw_gather8(s, step) x_gather8(s, step)
#define sw_gather_pair16(s, step) x_gather_pair16(s, step)
#define sw_store16(d, w) _mm_storeu_si128((__m128i *)(d), w)
#define sw_store8(d, w) x_store8(d, w)
#define sw_scatter16(d, step, w) x_scatter16(d, step, w)
#define sw_scatter8(d, step, w) x_scatter8(d, step, w)
#define sw_scatter_pair16(d, step, w) x_scatter_pair16(d, step, w)
#define sw_bswap(w) x_bswap16(w)
#define sw_srl(w, n) _mm_srl_epi16(w, _mm_cvtsi32_si128(n))
#define sw_sll(w, n) _mm_sll_epi16(w, _mm_cvtsi32_si128(n))
#define sw_and(a, b) _mm_and_si128(a, b)
#define sw_xor(a, b) _mm_xor_si128(a, b)
#define sw_sub(a, b) _mm_sub_epi16(a, b)
#define sw_set1(v) _mm_set1_epi16((short)(v))
#define sw_min_u(a, b) _mm_min_epu16(a, b)
#define sw_min_s(a, b) _mm_min_epi16(a, b)
#define sw_max_s(a, b) _mm_max_epi16(a, b)

static inline __m128i x_widen(__m128i w, bool sgn)
{
	return sgn ? _mm_cvtepi16_epi32(w) : _mm_cvtepu16_epi32(w);
}

static inline void sw_to_f32(__m128i w, bool sgn, float a, float b, uint8_t *dst)
{
	const __m128 va = _mm_set1_ps(a), vb = _mm_set1_ps(b);
	__m128 lo = _mm_cvtepi32_ps(x_widen(w, sgn));
	__m128 hi = _mm_cvtepi32_ps(x_widen(_mm_srli_si128(w, 8), sgn));

	lo = _mm_add_ps(_mm_mul_ps(lo, va), vb);
	hi = _mm_add_ps(_mm_mul_ps(hi, va), vb);
	_mm_storeu_ps((float *)dst, lo);
	_mm_storeu_ps((float *)dst + 4, hi);
}

static inline void sw_to_f64(__m128i w, bool sgn, double a, double b, uint8_t *dst)
{
	const __m128d va = _mm_set1_pd(a), vb = _mm_set1_pd(b);
	__m128i lo = x_widen(w, sgn), hi = x_widen(_mm_srli_si128(w, 8), sgn);
	double *d = (double *)dst;

	_mm_storeu_pd(d, _mm_add_pd(_mm_mul_pd(_mm_cvtepi32_pd(lo), va), vb));
	_mm_storeu_pd(d + 2, _mm_add_pd(_mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(lo, 8)), va), vb));
	_mm_storeu_pd(d + 4, _mm_add_pd(_mm_mul_pd(_mm_cvtepi32_pd(hi), va), vb));
	_mm_storeu_pd(d + 6, _mm_add_pd(_mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(hi, 8)), va), vb));
}

static inline __m128i x_ps_to_i32(__m128 t, float b, float inv_a, float lo, float hi)
{
	t = _mm_sub_ps(t, _mm_set1_ps(b));
	t = _mm_mul_ps(t, _mm_set1_ps(inv_a));
	t = _mm_and_ps(t, _mm_cmpord_ps(t, t)); /* NaN -> 0 */
	t = _mm_min_ps(_mm_max_ps(t, _mm_set1_ps(lo)), _mm_set1_ps(hi));
	return _mm_and_si128(_mm_cvtps_epi32(t), _mm_set1_epi32(0xFFFF));
}

static inline __m128i sw_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	__m128i i0 = x_ps_to_i32(_mm_loadu_ps((const float *)src), b, inv_a, lo, hi);
	__m128i i1 = x_ps_to_i32(_mm_loadu_ps((const float *)src + 4), b, inv_a, lo, hi);

	return _mm_packus_epi32(i0, i1);
}

/* two doubles -> two int32 in the low half */
static inline __m128i x_pd_to_i32(__m128d t, double b, double inv_a, double lo, double hi)
{
	t = _mm_sub_pd(t, _mm_set1_pd(b));
	t = _mm_mul_pd(t, _mm_set1_pd(inv_a));
	t = _mm_and_pd(t, _mm_cmpord_pd(t, t));
	t = _mm_min_pd(_mm_max_pd(t, _mm_set1_pd(lo)), _mm_set1_pd(hi));
	return _mm_cvtpd_epi32(t);
}

static inline __m128i sw_from_f64(const uint8_t *src, double b, double inv_a, double lo, double hi)
{
	const double *s = (const double *)src;
	__m128i i0 = _mm_unpacklo_epi64(x_pd_to_i32(_mm_loadu_pd(s), b, inv_a, lo, hi),
			x_pd_to_i32(_mm_loadu_pd(s + 2), b, inv_a, lo, hi));
	__m128i i1 = _mm_unpacklo_epi64(x_pd_to_i32(_mm_loadu_pd(s + 4), b, inv_a, lo, hi),
			x_pd_to_i32(_mm_loadu_pd(s + 6), b, inv_a, lo, hi));
	const __m128i m = _mm_set1_epi32(0xFFFF);

	return _mm_packus_epi32(_mm_and_si128(i0, m), _mm_and_si128(i1, m));
}

#include "simd16_tmpl.h"

const struct conv_backend conv_backend_sse41 = {
	.name = "sse41",
	.pick_rd = pick_rd16_sse41,
	.pick_wr = pick_wr16_sse41,
};
