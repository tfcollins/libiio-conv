/* SPDX-License-Identifier: MIT */
/*
 * The stream layer against the fake backend (fake_backend.h), for what the
 * emulation backend cannot show: blocks that are not ready, the wait
 * policies, hardware that enables more channels than asked for, failures, and
 * the exact order of backend calls.
 */
#include <errno.h>
#include <iio/iio-conv-stream.h>
#include <time.h>
#include "fake_backend.h"
#include "fixture_util.h"
#include "test_util.h"

static struct iio_context *ctx;
static struct iio_buffer *rx_buf, *tx_buf;
static const struct iio_channel *rx_iq[2], *tx_iq[2];

static const struct iio_conv_spec iq16 = {
	.type = IIO_CONV_I16, .layout = IIO_CONV_COLUMNS, .scale = IIO_CONV_SCALE_NONE,
	.complex_iq = true,
};

static const int16_t A[8] = { 100, 1, -200, 2, 300, 3, -400, 4 };
static const int16_t B[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };

/* --- failures the table of fake_backend.h cannot produce: a second context
 * on the same backend with three ops wrapped. An injected dequeue failure is
 * logged as '!'. --- */

static struct {
	int enable_err;     /* returned when the buffer is enabled */
	int enqueue_err;    /* returned by enqueue; the block is not queued */
	int dequeue_err;    /* returned by dequeue of a queued block */
	bool dequeue_drops; /* ... which is then no longer queued, as on libiio's
			     * generic block path after a transfer timed out */
	unsigned int blocking, polls; /* dequeue calls of either kind */
} inject;

static struct iio_backend_ops inject_ops;
static struct iio_backend inject_backend;
static struct iio_context *ictx;
static struct iio_buffer *irx_buf, *itx_buf;
static const struct iio_channel *irx_iq[2], *itx_iq[2];

static int inject_enable_buffer(struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable,
		bool cyclic)
{
	int ret = fake_enable_buffer(pdata, nb_samples, enable, cyclic);

	return enable && inject.enable_err ? inject.enable_err : ret;
}

static int inject_enqueue_block(struct iio_block_pdata *block, size_t bytes_used, bool cyclic)
{
	return inject.enqueue_err ? inject.enqueue_err : fake_enqueue_block(block, bytes_used, cyclic);
}

static int inject_dequeue_block(struct iio_block_pdata *block, bool nonblock)
{
	if (nonblock)
		inject.polls++;
	else
		inject.blocking++;
	if (!inject.dequeue_err || !block->queued)
		return fake_dequeue_block(block, nonblock);
	if (inject.dequeue_drops)
		block->queued = false;
	fake_log('!');
	return inject.dequeue_err;
}

static void reset(void)
{
	fake_reset();
	memset(&inject, 0, sizeof(inject));
}

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* 4 samples per frame */
static struct iio_conv_stream *open_rx(unsigned int nb_blocks)
{
	struct iio_conv_stream *s = NULL;

	CHECK(iio_conv_stream_open(rx_buf, rx_iq, 2, 4, nb_blocks, false, &iq16, &s) == 0);
	return s;
}

static struct iio_conv_stream *open_tx(unsigned int nb_blocks, bool cyclic)
{
	struct iio_conv_stream *s = NULL;

	CHECK(iio_conv_stream_open(tx_buf, tx_iq, 2, 4, nb_blocks, cyclic, &iq16, &s) == 0);
	return s;
}

/* the log continues with exactly `tail` after its first `from` letters */
static bool log_grew_by(size_t from, const char *tail)
{
	return strlen(fake.log) >= from && !strcmp(fake.log + from, tail);
}

static void test_order(void)
{
	struct iio_conv_stream *s;
	int16_t dst[8];
	int k;

	reset();
	s = open_rx(2);
	if (!s)
		return;
	/* both blocks queued, then the buffer enabled */
	CHECK(!strcmp(fake.log, "OBBEES"));
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	/* the block is taken back and queued again in the same call */
	CHECK(!strcmp(fake.log, "OBBEESDE"));
	for (k = 0; k < 8; k++)
		CHECK(dst[k] == k);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 256 && dst[7] == 263);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 512 && dst[7] == 519);

	/* cancel, stop, blocks, close (which cancels once more itself): no
	 * block is collected on the way out */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(s == NULL);
	CHECK(!strcmp(fake.log, "XsFFXC"));
}

