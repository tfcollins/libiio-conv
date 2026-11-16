/* SPDX-License-Identifier: MIT */
/*
 * The stream layer on libiio's generic block path. The backend below has no
 * block operations, only readbuf and writebuf, so libiio itself supplies the
 * blocks, a worker thread that makes one transfer per queued block, in order,
 * and the bookkeeping of what is queued - as it does for a local device
 * without the high-speed interface and for an old IIOD. On this path a
 * dequeue gives the result of the transfer once and then forgets the block,
 * and blocks really come back in the order they were queued.
 */
#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio-conv-stream.h>
#include <iio/iio.h>
#include <stdatomic.h>
#include "fixture_util.h"
#include "test_util.h"

/* Set by the test while no transfer runs, used on libiio's worker thread */
static struct {
	atomic_uint fail_every;  /* every n-th transfer fails; 0 = never */
	atomic_int fail_err;     /* ... with this errno */
	atomic_uint transfers;
	atomic_uint rx_seq;      /* receive frames delivered */
	atomic_uint tx_sent;     /* transmit frames taken */
	atomic_uint tx_disorder; /* ... that did not follow the one before */
	atomic_uint tx_next;     /* first word expected in the next one */
	atomic_uint tx_bytes;    /* size of the last one */
	atomic_uint io_stopped;  /* transfers while the buffer was not enabled */
	atomic_bool enabled;
} gen;

struct iio_buffer_pdata {
	bool output;
};

static struct iio_buffer_pdata *gen_open_buffer(const struct iio_device *dev, unsigned int idx,
		struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = calloc(1, sizeof(*pdata));

	(void)mask;
	if (!pdata)
		return iio_ptr(-ENOMEM);
	pdata->output = iio_buffer_is_output(iio_device_get_buffer(dev, idx));
	return pdata;
}

static void gen_close_buffer(struct iio_buffer_pdata *pdata)
{
	free(pdata);
}

static int gen_enable_buffer(struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable,
		bool cyclic)
{
	(void)pdata; (void)nb_samples; (void)cyclic;
	atomic_store(&gen.enabled, enable);
	return 0;
}

static void gen_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	(void)pdata;
}

/* Receive: 16-bit words seq + 7919 * i. Transmit: the first word of each
 * frame must count up from 0. */
static ssize_t gen_transfer(struct iio_buffer_pdata *pdata, unsigned char *data, size_t len)
{
	unsigned int n = atomic_fetch_add(&gen.transfers, 1) + 1;
	unsigned int every = atomic_load(&gen.fail_every), seq;
	size_t i;

	if (!atomic_load(&gen.enabled))
		atomic_fetch_add(&gen.io_stopped, 1);
	if (pdata->output) {
		/* a frame that is not sent still had its turn */
		seq = atomic_fetch_add(&gen.tx_next, 1);
		if ((unsigned int)(data[0] | data[1] << 8) != (seq & 0x7fff))
			atomic_fetch_add(&gen.tx_disorder, 1);
	}
	if (every && n % every == 0)
		return atomic_load(&gen.fail_err);
	if (pdata->output) {
		atomic_fetch_add(&gen.tx_sent, 1);
		atomic_store(&gen.tx_bytes, (unsigned int)len);
	} else {
		seq = atomic_fetch_add(&gen.rx_seq, 1);
		for (i = 0; 2 * i + 1 < len; i++) {
			unsigned int w = seq + 7919u * (unsigned int)i;

			data[2 * i] = (unsigned char)w;
			data[2 * i + 1] = (unsigned char)(w >> 8);
		}
	}
	return (ssize_t)len;
}

static ssize_t gen_readbuf(struct iio_buffer_pdata *pdata, void *dst, size_t len)
{
	return gen_transfer(pdata, dst, len);
}

static ssize_t gen_writebuf(struct iio_buffer_pdata *pdata, const void *src, size_t len)
{
	return gen_transfer(pdata, (unsigned char *)(uintptr_t)src, len);
}

static const struct iio_backend_ops gen_ops = {
	.open_buffer = gen_open_buffer,
	.close_buffer = gen_close_buffer,
	.enable_buffer = gen_enable_buffer,
	.cancel_buffer = gen_cancel_buffer,
	.readbuf = gen_readbuf,
	.writebuf = gen_writebuf,
};

static const struct iio_backend gen_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "generic",
	.uri_prefix = "generic:",
	.ops = &gen_ops,
	.default_timeout_ms = 1000,
};

static struct iio_buffer *rx_buf, *tx_buf;
static const struct iio_channel *rx_iq[2], *tx_iq[2];

