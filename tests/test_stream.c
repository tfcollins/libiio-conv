/* SPDX-License-Identifier: MIT */
/*
 * The stream layer against the emulation backend: real blocks, receive data
 * from the ramp file of the fixture (see fixture_util.h).
 */
#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-conv-stream.h>
#include <time.h>
#include "fixture_util.h"
#include "test_util.h"

static char dir[PATH_MAX];
static struct iio_context *ctx;
static const struct iio_device *rx, *tx, *multi, *zoo;
static struct iio_buffer *rx_buf, *tx_buf;
/* emu-rx: voltage0_i, voltage0_q, voltage1_i, voltage1_q, voltage2 */
static const struct iio_channel *rx_ch[5];
/* emu-tx: voltage0_i, voltage0_q, voltage1_i, voltage1_q */
static const struct iio_channel *tx_ch[4];

/* complex int16, no scaling: the spec of every case that does not say otherwise */
static const struct iio_conv_spec iq16 = {
	.type = IIO_CONV_I16, .layout = IIO_CONV_COLUMNS, .scale = IIO_CONV_SCALE_NONE,
	.complex_iq = true,
};

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static struct iio_conv_stream *open_rx(const struct iio_channel *const *chans,
		unsigned int nb_chans, size_t samples, unsigned int nb_blocks,
		const struct iio_conv_spec *spec)
{
	struct iio_conv_stream *s = NULL;

	CHECK(iio_conv_stream_open(rx_buf, chans, nb_chans, samples, nb_blocks, false, spec, &s) == 0);
	return s;
}

/* number of I/Q pairs of dst that are not the ramp from pair `first` on */
static unsigned int ramp_mismatches(const int16_t *dst, unsigned int first, unsigned int n)
{
	unsigned int k, bad = 0;

	for (k = 0; k < n; k++)
		bad += dst[2 * k] != fixture_ramp_i(first + k) || dst[2 * k + 1] != fixture_ramp_q(first + k);
	return bad;
}

static void test_rx_frames(void)
{
	struct iio_conv_stream *s = open_rx(rx_ch, 2, 1024, 2, &iq16);
	int16_t dst[2 * 1024];
	unsigned int f;

	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 4);
	CHECK(iio_conv_stream_get_columns(s) == 1);
	CHECK(iio_conv_stream_get_samples(s) == 1024);
	CHECK(iio_conv_stream_get_nb_blocks(s) == 2);
	CHECK(!iio_conv_stream_is_output(s));
	CHECK(iio_conv_stream_is_complex(s));
	CHECK(iio_conv_stream_get_type(s) == IIO_CONV_I16);

	/* the file holds four frames: the fifth read starts over */
	for (f = 0; f < 5; f++) {
		memset(dst, 0x55, sizeof(dst));
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		CHECK(ramp_mismatches(dst, 1024 * f, 1024) == 0);
	}
	CHECK(dst[0] == -2048 && dst[1] == 2047);
	iio_conv_stream_close(&s);
}

static void test_rx_one_block(void)
{
	struct iio_conv_stream *s = open_rx(rx_ch, 2, 1024, 1, &iq16);
	int16_t dst[2 * 1024];

	if (!s)
		return;
	CHECK(iio_conv_stream_get_nb_blocks(s) == 1);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(ramp_mismatches(dst, 0, 1024) == 0);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(ramp_mismatches(dst, 1024, 1024) == 0);
	CHECK(dst[0] == -1024 && dst[1] == 1023);
	iio_conv_stream_close(&s);
}

/* raw: the 16-bit words of the file, junk bits included */
static void test_rx_raw(void)
{
	struct iio_conv_spec spec = iq16;
	struct iio_conv_stream *s;
	int16_t dst[2 * 1024];
	unsigned int k, bad = 0;

	spec.raw = true;
	s = open_rx(rx_ch, 2, 1024, 2, &spec);
	if (!s)
		return;
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	for (k = 0; k < 1024; k++)
		bad += dst[2 * k] != (int16_t)fixture_ramp_word_i(k) ||
			dst[2 * k + 1] != (int16_t)fixture_ramp_word_q(k);
	CHECK(bad == 0);
	CHECK(dst[0] == 2048 && dst[1] == 2047);
	CHECK(dst[2] == 6145 && dst[3] == 2046);
	CHECK(dst[32] == 2064 && dst[33] == 6127);
	CHECK(dst[2046] == -1025 && dst[2047] == -3072);
	iio_conv_stream_close(&s);
}

