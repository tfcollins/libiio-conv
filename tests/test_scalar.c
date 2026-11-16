/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "conv_gen.h"

/* --- fixed read vectors ------------------------------------------------ */

static struct iio_conv_chan chan(unsigned int length, unsigned int bits, unsigned int shift,
		bool is_signed, bool is_be, size_t off)
{
	struct iio_conv_chan c = { .length = length, .bits = bits, .shift = shift,
		.is_signed = is_signed, .is_be = is_be, .byte_offset = off };
	return c;
}

static void wr1(struct iio_conv_chan ch, struct iio_conv_spec spec, const void *in, uint8_t *bytes);

/* read one sample of one channel */
static void rd1(struct iio_conv_chan ch, struct iio_conv_spec spec, const uint8_t *bytes,
		void *out)
{
	struct iio_conv *c = NULL;

	CHECK(iio_conv_create_raw(&ch, 1, ch.length / 8, &spec, &c) == 0);
	if (!c)
		return;
	iio_conv_read_raw(c, bytes, 1, out);
	iio_conv_destroy(c);
}

static void test_read_vectors(void)
{
	struct iio_conv_spec i16 = { .type = IIO_CONV_I16 };
	struct iio_conv_spec spec;
	int16_t s16 = 99; uint16_t u16 = 99; int32_t s32 = 99; float f = 99; double d = 99;
	int8_t s8 = 99; uint8_t u8 = 99;

	rd1(chan(16, 12, 4, true, false, 0), i16, (uint8_t[]){ 0xF0, 0xFF }, &s16);
	CHECK(s16 == -1);
	rd1(chan(16, 12, 4, true, false, 0), i16, (uint8_t[]){ 0x00, 0x80 }, &s16);
	CHECK(s16 == -2048);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_FULLSCALE };
	rd1(chan(16, 12, 4, true, false, 0), spec, (uint8_t[]){ 0xF0, 0xFF }, &d);
	CHECK(d == -1.0 / 2048);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_U16 };
	rd1(chan(16, 16, 0, false, true, 0), spec, (uint8_t[]){ 0x12, 0x34 }, &u16);
	CHECK(u16 == 0x1234);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_F32 };
	rd1(chan(8, 8, 0, false, false, 0), spec, (uint8_t[]){ 0xFF }, &f);
	CHECK(f == 255.0f);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_I32 };
	rd1(chan(32, 24, 8, true, false, 0), spec, (uint8_t[]){ 0x00, 0xFF, 0xFF, 0xFF }, &s32);
	CHECK(s32 == -1);

	/* narrowing: 300 into int8 */
	spec = (struct iio_conv_spec){ .type = IIO_CONV_I8, .saturate = true };
	rd1(chan(16, 16, 0, true, false, 0), spec, (uint8_t[]){ 0x2C, 0x01 }, &s8);
	CHECK(s8 == 127);
	spec.saturate = false;
	rd1(chan(16, 16, 0, true, false, 0), spec, (uint8_t[]){ 0x2C, 0x01 }, &s8);
	CHECK(s8 == 44);
	/* -1 into uint8 */
	spec = (struct iio_conv_spec){ .type = IIO_CONV_U8, .saturate = true };
	rd1(chan(16, 16, 0, true, false, 0), spec, (uint8_t[]){ 0xFF, 0xFF }, &u8);
	CHECK(u8 == 0);
	spec.saturate = false;
	rd1(chan(16, 16, 0, true, false, 0), spec, (uint8_t[]){ 0xFF, 0xFF }, &u8);
	CHECK(u8 == 255);
}

/* raw: the storage word as it is, only the byte order is handled */
static void test_raw_vectors(void)
{
	struct iio_conv_spec i16 = { .type = IIO_CONV_I16, .raw = true };
	struct iio_conv_spec u16s = { .type = IIO_CONV_U16, .raw = true };
	struct iio_conv_spec f32 = { .type = IIO_CONV_F32, .raw = true };
	int16_t s16 = 99; uint16_t u16 = 99; float f = 99;
	uint8_t b[2];

	rd1(chan(16, 12, 4, true, false, 0), i16, (uint8_t[]){ 0xF0, 0xFF }, &s16);
	CHECK(s16 == -16); /* -1 without raw */
	rd1(chan(16, 12, 4, false, true, 0), u16s, (uint8_t[]){ 0x12, 0x34 }, &u16);
	CHECK(u16 == 0x1234);
	rd1(chan(16, 12, 4, true, false, 0), f32, (uint8_t[]){ 0x00, 0x80 }, &f);
	CHECK(f == -32768.0f);

	s16 = 0x7FF1; memset(b, 0xAA, 2);
	wr1(chan(16, 12, 4, true, false, 0), i16, &s16, b);
	CHECK(b[0] == 0xF1 && b[1] == 0x7F); /* not masked to 12 bits, not shifted */
	u16 = 0x1234; memset(b, 0xAA, 2);
	wr1(chan(16, 12, 4, false, true, 0), u16s, &u16, b);
	CHECK(b[0] == 0x12 && b[1] == 0x34);
}

