/* SPDX-License-Identifier: MIT */
/*
 * Throughput of iio_conv for 12-bit samples in 16-bit storage.
 *
 * Usage: bench_conv [-n log2_samples] [conv_test.xml]
 *
 * With an XML file (and a build that has libiio) each case is also compared
 * with the usual way of doing the same job with libiio alone:
 * iio_channel_convert() per sample (what iio_channel_read() does internally)
 * into an int16 temporary, then a plain C loop for the type change, I/Q
 * pairing and scaling. Without one, backends are compared with the scalar
 * backend only, which needs no libiio and so also runs on a bare board.
 *
 * -n sets the block length (default 20, i.e. 2^20 samples). Small blocks are
 * converted repeatedly so that every timing covers about 2^22 samples.
 */
#ifdef IIO_CONV_BENCH_LIBIIO
#include <iio/iio.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal.h"

#define REPEATS 9
#define MAX_CH 8

static size_t nb_samples = (size_t)1 << 20;
static unsigned int iters = 1;

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

static const char *const backends[] = { "scalar", "sse41", "avx2", "avx512", "neon" };

/* Best time of REPEATS runs for one pass over nb_samples samples */
#define TIME_BEST(best, stmt) do { \
		unsigned int r_, k_; \
		(best) = 1e9; \
		for (r_ = 0; r_ < REPEATS; r_++) { \
			double t_ = now(); \
			for (k_ = 0; k_ < iters; k_++) { stmt; } \
			t_ = (now() - t_) / iters; \
			if (t_ < (best)) \
				(best) = t_; \
		} \
	} while (0)

#ifdef IIO_CONV_BENCH_LIBIIO
static const struct iio_channel *chans[MAX_CH];
static int16_t *tmp[MAX_CH];

/* host position of channel c, sample i (COLUMNS layout), in elements */
static size_t pos(unsigned int c, size_t i, bool cplx)
{
	return cplx ? (size_t)(c / 2) * nb_samples * 2 + 2 * i + (c & 1) :
		(size_t)c * nb_samples + i;
}

static void baseline_read(const struct bcase *bc, unsigned int nch, const uint8_t *src,
		size_t step, void *dst)
{
	const double scale = 1.0 / 2048;
	unsigned int c;
	size_t i;

	for (c = 0; c < nch; c++)
		for (i = 0; i < nb_samples; i++)
			iio_channel_convert(chans[c], &tmp[c][i], src + i * step + 2 * c);

	for (c = 0; c < nch; c++) {
		const int16_t *t = tmp[c];

		if (bc->type == IIO_CONV_I16) {
			int16_t *d = dst;
			for (i = 0; i < nb_samples; i++)
				d[pos(c, i, bc->cplx)] = t[i];
		} else if (bc->type == IIO_CONV_F32) {
			float *d = dst;
			for (i = 0; i < nb_samples; i++)
				d[pos(c, i, bc->cplx)] = (float)t[i] * (float)scale;
		} else {
			double *d = dst;
			for (i = 0; i < nb_samples; i++)
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

		for (i = 0; i < nb_samples; i++) {
			double v = nearbyint(src[pos(c, i, true)] * 2048.0);

			if (v != v)
				v = 0;
			if (v > 2047)
				v = 2047;
			if (v < -2048)
				v = -2048;
			t[i] = (int16_t)v;
		}
		for (i = 0; i < nb_samples; i++)
			iio_channel_convert_inverse(chans[c], dst + i * step + 2 * c, &t[i]);
	}
}
#endif /* IIO_CONV_BENCH_LIBIIO */

static void row(unsigned int nch, const char *dir, const char *name, const char *backend,
		double t, double t_libiio, double t_scalar)
{
	printf("| %u | %s%s | %s | %.0f |", nch, dir, name, backend, nb_samples / t / 1e6);
	if (t_libiio > 0)
		printf(" %.1fx |", t_libiio / t);
	else
		printf(" - |");
	if (t_scalar > 0)
		printf(" %.1fx |\n", t_scalar / t);
	else
		printf(" - |\n");
}

int main(int argc, char **argv)
{
	static const unsigned int nchs[] = { 2, 4, 8 };
	const char *xml = NULL;
	bool have_base = false;
	unsigned int ic, in, ib, c;
	int rc = 0, a;

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-n") && a + 1 < argc) {
			int lg = atoi(argv[++a]);

			if (lg < 4 || lg > 26) {
				fprintf(stderr, "-n must be between 4 and 26\n");
				return 2;
			}
			nb_samples = (size_t)1 << lg;
		} else if (argv[a][0] == '-') {
			fprintf(stderr, "usage: %s [-n log2_samples] [conv_test.xml]\n", argv[0]);
			return 2;
		} else {
			xml = argv[a];
		}
	}
	iters = nb_samples >= ((size_t)1 << 22) ? 1 : (unsigned int)(((size_t)1 << 22) / nb_samples);

