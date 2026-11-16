/* SPDX-License-Identifier: MIT */
/*
 * Stream layer: a buffer stream, its blocks and a converter behind one
 * object, so that a frame goes between the hardware and a host array in one
 * call. Uses only public libiio functions and iio_conv_*.
 *
 * Rules kept throughout:
 * - The layout comes from the mask the buffer stream really has, never from
 *   the one built here: hardware may enable more channels than asked for.
 * - A block is dequeued only while the buffer is started. Otherwise emu
 *   stalls for 5 s and libiio's generic block path waits for ever.
 * - The context timeout is never changed. A time limit of the stream's own
 *   is kept by polling.
 */
#include <iio/iio-conv-stream.h>

#include <errno.h>
#include <iio/iio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define NB_TYPES (IIO_CONV_F64 + 1)
#define POLL_INTERVAL_NS 1000000 /* 1 ms */

struct iio_conv_stream {
	struct iio_buffer_stream *bs;
	struct iio_conv *conv;
	struct iio_block **blocks;
	bool *queued;       /* the block is with the backend, as far as we know */
	unsigned int nb_blocks;
	/* Blocks are queued in rotation, so this is the one queued longest
	 * ago: the next to come back. */
	unsigned int curr;
	unsigned int nb_chans;
	size_t samples, step;
	enum iio_conv_type type;
	bool complex_iq, output, cyclic;
	bool started;       /* the buffer is enabled */
	int timeout_ms;     /* wait policy */
};

static double elapsed_ms(const struct timespec *since)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - since->tv_sec) * 1e3 +
		(double)(now.tv_nsec - since->tv_nsec) * 1e-6;
}

static bool not_ready(int ret)
{
	return ret == -ETIMEDOUT || ret == -EBUSY || ret == -EAGAIN;
}

/* Wait for block i to come back under a wait policy. Returns 1 when it is
 * ours again, 0 when it did not come back in time, or a negative errno. */
static int wait_block(struct iio_conv_stream *s, unsigned int i, int timeout_ms)
{
	const struct timespec pause = { 0, POLL_INTERVAL_NS };
	struct timespec start;
	int ret;

	if (timeout_ms < 0) {
		/* libiio blocks, for as long as the context timeout allows */
		ret = iio_block_dequeue(s->blocks[i], false);
	} else {
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (;;) {
			ret = iio_block_dequeue(s->blocks[i], true);
			if (!not_ready(ret) || elapsed_ms(&start) >= (double)timeout_ms)
				break;
			nanosleep(&pause, NULL);
		}
	}

	if (not_ready(ret))
		return 0;
	if (ret < 0)
		return ret;
	s->queued[i] = false;
	return 1;
}

/* One turn of the receive rotation: take the oldest block back, convert it
 * into dst (NULL drops the frame) and queue it again. */
static int rx_turn(struct iio_conv_stream *s, void *dst, int timeout_ms)
{
	unsigned int b = s->curr;
	bool frame = false;
	ssize_t n;
	int ret;

	/* A block that is not queued is one whose last enqueue failed: it
	 * holds nothing new and only needs queuing again. */
	if (s->queued[b]) {
		ret = wait_block(s, b, timeout_ms);
		if (ret == -EPERM) {
			/* libiio does not have the block. Its generic block
			 * path forgets a block whose dequeue timed out; the
			 * block then holds no frame. */
			s->queued[b] = false;
		} else if (ret <= 0) {
			return ret;
		} else {
			frame = true;
		}
	}

	if (frame && dst) {
		n = iio_conv_read(s->conv, s->blocks[b], dst, s->samples);
		if (n < 0)
			return (int)n;
	}

	/* Back to the backend for the next capture. It is now the block
	 * queued last, so the next one is the oldest. */
	ret = iio_block_enqueue(s->blocks[b], 0, false);
	if (ret < 0)
		return ret;
	s->queued[b] = true;
	s->curr = (b + 1) % s->nb_blocks;

	return frame ? IIO_CONV_STREAM_FRAME : IIO_CONV_STREAM_NO_FRAME;
}

