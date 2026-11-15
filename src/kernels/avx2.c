/* SPDX-License-Identifier: MIT */
/* AVX2 backend: 8 lanes of 16 bits per step, widened to 256-bit vectors. */
#include "x86_common.h"

#define S_NAME(x) x##_avx2
#define KT_NAME(x) x##_avx2
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

static inline void sw_to_f32(__m128i w, bool sgn, float a, float b, uint8_t *dst)
{
	__m256i i = sgn ? _mm256_cvtepi16_epi32(w) : _mm256_cvtepu16_epi32(w);
	__m256 f = _mm256_cvtepi32_ps(i);

	f = _mm256_mul_ps(f, _mm256_set1_ps(a));
	f = _mm256_add_ps(f, _mm256_set1_ps(b));
	_mm256_storeu_ps((float *)dst, f);
}

static inline void sw_to_f64(__m128i w, bool sgn, double a, double b, uint8_t *dst)
{
	__m256i i = sgn ? _mm256_cvtepi16_epi32(w) : _mm256_cvtepu16_epi32(w);
	__m256d lo = _mm256_cvtepi32_pd(_mm256_castsi256_si128(i));
	__m256d hi = _mm256_cvtepi32_pd(_mm256_extracti128_si256(i, 1));
	const __m256d va = _mm256_set1_pd(a), vb = _mm256_set1_pd(b);

	lo = _mm256_add_pd(_mm256_mul_pd(lo, va), vb);
	hi = _mm256_add_pd(_mm256_mul_pd(hi, va), vb);
	_mm256_storeu_pd((double *)dst, lo);
	_mm256_storeu_pd((double *)dst + 4, hi);
}

static inline __m128i sw_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	__m256 t = _mm256_loadu_ps((const float *)src);
	__m256i i;

	t = _mm256_sub_ps(t, _mm256_set1_ps(b));
	t = _mm256_mul_ps(t, _mm256_set1_ps(inv_a));
	t = _mm256_and_ps(t, _mm256_cmp_ps(t, t, _CMP_ORD_Q)); /* NaN -> 0 */
	t = _mm256_min_ps(_mm256_max_ps(t, _mm256_set1_ps(lo)), _mm256_set1_ps(hi));
	i = _mm256_and_si256(_mm256_cvtps_epi32(t), _mm256_set1_epi32(0xFFFF));
	return _mm_packus_epi32(_mm256_castsi256_si128(i), _mm256_extracti128_si256(i, 1));
}

static inline __m128i x_pd_to_i32(__m256d t, double b, double inv_a, double lo, double hi)
{
	t = _mm256_sub_pd(t, _mm256_set1_pd(b));
	t = _mm256_mul_pd(t, _mm256_set1_pd(inv_a));
	t = _mm256_and_pd(t, _mm256_cmp_pd(t, t, _CMP_ORD_Q));
	t = _mm256_min_pd(_mm256_max_pd(t, _mm256_set1_pd(lo)), _mm256_set1_pd(hi));
	return _mm_and_si128(_mm256_cvtpd_epi32(t), _mm_set1_epi32(0xFFFF));
}

static inline __m128i sw_from_f64(const uint8_t *src, double b, double inv_a, double lo, double hi)
{
	__m128i i0 = x_pd_to_i32(_mm256_loadu_pd((const double *)src), b, inv_a, lo, hi);
	__m128i i1 = x_pd_to_i32(_mm256_loadu_pd((const double *)src + 4), b, inv_a, lo, hi);

	return _mm_packus_epi32(i0, i1);
}

#include "simd16_tmpl.h"

const struct conv_backend conv_backend_avx2 = {
	.name = "avx2",
	.pick_rd = pick_rd16_avx2,
	.pick_wr = pick_wr16_avx2,
};
