/* SPDX-License-Identifier: MIT */
/*
 * Streams against a scripted backend, for what the emulation backend cannot
 * show: blocks that are not ready, timeouts, hard errors, a failing start,
 * hardware that enables more channels than asked for, and the exact order of
 * backend calls.
 */
#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-conv-stream.h>
#include "stream_util.h"

#define N 16

struct iio_buffer_pdata {
	bool output;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	uint8_t *data;
	size_t size;
	bool queued;
};

static struct fake_state {
	char log[2048];
	int start_err;  /* returned when the buffer is enabled */
	int enq_err;    /* returned by enqueue */
	int deq_err;    /* hard error from dequeue */
	int not_ready;  /* dequeue attempts that find the block not ready; < 0: never ready */
	bool widen;     /* enable one more channel at open, as coupled hardware does */
	bool cancelled;
	unsigned int creates, frees, opens, closes, polls;
	uint16_t word;  /* next word of receive data */
	uint8_t tx[256];
	size_t tx_len;
	bool tx_cyclic;
	unsigned int tx_count;
} fk;

/* a run of identical entries is logged once */
static void lg(const char *what)
{
	size_t len = strlen(fk.log), wl = strlen(what);

	if (len > wl && !strncmp(fk.log + len - wl - 1, what, wl) &&
			(len == wl + 1 || fk.log[len - wl - 2] == ' ') && !strcmp(what, "poll"))
		return;
	if (len + wl + 2 < sizeof(fk.log)) {
		strcat(fk.log, what);
		strcat(fk.log, " ");
	}
}

static struct iio_buffer_pdata *f_open(const struct iio_device *dev, unsigned int idx,
		struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *p = calloc(1, sizeof(*p));

	(void)idx;
	p->output = !strcmp(iio_device_get_name(dev), "conv-tx");
	if (fk.widen)
		iio_channel_enable(iio_device_find_channel(dev, "voltage2", false), mask);
	fk.opens++;
	lg("open");
	return p;
}

static void f_close(struct iio_buffer_pdata *p)
{
	fk.closes++;
	lg("close");
	free(p);
}

static int f_enable(struct iio_buffer_pdata *p, size_t nb_samples, bool enable, bool cyclic)
{
	(void)p; (void)nb_samples; (void)cyclic;
	lg(enable ? "start" : "stop");
	return enable ? fk.start_err : 0;
}

static void f_cancel(struct iio_buffer_pdata *p)
{
	(void)p;
	fk.cancelled = true;
	lg("cancel");
}

static struct iio_block_pdata *f_create(struct iio_buffer_pdata *p, size_t size, void **data)
{
	struct iio_block_pdata *b = calloc(1, sizeof(*b));

	b->buf = p;
	b->size = size;
	b->data = calloc(1, size);
	*data = b->data;
	fk.creates++;
	lg("create");
	return b;
}

static void f_free(struct iio_block_pdata *b)
{
	fk.frees++;
	lg("free");
	free(b->data);
	free(b);
}

static int f_enqueue(struct iio_block_pdata *b, size_t bytes_used, bool cyclic)
{
	lg("enq");
	if (fk.enq_err)
		return fk.enq_err;
	if (b->queued)
		return -EPERM;
	b->queued = true;
	if (b->buf->output) {
		fk.tx_len = bytes_used;
		memcpy(fk.tx, b->data, bytes_used < sizeof(fk.tx) ? bytes_used : sizeof(fk.tx));
		fk.tx_cyclic = cyclic;
		fk.tx_count++;
	}
	return 0;
}