static void test_rx_float(void)
{
	struct iio_conv_spec spec = iq16;
	struct iio_conv_stream *s;
	float dst[2 * 1024];
	unsigned int k, bad = 0;

	spec.type = IIO_CONV_F32;
	spec.scale = IIO_CONV_SCALE_FULLSCALE;
	s = open_rx(rx_ch, 2, 1024, 2, &spec);
	if (s) {
		CHECK(iio_conv_stream_get_type(s) == IIO_CONV_F32);
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		for (k = 0; k < 1024; k++)
			bad += dst[2 * k] != (float)fixture_ramp_i(k) / 2048.0f ||
				dst[2 * k + 1] != (float)fixture_ramp_q(k) / 2048.0f;
		CHECK(bad == 0);
		CHECK(dst[0] == -1.0f && dst[1] == 0.99951171875f);
		iio_conv_stream_close(&s);
	}

	/* scaling needs a float host type */
	spec.type = IIO_CONV_I16;
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 1024, 2, false, &spec, &s) == -EINVAL);
	CHECK(s == NULL);
}

/* The channel's IIO scale and offset. With voltage2 alone the samples are the
 * words of the file one by one: 14 bits from bit 2 up, (value - 100) * 0.25. */
static void test_rx_scale_iio(void)
{
	struct iio_conv_spec spec = { .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_IIO };
	struct iio_conv_stream *s = open_rx(&rx_ch[4], 1, 4, 1, &spec);
	double dst[4];
	unsigned int k;

	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 2);
	CHECK(iio_conv_stream_get_columns(s) == 1);
	CHECK(!iio_conv_stream_is_complex(s));
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	for (k = 0; k < 4; k++) {
		uint16_t w = k & 1 ? fixture_ramp_word_q(k / 2) : fixture_ramp_word_i(k / 2);

		CHECK(dst[k] == ((double)(w >> 2) - 100.0) * 0.25);
	}
	CHECK(dst[0] == 103.0 && dst[1] == 102.75 && dst[2] == 359.0);
	iio_conv_stream_close(&s);
}

/* Host columns follow the order of the channel list, not that of the device.
 * With four channels a sample is I0, Q0, I1, Q1: two ramp pairs. */
static void test_rx_pairing(void)
{
	const struct iio_channel *chans[4] = { rx_ch[2], rx_ch[3], rx_ch[0], rx_ch[1] };
	struct iio_conv_stream *s = open_rx(chans, 4, 512, 2, &iq16);
	int16_t dst[2 * 2 * 512];
	const int16_t *col0 = dst, *col1 = dst + 2 * 512;
	unsigned int j, bad = 0;

	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 8);
	CHECK(iio_conv_stream_get_columns(s) == 2);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	for (j = 0; j < 512; j++) {
		bad += col0[2 * j] != fixture_ramp_i(2 * j + 1) || col0[2 * j + 1] != fixture_ramp_q(2 * j + 1);
		bad += col1[2 * j] != fixture_ramp_i(2 * j) || col1[2 * j + 1] != fixture_ramp_q(2 * j);
	}
	CHECK(bad == 0);
	CHECK(col0[0] == -2047 && col0[1] == 2046);
	CHECK(col1[0] == -2048 && col1[1] == 2047);
	CHECK(col0[2 * 511] == -1025 && col0[2 * 511 + 1] == 1024);
	iio_conv_stream_close(&s);
}

