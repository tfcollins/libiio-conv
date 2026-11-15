/* SPDX-License-Identifier: MIT */
/* AVX-512 (F, BW, DQ, VL) backend: 16 lanes of 16 bits or 32 bits per step. */
#include "x86_common.h"

#define S_NAME(x) x##_avx512
#define KT_NAME(x) x##_avx512
#include "kernel_tmpl.h"

/* --- 8- and 16-bit storage ----------------------------------------------- */

#define SK 16
typedef __m256i sw_t;

#define sw_load16(s) _mm256_loadu_si256((const __m256i *)(s))
#define sw_load8(s) _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(s)))
#define sw_gather16(s, step) \
	_mm256_set_m128i(x_gather16((s) + 8 * (step), step), x_gather16(s, step))
#define sw_gather8(s, step) \
	_mm256_set_m128i(x_gather8((s) + 8 * (step), step), x_gather8(s, step))
#define sw_gather_pair16(s, step) \
	_mm256_set_m128i(x_gather_pair16((s) + 4 * (step), step), x_gather_pair16(s, step))
#define sw_store16(d, w) _mm256_storeu_si256((__m256i *)(d), w)
#define sw_store8(d, w) _mm_storeu_si128((__m128i *)(d), _mm256_cvtepi16_epi8(w))
#define sw_srl(w, n) _mm256_srl_epi16(w, _mm_cvtsi32_si128(n))
#define sw_sll(w, n) _mm256_sll_epi16(w, _mm_cvtsi32_si128(n))
#define sw_and(a, b) _mm256_and_si256(a, b)
#define sw_xor(a, b) _mm256_xor_si256(a, b)
#define sw_sub(a, b) _mm256_sub_epi16(a, b)
#define sw_set1(v) _mm256_set1_epi16((short)(v))
#define sw_min_u(a, b) _mm256_min_epu16(a, b)
#define sw_min_s(a, b) _mm256_min_epi16(a, b)
#define sw_max_s(a, b) _mm256_max_epi16(a, b)

static inline void sw_scatter16(uint8_t *d, size_t step, __m256i w)
{
	x_scatter16(d, step, _mm256_castsi256_si128(w));
	x_scatter16(d + 8 * step, step, _mm256_extracti128_si256(w, 1));
}

static inline void sw_scatter8(uint8_t *d, size_t step, __m256i w)
{
	x_scatter8(d, step, _mm256_castsi256_si128(w));
	x_scatter8(d + 8 * step, step, _mm256_extracti128_si256(w, 1));
}

static inline void sw_scatter_pair16(uint8_t *d, size_t step, __m256i w)
{
	x_scatter_pair16(d, step, _mm256_castsi256_si128(w));
	x_scatter_pair16(d + 4 * step, step, _mm256_extracti128_si256(w, 1));
}

static inline __m256i sw_bswap(__m256i w)
{
	return _mm256_or_si256(_mm256_slli_epi16(w, 8), _mm256_srli_epi16(w, 8));
}

static inline void sw_to_f32(__m256i w, bool sgn, float a, float b, uint8_t *dst)
{
	__m512i i = sgn ? _mm512_cvtepi16_epi32(w) : _mm512_cvtepu16_epi32(w);
	__m512 f = _mm512_cvtepi32_ps(i);

	f = _mm512_mul_ps(f, _mm512_set1_ps(a));
	f = _mm512_add_ps(f, _mm512_set1_ps(b));
	_mm512_storeu_ps(dst, f);
}

static inline void x_i32_to_f64(__m512i i, double a, double b, uint8_t *dst)
{
	__m512d lo = _mm512_cvtepi32_pd(_mm512_castsi512_si256(i));
	__m512d hi = _mm512_cvtepi32_pd(_mm512_extracti64x4_epi64(i, 1));
	const __m512d va = _mm512_set1_pd(a), vb = _mm512_set1_pd(b);

	lo = _mm512_add_pd(_mm512_mul_pd(lo, va), vb);
	hi = _mm512_add_pd(_mm512_mul_pd(hi, va), vb);
	_mm512_storeu_pd(dst, lo);
	_mm512_storeu_pd(dst + 64, hi);
}

static inline void sw_to_f64(__m256i w, bool sgn, double a, double b, uint8_t *dst)
{
	x_i32_to_f64(sgn ? _mm512_cvtepi16_epi32(w) : _mm512_cvtepu16_epi32(w), a, b, dst);
}

/* (x - b) * inv_a, NaN -> 0, clamped to [lo, hi] */
static inline __m512 x_ps_prepare(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	__m512 t = _mm512_loadu_ps(src);

	t = _mm512_sub_ps(t, _mm512_set1_ps(b));
	t = _mm512_mul_ps(t, _mm512_set1_ps(inv_a));
	t = _mm512_maskz_mov_ps(_mm512_cmp_ps_mask(t, t, _CMP_ORD_Q), t);
	return _mm512_min_ps(_mm512_max_ps(t, _mm512_set1_ps(lo)), _mm512_set1_ps(hi));
}