static void test_read_complex_and_order(void)
{
	struct iio_conv_chan iq[2] = { chan(16, 16, 0, true, false, 0), chan(16, 16, 0, true, false, 2) };
	struct iio_conv_chan rev[2] = { chan(16, 16, 0, true, false, 2), chan(16, 16, 0, true, false, 0) };
	struct iio_conv_spec spec = { .type = IIO_CONV_F32, .complex_iq = true };
	const uint8_t s1[] = { 0x01, 0x00, 0xFE, 0xFF };             /* (1, -2) */
	const uint8_t s3[] = { 1, 0, 2, 0,  3, 0, 4, 0,  5, 0, 6, 0 }; /* (1,2) (3,4) (5,6) */
	struct iio_conv *c = NULL;
	float f[2] = { 9, 9 };
	int16_t o[6];

	CHECK(iio_conv_create_raw(iq, 2, 4, &spec, &c) == 0);
	if (c) {
		iio_conv_read_raw(c, s1, 1, f);
		CHECK(f[0] == 1.0f && f[1] == -2.0f);
	}
	iio_conv_destroy(c); c = NULL;

	/* channels passed in reverse storage order: column 0 is the offset-2 channel */
	spec = (struct iio_conv_spec){ .type = IIO_CONV_I16, .layout = IIO_CONV_COLUMNS };
	CHECK(iio_conv_create_raw(rev, 2, 4, &spec, &c) == 0);
	if (c) {
		iio_conv_read_raw(c, s3, 3, o);
		CHECK(o[0] == 2 && o[1] == 4 && o[2] == 6 && o[3] == 1 && o[4] == 3 && o[5] == 5);
	}
	iio_conv_destroy(c); c = NULL;

	spec.layout = IIO_CONV_INTERLEAVED;
	CHECK(iio_conv_create_raw(rev, 2, 4, &spec, &c) == 0);
	if (c) {
		iio_conv_read_raw(c, s3, 3, o);
		CHECK(o[0] == 2 && o[1] == 1 && o[2] == 4 && o[3] == 3 && o[4] == 6 && o[5] == 5);
	}
	iio_conv_destroy(c);
}

/* --- random read property test ----------------------------------------- */

static void test_read_random(void)
{
	uint64_t rng = 0x1234567887654321ULL;
	unsigned int iter;

	for (iter = 0; iter < 2000; iter++) {
		struct gen_case g;
		struct iio_conv *c = NULL;
		size_t src_len, dst_len;
		uint8_t *src, *dst, *ref;
		int ret;

		gen_random_case(&rng, &g, 30, 70);
		src_len = g.step * g.nb_samples;
		dst_len = gen_host_bytes(&g);

		ret = iio_conv_create_raw(g.ch, g.nb_chans, g.step, &g.spec, &c);
		CHECK(ret == 0);
		if (ret)
			continue;
		CHECK(iio_conv_get_step(c) == g.step);
		CHECK(iio_conv_get_host_step(c) == gen_nb_cols(&g) * gen_elem_size(&g));

		src = guard_alloc(src_len);
		dst = guard_alloc(dst_len);
		ref = malloc(dst_len + 1);
		gen_fill(&rng, src, src_len);
		memset(dst, 0x5A, dst_len);

		ref_read(&g, src, ref);
		iio_conv_read_raw(c, src, g.nb_samples, dst);
		if (memcmp(dst, ref, dst_len)) {
			CHECK(!"random read case differs from reference");
			if (test_failures < 5)
				fprintf(stderr, "  iter %u: chans %u step %zu N %zu type %d scale %d cplx %d layout %d sat %d len0 %u bits0 %u shift0 %u\n",
					iter, g.nb_chans, g.step, g.nb_samples, g.spec.type, g.spec.scale,
					g.spec.complex_iq, g.spec.layout, g.spec.saturate,
					g.ch[0].length, g.ch[0].bits, g.ch[0].shift);
		}

		free(ref);
		guard_free(src, src_len);
		guard_free(dst, dst_len);
		iio_conv_destroy(c);
	}
}

/* --- fixed write vectors ----------------------------------------------- */

