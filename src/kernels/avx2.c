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

/* --- 32-bit storage: 8 lanes of 32 bits ---------------------------------- */

#define SD 8
typedef __m256i sd_t;

#define sd_load(s) _mm256_loadu_si256((const __m256i *)(s))
#define sd_store(d, w) _mm256_storeu_si256((__m256i *)(d), w)
#define sd_gather(base, idx) _mm256_i32gather_epi32((const int *)(base), idx, 1)
#define sd_iota() _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7)
#define sd_add(a, b) _mm256_add_epi32(a, b)
#define sd_mullo(a, b) _mm256_mullo_epi32(a, b)
#define sd_srl(w, n) _mm256_srl_epi32(w, _mm_cvtsi32_si128(n))
#define sd_sll(w, n) _mm256_sll_epi32(w, _mm_cvtsi32_si128(n))
#define sd_and(a, b) _mm256_and_si256(a, b)
#define sd_xor(a, b) _mm256_xor_si256(a, b)
#define sd_sub(a, b) _mm256_sub_epi32(a, b)
#define sd_set1(v) _mm256_set1_epi32((int)(v))
#define sd_min_u(a, b) _mm256_min_epu32(a, b)
#define sd_min_s(a, b) _mm256_min_epi32(a, b)
#define sd_max_s(a, b) _mm256_max_epi32(a, b)

/* AVX2 has no scatter: each word is written with its exact width */
static inline void sd_scatter(uint8_t *base, __m256i idx, __m256i w)
{
	uint32_t tmp[8], off[8];
	unsigned int k;

	_mm256_storeu_si256((__m256i *)tmp, w);
	_mm256_storeu_si256((__m256i *)off, idx);
	for (k = 0; k < 8; k++)
		memcpy(base + off[k], &tmp[k], sizeof(tmp[k]));
}

static inline __m256i sd_bswap(__m256i w)
{
	const __m256i m = _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);

	return _mm256_shuffle_epi8(w, m);
}

static inline void sd_to_f32(__m256i w, float a, float b, uint8_t *dst)
{
	__m256 f = _mm256_cvtepi32_ps(w);

	f = _mm256_mul_ps(f, _mm256_set1_ps(a));
	f = _mm256_add_ps(f, _mm256_set1_ps(b));
	_mm256_storeu_ps((float *)dst, f);
}

static inline void sd_to_f64(__m256i w, double a, double b, uint8_t *dst)
{
	__m256d lo = _mm256_cvtepi32_pd(_mm256_castsi256_si128(w));
	__m256d hi = _mm256_cvtepi32_pd(_mm256_extracti128_si256(w, 1));
	const __m256d va = _mm256_set1_pd(a), vb = _mm256_set1_pd(b);

	lo = _mm256_add_pd(_mm256_mul_pd(lo, va), vb);
	hi = _mm256_add_pd(_mm256_mul_pd(hi, va), vb);
	_mm256_storeu_pd((double *)dst, lo);
	_mm256_storeu_pd((double *)dst + 4, hi);
}

/* hi may not be representable as a float (2^31 - 1 rounds up to 2^31), so
 * lanes at or above it take hi_int instead of the conversion result. */
static inline __m256i sd_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi,
		int32_t hi_int)
{
	__m256 t = _mm256_loadu_ps((const float *)src);
	__m256 top;
	__m256i i;

	t = _mm256_sub_ps(t, _mm256_set1_ps(b));
	t = _mm256_mul_ps(t, _mm256_set1_ps(inv_a));
	t = _mm256_and_ps(t, _mm256_cmp_ps(t, t, _CMP_ORD_Q));
	t = _mm256_min_ps(_mm256_max_ps(t, _mm256_set1_ps(lo)), _mm256_set1_ps(hi));
	top = _mm256_cmp_ps(t, _mm256_set1_ps(hi), _CMP_GE_OQ);
	i = _mm256_cvtps_epi32(t);
	return _mm256_blendv_epi8(i, _mm256_set1_epi32(hi_int), _mm256_castps_si256(top));
}

static inline __m128i x_pd_to_i32_full(__m256d t, double b, double inv_a, double lo, double hi)
{
	t = _mm256_sub_pd(t, _mm256_set1_pd(b));
	t = _mm256_mul_pd(t, _mm256_set1_pd(inv_a));
	t = _mm256_and_pd(t, _mm256_cmp_pd(t, t, _CMP_ORD_Q));
	t = _mm256_min_pd(_mm256_max_pd(t, _mm256_set1_pd(lo)), _mm256_set1_pd(hi));
	return _mm256_cvtpd_epi32(t);
}

static inline __m256i sd_from_f64(const uint8_t *src, double b, double inv_a, double lo, double hi)
{
	__m128i i0 = x_pd_to_i32_full(_mm256_loadu_pd((const double *)src), b, inv_a, lo, hi);
	__m128i i1 = x_pd_to_i32_full(_mm256_loadu_pd((const double *)src + 4), b, inv_a, lo, hi);

	return _mm256_inserti128_si256(_mm256_castsi128_si256(i0), i1, 1);
}

#include "simd32_tmpl.h"

static conv_rd_fn pick_rd(const struct chan_plan *p)
{
	return p->length == 32 ? pick_rd32_avx2(p) : pick_rd16_avx2(p);
}

static conv_wr_fn pick_wr(const struct chan_plan *p)
{
	return p->length == 32 ? pick_wr32_avx2(p) : pick_wr16_avx2(p);
}

const struct conv_backend conv_backend_avx2 = {
	.name = "avx2",
	.pick_rd = pick_rd,
	.pick_wr = pick_wr,
};
