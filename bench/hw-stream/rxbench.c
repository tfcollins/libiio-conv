/* SPDX-License-Identifier: MIT */
/* Receive-only timing of the libiio-conv stream layer versus a plain libiio v1 block loop.
 * usage: rxbench info  <uri>
 *        rxbench conv  <uri> <samples_per_frame> <frames> <timeout_ms>
 *        rxbench plain <uri> <samples_per_frame> <frames>
 * Writes no attribute. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include <iio/iio.h>
#include <iio/iio-conv-stream.h>

#define NB_BLOCKS 4

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double cpu(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 +
		(double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
}

static int cmpd(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static void rd(const struct iio_attr *a, const char *label)
{
	char buf[256];
	ssize_t n;

	if (!a) {
		printf("%s: <no such attribute>\n", label);
		return;
	}
	n = iio_attr_read_raw(a, buf, sizeof(buf) - 1);
	if (n < 0) {
		printf("%s: error %zd\n", label, n);
		return;
	}
	buf[n] = 0;
	printf("%s: %s\n", label, buf);
}

static void stats(const char *name, size_t spf, const double *t, const int *ok, unsigned int n,
		unsigned int valid, unsigned int invalid, double wall, double cpu_s, double close_s,
		const char *extra)
{
	double *v = malloc(sizeof(*v) * (n ? n : 1)), sum = 0, steady = 0;
	unsigned int i, nv = 0, ns = 0;

	for (i = 0; i < n; i++) {
		if (!ok[i])
			continue;
		v[nv++] = t[i];
		sum += t[i];
	}
	/* steady state: leave out the first NB_BLOCKS frames (captured before the first read) */
	for (i = NB_BLOCKS; i < n; i++) {
		steady += t[i];
		ns += ok[i] != 0;
	}
	qsort(v, nv, sizeof(*v), cmpd);
	printf("RESULT %s spf=%zu frames=%u valid=%u invalid=%u mean_ms=%.3f median_ms=%.3f worst_ms=%.3f "
		"wall_s=%.3f rate_all_sps=%.0f rate_steady_sps=%.0f cpu_s=%.3f cpu_share=%.1f%% close_ms=%.1f %s\n",
		name, spf, n, valid, invalid, nv ? sum / nv * 1e3 : 0.0,
		nv ? (nv % 2 ? v[nv / 2] : (v[nv / 2 - 1] + v[nv / 2]) / 2) * 1e3 : 0.0,
		nv ? v[nv - 1] * 1e3 : 0.0, wall, wall > 0 ? valid * (double)spf / wall : 0.0,
		steady > 0 ? ns * (double)spf / steady : 0.0, cpu_s,
		wall > 0 ? 100.0 * cpu_s / wall : 0.0, close_s * 1e3, extra);
	free(v);
}