static void test_no_frame(void)
{
	struct iio_conv_stream *s;
	int16_t dst[8], same[8];
	int timeout_before = iio_context_get_params(ctx)->timeout_ms;
	size_t len;
	double t0, dt;
	int k;

	reset();
	s = open_rx(2);
	if (!s)
		return;
	for (k = 0; k < 3; k++)
		CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	memset(dst, 0x55, sizeof(dst));
	memset(same, 0x55, sizeof(same));

	/* default policy: one blocking dequeue; its timeout is "no frame" */
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(memcmp(dst, same, sizeof(dst)) == 0);
	CHECK(log_grew_by(len, "d"));

	/* never wait: one look */
	CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_NONBLOCK) == 0);
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(log_grew_by(len, "d"));

	/* a time: polls until it has passed, whatever the context timeout is */
	CHECK(iio_conv_stream_set_timeout(s, 30) == 0);
	len = strlen(fake.log);
	t0 = now_s();
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	dt = now_s() - t0;
	CHECK(dt >= 0.030 && dt < 1.0);
	/* nothing but looks in vain */
	CHECK(strlen(fake.log) > len && strspn(fake.log + len, "d") == strlen(fake.log + len));
	CHECK(memcmp(dst, same, sizeof(dst)) == 0);
	CHECK(iio_context_get_params(ctx)->timeout_ms == timeout_before);

	CHECK(iio_conv_stream_set_timeout(s, -2) == -EINVAL);

	/* the call can be repeated: the next frame is not lost */
	fake.rx_frames = 4;
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 768);
	iio_conv_stream_close(&s);
}

/* Which kind of dequeue each policy makes: the fake answers both at once, so
 * the log alone cannot tell them apart. */
static void test_policy_calls(void)
{
	struct iio_conv_stream *s = NULL;
	int16_t dst[8];

	reset();
	fake.rx_frames = 0;
	CHECK(iio_conv_stream_open(irx_buf, irx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	/* default: the context policy, one blocking dequeue */
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(inject.blocking == 1 && inject.polls == 0);
	CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_NONBLOCK) == 0);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(inject.blocking == 1 && inject.polls == 1);
	CHECK(iio_conv_stream_set_timeout(s, 5) == 0);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(inject.blocking == 1 && inject.polls >= 2);
	CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_CONTEXT) == 0);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(inject.blocking == 2);
	/* a flush never waits */
	inject.polls = 0;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(inject.blocking == 2 && inject.polls == 1);
	iio_conv_stream_close(&s);
}

static void test_late_frame(void)
{
	struct iio_conv_stream *s;
	int16_t dst[8];
	size_t len;

	reset();
	fake.rx_frames = 100;
	s = open_rx(2);
	if (!s)
		return;
	fake.busy = 3;
	CHECK(iio_conv_stream_set_timeout(s, 500) == 0);
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	/* three looks in vain, then the frame: the read returns as soon as
	 * the block is ready, not when the time is up */
	CHECK(log_grew_by(len, "dddDE"));
	CHECK(dst[0] == 0 && dst[7] == 7);
	iio_conv_stream_close(&s);
}

/* The hardware turns on a third channel. Block size and the place of I and Q
 * must come from the mask the buffer really has. */
static void test_coupled(void)
{
	static const int16_t expect[8] = { 0, 1, 3, 4, 6, 7, 9, 10 };
	struct iio_conv_stream *s;
	int16_t dst[8];

	reset();
	fake.couple = "voltage1_i";
	s = open_rx(1);
	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 6);
	CHECK(iio_conv_stream_get_columns(s) == 1);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(memcmp(dst, expect, sizeof(expect)) == 0);
	iio_conv_stream_close(&s);
}

