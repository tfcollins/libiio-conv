/* SPDX-License-Identifier: MIT */
/* 128-bit helpers shared by the x86 backends (SSE4.1 level). */
#ifndef IIO_CONV_X86_COMMON_H
#define IIO_CONV_X86_COMMON_H

#include <immintrin.h>
#include <stdint.h>
#include <string.h>

static inline uint16_t x_ld16(const uint8_t *s)
{
	uint16_t v;
	memcpy(&v, s, sizeof(v));
	return v;
}

static inline uint32_t x_ld32(const uint8_t *s)
{
	uint32_t v;
	memcpy(&v, s, sizeof(v));
	return v;
}

/* 8 words, one every step bytes. Reads exactly the 16 bytes it needs. */
static inline __m128i x_gather16(const uint8_t *s, size_t step)
{
	return _mm_setr_epi16((short)x_ld16(s), (short)x_ld16(s + step),
			(short)x_ld16(s + 2 * step), (short)x_ld16(s + 3 * step),
			(short)x_ld16(s + 4 * step), (short)x_ld16(s + 5 * step),
			(short)x_ld16(s + 6 * step), (short)x_ld16(s + 7 * step));
}

/* 8 bytes, one every step bytes, zero extended to words */
static inline __m128i x_gather8(const uint8_t *s, size_t step)
{
	return _mm_setr_epi16(s[0], s[step], s[2 * step], s[3 * step],
			s[4 * step], s[5 * step], s[6 * step], s[7 * step]);
}

/* 4 pairs of adjacent words, one pair every step bytes */
static inline __m128i x_gather_pair16(const uint8_t *s, size_t step)
{
	return _mm_setr_epi32((int)x_ld32(s), (int)x_ld32(s + step),
			(int)x_ld32(s + 2 * step), (int)x_ld32(s + 3 * step));
}

#define X_SCATTER16_ONE(k) do { \
		uint16_t v_ = (uint16_t)_mm_extract_epi16(w, k); \
		memcpy(d + (k) * step, &v_, sizeof(v_)); \
	} while (0)

static inline void x_scatter16(uint8_t *d, size_t step, __m128i w)
{
	X_SCATTER16_ONE(0); X_SCATTER16_ONE(1); X_SCATTER16_ONE(2); X_SCATTER16_ONE(3);
	X_SCATTER16_ONE(4); X_SCATTER16_ONE(5); X_SCATTER16_ONE(6); X_SCATTER16_ONE(7);
}

#define X_SCATTER8_ONE(k) d[(k) * step] = (uint8_t)_mm_extract_epi16(w, k)

static inline void x_scatter8(uint8_t *d, size_t step, __m128i w)
{
	X_SCATTER8_ONE(0); X_SCATTER8_ONE(1); X_SCATTER8_ONE(2); X_SCATTER8_ONE(3);
	X_SCATTER8_ONE(4); X_SCATTER8_ONE(5); X_SCATTER8_ONE(6); X_SCATTER8_ONE(7);
}

#define X_SCATTER_PAIR_ONE(k) do { \
		uint32_t v_ = (uint32_t)_mm_extract_epi32(w, k); \
		memcpy(d + (k) * step, &v_, sizeof(v_)); \
	} while (0)

static inline void x_scatter_pair16(uint8_t *d, size_t step, __m128i w)
{
	X_SCATTER_PAIR_ONE(0); X_SCATTER_PAIR_ONE(1);
	X_SCATTER_PAIR_ONE(2); X_SCATTER_PAIR_ONE(3);
}

static inline __m128i x_bswap16(__m128i w)
{
	return _mm_or_si128(_mm_slli_epi16(w, 8), _mm_srli_epi16(w, 8));
}

/* low byte of each of the 8 words */
static inline void x_store8(uint8_t *d, __m128i w)
{
	_mm_storel_epi64((__m128i *)d, _mm_packus_epi16(w, w));
}

static inline __m128i x_load8(const uint8_t *s)
{
	return _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i *)s));
}

#endif /* IIO_CONV_X86_COMMON_H */