int main(int argc, char **argv)
{
	struct iio_context *ctx;
	const struct iio_device *dev, *phy;
	const struct iio_channel *ch[2];
	struct iio_buffer *buf;
	size_t spf;
	unsigned int n, i, valid = 0, invalid = 0;
	double *t, t0, t1, c0, c1, close_s;
	int *ok, ret, first_err = 0;
	char extra[256] = "";

	if (argc < 3)
		return 2;
	ctx = iio_create_context(NULL, argv[2]);
	if (iio_err(ctx)) {
		fprintf(stderr, "context: error %d\n", iio_err(ctx));
		return 1;
	}
	dev = iio_context_find_device(ctx, "cf-ad9361-lpc");
	phy = iio_context_find_device(ctx, "ad9361-phy");
	if (!dev || !phy) {
		fprintf(stderr, "not this board: cf-ad9361-lpc %p ad9361-phy %p\n", (void *)dev, (void *)phy);
		return 1;
	}

	if (!strcmp(argv[1], "info")) {
		const struct iio_channel *rx = iio_device_find_channel(phy, "voltage0", false);
		const struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage0", true);
		unsigned int k;

		printf("library: libiio %u.%u %s\n", iio_context_get_version_major(NULL),
			iio_context_get_version_minor(NULL), iio_context_get_version_tag(NULL));
		printf("remote: %u.%u %s\n", iio_context_get_version_major(ctx),
			iio_context_get_version_minor(ctx), iio_context_get_version_tag(ctx));
		printf("context: %s / %s\n", iio_context_get_name(ctx), iio_context_get_description(ctx));
		for (k = 0; k < iio_context_get_attrs_count(ctx); k++) {
			const struct iio_attr *a = iio_context_get_attr(ctx, k);
			printf("ctx attr %s = %s\n", iio_attr_get_name(a), iio_attr_get_static_value(a));
		}
		for (k = 0; k < iio_context_get_devices_count(ctx); k++)
			printf("device %s\n", iio_device_get_name(iio_context_get_device(ctx, k)));
		if (rx) {
			rd(iio_channel_find_attr(rx, "sampling_frequency"), "sampling_frequency");
			rd(iio_channel_find_attr(rx, "rf_bandwidth"), "rf_bandwidth");
		}
		if (lo)
			rd(iio_channel_find_attr(lo, "frequency"), "RX_LO frequency");
		iio_context_destroy(ctx);
		return 0;
	}
	if (argc < 5)
		return 2;
	spf = strtoul(argv[3], NULL, 0);
	n = (unsigned int)strtoul(argv[4], NULL, 0);
	t = calloc(n, sizeof(*t));
	ok = calloc(n, sizeof(*ok));
	ch[0] = iio_device_find_channel(dev, "voltage0", false);
	ch[1] = iio_device_find_channel(dev, "voltage1", false);
	buf = iio_device_get_buffer(dev, 0);
	if (!ch[0] || !ch[1] || !buf) {
		fprintf(stderr, "channels or buffer missing\n");
		return 1;
	}

	if (!strcmp(argv[1], "conv")) {
		struct iio_conv_spec spec = { .type = IIO_CONV_F64, .layout = IIO_CONV_COLUMNS,
			.scale = IIO_CONV_SCALE_NONE, .complex_iq = true, .raw = true };
		struct iio_conv_stream *s = NULL;
		double *dst = malloc(spf * 2 * sizeof(double));
		double mn = 1e300, mx = -1e300;
		size_t changes = 0, k;
		int tmo = argc > 5 ? atoi(argv[5]) : -1;

		ret = iio_conv_stream_open(buf, ch, 2, spf, NB_BLOCKS, false, &spec, &s);
		if (ret < 0) {
			fprintf(stderr, "stream open: error %d\n", ret);
			return 1;
		}
		ret = iio_conv_stream_set_timeout(s, tmo);
		if (ret < 0)
			fprintf(stderr, "set_timeout: error %d\n", ret);
		c0 = cpu();
		t0 = now();
		for (i = 0; i < n; i++) {
			double a = now();

			ret = iio_conv_stream_read(s, dst);
			t[i] = now() - a;
			ok[i] = ret == IIO_CONV_STREAM_FRAME;
			if (ok[i]) {
				valid++;
			} else {
				invalid++;
				if (!first_err)
					first_err = ret ? ret : 1; /* 1: no frame in time */
				if (ret < 0 && invalid >= 3)
					{ i++; break; }
			}
		}
		t1 = now();
		c1 = cpu();
		for (k = 0; k < 2 * spf && valid; k++) { /* last frame: interleaved re, im */
			if (dst[k] < mn) mn = dst[k];
			if (dst[k] > mx) mx = dst[k];
			if (k >= 2 && dst[k] != dst[k - 2]) changes++;
		}
		snprintf(extra, sizeof(extra), "timeout_ms=%d step=%zu first_err=%d last_frame_min=%g max=%g changes=%zu/%zu",
			tmo, iio_conv_stream_get_step(s), first_err, mn, mx, changes, 2 * spf - 2);
		close_s = now();
		iio_conv_stream_close(&s);
		close_s = now() - close_s;
		stats("conv", spf, t, ok, i, valid, invalid, t1 - t0, c1 - c0, close_s, extra);
	} else {
		struct iio_channels_mask *mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
		struct iio_buffer_stream *bs;
		struct iio_block *blk[NB_BLOCKS];
		size_t step, changes = 0, k;
		const int16_t *p = NULL;
		int mn = 32767, mx = -32768;
		unsigned int b;

		iio_channel_enable(ch[0], mask);
		iio_channel_enable(ch[1], mask);
		bs = iio_buffer_open(buf, mask);
		if (iio_err(bs)) {
			fprintf(stderr, "buffer open: error %d\n", iio_err(bs));
			return 1;
		}
		step = (size_t)iio_device_get_sample_size(dev, iio_buffer_stream_get_channels_mask(bs));
		for (b = 0; b < NB_BLOCKS; b++) {
			blk[b] = iio_buffer_stream_create_block(bs, spf * step);
			if (iio_err(blk[b])) {
				fprintf(stderr, "create block: error %d\n", iio_err(blk[b]));
				return 1;
			}
			ret = iio_block_enqueue(blk[b], 0, false);
			if (ret < 0) {
				fprintf(stderr, "enqueue: error %d\n", ret);
				return 1;
			}
		}
		ret = iio_buffer_stream_start(bs);
		if (ret < 0) {
			fprintf(stderr, "start: error %d\n", ret);
			return 1;
		}
		c0 = cpu();
		t0 = now();
		for (i = 0; i < n; i++) {
			double a = now();

			b = i % NB_BLOCKS;
			ret = iio_block_dequeue(blk[b], false);
			if (ret == 0 && i == n - 1) { /* look at the last frame before it is queued again */
				p = iio_block_start(blk[b]);
				for (k = 0; k < 2 * spf; k++) {
					if (p[k] < mn) mn = p[k];
					if (p[k] > mx) mx = p[k];
					if (k >= 2 && p[k] != p[k - 2]) changes++;
				}
			}
			if (ret == 0)
				ret = iio_block_enqueue(blk[b], 0, false);
			t[i] = now() - a;
			ok[i] = ret == 0;
			if (ok[i]) {
				valid++;
			} else {
				invalid++;
				if (!first_err)
					first_err = ret;
				if (invalid >= 3)
					{ i++; break; }
			}
		}
		t1 = now();
		c1 = cpu();
		snprintf(extra, sizeof(extra), "step=%zu first_err=%d last_frame_min=%d max=%d changes=%zu/%zu",
			step, first_err, mn, mx, changes, 2 * spf - 2);
		close_s = now();
		iio_buffer_stream_cancel(bs);
		iio_buffer_stream_stop(bs);
		for (b = 0; b < NB_BLOCKS; b++)
			iio_block_destroy(blk[b]);
		iio_buffer_close(bs);
		close_s = now() - close_s;
		iio_channels_mask_destroy(mask);
		stats("plain", spf, t, ok, i, valid, invalid, t1 - t0, c1 - c0, close_s, extra);
	}
	iio_context_destroy(ctx);
	return 0;
}