static int f_dequeue(struct iio_block_pdata *b, bool nonblock)
{
	size_t i;

	if (nonblock)
		fk.polls++;
	lg(nonblock ? "poll" : "deq");
	if (!b->queued)
		return -EPERM;
	if (fk.cancelled)
		return -EBADF;
	if (fk.deq_err)
		return fk.deq_err;
	if (fk.not_ready) {
		if (fk.not_ready > 0)
			fk.not_ready--;
		return nonblock ? -EBUSY : -ETIMEDOUT;
	}
	if (!b->buf->output) {
		for (i = 0; i + 2 <= b->size; i += 2) {
			uint16_t w = fk.word++;

			memcpy(b->data + i, &w, sizeof(w));
		}
	}
	b->queued = false;
	return 0;
}

static const struct iio_backend_ops fake_ops = {
	.open_buffer = f_open, .close_buffer = f_close, .enable_buffer = f_enable,
	.cancel_buffer = f_cancel, .create_block = f_create, .free_block = f_free,
	.enqueue_block = f_enqueue, .dequeue_block = f_dequeue,
};

static const struct iio_backend fake_backend = {
	.api_version = IIO_BACKEND_API_V1, .name = "fake", .uri_prefix = "fake:",
	.ops = &fake_ops, .default_timeout_ms = 100,
};

static struct iio_buffer *rx_buf, *tx_buf;
static const struct iio_channel *rx_iq[2], *tx_iq[2];

static void reset(void)
{
	memset(&fk, 0, sizeof(fk));
}

static struct iio_conv_stream *open_stream(bool output, unsigned int nb_blocks, int timeout_ms,
		bool cyclic, int *err)
{
	struct iio_conv_stream_params p = { .nb_samples = N, .nb_blocks = nb_blocks,
		.cyclic = cyclic, .timeout_ms = timeout_ms,
		.spec = { .type = IIO_CONV_I16, .complex_iq = true } };
	struct iio_conv_stream *s = NULL;
	int ret = iio_conv_stream_open(output ? tx_buf : rx_buf, output ? tx_iq : rx_iq, 2, &p, &s);

	if (err)
		*err = ret;
	else
		CHECK(ret == 0);
	return s;
}

static void test_rx_call_order_and_values(void)
{
	struct iio_conv_stream *s;
	int16_t out[2 * N];
	int k;

	reset();
	s = open_stream(false, 2, -1, false, NULL);
	if (!s)
		return;
	CHECK(iio_conv_stream_read(s, out) == N);
	for (k = 0; k < N; k++)
		CHECK(out[2 * k] == 2 * k && out[2 * k + 1] == 2 * k + 1);
	CHECK(iio_conv_stream_read(s, out) == N);
	CHECK(out[0] == 2 * N && out[2 * N - 1] == 4 * N - 1);
	iio_conv_stream_close(s);
	/* open: both blocks queued, then started. Each read: take a block,
	 * give it back. Close: unblock, collect the queued blocks, free them,
	 * stop, close (libiio itself cancels once more inside close). */
	CHECK(!strcmp(fk.log, "open create create enq enq start deq enq deq enq "
				"cancel deq deq free free stop cancel close "));
	CHECK(fk.creates == 2 && fk.frees == 2 && fk.opens == 1 && fk.closes == 1);
}