static const struct iio_conv_spec raw16 = {
	.type = IIO_CONV_U16, .layout = IIO_CONV_INTERLEAVED, .complex_iq = true, .raw = true,
};

#define FRAMES 300u
#define MAX_CALLS 50000000ul /* the "never wait" policy spins while a transfer runs */

static void gen_reset(unsigned int fail_every, int fail_err)
{
	memset(&gen, 0, sizeof(gen));
	atomic_store(&gen.fail_every, fail_every);
	atomic_store(&gen.fail_err, fail_err);
}

/* Every frame once, whole and in the order captured, whatever goes wrong in
 * between */
static void rx_run(unsigned int nb_blocks, int policy, unsigned int fail_every, int fail_err)
{
	struct iio_conv_stream *s = NULL;
	unsigned long calls = 0, bad = 0;
	unsigned int frames = 0, errors = 0, k;
	uint16_t dst[16];
	int ret;

	gen_reset(fail_every, fail_err);
	CHECK(iio_conv_stream_open(rx_buf, rx_iq, 2, 8, nb_blocks, false, &raw16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_set_timeout(s, policy) == 0);
	while (frames < FRAMES && calls++ < MAX_CALLS) {
		ret = iio_conv_stream_read(s, dst);
		if (ret == IIO_CONV_STREAM_FRAME) {
			for (k = 0; k < 16; k++)
				bad += dst[k] != (uint16_t)(frames + 7919u * k);
			frames++;
		} else if (ret < 0) {
			/* only the error of a transfer, once per transfer */
			CHECK(ret == fail_err && fail_err != -ETIMEDOUT);
			errors++;
		}
	}
	CHECK(frames == FRAMES);
	CHECK(bad == 0);
	CHECK(errors <= atomic_load(&gen.transfers) - atomic_load(&gen.rx_seq));
	if (!fail_every)
		CHECK(errors == 0);
	iio_conv_stream_close(&s);
	CHECK(atomic_load(&gen.io_stopped) == 0);
}

/* Every frame written is sent, in the order written, or the caller is told:
 * a frame whose transfer fails is lost, libiio does not try again. */
static void tx_run(unsigned int nb_blocks, int policy, unsigned int fail_every, int fail_err)
{
	struct iio_conv_stream *s = NULL;
	unsigned long calls = 0;
	unsigned int frames = 0, errors = 0, lost;
	uint16_t src[16] = { 0 };
	size_t n = 8;
	int ret;

	gen_reset(fail_every, fail_err);
	CHECK(iio_conv_stream_open(tx_buf, tx_iq, 2, 8, nb_blocks, false, &raw16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME); /* nothing to wait for */
	CHECK(iio_conv_stream_set_timeout(s, policy) == 0);
	while (frames < FRAMES && calls++ < MAX_CALLS) {
		src[0] = (uint16_t)frames;
		n = 1 + frames % 8;
		ret = iio_conv_stream_write(s, src, n);
		if (ret == IIO_CONV_STREAM_FRAME) {
			frames++;
		} else if (ret < 0) {
			CHECK(ret == (fail_err == -ETIMEDOUT ? -EIO : fail_err));
			errors++;
		}
	}
	CHECK(frames == FRAMES);

	/* wait for the rest; a flush tells of the frames lost in one go */
	do {
		ret = iio_conv_stream_flush(s);
		if (ret < 0) {
			CHECK(ret == (fail_err == -ETIMEDOUT ? -EIO : fail_err));
			errors++;
		}
	} while (ret != IIO_CONV_STREAM_FRAME && calls++ < MAX_CALLS);
	CHECK(ret == IIO_CONV_STREAM_FRAME);

	lost = FRAMES - atomic_load(&gen.tx_sent);
	CHECK(atomic_load(&gen.transfers) == FRAMES);
	CHECK(atomic_load(&gen.tx_disorder) == 0);
	if (!fail_every) {
		CHECK(lost == 0 && errors == 0);
		CHECK(atomic_load(&gen.tx_bytes) == 4 * n); /* a partial frame is a shorter transfer */
	} else {
		/* no loss goes unreported, and nothing is reported twice */
		CHECK(lost == FRAMES / fail_every);
		CHECK(errors >= 1 && errors <= lost);
		if (nb_blocks == 1)
			CHECK(errors == lost);
	}
	iio_conv_stream_close(&s);
	CHECK(atomic_load(&gen.transfers) == FRAMES);
	CHECK(atomic_load(&gen.io_stopped) == 0);
}

/* A frame written just before the close goes out, under each policy that
 * waits */
static void tx_close_drains(unsigned int nb_blocks, int policy)
{
	struct iio_conv_stream *s = NULL;
	uint16_t src[16] = { 0 };
	unsigned int k;

	gen_reset(0, 0);
	CHECK(iio_conv_stream_open(tx_buf, tx_iq, 2, 8, nb_blocks, false, &raw16, &s) == 0);
	if (!s)
		return;
	CHECK(iio_conv_stream_set_timeout(s, policy) == 0);
	for (k = 0; k < nb_blocks; k++) {
		src[0] = (uint16_t)k;
		CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_FRAME);
	}
	iio_conv_stream_close(&s);
	CHECK(atomic_load(&gen.tx_sent) == nb_blocks);
	CHECK(atomic_load(&gen.tx_disorder) == 0);
}

/* What a stalled device looks like on this path: every transfer times out */
static void tx_all_lost(void)
{
	struct iio_conv_stream *s = NULL;
	uint16_t src[16] = { 0 };

	gen_reset(1, -ETIMEDOUT);
	CHECK(iio_conv_stream_open(tx_buf, tx_iq, 2, 8, 1, false, &raw16, &s) == 0);
	if (!s)
		return;
	/* a time: "not ready" and "lost" arrive within one wait */
	CHECK(iio_conv_stream_set_timeout(s, 2000) == 0);
	CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_FRAME);
	src[0] = 1;
	CHECK(iio_conv_stream_write(s, src, 8) == -EIO);
	CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_FRAME);
	CHECK(iio_conv_stream_flush(s) == -EIO);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);

	/* the context policy: the timeout of the transfer is "no frame" first */
	CHECK(iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_CONTEXT) == 0);
	src[0] = 2;
	CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_FRAME);
	src[0] = 3;
	CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(iio_conv_stream_write(s, src, 8) == -EIO);
	CHECK(iio_conv_stream_write(s, src, 8) == IIO_CONV_STREAM_FRAME);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME);
	CHECK(iio_conv_stream_flush(s) == -EIO);
	CHECK(iio_conv_stream_flush(s) == IIO_CONV_STREAM_FRAME);
	iio_conv_stream_close(&s);
	CHECK(atomic_load(&gen.tx_sent) == 0 && atomic_load(&gen.transfers) == 4);
	CHECK(atomic_load(&gen.tx_disorder) == 0);
}

