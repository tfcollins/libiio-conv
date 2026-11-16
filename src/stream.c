/* SPDX-License-Identifier: MIT */
/*
 * Streams: a buffer stream, its blocks and a converter behind one object, so
 * that a frame goes between the hardware and a host array in one call.
 *
 * Built on explicit blocks rather than libiio's iio_stream helper, which can
 * neither transmit cyclically nor hand a frame over in the call that
 * received it.
 */
#include <iio/iio-conv-stream.h>

#include <errno.h>
#include <iio/iio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_NB_BLOCKS 2
#define POLL_INTERVAL_NS 200000 /* 0.2 ms */

struct iio_conv_stream {
	struct iio_buffer_stream *bs;
	struct iio_conv *conv;
	struct iio_block **blocks;
	bool *queued;           /* the block is with the backend */
	unsigned int nb_blocks;
	unsigned int next;      /* block the next read or write uses */
	size_t nb_samples, step;
	bool is_output, cyclic, cyclic_sent;
	bool started;           /* the buffer is enabled */
	int timeout_ms;
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
	return ret == -EBUSY || ret == -EAGAIN || ret == -ETIMEDOUT;
}

/* Take a queued block back from the backend, waiting as the stream's timeout
 * says. Returns 0 when the block is ours, 1 when it did not come back in
 * time, or a negative errno. */
static int take_block(struct iio_conv_stream *s, unsigned int b)
{
	const struct timespec pause = { 0, POLL_INTERVAL_NS };
	struct timespec start;
	int ret;

	if (s->timeout_ms < 0) {
		/* libiio blocks; its own timeout means "nothing yet" */
		ret = iio_block_dequeue(s->blocks[b], false);
		if (ret == -ETIMEDOUT)
			return 1;
	} else {
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (;;) {
			ret = iio_block_dequeue(s->blocks[b], true);
			if (!not_ready(ret))
				break;
			if (elapsed_ms(&start) >= (double)s->timeout_ms)
				return 1;
			nanosleep(&pause, NULL);
		}
	}

	if (ret < 0)
		return ret;
	s->queued[b] = false;
	return 0;
}

int iio_conv_stream_open(struct iio_buffer *buf, const struct iio_channel *const *chans,
		unsigned int nb_chans, const struct iio_conv_stream_params *params,
		struct iio_conv_stream **stream)
{
	const struct iio_device *dev;
	struct iio_channels_mask *mask;
	struct iio_conv_stream *s;
	unsigned int i;
	int ret;

	if (!buf || !chans || !nb_chans || !params || !stream || !params->nb_samples)
		return -EINVAL;

	dev = iio_buffer_get_device(buf);
	for (i = 0; i < nb_chans; i++) {
		if (!chans[i] || iio_channel_get_device(chans[i]) != dev ||
				!iio_channel_is_scan_element(chans[i]))
			return -EINVAL;
	}

	s = calloc(1, sizeof(*s));
	if (!s)
		return -ENOMEM;
	s->nb_samples = params->nb_samples;
	s->nb_blocks = params->nb_blocks ? params->nb_blocks : DEFAULT_NB_BLOCKS;
	s->timeout_ms = params->timeout_ms;
	s->is_output = iio_buffer_is_output(buf);
	s->cyclic = s->is_output && params->cyclic;

	/* Enable exactly the channels asked for. The buffer keeps its own
	 * copy of the mask, and the hardware may turn on more: from here on
	 * only the buffer's mask counts. */
	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!mask) {
		free(s);
		return -ENOMEM;
	}
	for (i = 0; i < nb_chans; i++)
		iio_channel_enable(chans[i], mask);
	s->bs = iio_buffer_open(buf, mask);
	iio_channels_mask_destroy(mask);
	ret = iio_err(s->bs);
	if (ret) {
		free(s);
		return ret;
	}

	ret = iio_conv_create(iio_buffer_stream_get_channels_mask(s->bs), chans, nb_chans,
			&params->spec, &s->conv);
	if (ret)
		goto err_close;
	s->step = iio_conv_get_step(s->conv);
	if (s->nb_samples > SIZE_MAX / s->step) {
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
				s->nb_samples * s->step);

		ret = iio_err(block);
		if (ret)
			goto err_close;
		s->blocks[i] = block;
	}

	if (!s->is_output) {
		/* Capture starts now: every block goes to the backend, then
		 * the buffer is enabled. A transmit buffer is enabled by the
		 * first write instead. */
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
	iio_conv_stream_close(s);
	return ret;
}