static void wr1(struct iio_conv_chan ch, struct iio_conv_spec spec, const void *in, uint8_t *bytes)
{
	struct iio_conv *c = NULL;

	CHECK(iio_conv_create_raw(&ch, 1, ch.length / 8, &spec, &c) == 0);
	if (!c)
		return;
	iio_conv_write_raw(c, bytes, 1, in);
	iio_conv_destroy(c);
}

static void test_write_vectors(void)
{
	struct iio_conv_spec fs = { .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_FULLSCALE };
	struct iio_conv_spec f32 = { .type = IIO_CONV_F32 };
	struct iio_conv_spec i32 = { .type = IIO_CONV_I32 };
	struct iio_conv_chan ch = chan(16, 12, 4, true, false, 0);
	uint8_t b[2];
	double d;
	float f;
	int32_t v;

	d = 0.5;  memset(b, 0xAA, 2); wr1(ch, fs, &d, b); CHECK(b[0] == 0x00 && b[1] == 0x40);
	d = 1.0;  memset(b, 0xAA, 2); wr1(ch, fs, &d, b); CHECK(b[0] == 0xF0 && b[1] == 0x7F);
	d = -2.0; memset(b, 0xAA, 2); wr1(ch, fs, &d, b); CHECK(b[0] == 0x00 && b[1] == 0x80);
	d = NAN;  memset(b, 0xAA, 2); wr1(ch, fs, &d, b); CHECK(b[0] == 0x00 && b[1] == 0x00);
	d = INFINITY; memset(b, 0xAA, 2); wr1(ch, fs, &d, b); CHECK(b[0] == 0xF0 && b[1] == 0x7F);

	ch = chan(16, 12, 0, true, false, 0);
	f = 2.5f; wr1(ch, f32, &f, b); CHECK(b[0] == 2 && b[1] == 0);
	f = 3.5f; wr1(ch, f32, &f, b); CHECK(b[0] == 4 && b[1] == 0);
	f = -2.5f; wr1(ch, f32, &f, b); CHECK(b[0] == 0xFE && b[1] == 0x0F);

	v = 5000;
	i32.saturate = true;  wr1(ch, i32, &v, b); CHECK(b[0] == 0xFF && b[1] == 0x07);
	i32.saturate = false; wr1(ch, i32, &v, b); CHECK(b[0] == 0x88 && b[1] == 0x03);
	v = -5000;
	i32.saturate = true;  wr1(ch, i32, &v, b); CHECK(b[0] == 0x00 && b[1] == 0x08);

	/* big-endian storage */
	ch = chan(16, 16, 0, false, true, 0);
	v = 0x1234; i32.saturate = false; wr1(ch, i32, &v, b); CHECK(b[0] == 0x12 && b[1] == 0x34);
}

static void test_write_untouched(void)
{
	struct iio_conv_chan ch = chan(16, 16, 0, true, false, 2);
	struct iio_conv_spec spec = { .type = IIO_CONV_I16 };
	struct iio_conv *c = NULL;
	const int16_t in[3] = { 0x0102, 0x0304, 0x0506 };
	uint8_t buf[24];
	unsigned int i, k;

	memset(buf, 0xAA, sizeof(buf));
	CHECK(iio_conv_create_raw(&ch, 1, 8, &spec, &c) == 0);
	if (!c)
		return;
	iio_conv_write_raw(c, buf, 3, in);
	for (i = 0; i < 3; i++) {
		for (k = 0; k < 8; k++) {
			if (k == 2)
				CHECK(buf[i * 8 + k] == 2 * i + 2);
			else if (k == 3)
				CHECK(buf[i * 8 + k] == 2 * i + 1);
			else
				CHECK(buf[i * 8 + k] == 0xAA);
		}
	}
	iio_conv_destroy(c);
}

/* --- round trip: read(write(read(x))) == read(x) for wide integer types -- */