static void find(const struct iio_context *ctx, const char *name, bool output,
		struct iio_buffer **buf, const struct iio_channel **iq)
{
	const struct iio_device *dev = iio_context_find_device(ctx, name);

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
	static const int policies[3] = {
		IIO_CONV_STREAM_TIMEOUT_CONTEXT, IIO_CONV_STREAM_TIMEOUT_NONBLOCK, 20,
	};
	static const struct iio_context_params params;
	char dir[PATH_MAX], xml[PATH_MAX], uri[PATH_MAX + 8];
	struct iio_context *ctx;
	unsigned int nb, p;
	int ret;

	if (argc < 2)
		return 2;
	ret = fixture_setup(argv[1], dir);
	CHECK(ret == 0);
	if (ret)
		TEST_MAIN_RETURN();
	CHECK(fixture_path(dir, "iio_fixture.xml", xml) == 0);
	snprintf(uri, sizeof(uri), "xml:%s", xml);
	ctx = iio_create_context_from_xml(&params, uri, &gen_backend, "(generic block path)",
			NULL, NULL, 0);
	CHECK(!iio_err(ctx));
	if (!iio_err(ctx)) {
		find(ctx, "emu-rx", false, &rx_buf, rx_iq);
		find(ctx, "emu-tx", true, &tx_buf, tx_iq);
	}

	if (!test_failures) {
		for (nb = 1; nb <= 4; nb++) {
			for (p = 0; p < 3; p++) {
				rx_run(nb, policies[p], 0, 0);
				rx_run(nb, policies[p], 5, -ETIMEDOUT);
				rx_run(nb, policies[p], 7, -EIO);
				tx_run(nb, policies[p], 0, 0);
				tx_run(nb, policies[p], 5, -ETIMEDOUT);
				tx_run(nb, policies[p], 7, -EIO);
			}
			tx_close_drains(nb, IIO_CONV_STREAM_TIMEOUT_CONTEXT);
			tx_close_drains(nb, 2000);
		}
		tx_all_lost();
	}

	if (!iio_err(ctx))
		iio_context_destroy(ctx);
	fixture_cleanup(dir);
	TEST_MAIN_RETURN();
}