ssize_t iio_conv_stream_read(struct iio_conv_stream *s, void *dst)
{
	unsigned int b;
	ssize_t n;
	int ret;

	if (!s || !dst)
		return -EINVAL;
	if (s->is_output)
		return -EPERM;

	b = s->next;
	ret = take_block(s, b);
	if (ret)
		return ret > 0 ? 0 : ret;

	n = iio_conv_read(s->conv, s->blocks[b], dst, s->nb_samples);
	if (n < 0)
		return n;

	/* give the block back for the next capture */
	ret = iio_block_enqueue(s->blocks[b], 0, false);
	if (ret < 0)
		return ret;
	s->queued[b] = true;
	s->next = (b + 1) % s->nb_blocks;

	return n;
}

ssize_t iio_conv_stream_write(struct iio_conv_stream *s, const void *src)
{
	unsigned int b;
	ssize_t n;
	int ret;

	if (!s || !src)
		return -EINVAL;
	if (!s->is_output)
		return -EPERM;
	if (s->cyclic_sent)
		return -EBUSY;

	b = s->next;
	if (s->queued[b]) {
		/* every block is out: wait for the oldest frame to finish */
		ret = take_block(s, b);
		if (ret)
			return ret > 0 ? 0 : ret;
	}

	n = iio_conv_write(s->conv, s->blocks[b], src, s->nb_samples);
	if (n < 0)
		return n;

	ret = iio_block_enqueue(s->blocks[b], s->nb_samples * s->step, s->cyclic);
	if (ret < 0)
		return ret;
	s->queued[b] = true;

	if (!s->started) {
		ret = iio_buffer_stream_start(s->bs);
		if (ret < 0)
			return ret;
		s->started = true;
	}

	s->cyclic_sent = s->cyclic;
	s->next = (b + 1) % s->nb_blocks;

	return n;
}

int iio_conv_stream_flush(struct iio_conv_stream *s)
{
	unsigned int i, b;
	int ret;

	if (!s)
		return -EINVAL;
	if (s->is_output)
		return -EPERM;

	/* whatever is already captured goes straight back, oldest first */
	for (i = 0; i < s->nb_blocks; i++) {
		b = s->next;
		ret = iio_block_dequeue(s->blocks[b], true);
		if (not_ready(ret))
			break;
		if (ret < 0)
			return ret;
		s->queued[b] = false;

		ret = iio_block_enqueue(s->blocks[b], 0, false);
		if (ret < 0)
			return ret;
		s->queued[b] = true;
		s->next = (b + 1) % s->nb_blocks;
	}

	return (int)i;
}

void iio_conv_stream_close(struct iio_conv_stream *s)
{
	unsigned int i, b;

	if (!s)
		return;

	if (s->bs && s->blocks && s->queued && s->started) {
		/* Frames written to a non-cyclic transmit stream are allowed
		 * to go out. Anything else is abandoned: unblock the backend
		 * first so that collecting the blocks cannot hang.
		 *
		 * Blocks are only ever collected from a started buffer: some
		 * backends wait a long time for a block that can never
		 * complete. */
		if (!s->is_output || s->cyclic)
			iio_buffer_stream_cancel(s->bs);
		for (i = 0; i < s->nb_blocks; i++) {
			b = (s->next + i) % s->nb_blocks; /* oldest first */
			if (s->queued[b])
				iio_block_dequeue(s->blocks[b], false);
		}
	}

	for (i = 0; s->blocks && i < s->nb_blocks; i++)
		if (s->blocks[i])
			iio_block_destroy(s->blocks[i]);
	if (s->bs) {
		if (s->started)
			iio_buffer_stream_stop(s->bs);
		iio_buffer_close(s->bs);
	}

	iio_conv_destroy(s->conv);
	free(s->blocks);
	free(s->queued);
	free(s);
}

void iio_conv_stream_set_timeout(struct iio_conv_stream *s, int timeout_ms)
{
	if (s)
		s->timeout_ms = timeout_ms;
}

bool iio_conv_stream_is_output(const struct iio_conv_stream *s)
{
	return s->is_output;
}

size_t iio_conv_stream_get_nb_samples(const struct iio_conv_stream *s)
{
	return s->nb_samples;
}

unsigned int iio_conv_stream_get_nb_blocks(const struct iio_conv_stream *s)
{
	return s->nb_blocks;
}

size_t iio_conv_stream_get_step(const struct iio_conv_stream *s)
{
	return s->step;
}

size_t iio_conv_stream_get_host_step(const struct iio_conv_stream *s)
{
	return iio_conv_get_host_step(s->conv);
}