static void test_rx_no_frame(void)
{
	struct iio_conv_stream *s;
	int16_t out[2 * N], untouched[2 * N];
	double t0;
	int i;

	/* timeout 0: one look, no waiting */
	reset();
	s = open_stream(false, 2, 0, false, NULL);
	if (!s)
		return;
	memset(out, 0x55, sizeof(out));
	memset(untouched, 0x55, sizeof(untouched));
	fk.not_ready = 3;
	for (i = 0; i < 3; i++)
		CHECK(iio_conv_stream_read(s, out) == 0);
	CHECK(memcmp(out, untouched, sizeof(out)) == 0);
	CHECK(fk.polls == 3);
	CHECK(iio_conv_stream_read(s, out) == N);
	CHECK(out[0] == 0 && out[1] == 1);

	/* a positive timeout waits that long, by polling, then gives up */
	fk.not_ready = -1;
	iio_conv_stream_set_timeout(s, 40);
	t0 = now_ms();
	CHECK(iio_conv_stream_read(s, out) == 0);
	CHECK(now_ms() - t0 >= 40.0 && now_ms() - t0 < 1000.0);
	CHECK(strstr(fk.log, "deq enq poll enq poll ") == NULL); /* nothing was re-queued */

	/* ... and returns as soon as the block is ready */
	fk.not_ready = 5;
	iio_conv_stream_set_timeout(s, 5000);
	t0 = now_ms();
	CHECK(iio_conv_stream_read(s, out) == N);
	CHECK(now_ms() - t0 < 1000.0);
	iio_conv_stream_close(s);

	/* negative timeout: libiio blocks, and its timeout means "no frame" */
	reset();
	s = open_stream(false, 1, -1, false, NULL);
	if (!s)
		return;
	fk.not_ready = 1;
	CHECK(iio_conv_stream_read(s, out) == 0);
	CHECK(fk.polls == 0);
	CHECK(iio_conv_stream_read(s, out) == N);
	iio_conv_stream_close(s);
}

static void test_rx_errors(void)
{
	struct iio_conv_stream *s;
	int16_t out[2 * N];
	int err = 0;

	/* a hard error from the backend is passed on */
	reset();
	s = open_stream(false, 2, -1, false, NULL);
	if (s) {
		fk.deq_err = -EIO;
		CHECK(iio_conv_stream_read(s, out) == -EIO);
		fk.deq_err = 0;
		iio_conv_stream_close(s);
		CHECK(fk.creates == fk.frees && fk.closes == 1);
	}

	/* the buffer does not start: nothing is dequeued, everything is released */
	reset();
	fk.start_err = -EACCES;
	s = open_stream(false, 2, -1, false, &err);
	CHECK(err == -EACCES && s == NULL);
	CHECK(!strcmp(fk.log, "open create create enq enq start free free cancel close "));
	CHECK(fk.creates == 2 && fk.frees == 2 && fk.closes == 1);

	/* a block cannot be queued */
	reset();
	fk.enq_err = -ENOSPC;
	s = open_stream(false, 2, -1, false, &err);
	CHECK(err == -ENOSPC && s == NULL);
	CHECK(strstr(fk.log, "deq") == NULL && strstr(fk.log, "start") == NULL);
	CHECK(fk.creates == fk.frees && fk.closes == 1);
}

/* The hardware turns on a third channel. The stream must size its blocks and
 * find I and Q from the mask the buffer really has. */
static void test_rx_coupled_channels(void)
{
	struct iio_conv_stream *s;
	int16_t out[2 * N];
	int k;

	reset();
	fk.widen = true;
	s = open_stream(false, 1, -1, false, NULL);
	if (!s)
		return;
	CHECK(iio_conv_stream_get_step(s) == 6);
	CHECK(iio_conv_stream_get_host_step(s) == 4);
	CHECK(iio_conv_stream_read(s, out) == N);
	for (k = 0; k < N; k++)
		CHECK(out[2 * k] == 3 * k && out[2 * k + 1] == 3 * k + 1);
	iio_conv_stream_close(s);
}

static void tx_frame(int16_t *host, int f)
{
	int k;

	for (k = 0; k < 2 * N; k++)
		host[k] = (int16_t)(100 * f + k);
}

static void test_tx_call_order(void)
{
	struct iio_conv_stream *s;
	int16_t host[2 * N];
	int f;

	reset();
	s = open_stream(true, 2, -1, false, NULL);
	if (!s)
		return;
	CHECK(!strcmp(fk.log, "open create create ")); /* nothing sent, nothing started */
	for (f = 0; f < 3; f++) {
		tx_frame(host, f);
		CHECK(iio_conv_stream_write(s, host) == N);
		/* the frame reached the backend inside the call */
		CHECK(fk.tx_count == (unsigned int)f + 1);
		CHECK(fk.tx_len == N * 4 && !fk.tx_cyclic);
		CHECK(memcmp(fk.tx, host, sizeof(host)) == 0);
	}
	iio_conv_stream_close(s);
	/* first write starts the buffer; the third needs block 0 back first;
	 * close waits for the two frames still out, without cancelling */
	CHECK(!strcmp(fk.log, "open create create enq start enq deq enq "
				"deq deq free free stop cancel close "));
}