/* eight doubles -> eight int32 */
static inline __m256i x_pd_to_i32(const uint8_t *src, double b, double inv_a, double lo, double hi)
{
	__m512d t = _mm512_loadu_pd(src);

	t = _mm512_sub_pd(t, _mm512_set1_pd(b));
	t = _mm512_mul_pd(t, _mm512_set1_pd(inv_a));
	t = _mm512_maskz_mov_pd(_mm512_cmp_pd_mask(t, t, _CMP_ORD_Q), t);
	t = _mm512_min_pd(_mm512_max_pd(t, _mm512_set1_pd(lo)), _mm512_set1_pd(hi));
	return _mm512_cvtpd_epi32(t);
}

static inline __m512i x_pd16_to_i32(const uint8_t *src, double b, double inv_a, double lo,
		double hi)
{
	__m256i i0 = x_pd_to_i32(src, b, inv_a, lo, hi);
	__m256i i1 = x_pd_to_i32(src + 64, b, inv_a, lo, hi);

	return _mm512_inserti64x4(_mm512_castsi256_si512(i0), i1, 1);
}

static inline __m256i sw_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi)
{
	return _mm512_cvtepi32_epi16(_mm512_cvtps_epi32(x_ps_prepare(src, b, inv_a, lo, hi)));
}

static inline __m256i sw_from_f64(const uint8_t *src, double b, double inv_a, double lo, double hi)
{
	return _mm512_cvtepi32_epi16(x_pd16_to_i32(src, b, inv_a, lo, hi));
}

#include "simd16_tmpl.h"

/* --- 32-bit storage ------------------------------------------------------ */

#define SD 16
typedef __m512i sd_t;

#define sd_load(s) _mm512_loadu_si512(s)
#define sd_store(d, w) _mm512_storeu_si512(d, w)
#define sd_gather(base, idx) _mm512_i32gather_epi32(idx, base, 1)
#define sd_scatter(base, idx, w) _mm512_i32scatter_epi32(base, idx, w, 1)
#define sd_iota() _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
#define sd_add(a, b) _mm512_add_epi32(a, b)
#define sd_mullo(a, b) _mm512_mullo_epi32(a, b)
#define sd_srl(w, n) _mm512_srl_epi32(w, _mm_cvtsi32_si128(n))
#define sd_sll(w, n) _mm512_sll_epi32(w, _mm_cvtsi32_si128(n))
#define sd_and(a, b) _mm512_and_si512(a, b)
#define sd_xor(a, b) _mm512_xor_si512(a, b)
#define sd_sub(a, b) _mm512_sub_epi32(a, b)
#define sd_set1(v) _mm512_set1_epi32((int)(v))
#define sd_min_u(a, b) _mm512_min_epu32(a, b)
#define sd_min_s(a, b) _mm512_min_epi32(a, b)
#define sd_max_s(a, b) _mm512_max_epi32(a, b)

static inline __m512i sd_bswap(__m512i w)
{
	const __m512i m = _mm512_broadcast_i32x4(
			_mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));

	return _mm512_shuffle_epi8(w, m);
}

static inline void sd_to_f32(__m512i w, float a, float b, uint8_t *dst)
{
	__m512 f = _mm512_cvtepi32_ps(w);

	f = _mm512_mul_ps(f, _mm512_set1_ps(a));
	f = _mm512_add_ps(f, _mm512_set1_ps(b));
	_mm512_storeu_ps(dst, f);
}

#define sd_to_f64(w, a, b, dst) x_i32_to_f64(w, a, b, dst)

/* hi may round up to 2^31 as a float; those lanes take hi_int */
static inline __m512i sd_from_f32(const uint8_t *src, float b, float inv_a, float lo, float hi,
		int32_t hi_int)
{
	__m512 t = x_ps_prepare(src, b, inv_a, lo, hi);
	__mmask16 top = _mm512_cmp_ps_mask(t, _mm512_set1_ps(hi), _CMP_GE_OQ);

	return _mm512_mask_mov_epi32(_mm512_cvtps_epi32(t), top, _mm512_set1_epi32(hi_int));
}

#define sd_from_f64(src, b, inv_a, lo, hi) x_pd16_to_i32(src, b, inv_a, lo, hi)

#include "simd32_tmpl.h"

/* A single 8- or 16-bit channel read with a stride is picked up one sample at
 * a time; measured, the 128-bit path of the AVX2 backend is faster for that
 * than filling 256 bits, so those are left to the fallback. */
static bool strided_single(const struct chan_plan *p)
{
	return !p->pair && p->step != p->length / 8;
}

static conv_rd_fn pick_rd(const struct chan_plan *p)
{
	if (p->length == 32)
		return pick_rd32_avx512(p);
	return strided_single(p) ? NULL : pick_rd16_avx512(p);
}

static conv_wr_fn pick_wr(const struct chan_plan *p)
{
	if (p->length == 32)
		return pick_wr32_avx512(p);
	return strided_single(p) ? NULL : pick_wr16_avx512(p);
}

const struct conv_backend conv_backend_avx512 = {
	.name = "avx512",
	.pick_rd = pick_rd,
	.pick_wr = pick_wr,
#ifdef IIO_CONV_HAVE_AVX2
	.fallback = &conv_backend_avx2,
#endif
};
