/* SPDX-License-Identifier: MIT */
/* Streams against the emulation backend: real blocks, data from files. */
#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-conv-stream.h>
#include "stream_util.h"

#define N 64
#define FILE_FRAMES 4

static struct iio_context *ctx;
static const struct iio_device *rx, *tx;
static struct iio_buffer *rx_buf, *tx_buf;
static const struct iio_channel *rx_i, *rx_q, *rx_v2, *tx0, *tx1;

/* RX data for the mask {voltage0_i, voltage0_q}: sample k holds I = k + 1,
 * Q = -(k + 1). emu delivers the file as it is and wraps at its end. */
static void write_rx_iq(void)
{
	int16_t d[2 * N * FILE_FRAMES];
	int k;

	for (k = 0; k < N * FILE_FRAMES; k++) {
		d[2 * k] = (int16_t)(k + 1);
		d[2 * k + 1] = (int16_t)-(k + 1);
	}
	fx_write("conv-rx_buf0.bin", d, sizeof(d));
}

/* value of sample k of frame f */
static int rx_value(unsigned int frame, unsigned int k)
{
	return (int)((frame * N + k) % (N * FILE_FRAMES)) + 1;
}

static struct iio_conv_stream *open_rx(enum iio_conv_type type, enum iio_conv_scale scale,
		unsigned int nb_blocks, int timeout_ms)
{
	const struct iio_channel *chans[2] = { rx_i, rx_q };
	struct iio_conv_stream_params p = { .nb_samples = N, .nb_blocks = nb_blocks,
		.timeout_ms = timeout_ms,
		.spec = { .type = type, .scale = scale, .complex_iq = true } };
	struct iio_conv_stream *s = NULL;

	write_rx_iq();
	CHECK(iio_conv_stream_open(rx_buf, chans, 2, &p, &s) == 0);
	return s;
}

static void test_rx_complex_single(void)
{
	struct iio_conv_stream *s = open_rx(IIO_CONV_F32, IIO_CONV_SCALE_FULLSCALE, 2, -1);
	float out[2 * N];
	unsigned int f, k;

	if (!s)
		return;
	CHECK(!iio_conv_stream_is_output(s));
	CHECK(iio_conv_stream_get_nb_samples(s) == N);
	CHECK(iio_conv_stream_get_nb_blocks(s) == 2);
	CHECK(iio_conv_stream_get_step(s) == 4);
	CHECK(iio_conv_stream_get_host_step(s) == 8);

	/* six frames: the fifth and sixth come from the start of the file again */
	for (f = 0; f < 6; f++) {
		memset(out, 0, sizeof(out));
		CHECK(iio_conv_stream_read(s, out) == N);
		for (k = 0; k < N; k++) {
			float v = (float)rx_value(f, k) / 2048.0f;

			CHECK(out[2 * k] == v && out[2 * k + 1] == -v);
		}
	}
	iio_conv_stream_close(s);
}

static void test_rx_int16(void)
{
	static const unsigned int blocks[] = { 1, 2, 4 };
	static const int timeouts[] = { -1, 0, 50 };
	int16_t out[2 * N];
	unsigned int b, f, k;

	for (b = 0; b < 3; b++) {
		struct iio_conv_stream *s = open_rx(IIO_CONV_I16, IIO_CONV_SCALE_NONE, blocks[b],
				timeouts[b]);

		if (!s)
			continue;
		CHECK(iio_conv_stream_get_nb_blocks(s) == blocks[b]);
		for (f = 0; f < 3; f++) {
			CHECK(iio_conv_stream_read(s, out) == N);
			for (k = 0; k < N; k++)
				CHECK(out[2 * k] == rx_value(f, k) && out[2 * k + 1] == -rx_value(f, k));
		}
		iio_conv_stream_close(s);
	}
}

static void test_rx_defaults_and_flush(void)
{
	struct iio_conv_stream *s = open_rx(IIO_CONV_I16, IIO_CONV_SCALE_NONE, 0, -1);
	int16_t out[2 * N];

	if (!s)
		return;
	CHECK(iio_conv_stream_get_nb_blocks(s) == 2); /* the default */
	CHECK(iio_conv_stream_read(s, out) == N);
	CHECK(out[0] == rx_value(0, 0));
	/* on emu every block is ready at once, so a flush drops one frame per block */
	CHECK(iio_conv_stream_flush(s) == 2);
	CHECK(iio_conv_stream_read(s, out) == N);
	CHECK(out[0] == rx_value(3, 0) && out[2 * N - 1] == -rx_value(3, N - 1));
	iio_conv_stream_close(s);
}