static void test_tx_no_free_block(void)
{
	struct iio_conv_stream *s;
	int16_t a[2 * N], b[2 * N];

	reset();
	s = open_stream(true, 1, 0, false, NULL);
	if (!s)
		return;
	tx_frame(a, 1);
	tx_frame(b, 2);
	CHECK(iio_conv_stream_write(s, a) == N);
	fk.not_ready = -1; /* the hardware still holds the only block */
	CHECK(iio_conv_stream_write(s, b) == 0);
	CHECK(fk.tx_count == 1 && memcmp(fk.tx, a, sizeof(a)) == 0);
	fk.not_ready = 0;
	CHECK(iio_conv_stream_write(s, b) == N);
	CHECK(fk.tx_count == 2 && memcmp(fk.tx, b, sizeof(b)) == 0);
	iio_conv_stream_close(s);
}

static void test_tx_cyclic_and_start_failure(void)
{
	struct iio_conv_stream *s;
	int16_t host[2 * N];

	reset();
	s = open_stream(true, 2, -1, true, NULL);
	if (s) {
		tx_frame(host, 3);
		CHECK(iio_conv_stream_write(s, host) == N);
		CHECK(fk.tx_cyclic && fk.tx_count == 1);
		CHECK(iio_conv_stream_write(s, host) == -EBUSY);
		CHECK(fk.tx_count == 1);
		iio_conv_stream_close(s);
		/* a cyclic frame never completes: cancel, then collect it */
		CHECK(!strcmp(fk.log, "open create create enq start cancel deq free free stop cancel close "));
	}

	reset();
	fk.start_err = -EIO;
	s = open_stream(true, 2, -1, false, NULL);
	if (s) {
		tx_frame(host, 4);
		CHECK(iio_conv_stream_write(s, host) == -EIO);
		iio_conv_stream_close(s);
		CHECK(!strcmp(fk.log, "open create create enq start free free cancel close "));
	}
}

int main(int argc, char **argv)
{
	struct iio_context_params prm = { 0 };
	const struct iio_device *rx, *tx;
	struct iio_context *ctx;
	char uri[1024];

	if (argc < 2)
		return 2;
	snprintf(uri, sizeof(uri), "xml:%s", argv[1]);
	ctx = iio_create_context_from_xml(&prm, uri, &fake_backend, "scripted", NULL, NULL, 0);
	CHECK(!iio_err(ctx));
	if (iio_err(ctx))
		TEST_MAIN_RETURN();
	rx = iio_context_find_device(ctx, "conv-rx");
	tx = iio_context_find_device(ctx, "conv-tx");
	rx_buf = iio_device_get_buffer(rx, 0);
	tx_buf = iio_device_get_buffer(tx, 0);
	rx_iq[0] = iio_device_find_channel(rx, "voltage0_i", false);
	rx_iq[1] = iio_device_find_channel(rx, "voltage0_q", false);
	tx_iq[0] = iio_device_find_channel(tx, "voltage0", true);
	tx_iq[1] = iio_device_find_channel(tx, "voltage1", true);
	CHECK(rx_buf && tx_buf && rx_iq[0] && rx_iq[1] && tx_iq[0] && tx_iq[1]);

	test_rx_call_order_and_values();
	test_rx_no_frame();
	test_rx_errors();
	test_rx_coupled_channels();
	test_tx_call_order();
	test_tx_no_free_block();
	test_tx_cyclic_and_start_failure();

	iio_context_destroy(ctx);
	TEST_MAIN_RETURN();
}