static void test_rx_flush(void)
{
	struct iio_conv_stream *s;
	int16_t dst[8];
	size_t len;

	reset();
	fake.rx_frames = 4;
	s = open_rx(2);
	if (!s)
		return;
	/* one frame per block is dropped ... */
	len = strlen(fake.log);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(log_grew_by(len, "DEDE"));
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 512);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 768);

	/* ... and with nothing captured a flush is one look, whatever the
	 * wait policy says */
	CHECK(iio_conv_stream_set_timeout(s, 500) == 0);
	len = strlen(fake.log);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(log_grew_by(len, "d"));
	CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_CONTEXT) == 0);
	len = strlen(fake.log);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(log_grew_by(len, "d"));
	iio_conv_stream_close(&s);
}

/* An open that fails releases what it created, without collecting any block */
static void test_open_failures(void)
{
	struct iio_conv_spec spec = iq16;
	struct iio_conv_stream *s = NULL;

	/* the buffer does not start */
	reset();
	inject.enable_err = -EACCES;
	CHECK(iio_conv_stream_open(irx_buf, irx_iq, 2, 4, 2, false, &iq16, &s) == -EACCES);
	CHECK(s == NULL);
	CHECK(!strcmp(fake.log, "OBBEESXFFXC"));

	/* a block cannot be queued */
	reset();
	inject.enqueue_err = -ENOSPC;
	CHECK(iio_conv_stream_open(irx_buf, irx_iq, 2, 4, 2, false, &iq16, &s) == -ENOSPC);
	CHECK(s == NULL);
	CHECK(!strcmp(fake.log, "OBBXFFXC"));

	/* the spec is refused once the real mask is known: the buffer was
	 * opened and is closed again, no block was made */
	reset();
	spec.scale = IIO_CONV_SCALE_FULLSCALE;
	CHECK(iio_conv_stream_open(rx_buf, rx_iq, 2, 4, 2, false, &spec, &s) == -EINVAL);
	CHECK(s == NULL);
	CHECK(!strcmp(fake.log, "OXXC"));

	/* refused before anything is touched */
	reset();
	CHECK(iio_conv_stream_open(rx_buf, rx_iq, 2, 4, 2, true, &iq16, &s) == -EINVAL);
	CHECK(iio_conv_stream_open(rx_buf, rx_iq, 2, 0, 2, false, &iq16, &s) == -EINVAL);
	CHECK(s == NULL);
	CHECK(!strcmp(fake.log, ""));
}

