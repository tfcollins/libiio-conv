/* SPDX-License-Identifier: MIT */
/*
 * Throughput of iio_conv against the usual way of doing the same job with
 * libiio alone: iio_channel_convert() per sample (what iio_channel_read()
 * does internally) into an int16 temporary, then a plain C loop for the type
 * change, I/Q pairing and scaling.
 *
 * Usage: bench_conv tests/resources/conv_test.xml
 */
#include <iio/iio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal.h"

#define NB_SAMPLES (1u << 20)
#define REPEATS 9
#define MAX_CH 8

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

struct bcase {
	const char *name;
	enum iio_conv_type type;
	bool cplx;
};

static const struct bcase cases[] = {
	{ "int16 real", IIO_CONV_I16, false },
	{ "int16 complex", IIO_CONV_I16, true },
	{ "single complex", IIO_CONV_F32, true },
	{ "double complex", IIO_CONV_F64, true },
};

static const struct iio_channel *chans[MAX_CH];
static int16_t *tmp[MAX_CH];

/* host position of channel c, sample i (COLUMNS layout), in elements of type */
static size_t pos(unsigned int c, size_t i, bool cplx)
{
	return cplx ? (size_t)(c / 2) * NB_SAMPLES * 2 + 2 * i + (c & 1) :
		(size_t)c * NB_SAMPLES + i;
}

static void baseline_read(const struct bcase *bc, unsigned int nch, const uint8_t *src,
		size_t step, void *dst)
{
	const double scale = 1.0 / 2048;
	unsigned int c;
	size_t i;

	for (c = 0; c < nch; c++)
		for (i = 0; i < NB_SAMPLES; i++)
			iio_channel_convert(chans[c], &tmp[c][i], src + i * step + 2 * c);

	for (c = 0; c < nch; c++) {
		const int16_t *t = tmp[c];

		if (bc->type == IIO_CONV_I16) {
			int16_t *d = dst;
			for (i = 0; i < NB_SAMPLES; i++)
				d[pos(c, i, bc->cplx)] = t[i];
		} else if (bc->type == IIO_CONV_F32) {
			float *d = dst;
			for (i = 0; i < NB_SAMPLES; i++)
				d[pos(c, i, bc->cplx)] = (float)t[i] * (float)scale;
		} else {
			double *d = dst;
			for (i = 0; i < NB_SAMPLES; i++)
				d[pos(c, i, bc->cplx)] = (double)t[i] * scale;
		}
	}
}

static void baseline_write(unsigned int nch, uint8_t *dst, size_t step, const double *src)
{
	unsigned int c;
	size_t i;

	for (c = 0; c < nch; c++) {
		int16_t *t = tmp[c];

		for (i = 0; i < NB_SAMPLES; i++) {
			double v = nearbyint(src[pos(c, i, true)] * 2048.0);

			if (v != v)
				v = 0;
			if (v > 2047)
				v = 2047;
			if (v < -2048)
				v = -2048;
			t[i] = (int16_t)v;
		}
		for (i = 0; i < NB_SAMPLES; i++)
			iio_channel_convert_inverse(chans[c], dst + i * step + 2 * c, &t[i]);
	}
}

