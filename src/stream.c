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
#include <string.h>
#include <time.h>

#define NB_TYPES (IIO_CONV_F64 + 1)
#define POLL_INTERVAL_NS 1000000 /* 1 ms */

struct iio_conv_stream {
	struct iio_buffer_stream *bs;
	const struct iio_channels_mask *mask; /* the one in use; belongs to bs */
	struct iio_block **blocks;
	bool *queued;       /* the block is with the backend, as far as we know */
	bool *failed;       /* ... and its last dequeue failed with an errno the caller got */
	unsigned int nb_blocks;
	/* Blocks are used in rotation. Receive: the block queued longest ago,
	 * the next to come back. Transmit: the block the next write fills,
	 * which is the oldest in flight once every block is. */
	unsigned int curr;

	/* What the converters are made from. chans, gain and offset hold
	 * nb_chans entries as given and then, with complex_iq, the nb_chans / 2
	 * entries of the I channels, for real arrays written to I/Q pairs.
	 * spec.user_gain and spec.user_offset point at these copies. */
	const struct iio_channel **chans;
	double *gain, *offset;
	unsigned int nb_chans;
	struct iio_conv_spec spec;
	/* [host type][host array is complex]; transmit makes them on first
	 * use. conv is the one of the spec itself. */
	struct iio_conv *convs[NB_TYPES][2];
	struct iio_conv *conv;

	size_t samples, step;
	bool output, cyclic;
	bool cyclic_sent;   /* the one frame of a cyclic stream is out */
	bool started;       /* the buffer is enabled */
	bool start_failed;  /* transmit: a frame is queued but the buffer is not started */
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
 * ours again, 0 (which is IIO_CONV_STREAM_NO_FRAME) when it did not come back
 * in time, or a negative errno. */
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
	if (ret < 0) {
		if (ret != -EPERM)
			s->failed[i] = true;
		return ret;
	}
	s->queued[i] = false;
	s->failed[i] = false;
	return 1;
}

/* The converter for host arrays of this type and complexity */
static int get_conv(struct iio_conv_stream *s, enum iio_conv_type type, bool src_complex,
		struct iio_conv **conv)
{
	struct iio_conv **slot = &s->convs[type][src_complex];
	const struct iio_channel *const *chans = s->chans;
	struct iio_conv_spec spec = s->spec;
	unsigned int nb_chans = s->nb_chans;
	int ret;

	if (!*slot) {
		spec.type = type;
		if (spec.complex_iq && !src_complex) {
			/* a real array on I/Q pairs: one column per pair, for
			 * its I channel. The caller clears the Q channels. */
			spec.complex_iq = false;
			chans += nb_chans;
			if (spec.user_gain)
				spec.user_gain += nb_chans;
			if (spec.user_offset)
				spec.user_offset += nb_chans;
			nb_chans /= 2;
		}
		ret = iio_conv_create(s->mask, chans, nb_chans, &spec, slot);
		if (ret)
			return ret;
	}
	*conv = *slot;
	return 0;
}

/* Transmit: enable the buffer, which is done once the first frame is queued.
 * A start that fails is tried again by the next write or flush, before any
 * block is waited for. */
static int tx_start(struct iio_conv_stream *s)
{
	int ret = iio_buffer_stream_start(s->bs);

	s->started = ret >= 0;
	s->start_failed = !s->started;
	return s->started ? 0 : ret;
}

/* Transmit: wait under the wait policy until block i is free again. Returns
 * 1, 0 or a negative errno as wait_block() does.
 *
 * A block libiio no longer has (-EPERM, see rx_turn()) is free, but its frame
 * was not sent: libiio's generic block path gives the result of the transfer
 * once, and a transfer that timed out reads as "not ready". That is -EIO
 * here, once, unless the caller already got the error of that transfer. */
static int tx_wait(struct iio_conv_stream *s, unsigned int i)
{
	int ret = wait_block(s, i, s->timeout_ms);

	if (ret == -EPERM) {
		ret = s->failed[i] ? 1 : -EIO;
		s->queued[i] = false;
		s->failed[i] = false;
	}
	return ret;
}

/* Transmit: wait for the frames in flight, oldest first. Stops at the first
 * that is not ready or fails, but goes on behind one that was lost. Returns
 * IIO_CONV_STREAM_FRAME when all were sent, -EIO when none is left in flight
 * but one was lost, or what tx_wait() said about the one it stopped at. */