/* two channels that are not neighbours, passed against their device order */
static void test_rx_subset_in_caller_order(void)
{
	const struct iio_channel *chans[2] = { rx_v2, rx_i };
	struct iio_conv_stream_params p = { .nb_samples = N, .nb_blocks = 2, .timeout_ms = -1,
		.spec = { .type = IIO_CONV_F64, .layout = IIO_CONV_COLUMNS } };
	struct iio_conv_stream *s = NULL;
	int16_t d[2 * N];
	double out[2 * N];
	int k;

	/* mask {voltage0_i, voltage2}: I at byte 0, voltage2 (14 bits, shift 2) at byte 2 */
	for (k = 0; k < N; k++) {
		d[2 * k] = (int16_t)(k + 1);
		d[2 * k + 1] = (int16_t)((2 * (k + 1)) << 2);
	}
	fx_write("conv-rx_buf0.bin", d, sizeof(d));

	CHECK(iio_conv_stream_open(rx_buf, chans, 2, &p, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 4);
	CHECK(iio_conv_stream_read(s, out) == N);
	for (k = 0; k < N; k++)
		CHECK(out[k] == 2.0 * (k + 1) && out[N + k] == (double)(k + 1));
	iio_conv_stream_close(s);
}

static void test_errors(void)
{
	const struct iio_channel *iq[2] = { rx_i, rx_q }, *mixed[2] = { rx_i, tx0 };
	const struct iio_channel *txc[2] = { tx0, tx1 };
	struct iio_conv_stream_params p = { .nb_samples = N, .timeout_ms = -1,
		.spec = { .type = IIO_CONV_I16, .complex_iq = true } };
	struct iio_conv_stream_params bad;
	struct iio_conv_stream *s = NULL;
	int16_t buf[2 * N] = { 0 };

	write_rx_iq();
	CHECK(iio_conv_stream_open(NULL, iq, 2, &p, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, NULL, 2, &p, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, iq, 0, &p, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, iq, 2, NULL, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, iq, 2, &p, NULL) == -EINVAL);
	bad = p; bad.nb_samples = 0;
	CHECK(iio_conv_stream_open(rx_buf, iq, 2, &bad, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, mixed, 2, &p, &s) == -EINVAL); /* other device */
	CHECK(iio_conv_stream_open(rx_buf, iq, 1, &p, &s) == -EINVAL);    /* odd count, complex */
	bad = p; bad.spec.scale = IIO_CONV_SCALE_FULLSCALE;                /* scaling needs floats */
	CHECK(iio_conv_stream_open(rx_buf, iq, 2, &bad, &s) == -EINVAL);
	CHECK(s == NULL);

	/* wrong direction */
	CHECK(iio_conv_stream_open(rx_buf, iq, 2, &p, &s) == 0);
	if (s) {
		CHECK(iio_conv_stream_write(s, buf) == -EPERM);
		CHECK(iio_conv_stream_read(s, NULL) == -EINVAL);
		iio_conv_stream_close(s);
	}
	s = NULL;
	CHECK(iio_conv_stream_open(tx_buf, txc, 2, &p, &s) == 0);
	if (s) {
		CHECK(iio_conv_stream_is_output(s));
		CHECK(iio_conv_stream_read(s, buf) == -EPERM);
		CHECK(iio_conv_stream_flush(s) == -EPERM);
		CHECK(iio_conv_stream_write(s, NULL) == -EINVAL);
		iio_conv_stream_close(s);
	}
	CHECK(iio_conv_stream_read(NULL, buf) == -EINVAL);
	CHECK(iio_conv_stream_write(NULL, buf) == -EINVAL);
	CHECK(iio_conv_stream_flush(NULL) == -EINVAL);
	iio_conv_stream_close(NULL);
}

/* --- transmit: emu writes a block to <dir>/conv-tx_buf0.bin when it is
 * dequeued, replacing the file, so the file shows the last frame sent. */

static struct iio_conv_stream *open_tx(enum iio_conv_type type, enum iio_conv_scale scale,
		unsigned int nb_blocks, bool cyclic)
{
	const struct iio_channel *chans[2] = { tx0, tx1 };
	struct iio_conv_stream_params p = { .nb_samples = N, .nb_blocks = nb_blocks,
		.cyclic = cyclic, .timeout_ms = -1,
		.spec = { .type = type, .scale = scale, .complex_iq = true } };
	struct iio_conv_stream *s = NULL;

	fx_remove("conv-tx_buf0.bin");
	CHECK(iio_conv_stream_open(tx_buf, chans, 2, &p, &s) == 0);
	return s;
}

/* frame number f as doubles in [-1, 1) and the int16 words it must become */
static void tx_frame(unsigned int f, double *host, int16_t *raw)
{
	int k;

	for (k = 0; k < N; k++) {
		raw[2 * k] = (int16_t)((k - 16) * 100 + (int)f);
		raw[2 * k + 1] = (int16_t)(-7 * k - (int)f);
		host[2 * k] = raw[2 * k] / 32768.0;
		host[2 * k + 1] = raw[2 * k + 1] / 32768.0;
	}
}

static void check_tx_file(const int16_t *raw)
{
	int16_t got[2 * N + 8];

	CHECK(fx_read("conv-tx_buf0.bin", got, sizeof(got)) == (long)(2 * N * sizeof(int16_t)));
	CHECK(memcmp(got, raw, 2 * N * sizeof(int16_t)) == 0);
}

static void test_tx_frames(void)
{
	static const unsigned int blocks[] = { 1, 2, 3 };
	double host[2 * N];
	int16_t raw[2 * N];
	unsigned int b, f;

	for (b = 0; b < 3; b++) {
		struct iio_conv_stream *s = open_tx(IIO_CONV_F64, IIO_CONV_SCALE_FULLSCALE, blocks[b], false);

		if (!s)
			continue;
		CHECK(iio_conv_stream_get_step(s) == 4);
		CHECK(iio_conv_stream_get_host_step(s) == 16);
		for (f = 0; f < 5; f++) {
			tx_frame(f, host, raw);
			CHECK(iio_conv_stream_write(s, host) == N);
		}
		/* closing lets the last frame out */
		iio_conv_stream_close(s);
		check_tx_file(raw);
	}
}

static void test_tx_one_frame_then_close(void)
{
	struct iio_conv_stream *s = open_tx(IIO_CONV_I16, IIO_CONV_SCALE_NONE, 2, false);
	double host[2 * N];
	int16_t raw[2 * N];

	if (!s)
		return;
	tx_frame(9, host, raw);
	CHECK(iio_conv_stream_write(s, raw) == N); /* int16 in, same words out */
	iio_conv_stream_close(s);
	check_tx_file(raw);
}

static void test_tx_cyclic(void)
{
	struct iio_conv_stream *s = open_tx(IIO_CONV_F64, IIO_CONV_SCALE_FULLSCALE, 2, true);
	double host[2 * N];
	int16_t raw[2 * N];

	if (!s)
		return;
	tx_frame(1, host, raw);
	CHECK(iio_conv_stream_write(s, host) == N);
	CHECK(iio_conv_stream_write(s, host) == -EBUSY);
	iio_conv_stream_close(s);
}

/* emu cannot start a receive buffer without its data file. Opening must fail
 * at once and leave nothing behind (emu would stall 5 s per block if a block
 * were dequeued after a failed start). */
static void test_rx_start_failure_is_prompt(void)
{
	const struct iio_channel *chans[2] = { rx_i, rx_q };
	struct iio_conv_stream_params p = { .nb_samples = N, .nb_blocks = 2, .timeout_ms = -1,
		.spec = { .type = IIO_CONV_I16, .complex_iq = true } };
	struct iio_conv_stream *s = NULL;
	double t0 = now_ms();
	int ret;

	fx_remove("conv-rx_buf0.bin");
	ret = iio_conv_stream_open(rx_buf, chans, 2, &p, &s);
	CHECK(ret < 0);
	CHECK(s == NULL);
	CHECK(now_ms() - t0 < 2000.0);
	if (s)
		iio_conv_stream_close(s);
}

int main(int argc, char **argv)
{
	char uri[700];

	if (argc < 2)
		return 2;
	fx_setup(argv[1]);
	snprintf(uri, sizeof(uri), "emu:%s", fx_xml);
	ctx = iio_create_context(NULL, uri);
	CHECK(!iio_err(ctx));
	if (iio_err(ctx)) {
		fx_teardown();
		TEST_MAIN_RETURN();
	}
	rx = iio_context_find_device(ctx, "conv-rx");
	tx = iio_context_find_device(ctx, "conv-tx");
	rx_buf = iio_device_get_buffer(rx, 0);
	tx_buf = iio_device_get_buffer(tx, 0);
	rx_i = iio_device_find_channel(rx, "voltage0_i", false);
	rx_q = iio_device_find_channel(rx, "voltage0_q", false);
	rx_v2 = iio_device_find_channel(rx, "voltage2", false);
	tx0 = iio_device_find_channel(tx, "voltage0", true);
	tx1 = iio_device_find_channel(tx, "voltage1", true);
	CHECK(rx_buf && tx_buf && rx_i && rx_q && rx_v2 && tx0 && tx1);

	test_rx_complex_single();
	test_rx_int16();
	test_rx_defaults_and_flush();
	test_rx_subset_in_caller_order();
	test_errors();
	test_tx_frames();
	test_tx_one_frame_then_close();
	test_tx_cyclic();
	test_rx_start_failure_is_prompt();

	iio_context_destroy(ctx);
	fx_teardown();
	TEST_MAIN_RETURN();
}