static void test_rx_subset(void)
{
	const struct iio_channel *chans[2] = { rx_ch[3], rx_ch[0] };
	struct iio_conv_spec spec = iq16;
	struct iio_conv_stream *s;
	int16_t dst[5 * 4];
	int k;

	/* two channels that are not neighbours, against their device order:
	 * only they are enabled, so a sample is two words of the file */
	spec.complex_iq = false;
	s = open_rx(chans, 2, 8, 1, &spec);
	if (s) {
		CHECK(iio_conv_stream_get_step(s) == 4);
		CHECK(iio_conv_stream_get_columns(s) == 2);
		CHECK(!iio_conv_stream_is_complex(s));
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		for (k = 0; k < 8; k++) {
			CHECK(dst[k] == 2047 - k);      /* voltage1_q: second word */
			CHECK(dst[8 + k] == -2048 + k); /* voltage0_i: first word */
		}
		iio_conv_stream_close(&s);
	}

	s = open_rx(rx_ch, 5, 4, 1, &spec);
	if (s) {
		CHECK(iio_conv_stream_get_step(s) == 10);
		CHECK(iio_conv_stream_get_columns(s) == 5);
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		/* sample j is words 5j .. 5j+4; voltage0_i of sample 1 is word 5 */
		CHECK(dst[0] == fixture_ramp_i(0) && dst[1] == fixture_ramp_q(2));
		iio_conv_stream_close(&s);
	}
}

static void test_rx_flush(void)
{
	struct iio_conv_stream *s = open_rx(rx_ch, 2, 1024, 2, &iq16);
	int16_t dst[2 * 1024];

	if (!s)
		return;
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(ramp_mismatches(dst, 0, 1024) == 0);
	/* on emu every block is ready at once: one frame per block is dropped */
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(ramp_mismatches(dst, 3 * 1024, 1024) == 0);
	CHECK(dst[0] == 1024 && dst[1] == -1025);
	iio_conv_stream_close(&s);
}

/* emu is always ready, so every wait policy delivers every frame */
static void test_rx_wait_policies(void)
{
	static const int policies[] = { IIO_CONV_STREAM_TIMEOUT_CONTEXT,
		IIO_CONV_STREAM_TIMEOUT_NONBLOCK, 50 };
	int16_t dst[2 * 64];
	unsigned int p, f;

	for (p = 0; p < 3; p++) {
		struct iio_conv_stream *s = open_rx(rx_ch, 2, 64, 3, &iq16);

		if (!s)
			continue;
		CHECK(iio_conv_stream_set_timeout(s, policies[p]) == 0);
		for (f = 0; f < 5; f++) {
			CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
			CHECK(ramp_mismatches(dst, 64 * f, 64) == 0);
		}
		iio_conv_stream_close(&s);
	}
}