static int tx_drain(struct iio_conv_stream *s)
{
	bool lost = false;
	unsigned int i, b;
	int ret;

	for (i = 0; i < s->nb_blocks; i++) {
		b = (s->curr + i) % s->nb_blocks;
		if (!s->queued[b])
			continue;
		ret = tx_wait(s, b);
		if (ret == -EIO && !s->queued[b])
			lost = true;
		else if (ret <= 0)
			return ret;
	}
	return lost ? -EIO : IIO_CONV_STREAM_FRAME;
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
			 * path forgets a block once it has given the result
			 * of the transfer, also when that was a timeout; the
			 * block then holds no frame. */
			s->queued[b] = false;
			s->failed[b] = false;
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
	unsigned int i, k, nb_scan, nb_all;
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
	s->spec = *spec;
	s->timeout_ms = IIO_CONV_STREAM_TIMEOUT_CONTEXT;

	/* the caller's arrays need not outlive this call */
	nb_all = nb_chans + (spec->complex_iq ? nb_chans / 2 : 0);
	s->chans = calloc(nb_all, sizeof(*s->chans));
	s->gain = spec->user_gain ? calloc(nb_all, sizeof(*s->gain)) : NULL;
	s->offset = spec->user_offset ? calloc(nb_all, sizeof(*s->offset)) : NULL;
	if (!s->chans || (spec->user_gain && !s->gain) || (spec->user_offset && !s->offset)) {
		ret = -ENOMEM;
		goto err_close;
	}
	for (i = 0; i < nb_all; i++) {
		k = i < nb_chans ? i : 2 * (i - nb_chans);
		s->chans[i] = chans[k];
		if (s->gain)
			s->gain[i] = spec->user_gain[k];
		if (s->offset)
			s->offset[i] = spec->user_offset[k];
	}
	s->spec.user_gain = s->gain;
	s->spec.user_offset = s->offset;

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

	s->mask = iio_buffer_stream_get_channels_mask(s->bs);

	ret = get_conv(s, spec->type, spec->complex_iq, &s->conv);
	if (ret)
		goto err_close;
	s->step = iio_conv_get_step(s->conv);
	if (s->samples > SIZE_MAX / s->step) {
		ret = -EINVAL;
		goto err_close;
	}

	s->blocks = calloc(s->nb_blocks, sizeof(*s->blocks));
	s->queued = calloc(s->nb_blocks, sizeof(*s->queued));
	s->failed = calloc(s->nb_blocks, sizeof(*s->failed));
	if (!s->blocks || !s->queued || !s->failed) {
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
		/* a write fills in the channels of the stream only: whatever
		 * else the mask in use holds is sent as zero */
		if (s->output)
			memset(iio_block_start(block), 0, s->samples * s->step);
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

	/* A transmit stream is started by its first write. */

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
	unsigned int i, k;

	if (!stream || !*stream)
		return;
	s = *stream;
	*stream = NULL;

	if (s->bs) {
		/* Frames written just before the close must still go out:
		 * wait for them, each under the wait policy, and give up at
		 * the first that does not complete. Not for a cyclic frame,
		 * which never completes and must never be dequeued, and not
		 * while the buffer is not started. */
		if (s->output && s->started && !s->cyclic)
			tx_drain(s);

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

	for (i = 0; i < NB_TYPES; i++)
		for (k = 0; k < 2; k++)
			iio_conv_destroy(s->convs[i][k]);
	free(s->chans);
	free(s->gain);
	free(s->offset);
	free(s->blocks);
	free(s->queued);
	free(s->failed);
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
	return iio_conv_stream_write_typed(stream, src, nb_samples, stream->spec.type,
			stream->spec.complex_iq);
}

int iio_conv_stream_write_typed(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples, enum iio_conv_type type, bool src_complex)
{
	struct iio_conv_stream *s = stream;
	struct iio_conv *conv;
	unsigned int b;
	ssize_t n;
	int ret;

	if (!s)
		return -EINVAL;
	if (!s->output)
		return -EBADF;
	if (!src || !nb_samples || nb_samples > s->samples || (unsigned int)type >= NB_TYPES)
		return -EINVAL;
	if (src_complex && !s->spec.complex_iq)
		return -EINVAL;

	/* An earlier write queued its frame but could not start the buffer.
	 * Nothing may be waited for until it runs. */
	if (s->start_failed) {
		ret = tx_start(s);
		if (ret)
			return ret;
	}
	if (s->cyclic_sent)
		return -EBUSY;

	ret = get_conv(s, type, src_complex, &conv);
	if (ret)
		return ret;

	b = s->curr;
	if (s->queued[b]) {
		/* every block is in flight: wait for the oldest frame. If it
		 * was lost the block is free, but the caller has to hear of
		 * it: this frame waits for the next call. */
		ret = tx_wait(s, b);
		if (ret <= 0)
			return ret;
	}

	/* a real array on I/Q pairs fills in the I channels only: the Q
	 * channels must not keep what an earlier frame left there */
	if (s->spec.complex_iq && !src_complex)
		memset(iio_block_start(s->blocks[b]), 0, nb_samples * s->step);
	n = iio_conv_write(conv, s->blocks[b], src, nb_samples);
	if (n < 0)
		return (int)n;

	ret = iio_block_enqueue(s->blocks[b], nb_samples * s->step, s->cyclic);
	if (ret < 0)
		return ret;
	s->queued[b] = true;
	s->cyclic_sent = s->cyclic;
	s->curr = (b + 1) % s->nb_blocks;

	/* the first frame is queued: the buffer can run */
	if (!s->started) {
		ret = tx_start(s);
		if (ret)
			return ret;
	}
	return IIO_CONV_STREAM_FRAME;
}

int iio_conv_stream_flush(struct iio_conv_stream *stream)
{
	struct iio_conv_stream *s = stream;
	unsigned int i;
	int ret;

	if (!s)
		return -EINVAL;

	if (s->output) {
		/* a cyclic frame never completes */
		if (s->cyclic)
			return IIO_CONV_STREAM_FRAME;
		if (s->start_failed) {
			ret = tx_start(s);
			if (ret)
				return ret;
		}
		/* before the first write no block is in flight */
		return tx_drain(s);
	}

	/* whatever is captured goes straight back, oldest first */
	for (i = 0; i < s->nb_blocks; i++) {
		ret = rx_turn(s, NULL, IIO_CONV_STREAM_TIMEOUT_NONBLOCK);
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
	return stream->spec.complex_iq ? stream->nb_chans / 2 : stream->nb_chans;
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
	return stream->spec.type;
}

bool iio_conv_stream_is_complex(const struct iio_conv_stream *stream)
{
	return stream->spec.complex_iq;
}

bool iio_conv_stream_is_output(const struct iio_conv_stream *stream)
{
	return stream->output;
}