static void test_rx_hard_error(void)
{
	struct iio_conv_stream *s = NULL;
	int16_t dst[8];

	reset();
	CHECK(iio_conv_stream_open(irx_buf, irx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	/* passed on as it is, by every wait policy; the block stays queued */
	inject.dequeue_err = -EIO;
	CHECK(iio_conv_stream_read(s, dst) == -EIO);
	CHECK(iio_conv_stream_set_timeout(s, 200) == 0);
	CHECK(iio_conv_stream_read(s, dst) == -EIO);
	CHECK(iio_conv_stream_flush(s) == -EIO);
	CHECK(!strcmp(fake.log, "OBBEES!!!"));
	inject.dequeue_err = 0;
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 0 && dst[7] == 7);
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XsFFXC"));
}

/* libiio's generic block path forgets a block whose dequeue timed out: the
 * next dequeue reports -EPERM. The read must queue the block again and say
 * "no frame", not fail for ever. */
static void test_rx_lost_block(void)
{
	struct iio_conv_stream *s = NULL;
	int16_t dst[8], same[8];
	size_t len;

	reset();
	fake.rx_frames = 100;
	CHECK(iio_conv_stream_open(irx_buf, irx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	memset(dst, 0x55, sizeof(dst));
	memset(same, 0x55, sizeof(same));

	inject.dequeue_err = -ETIMEDOUT;
	inject.dequeue_drops = true;
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(log_grew_by(len, "!"));
	inject.dequeue_err = 0;

	/* -EPERM from the backend (not logged), then the block is queued again */
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(log_grew_by(len, "E"));
	CHECK(memcmp(dst, same, sizeof(dst)) == 0);

	/* and the frames flow again */
	len = strlen(fake.log);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(log_grew_by(len, "DE"));
	CHECK(dst[0] == 0);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 256);
	CHECK(iio_conv_stream_read(s, dst) == IIO_CONV_STREAM_FRAME);
	CHECK(dst[0] == 512);
	iio_conv_stream_close(&s);
}

/* --- transmit --- */

static void test_tx_order(void)
{
	struct iio_conv_stream *s;
	unsigned char sent[16];

	reset();
	s = open_tx(2, false);
	if (!s)
		return;
	/* nothing queued, nothing started */
	CHECK(!strcmp(fake.log, "OBB"));
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	/* the frame is with the backend when the call returns; the first
	 * write starts the buffer */
	CHECK(!strcmp(fake.log, "OBBES"));
	CHECK(fake.tx_bytes == 16 && !fake.tx_cyclic);
	CHECK(memcmp(fake.tx_data, A, 16) == 0);
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBBESE"));
	CHECK(memcmp(fake.tx_data, B, 16) == 0);
	/* both blocks in flight: each further write takes the oldest back */
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBBESEDE"));
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBBESEDEDE"));

	/* the hardware stalls: no block comes back, nothing is queued */
	memcpy(sent, fake.tx_data, 16);
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(!strcmp(fake.log, "OBBESEDEDEd"));
	CHECK(memcmp(fake.tx_data, sent, 16) == 0);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(!strcmp(fake.log, "OBBESEDEDEdd"));

	/* the close waits for the oldest frame, gives up, and tears down
	 * without collecting anything else */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "dXsFFXC"));
}

static void test_tx_drain(void)
{
	struct iio_conv_stream *s;

	/* frames still in flight go out before the stream is cancelled */
	reset();
	fake.tx_frames = 100;
	s = open_tx(2, false);
	if (!s)
		return;
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(iio_conv_stream_write(s, A, 2) == IIO_CONV_STREAM_FRAME);
	CHECK(fake.tx_bytes == 8); /* a partial frame is a shorter block */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "DDXsFFXC"));

	/* a flush has done it already */
	reset();
	fake.tx_frames = 100;
	s = open_tx(2, false);
	if (!s)
		return;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME); /* before the first write */
	CHECK(!strcmp(fake.log, "OBB"));
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	fake_clear_log();
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "D"));
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "D"));
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XsFFXC"));

	/* never written: never started, so never stopped */
	reset();
	s = open_tx(2, false);
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XFFXC"));
}

static void test_tx_cyclic(void)
{
	struct iio_conv_stream *s;

	reset();
	s = open_tx(4, true);
	if (!s)
		return;
	CHECK(iio_conv_stream_get_nb_blocks(s) == 1);
	CHECK(!strcmp(fake.log, "OB"));
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBeS"));
	CHECK(fake.tx_cyclic && fake.tx_bytes == 16);
	CHECK(iio_conv_stream_write(s, B, 4) == -EBUSY);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBeS"));
	CHECK(memcmp(fake.tx_data, A, 16) == 0);
	/* a cyclic block is never dequeued */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XsFXC"));
}

/* The hardware turns on a third channel: its bytes are sent as zero */
static void test_tx_coupled(void)
{
	static const unsigned char sent[24] = {
		0x64, 0x00, 0x01, 0x00, 0x00, 0x00, 0x38, 0xff, 0x02, 0x00, 0x00, 0x00,
		0x2c, 0x01, 0x03, 0x00, 0x00, 0x00, 0x70, 0xfe, 0x04, 0x00, 0x00, 0x00,
	};
	static const int16_t R[4] = { 7, -8, 9, -10 };
	static const unsigned char sent_r[24] = {
		0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0x00, 0x00, 0x00, 0x00,
		0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x00, 0x00, 0x00,
	};
	struct iio_conv_stream *s;

	reset();
	fake.tx_frames = 100;
	fake.couple = "voltage1_i";
	s = open_tx(1, false);
	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 6);
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(fake.tx_bytes == 24);
	CHECK(memcmp(fake.tx_data, sent, 24) == 0);
	/* a real array: the Q bytes of the frame before are cleared too */
	CHECK(iio_conv_stream_write_typed(s, R, 4, IIO_CONV_I16, false) == IIO_CONV_STREAM_FRAME);
	CHECK(memcmp(fake.tx_data, sent_r, 24) == 0);
	iio_conv_stream_close(&s);
}