static void test_open_errors(void)
{
	const struct iio_channel *twice[2] = { rx_ch[0], rx_ch[0] };
	const struct iio_channel *mixed[2] = { rx_ch[0], tx_ch[0] };
	const struct iio_channel *hole[2] = { rx_ch[0], NULL };
	const struct iio_channel *one;
	struct iio_conv_spec real = iq16, bad_type = iq16;
	struct iio_conv_stream *s = NULL;
	struct iio_context *xml;
	char uri[PATH_MAX + 32];

	real.complex_iq = false;
	bad_type.type = (enum iio_conv_type)10;

	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 0, 4, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 0, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 4, 0, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 4, 1, false, NULL, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(NULL, rx_ch, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, NULL, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 4, 1, false, &iq16, NULL) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 4, 1, false, &bad_type, &s) == -EINVAL);

	/* a channel listed twice, or missing */
	CHECK(iio_conv_stream_open(rx_buf, twice, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, hole, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	/* channels that are not scan elements of this buffer: another device's,
	 * one without samples, one of the other direction */
	CHECK(iio_conv_stream_open(rx_buf, tx_ch, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, mixed, 2, 4, 1, false, &iq16, &s) == -EINVAL);
	one = iio_device_find_channel(tx, "altvoltage0", true);
	CHECK(one != NULL);
	CHECK(iio_conv_stream_open(tx_buf, &one, 1, 4, 1, false, &real, &s) == -EINVAL);
	one = iio_device_find_channel(multi, "voltage0", false);
	CHECK(one != NULL);
	CHECK(iio_conv_stream_open(iio_device_get_buffer(multi, 1), &one, 1, 4, 1, false, &real, &s) == -EINVAL);

	/* I/Q pairing needs an even number of channels */
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 3, 4, 1, false, &iq16, &s) == -EINVAL);
	/* nothing to repeat on an input */
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 4, 1, true, &iq16, &s) == -EINVAL);

	/* formats the converter does not handle: repeated and float storage */
	one = iio_device_find_channel(zoo, "rot_quaternion", false);
	CHECK(one != NULL);
	CHECK(iio_conv_stream_open(iio_device_get_buffer(zoo, 0), &one, 1, 4, 1, false, &real, &s) == -ENOTSUP);
	one = iio_device_find_channel(zoo, "voltage3", false);
	CHECK(one != NULL);
	CHECK(iio_conv_stream_open(iio_device_get_buffer(zoo, 0), &one, 1, 4, 1, false, &real, &s) == -ENOTSUP);
	CHECK(s == NULL);

	/* a backend without buffers: the error of iio_buffer_open() */
	fixture_uri(dir, "xml", uri, sizeof(uri));
	xml = iio_create_context(NULL, uri);
	CHECK(!iio_err(xml));
	if (!iio_err(xml)) {
		const struct iio_device *dev = iio_context_find_device(xml, "emu-rx");
		const struct iio_channel *chans[2] = {
			iio_device_find_channel(dev, "voltage0_i", false),
			iio_device_find_channel(dev, "voltage0_q", false),
		};

		CHECK(iio_conv_stream_open(iio_device_get_buffer(dev, 0), chans, 2, 4, 1, false,
					&iq16, &s) == -ENOSYS);
		CHECK(s == NULL);
		iio_context_destroy(xml);
	}
}

/* emu cannot start a receive buffer without its data file. The open must fail
 * at once: a dequeue from a buffer that is not started stalls for 5 s on emu
 * and for ever on other paths. */
static void test_rx_missing_file(void)
{
	static unsigned char ramp[4 * FIXTURE_RAMP_LEN];
	struct iio_conv_stream *s = NULL;
	int16_t dst[2 * 4];
	char path[PATH_MAX];
	double t0;

	CHECK(fixture_read_file(dir, "emu-rx_buf0.bin", ramp, sizeof(ramp)) == (long)sizeof(ramp));
	CHECK(fixture_path(dir, "emu-rx_buf0.bin", path) == 0);
	CHECK(remove(path) == 0);

	t0 = now_s();
	CHECK(iio_conv_stream_open(rx_buf, rx_ch, 2, 1024, 2, false, &iq16, &s) == -ENOENT);
	CHECK(now_s() - t0 < 1.0);
	CHECK(s == NULL);

	CHECK(fixture_write_file(dir, "emu-rx_buf0.bin", ramp, sizeof(ramp)) == 0);
	s = open_rx(rx_ch, 2, 4, 1, &iq16);
	if (s) {
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		CHECK(ramp_mismatches(dst, 0, 4) == 0);
		iio_conv_stream_close(&s);
	}
}

static void test_close_twice(void)
{
	struct iio_conv_stream *s = open_rx(rx_ch, 2, 16, 2, &iq16);
	int16_t dst[2 * 16];
	unsigned int i;

	CHECK(s != NULL);
	iio_conv_stream_close(&s);
	CHECK(s == NULL);
	iio_conv_stream_close(&s);
	CHECK(s == NULL);
	iio_conv_stream_close(NULL);

	/* nothing of a closed stream is left behind: every new one starts at
	 * the beginning of the file */
	for (i = 0; i < 50; i++) {
		s = open_rx(rx_ch, 2, 16, 2, &iq16);
		if (!s)
			break;
		memset(dst, 0x55, sizeof(dst));
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
		CHECK(ramp_mismatches(dst, 0, 16) == 0);
		iio_conv_stream_close(&s);
		CHECK(s == NULL);
	}
}