int main(int argc, char **argv)
{
	static const char *const backends[] = { "scalar", "sse41", "avx2", "avx512", "neon" };
	static const unsigned int nchs[] = { 2, 4, 8 };
	const struct iio_device *dev;
	struct iio_context *ctx;
	char uri[1024];
	unsigned int ic, in, ib, c, r;
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s conv_test.xml\n", argv[0]);
		return 2;
	}
	snprintf(uri, sizeof(uri), "xml:%s", argv[1]);
	ctx = iio_create_context(NULL, uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "cannot open %s\n", uri);
		return 1;
	}
	dev = iio_context_find_device(ctx, "conv-bench");
	for (c = 0; c < MAX_CH; c++) {
		char id[16];

		snprintf(id, sizeof(id), "voltage%u", c);
		chans[c] = iio_device_find_channel(dev, id, false);
		tmp[c] = malloc(NB_SAMPLES * sizeof(int16_t));
	}

	printf("| channels | host array | backend | MS/s per channel | vs libiio |\n");
	printf("|---|---|---|---|---|\n");

	for (in = 0; in < 3; in++) {
		unsigned int nch = nchs[in];
		size_t step = 2 * nch, i;
		uint8_t *src = malloc(step * NB_SAMPLES), *back = malloc(step * NB_SAMPLES);
		struct iio_conv_chan desc[MAX_CH];
		uint32_t x = 12345;

		for (i = 0; i < step * NB_SAMPLES; i++) {
			x = x * 1664525u + 1013904223u;
			src[i] = (uint8_t)(x >> 24);
		}
		for (c = 0; c < nch; c++)
			desc[c] = (struct iio_conv_chan){ .length = 16, .bits = 12, .is_signed = true,
				.byte_offset = 2 * c };

		for (ic = 0; ic < sizeof(cases) / sizeof(cases[0]); ic++) {
			const struct bcase *bc = &cases[ic];
			size_t bytes = (size_t)nch * NB_SAMPLES * conv_type_size(bc->type);
			struct iio_conv_spec spec = { .type = bc->type, .complex_iq = bc->cplx,
				.scale = bc->type == IIO_CONV_I16 ? IIO_CONV_SCALE_NONE :
					IIO_CONV_SCALE_FULLSCALE };
			void *ref = malloc(bytes), *out = malloc(bytes);
			double best = 1e9, base;

			memset(ref, 0, bytes);
			memset(out, 0, bytes);
			for (r = 0; r < REPEATS; r++) {
				double t0 = now();

				baseline_read(bc, nch, src, step, ref);
				t0 = now() - t0;
				if (t0 < best)
					best = t0;
			}
			base = best;
			printf("| %u | %s | libiio + C loop | %.0f | 1.0x |\n", nch, bc->name,
					NB_SAMPLES / base / 1e6);

			for (ib = 0; ib < 5; ib++) {
				struct iio_conv *conv = NULL;

				if (iio_conv_create_raw_backend(backends[ib], desc, nch, step, &spec, &conv))
					continue;
				best = 1e9;
				for (r = 0; r < REPEATS; r++) {
					double t0 = now();

					iio_conv_read_raw(conv, src, NB_SAMPLES, out);
					t0 = now() - t0;
					if (t0 < best)
						best = t0;
				}
				if (memcmp(ref, out, bytes)) {
					fprintf(stderr, "MISMATCH: %u ch %s %s\n", nch, bc->name, backends[ib]);
					rc = 1;
				}
				printf("| %u | %s | %s | %.0f | %.1fx |\n", nch, bc->name, backends[ib],
						NB_SAMPLES / best / 1e6, base / best);
				iio_conv_destroy(conv);
			}

			/* transmit direction, double complex only */
			if (bc->type == IIO_CONV_F64) {
				uint8_t *sref = malloc(step * NB_SAMPLES);

				best = 1e9;
				for (r = 0; r < REPEATS; r++) {
					double t0 = now();

					baseline_write(nch, sref, step, ref);
					t0 = now() - t0;
					if (t0 < best)
						best = t0;
				}
				base = best;
				printf("| %u | Tx %s | libiio + C loop | %.0f | 1.0x |\n", nch, bc->name,
						NB_SAMPLES / base / 1e6);
				for (ib = 0; ib < 5; ib++) {
					struct iio_conv *conv = NULL;

					if (iio_conv_create_raw_backend(backends[ib], desc, nch, step, &spec, &conv))
						continue;
					best = 1e9;
					for (r = 0; r < REPEATS; r++) {
						double t0 = now();

						iio_conv_write_raw(conv, back, NB_SAMPLES, ref);
						t0 = now() - t0;
						if (t0 < best)
							best = t0;
					}
					if (memcmp(sref, back, step * NB_SAMPLES)) {
						fprintf(stderr, "MISMATCH: Tx %u ch %s\n", nch, backends[ib]);
						rc = 1;
					}
					printf("| %u | Tx %s | %s | %.0f | %.1fx |\n", nch, bc->name,
							backends[ib], NB_SAMPLES / best / 1e6, base / best);
					iio_conv_destroy(conv);
				}
				free(sref);
			}
			free(ref);
			free(out);
		}
		free(src);
		free(back);
	}

	iio_context_destroy(ctx);
	return rc;
}