int iio_conv_stream_open(struct iio_buffer *buf,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		size_t samples_per_frame, unsigned int nb_blocks, bool cyclic,
		const struct iio_conv_spec *spec, struct iio_conv_stream **stream)
{
	struct iio_channels_mask *mask;
	struct iio_conv_stream *s;
	unsigned int i, k, nb_scan;
	int ret;

	if (!buf || !chans || !nb_chans || !samples_per_frame || !nb_blocks || !spec || !stream)
		return -EINVAL;
	if ((unsigned int)spec->type >= NB_TYPES)
		return -EINVAL;
	if (cyclic && !iio_buffer_is_output(buf))
		return -EINVAL;

	/* every channel a scan element of this buffer, none twice */
	nb_scan = iio_buffer_get_scan_elements_count(buf);
	for (i = 0; i < nb_chans; i++) {
		if (!chans[i])
			return -EINVAL;
		for (k = 0; k < nb_scan; k++)
			if (iio_buffer_get_scan_element(buf, k) == chans[i])
				break;
		if (k == nb_scan)
			return -EINVAL;
		for (k = 0; k < i; k++)
			if (chans[k] == chans[i])
				return -EINVAL;
	}

	s = calloc(1, sizeof(*s));
	if (!s)
		return -ENOMEM;
	s->output = iio_buffer_is_output(buf);
	s->cyclic = cyclic;
	s->nb_blocks = cyclic ? 1 : nb_blocks;
	s->nb_chans = nb_chans;
	s->samples = samples_per_frame;
	s->type = spec->type;
	s->complex_iq = spec->complex_iq;
	s->timeout_ms = IIO_CONV_STREAM_TIMEOUT_CONTEXT;

	/* Enable exactly the channels asked for. The buffer stream keeps its
	 * own copy of the mask, which the backend may extend. */
	mask = iio_create_channels_mask(iio_device_get_channels_count(iio_buffer_get_device(buf)));
	if (!mask) {
		ret = -ENOMEM;
		goto err_close;
	}
	for (i = 0; i < nb_chans; i++)
		iio_channel_enable(chans[i], mask);
	s->bs = iio_buffer_open(buf, mask);
	iio_channels_mask_destroy(mask);
	ret = iio_err(s->bs);
	if (ret) {
		s->bs = NULL;
		goto err_close;
	}

	ret = iio_conv_create(iio_buffer_stream_get_channels_mask(s->bs), chans, nb_chans,
			spec, &s->conv);
	if (ret)
		goto err_close;
	s->step = iio_conv_get_step(s->conv);
	if (s->samples > SIZE_MAX / s->step) {
		ret = -EINVAL;
		goto err_close;
	}

	s->blocks = calloc(s->nb_blocks, sizeof(*s->blocks));
	s->queued = calloc(s->nb_blocks, sizeof(*s->queued));
	if (!s->blocks || !s->queued) {
		ret = -ENOMEM;
		goto err_close;
	}
	for (i = 0; i < s->nb_blocks; i++) {
		struct iio_block *block = iio_buffer_stream_create_block(s->bs,
				s->samples * s->step);

		ret = iio_err(block);
		if (ret)
			goto err_close;
		s->blocks[i] = block;
	}

	if (!s->output) {
		/* Capture starts now: every block goes to the backend, then
		 * the buffer is enabled. */
		for (i = 0; i < s->nb_blocks; i++) {
			ret = iio_block_enqueue(s->blocks[i], 0, false);
			if (ret < 0)
				goto err_close;
			s->queued[i] = true;
		}
		ret = iio_buffer_stream_start(s->bs);
		if (ret < 0)
			goto err_close;
		s->started = true;
	}

	*stream = s;
	return 0;

err_close:
	/* nothing is dequeued on the way out, see iio_conv_stream_close() */
	iio_conv_stream_close(&s);
	return ret;
}

void iio_conv_stream_close(struct iio_conv_stream **stream)
{
	struct iio_conv_stream *s;
	unsigned int i;

	if (!stream || !*stream)
		return;
	s = *stream;
	*stream = NULL;

	if (s->bs) {
		/* From the cancel on, no block is dequeued: a cancelled
		 * stream fails every dequeue, a stopped one never completes
		 * it. Destroying a block that is still queued is fine. */
		iio_buffer_stream_cancel(s->bs);
		if (s->started)
			iio_buffer_stream_stop(s->bs);
		for (i = 0; s->blocks && i < s->nb_blocks; i++)
			if (s->blocks[i])
				iio_block_destroy(s->blocks[i]);
		iio_buffer_close(s->bs);
	}

	iio_conv_destroy(s->conv);
	free(s->blocks);
	free(s->queued);
	free(s);
}

int iio_conv_stream_set_timeout(struct iio_conv_stream *stream, int timeout_ms)
{
	if (!stream || timeout_ms < IIO_CONV_STREAM_TIMEOUT_CONTEXT)
		return -EINVAL;
	stream->timeout_ms = timeout_ms;
	return 0;
}

int iio_conv_stream_read(struct iio_conv_stream *stream, void *dst)
{
	if (!stream)
		return -EINVAL;
	if (stream->output)
		return -EBADF;
	if (!dst)
		return -EINVAL;

	return rx_turn(stream, dst, stream->timeout_ms);
}

int iio_conv_stream_write(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples)
{
	if (!stream)
		return -EINVAL;
	return iio_conv_stream_write_typed(stream, src, nb_samples, stream->type,
			stream->complex_iq);
}

int iio_conv_stream_write_typed(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples, enum iio_conv_type type, bool src_complex)
{
	(void)src; (void)nb_samples; (void)type; (void)src_complex;

	if (!stream)
		return -EINVAL;
	if (!stream->output)
		return -EBADF;
	return -ENOSYS; /* transmit is not implemented yet */
}

int iio_conv_stream_flush(struct iio_conv_stream *stream)
{
	unsigned int i;
	int ret;

	if (!stream)
		return -EINVAL;
	if (stream->output)
		return -ENOSYS; /* transmit is not implemented yet */

	/* whatever is captured goes straight back, oldest first */
	for (i = 0; i < stream->nb_blocks; i++) {
		ret = rx_turn(stream, NULL, IIO_CONV_STREAM_TIMEOUT_NONBLOCK);
		if (ret < 0)
			return ret;
		if (ret == IIO_CONV_STREAM_NO_FRAME)
			break;
	}
	return IIO_CONV_STREAM_FRAME;
}

size_t iio_conv_stream_get_samples(const struct iio_conv_stream *stream)
{
	return stream->samples;
}

unsigned int iio_conv_stream_get_columns(const struct iio_conv_stream *stream)
{
	return stream->complex_iq ? stream->nb_chans / 2 : stream->nb_chans;
}

size_t iio_conv_stream_get_step(const struct iio_conv_stream *stream)
{
	return stream->step;
}

unsigned int iio_conv_stream_get_nb_blocks(const struct iio_conv_stream *stream)
{
	return stream->nb_blocks;
}

enum iio_conv_type iio_conv_stream_get_type(const struct iio_conv_stream *stream)
{
	return stream->type;
}

bool iio_conv_stream_is_complex(const struct iio_conv_stream *stream)
{
	return stream->complex_iq;
}

bool iio_conv_stream_is_output(const struct iio_conv_stream *stream)
{
	return stream->output;
}