static void test_tx_timeout(void)
{
	struct iio_conv_stream *s;
	double t0, dt;

	reset();
	fake.tx_frames = 0;
	s = open_tx(1, false);
	if (!s)
		return;
	CHECK(iio_conv_stream_set_timeout(s, 30) == 0);
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBES")); /* a free block: nothing to wait for */
	t0 = now_s();
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_NO_FRAME);
	dt = now_s() - t0;
	CHECK(dt >= 0.030 && dt < 1.0);
	CHECK(memcmp(fake.tx_data, A, 16) == 0);
	t0 = now_s();
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME);
	dt = now_s() - t0;
	CHECK(dt >= 0.030 && dt < 1.0);

	/* the block comes back: the same call now succeeds */
	fake.tx_frames = 1;
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(memcmp(fake.tx_data, B, 16) == 0);
	iio_conv_stream_close(&s);
}

/* The buffer does not start. The frame stays queued, no block is dequeued
 * while the buffer is not running, and the next call tries the start again. */
static void test_tx_start_failure(void)
{
	struct iio_conv_stream *s = NULL;

	reset();
	fake.tx_frames = 100;
	inject.enable_err = -EIO;
	CHECK(iio_conv_stream_open(itx_buf, itx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_write(s, A, 4) == -EIO);
	CHECK(!strcmp(fake.log, "OBBES"));
	CHECK(iio_conv_stream_write(s, B, 4) == -EIO);
	CHECK(iio_conv_stream_flush(s) == -EIO);
	CHECK(!strcmp(fake.log, "OBBESSS"));
	CHECK(memcmp(fake.tx_data, A, 16) == 0);
	/* not started: nothing to wait for, nothing to stop */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XFFXC"));

	reset();
	fake.tx_frames = 100;
	inject.enable_err = -EIO;
	CHECK(iio_conv_stream_open(itx_buf, itx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_write(s, A, 4) == -EIO);
	inject.enable_err = 0;
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBBESSE"));
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "DDXsFFXC")); /* A, then B */

	/* cyclic: once started, the stream has its frame */
	reset();
	inject.enable_err = -EIO;
	CHECK(iio_conv_stream_open(itx_buf, itx_iq, 2, 4, 2, true, &iq16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_write(s, A, 4) == -EIO);
	CHECK(iio_conv_stream_write(s, A, 4) == -EIO);
	inject.enable_err = 0;
	CHECK(iio_conv_stream_write(s, A, 4) == -EBUSY);
	CHECK(!strcmp(fake.log, "OBeSSS"));
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XsFXC"));
}

static void test_tx_errors(void)
{
	struct iio_conv_stream *s = NULL;

	/* a block cannot be queued: it stays ours and the next write uses it */
	reset();
	fake.tx_frames = 100;
	CHECK(iio_conv_stream_open(itx_buf, itx_iq, 2, 4, 2, false, &iq16, &s) == 0);
	if (!s)
		return;
	inject.enqueue_err = -ENOSPC;
	CHECK(iio_conv_stream_write(s, A, 4) == -ENOSPC);
	CHECK(!strcmp(fake.log, "OBB"));
	inject.enqueue_err = 0;
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBBES"));
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);

	/* a hard error while waiting for a block is passed on as it is */
	inject.dequeue_err = -EIO;
	CHECK(iio_conv_stream_write(s, B, 4) == -EIO);
	CHECK(iio_conv_stream_flush(s) == -EIO);
	CHECK(!strcmp(fake.log, "OBBESE!!"));
	CHECK(memcmp(fake.tx_data, A, 16) == 0);
	/* and stops the wait of the close at the first frame */
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "!XsFFXC"));
}