#ifdef IIO_CONV_BENCH_LIBIIO
	struct iio_context *ctx = NULL;

	if (xml) {
		const struct iio_device *dev;
		char uri[1024];

		snprintf(uri, sizeof(uri), "xml:%s", xml);
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
			tmp[c] = calloc(nb_samples, sizeof(int16_t));
		}
		have_base = true;
	}
#else
	if (xml)
		fprintf(stderr, "built without libiio: %s ignored, no libiio baseline\n", xml);
#endif

	printf("%zu samples per block, %u pass(es) per timing, best of %d\n\n",
			nb_samples, iters, REPEATS);
	printf("| channels | host array | backend | MS/s per channel | vs libiio | vs scalar |\n");
	printf("|---|---|---|---|---|---|\n");

	for (in = 0; in < 3; in++) {
		unsigned int nch = nchs[in];
		size_t step = 2 * nch, i;
		uint8_t *src = malloc(step * nb_samples), *back = calloc(step, nb_samples);
		struct iio_conv_chan desc[MAX_CH];
		uint32_t x = 12345;

		for (i = 0; i < step * nb_samples; i++) {
			x = x * 1664525u + 1013904223u;
			src[i] = (uint8_t)(x >> 24);
		}
		for (c = 0; c < nch; c++)
			desc[c] = (struct iio_conv_chan){ .length = 16, .bits = 12, .is_signed = true,
				.byte_offset = 2 * c };

		for (ic = 0; ic < sizeof(cases) / sizeof(cases[0]); ic++) {
			const struct bcase *bc = &cases[ic];
			size_t bytes = (size_t)nch * nb_samples * conv_type_size(bc->type);
			struct iio_conv_spec spec = { .type = bc->type, .complex_iq = bc->cplx,
				.scale = bc->type == IIO_CONV_I16 ? IIO_CONV_SCALE_NONE :
					IIO_CONV_SCALE_FULLSCALE };
			void *ref = calloc(1, bytes), *out = calloc(1, bytes);
			uint8_t *sref = calloc(step, nb_samples);
			double best, base = 0, scalar = 0;
			bool have_ref = false;

#ifdef IIO_CONV_BENCH_LIBIIO
			if (have_base) {
				TIME_BEST(base, baseline_read(bc, nch, src, step, ref));
				have_ref = true;
				row(nch, "", bc->name, "libiio + C loop", base, base, 0);
			}
#endif
			for (ib = 0; ib < 5; ib++) {
				struct iio_conv *conv = NULL;

				if (iio_conv_create_raw_backend(backends[ib], desc, nch, step, &spec, &conv))
					continue;
				TIME_BEST(best, iio_conv_read_raw(conv, src, nb_samples, out));
				if (!have_ref) { /* first backend is scalar: the reference */
					memcpy(ref, out, bytes);
					have_ref = true;
				} else if (memcmp(ref, out, bytes)) {
					fprintf(stderr, "MISMATCH: %u ch %s %s\n", nch, bc->name, backends[ib]);
					rc = 1;
				}
				if (ib == 0)
					scalar = best;
				row(nch, "", bc->name, backends[ib], best, base, scalar);
				iio_conv_destroy(conv);
			}

			/* transmit direction, double complex only */
			if (bc->type == IIO_CONV_F64) {
				have_ref = false;
				base = scalar = 0;
#ifdef IIO_CONV_BENCH_LIBIIO
				if (have_base) {
					TIME_BEST(base, baseline_write(nch, sref, step, ref));
					have_ref = true;
					row(nch, "Tx ", bc->name, "libiio + C loop", base, base, 0);
				}
#endif
				for (ib = 0; ib < 5; ib++) {
					struct iio_conv *conv = NULL;

					if (iio_conv_create_raw_backend(backends[ib], desc, nch, step, &spec, &conv))
						continue;
					TIME_BEST(best, iio_conv_write_raw(conv, back, nb_samples, ref));
					if (!have_ref) {
						memcpy(sref, back, step * nb_samples);
						have_ref = true;
					} else if (memcmp(sref, back, step * nb_samples)) {
						fprintf(stderr, "MISMATCH: Tx %u ch %s\n", nch, backends[ib]);
						rc = 1;
					}
					if (ib == 0)
						scalar = best;
					row(nch, "Tx ", bc->name, backends[ib], best, base, scalar);
					iio_conv_destroy(conv);
				}
			}
			free(sref);
			free(ref);
			free(out);
		}
		free(src);
		free(back);
	}

#ifdef IIO_CONV_BENCH_LIBIIO
	if (ctx)
		iio_context_destroy(ctx);
#endif
	(void)have_base;
	return rc;
}