static void test_round_trip(void)
{
	uint64_t rng = 0xDEADBEEFCAFEF00DULL;
	unsigned int iter, i;

	for (iter = 0; iter < 2000; iter++) {
		struct gen_case g;
		struct iio_conv *c = NULL;
		size_t src_len, dst_len;
		uint8_t *src, *src2, *host, *host2;
		bool sgn;

		gen_random_case(&rng, &g, 30, 70);
		sgn = rng_range(&rng, 2);
		for (i = 0; i < g.nb_chans; i++)
			g.ch[i].is_signed = sgn;
		g.spec.type = sgn ? IIO_CONV_I64 : IIO_CONV_U64;
		g.spec.scale = IIO_CONV_SCALE_NONE;
		g.spec.user_gain = g.spec.user_offset = NULL;

		src_len = g.step * g.nb_samples;
		dst_len = gen_host_bytes(&g);
		CHECK(iio_conv_create_raw(g.ch, g.nb_chans, g.step, &g.spec, &c) == 0);
		if (!c)
			continue;

		src = guard_alloc(src_len);
		src2 = guard_alloc(src_len);
		host = guard_alloc(dst_len);
		host2 = guard_alloc(dst_len);
		gen_fill(&rng, src, src_len);
		memset(src2, 0, src_len);

		iio_conv_read_raw(c, src, g.nb_samples, host);
		iio_conv_write_raw(c, src2, g.nb_samples, host);
		iio_conv_read_raw(c, src2, g.nb_samples, host2);
		CHECK(memcmp(host, host2, dst_len) == 0);

		/* stored word holds only the valid bits, shifted into place */
		for (i = 0; i < g.nb_chans && g.nb_samples; i++) {
			const struct iio_conv_chan *ch = &g.ch[i];
			unsigned int k, nbytes = ch->length / 8;
			unsigned __int128 w = 0, w2 = 0, keep;

			for (k = 0; k < nbytes; k++) {
				unsigned int sh = ch->is_be ? 8 * (nbytes - 1 - k) : 8 * k;
				w |= (unsigned __int128)src[ch->byte_offset + k] << sh;
				w2 |= (unsigned __int128)src2[ch->byte_offset + k] << sh;
			}
			if (g.spec.raw)
				keep = ((unsigned __int128)1 << ch->length) - 1;
			else
				keep = (((unsigned __int128)1 << ch->bits) - 1) << ch->shift;
			CHECK(w2 == (w & keep));
		}

		guard_free(src, src_len);
		guard_free(src2, src_len);
		guard_free(host, dst_len);
		guard_free(host2, dst_len);
		iio_conv_destroy(c);
	}
}

/* --- float write against a simple model -------------------------------- */

static void test_write_float_random(void)
{
	uint64_t rng = 0x0F0F0F0F12345678ULL;
	unsigned int iter;

	for (iter = 0; iter < 20000; iter++) {
		unsigned int bits = 1 + rng_range(&rng, 16), shift = rng_range(&rng, 16 - bits + 1);
		bool sgn = rng_range(&rng, 2);
		struct iio_conv_chan ch = chan(16, bits, shift, sgn, false, 0);
		struct iio_conv_spec spec = { .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_FULLSCALE };
		double x = ((double)(int64_t)rng_range(&rng, 4000001) - 2000000.0) / 1000000.0;
		double full = pow(2.0, sgn ? bits - 1 : bits);
		double lo = sgn ? -full : 0.0, hi = sgn ? full - 1 : full - 1;
		double t = nearbyint(x * full);
		uint16_t expect, got;
		uint8_t b[2];

		if (t < lo) t = lo;
		if (t > hi) t = hi;
		expect = (uint16_t)(((uint16_t)(int32_t)t & ((1u << bits) - 1)) << shift);
		wr1(ch, spec, &x, b);
		got = (uint16_t)(b[0] | b[1] << 8);
		CHECK(got == expect);
	}
}

int main(void)
{
	static const size_t tail_n[] = { 0, 1, 7, 33 };
	unsigned int i;

	test_read_vectors();
	test_raw_vectors();
	test_read_complex_and_order();
	test_read_random();
	test_write_vectors();
	test_write_untouched();
	test_round_trip();
	test_write_float_random();

	/* zero and short sample counts against guard pages */
	for (i = 0; i < 4; i++) {
		struct iio_conv_chan ch[2] = { chan(16, 12, 0, true, false, 0), chan(16, 12, 0, true, false, 2) };
		struct iio_conv_spec spec = { .type = IIO_CONV_F32, .complex_iq = true,
			.scale = IIO_CONV_SCALE_FULLSCALE };
		struct iio_conv *c = NULL;
		size_t n = tail_n[i];
		uint8_t *src = guard_alloc(n * 4), *dst = guard_alloc(n * 8), *back = guard_alloc(n * 4);

		/* 0x0111: fits the 12 valid bits, so the round trip is exact */
		for (size_t k = 0; k < n * 4; k++)
			src[k] = k & 1 ? 0x01 : 0x11;
		CHECK(iio_conv_create_raw(ch, 2, 4, &spec, &c) == 0);
		if (c) {
			iio_conv_read_raw(c, src, n, dst);
			iio_conv_write_raw(c, back, n, dst);
			CHECK(memcmp(src, back, n * 4) == 0);
		}
		iio_conv_destroy(c);
		guard_free(src, n * 4); guard_free(dst, n * 8); guard_free(back, n * 4);
	}

	TEST_MAIN_RETURN();
}