/* As test_rx_lost_block: a block libiio no longer has is free to fill */
static void test_tx_lost_block(void)
{
	struct iio_conv_stream *s = NULL;

	reset();
	fake.tx_frames = 100;
	CHECK(iio_conv_stream_open(itx_buf, itx_iq, 2, 4, 1, false, &iq16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	inject.dequeue_err = -ETIMEDOUT;
	inject.dequeue_drops = true;
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(!strcmp(fake.log, "OBES!"));
	inject.dequeue_err = 0;
	CHECK(iio_conv_stream_write(s, B, 4) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBES!E"));
	CHECK(memcmp(fake.tx_data, B, 16) == 0);

	/* the same for a flush and for the wait of the close */
	inject.dequeue_err = -ETIMEDOUT;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME);
	inject.dequeue_err = 0;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	CHECK(!strcmp(fake.log, "OBES!E!"));
	CHECK(iio_conv_stream_write(s, A, 4) == IIO_CONV_STREAM_FRAME);
	inject.dequeue_err = -ETIMEDOUT;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME);
	inject.dequeue_err = 0;
	fake_clear_log();
	iio_conv_stream_close(&s);
	CHECK(!strcmp(fake.log, "XsFXC"));
}

static void find(const struct iio_context *c, const char *name, bool output,
		struct iio_buffer **buf, const struct iio_channel **iq)
{
	const struct iio_device *dev = iio_context_find_device(c, name);

	CHECK(dev != NULL);
	if (!dev)
		return;
	*buf = iio_device_get_buffer(dev, 0);
	iq[0] = iio_device_find_channel(dev, "voltage0_i", output);
	iq[1] = iio_device_find_channel(dev, "voltage0_q", output);
	CHECK(*buf && iq[0] && iq[1]);
}

int main(int argc, char **argv)
{
	static const struct iio_context_params params;
	char dir[PATH_MAX], xml[PATH_MAX], uri[PATH_MAX + 8];
	int ret;

	if (argc < 2)
		return 2;
	ret = fixture_setup(argv[1], dir);
	CHECK(ret == 0);
	if (ret)
		TEST_MAIN_RETURN();
	CHECK(fixture_path(dir, "iio_fixture.xml", xml) == 0);

	ctx = fake_create_context(xml);
	inject_ops = fake_ops;
	inject_ops.enable_buffer = inject_enable_buffer;
	inject_ops.enqueue_block = inject_enqueue_block;
	inject_ops.dequeue_block = inject_dequeue_block;
	inject_backend = fake_backend;
	inject_backend.ops = &inject_ops;
	snprintf(uri, sizeof(uri), "xml:%s", xml);
	ictx = iio_create_context_from_xml(&params, uri, &inject_backend, "(fake, failing)",
			NULL, NULL, 0);
	CHECK(!iio_err(ctx) && !iio_err(ictx));

	if (!iio_err(ctx) && !iio_err(ictx)) {
		find(ctx, "emu-rx", false, &rx_buf, rx_iq);
		find(ctx, "emu-tx", true, &tx_buf, tx_iq);
		find(ictx, "emu-rx", false, &irx_buf, irx_iq);
		find(ictx, "emu-tx", true, &itx_buf, itx_iq);
	}
	if (!test_failures) {
		test_order();
		test_no_frame();
		test_policy_calls();
		test_late_frame();
		test_coupled();
		test_rx_flush();
		test_open_failures();
		test_rx_hard_error();
		test_rx_lost_block();

		test_tx_order();
		test_tx_drain();
		test_tx_cyclic();
		test_tx_coupled();
		test_tx_timeout();
		test_tx_start_failure();
		test_tx_errors();
		test_tx_lost_block();
	}

	if (!iio_err(ictx))
		iio_context_destroy(ictx);
	if (!iio_err(ctx))
		iio_context_destroy(ctx);
	fixture_cleanup(dir);
	TEST_MAIN_RETURN();
}
