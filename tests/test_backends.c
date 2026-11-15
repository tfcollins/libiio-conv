/* SPDX-License-Identifier: MIT */
/* Every SIMD backend must produce exactly the bytes the scalar backend does. */
#include <errno.h>
#include "conv_gen.h"
#include "internal.h"

static uint64_t rng = 0xA5A5A5A55A5A5A5AULL;

/* Host-side input for the write direction: a mix of raw random bytes (wild
 * floats, NaN, infinities) and values around the channel range with ties. */
static void fill_host(const struct gen_case *g, uint8_t *host, size_t len)
{
	size_t i, n;

	gen_fill(&rng, host, len);
	if (!gen_type_is_float(g->spec.type) || rng_range(&rng, 4) == 0)
		return;
	if (g->spec.type == IIO_CONV_F32) {
		n = len / sizeof(float);
		for (i = 0; i < n; i++) {
			float f = ((float)rng_range(&rng, 400001) - 200000.0f) / 2.0f;
			if (g->spec.scale == IIO_CONV_SCALE_FULLSCALE)
				f /= 65536.0f;
			if (rng_range(&rng, 50) == 0)
				f = rng_range(&rng, 2) ? NAN : (rng_range(&rng, 2) ? INFINITY : -INFINITY);
			memcpy(host + i * sizeof(f), &f, sizeof(f));
		}
	} else {
		n = len / sizeof(double);
		for (i = 0; i < n; i++) {
			double f = ((double)rng_range(&rng, 400001) - 200000.0) / 2.0;
			if (g->spec.scale == IIO_CONV_SCALE_FULLSCALE)
				f /= 65536.0;
			if (rng_range(&rng, 50) == 0)
				f = rng_range(&rng, 2) ? NAN : (rng_range(&rng, 2) ? INFINITY : -INFINITY);
			memcpy(host + i * sizeof(f), &f, sizeof(f));
		}
	}
}

static void describe(const char *be, const char *what, const struct gen_case *g)
{
	if (test_failures > 8)
		return;
	fprintf(stderr, "  %s %s: chans %u step %zu N %zu type %d scale %d cplx %d layout %d sat %d "
			"ch0 len %u bits %u shift %u sgn %d be %d off %zu\n",
			be, what, g->nb_chans, g->step, g->nb_samples, g->spec.type, g->spec.scale,
			g->spec.complex_iq, g->spec.layout, g->spec.saturate, g->ch[0].length,
			g->ch[0].bits, g->ch[0].shift, g->ch[0].is_signed, g->ch[0].is_be,
			g->ch[0].byte_offset);
}

/* Returns the backend's converter for inspection by the caller (or NULL) */
static void compare_case(const char *be, const struct gen_case *g)
{
	struct iio_conv *ref = NULL, *c = NULL;
	size_t src_len = g->step * g->nb_samples, host_len = gen_host_bytes(g);
	uint8_t *src, *h_ref, *h_be, *s_ref, *s_be;
	int ret;

	CHECK(iio_conv_create_raw_backend("scalar", g->ch, g->nb_chans, g->step, &g->spec, &ref) == 0);
	ret = iio_conv_create_raw_backend(be, g->ch, g->nb_chans, g->step, &g->spec, &c);
	CHECK(ret == 0);
	if (!ref || !c) {
		iio_conv_destroy(ref);
		iio_conv_destroy(c);
		return;
	}

	src = guard_alloc(src_len);
	h_ref = guard_alloc(host_len);
	h_be = guard_alloc(host_len);
	s_ref = guard_alloc(src_len);
	s_be = guard_alloc(src_len);

	gen_fill(&rng, src, src_len);
	memset(h_ref, 0x3C, host_len);
	memset(h_be, 0x3C, host_len);
	iio_conv_read_raw(ref, src, g->nb_samples, h_ref);
	iio_conv_read_raw(c, src, g->nb_samples, h_be);
	if (memcmp(h_ref, h_be, host_len)) {
		CHECK(!"read differs from scalar");
		describe(be, "read", g);
	}

	fill_host(g, h_ref, host_len);
	memset(s_ref, 0xA5, src_len);
	memset(s_be, 0xA5, src_len);
	iio_conv_write_raw(ref, s_ref, g->nb_samples, h_ref);
	iio_conv_write_raw(c, s_be, g->nb_samples, h_ref);
	if (memcmp(s_ref, s_be, src_len)) {
		CHECK(!"write differs from scalar");
		describe(be, "write", g);
	}

	guard_free(src, src_len);
	guard_free(h_ref, host_len);
	guard_free(h_be, host_len);
	guard_free(s_ref, src_len);
	guard_free(s_be, src_len);
	iio_conv_destroy(ref);
	iio_conv_destroy(c);
}