static void test_wrong_direction(void)
{
	struct iio_conv_stream *s = open_rx(rx_ch, 2, 1024, 2, &iq16);
	int16_t buf[2 * 1024] = { 0 };

	if (s) {
		CHECK(iio_conv_stream_write(s, buf, 4) == -EBADF);
		CHECK(iio_conv_stream_write_typed(s, buf, 4, IIO_CONV_I16, true) == -EBADF);
		CHECK(iio_conv_stream_read(s, NULL) == -EINVAL);
		CHECK(iio_conv_stream_set_timeout(s, -2) == -EINVAL);
		CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_CONTEXT) == 0);
		iio_conv_stream_close(&s);
	}

	/* an output buffer gives a transmit stream */
	CHECK(iio_conv_stream_open(tx_buf, tx_ch, 2, 4, 3, false, &iq16, &s) == 0);
	if (s) {
		CHECK(iio_conv_stream_is_output(s));
		CHECK(iio_conv_stream_get_step(s) == 4);
		CHECK(iio_conv_stream_get_nb_blocks(s) == 3);
		CHECK(iio_conv_stream_read(s, buf) == -EBADF);
		iio_conv_stream_close(&s);
	}

	CHECK(iio_conv_stream_read(NULL, buf) == -EINVAL);
	CHECK(iio_conv_stream_write(NULL, buf, 4) == -EINVAL);
	CHECK(iio_conv_stream_write_typed(NULL, buf, 4, IIO_CONV_I16, true) == -EINVAL);
	CHECK(iio_conv_stream_flush(NULL) == -EINVAL);
	CHECK(iio_conv_stream_set_timeout(NULL, 0) == -EINVAL);
}

int main(int argc, char **argv)
{
	static const char *const rx_ids[5] = { "voltage0_i", "voltage0_q", "voltage1_i",
		"voltage1_q", "voltage2" };
	char uri[PATH_MAX + 32];
	unsigned int i;
	int ret;

	if (argc < 2)
		return 2;
	ret = fixture_setup(argv[1], dir);
	CHECK(ret == 0);
	if (ret)
		TEST_MAIN_RETURN();
	fixture_uri(dir, "emu", uri, sizeof(uri));
	ctx = iio_create_context(NULL, uri);
	CHECK(!iio_err(ctx));
	if (iio_err(ctx)) {
		fixture_cleanup(dir);
		TEST_MAIN_RETURN();
	}

	rx = iio_context_find_device(ctx, "emu-rx");
	tx = iio_context_find_device(ctx, "emu-tx");
	multi = iio_context_find_device(ctx, "emu-multi");
	zoo = iio_context_find_device(ctx, "iio:device4");
	CHECK(rx && tx && multi && zoo);
	if (rx && tx && multi && zoo) {
		rx_buf = iio_device_get_buffer(rx, 0);
		tx_buf = iio_device_get_buffer(tx, 0);
		for (i = 0; i < 5; i++)
			rx_ch[i] = iio_device_find_channel(rx, rx_ids[i], false);
		for (i = 0; i < 4; i++)
			tx_ch[i] = iio_device_find_channel(tx, rx_ids[i], true);
		CHECK(rx_buf && tx_buf && rx_ch[0] && rx_ch[1] && rx_ch[2] && rx_ch[3] && rx_ch[4]);
		CHECK(tx_ch[0] && tx_ch[1] && tx_ch[2] && tx_ch[3]);

		test_rx_frames();
		test_rx_one_block();
		test_rx_raw();
		test_rx_float();
		test_rx_scale_iio();
		test_rx_pairing();
		test_rx_subset();
		test_rx_flush();
		test_rx_wait_policies();
		test_open_errors();
		test_rx_missing_file();
		test_close_twice();
		test_wrong_direction();
	}

	iio_context_destroy(ctx);
	fixture_cleanup(dir);
	TEST_MAIN_RETURN();
}