/* Explicit sweep over one storage length */
static void sweep(const char *be, unsigned int length, const unsigned int *bits_list,
		unsigned int nb_bits, const enum iio_conv_type *types, unsigned int nb_types,
		size_t max_n)
{
	static const unsigned int shifts[] = { 0, 2, 4 };
	static const size_t steps[] = { 1, 2, 4, 8, 16 };
	static double gain[2] = { 1.7, 1.7 }, off[2] = { 0.3, 0.3 };
	unsigned int ib, is, sgn, be_, it, cplx, lay, ist, variant = 0;
	size_t n, lb = length / 8;

	for (ib = 0; ib < nb_bits; ib++)
	for (is = 0; is < 3; is++)
	for (sgn = 0; sgn < 2; sgn++)
	for (be_ = 0; be_ < 2; be_++)
	for (it = 0; it < nb_types; it++)
	for (cplx = 0; cplx < 2; cplx++)
	for (lay = 0; lay < 2; lay++)
	for (ist = 0; ist < 5; ist++) {
		struct gen_case g;
		size_t need = lb * (cplx ? 2 : 1);

		if (bits_list[ib] + shifts[is] > length || steps[ist] < need)
			continue;
		if (be_ && length == 8)
			continue;

		memset(&g, 0, sizeof(g));
		g.nb_chans = cplx ? 2 : 1;
		g.step = steps[ist];
		g.ch[0].length = length;
		g.ch[0].bits = bits_list[ib];
		g.ch[0].shift = shifts[is];
		g.ch[0].is_signed = sgn;
		g.ch[0].is_be = be_;
		/* last possible position in the sample, or the first */
		g.ch[0].byte_offset = (variant & 1) ? 0 : g.step - need;
		g.ch[1] = g.ch[0];
		g.ch[1].byte_offset += lb;
		g.spec.type = types[it];
		g.spec.complex_iq = cplx;
		g.spec.layout = lay ? IIO_CONV_INTERLEAVED : IIO_CONV_COLUMNS;
		g.spec.saturate = (variant >> 1) & 1;
		if (gen_type_is_float(types[it])) {
			g.spec.scale = (enum iio_conv_scale)(variant % 3);
			if (variant % 5 == 0) {
				g.spec.user_gain = gain;
				g.spec.user_offset = off;
			}
		}
		variant++;

		for (n = 0; n <= max_n; n++) {
			g.nb_samples = n;
			compare_case(be, &g);
		}
	}
}

static bool has_pair_rd(const char *be, unsigned int length, bool sgn, enum iio_conv_type type)
{
	struct iio_conv_chan ch[2] = {
		{ .length = length, .bits = length, .is_signed = sgn, .byte_offset = 0 },
		{ .length = length, .bits = length, .is_signed = sgn, .byte_offset = length / 8 },
	};
	struct iio_conv_spec spec = { .type = type, .complex_iq = true };
	struct iio_conv *c = NULL;
	bool ok;

	if (iio_conv_create_raw_backend(be, ch, 2, length / 4, &spec, &c))
		return false;
	ok = c->rd_pair[0] != NULL && c->wr_pair[0] != NULL;
	iio_conv_destroy(c);
	return ok;
}

int main(void)
{
	static const char *const names[] = { "sse41", "avx2", "avx512", "neon" };
	static const unsigned int bits16[] = { 12, 14, 16 };
	static const enum iio_conv_type types16[] = { IIO_CONV_I16, IIO_CONV_F32, IIO_CONV_F64 };
	struct iio_conv *c = NULL;
	struct iio_conv_chan one = { .length = 16, .bits = 16 };
	struct iio_conv_spec spec = { .type = IIO_CONV_I16 };
	unsigned int b, iter, tested = 0;

	CHECK(iio_conv_create_raw_backend("nonsense", &one, 1, 2, &spec, &c) == -ENOTSUP);

	for (b = 0; b < 4; b++) {
		const char *be = names[b];

		if (!conv_backend_by_name(be)) {
			printf("skip %s\n", be);
			continue;
		}
		printf("test %s\n", be);
		tested++;

		/* the fast path is really in use */
		CHECK(has_pair_rd(be, 16, true, IIO_CONV_F32));
		CHECK(has_pair_rd(be, 16, true, IIO_CONV_F64));
		CHECK(has_pair_rd(be, 16, true, IIO_CONV_I16));

		sweep(be, 16, bits16, 3, types16, 3, 130);

		for (iter = 0; iter < 5000; iter++) {
			struct gen_case g;

			gen_random_case(&rng, &g, 70, 150);
			compare_case(be, &g);
		}
	}

	printf("%u backend(s) tested\n", tested);
	TEST_MAIN_RETURN();
}
