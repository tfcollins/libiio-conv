/* SPDX-License-Identifier: MIT */
/*
 * libiio-conv stream layer: whole frames of converted samples to and from one
 * libiio buffer. Built on explicit blocks (iio_buffer_open,
 * iio_buffer_stream_create_block, iio_block_enqueue / iio_block_dequeue), not
 * on struct iio_stream, which can neither transmit cyclically nor submit a
 * frame in the call that received it.
 *
 * Functions take and return only pointers and scalars and report failure as a
 * negative errno. A stream must be used from one thread at a time.
 */
#ifndef IIO_CONV_STREAM_H
#define IIO_CONV_STREAM_H

#include <iio/iio-conv.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_buffer;
struct iio_conv_stream;

/** Results of read, write and flush. Negative values are errno codes. */
#define IIO_CONV_STREAM_NO_FRAME 0 /**< nothing was transferred within the wait policy */
#define IIO_CONV_STREAM_FRAME    1 /**< one frame was transferred, or the flush completed */

/** Wait policies for iio_conv_stream_set_timeout(); a value > 0 is a time in ms */
#define IIO_CONV_STREAM_TIMEOUT_CONTEXT  (-1) /**< block; the context timeout bounds the wait */
#define IIO_CONV_STREAM_TIMEOUT_NONBLOCK 0    /**< never wait */

/**
 * Open a stream on buf for chans. The direction is that of the buffer: an
 * input buffer gives a receive stream, an output buffer a transmit stream.
 *
 * chans: nb_chans distinct scan elements of buf in host column order (with
 *   spec->complex_iq: I then Q of each column). Only these are enabled. The
 *   hardware may enable more; the layout follows the mask actually in use.
 * samples_per_frame: frame length in samples; every block holds one frame.
 * nb_blocks: blocks in rotation. Receive: all are queued for capture at all
 *   times. Transmit: that many frames can be in flight.
 * cyclic: transmit only. The first frame written repeats until the stream is
 *   closed; one block is used whatever nb_blocks says.
 * spec: type, layout, scale, complex_iq, saturate, raw. user_gain and
 *   user_offset (nb_chans entries each) are copied. For transmit, type and
 *   complex_iq describe the array iio_conv_stream_write() takes.
 *
 * A receive stream is started here (blocks queued, buffer enabled): data
 * captured before the first read is delivered first, see
 * iio_conv_stream_flush(). A transmit stream is started by its first write;
 * its blocks start zeroed.
 *
 * Returns 0 and sets *stream, or a negative errno and leaves *stream alone:
 * -EINVAL for a NULL argument; nb_chans, samples_per_frame or nb_blocks of 0;
 * a channel that is not a scan element of buf; a channel listed twice; cyclic
 * on an input buffer; anything iio_conv_create() rejects with -EINVAL.
 * -ENOTSUP for channel formats iio_conv_create() does not support. Otherwise
 * the error of iio_buffer_open(), iio_buffer_stream_create_block(),
 * iio_block_enqueue() or iio_buffer_stream_start(). On failure everything
 * created so far is released, without dequeuing any block.
 */
__iio_conv_api int iio_conv_stream_open(struct iio_buffer *buf,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		size_t samples_per_frame, unsigned int nb_blocks, bool cyclic,
		const struct iio_conv_spec *spec, struct iio_conv_stream **stream);

/**
 * Close *stream and set it to NULL. NULL and a pointer to NULL are accepted,
 * so closing twice is harmless.
 *
 * Order: (transmit, started, not cyclic) wait for the frames in flight,
 * oldest first, each under the wait policy, stopping at the first that does
 * not complete; iio_buffer_stream_cancel(); iio_buffer_stream_stop() if the
 * buffer was started; iio_block_destroy() for every block;
 * iio_buffer_close(); then the converters. A cyclic block is never dequeued.
 */
__iio_conv_api void iio_conv_stream_close(struct iio_conv_stream **stream);

/**
 * Set how long read, write and flush may wait for a block:
 * IIO_CONV_STREAM_TIMEOUT_CONTEXT (default): one blocking dequeue, bounded by
 *   the context timeout, which is shared by everything on that context;
 * IIO_CONV_STREAM_TIMEOUT_NONBLOCK: one non-blocking dequeue;
 * > 0: non-blocking dequeues 1 ms apart until that many milliseconds have
 *   passed. Independent of the context timeout, which is never changed.
 * Returns 0, or -EINVAL for a NULL stream or a value below -1.
 */
__iio_conv_api int iio_conv_stream_set_timeout(struct iio_conv_stream *stream, int timeout_ms);

/**
 * Receive one frame into dst, a host array of samples_per_frame samples in
 * the layout of the spec (COLUMNS: samples_per_frame rows per column).
 *
 * Returns IIO_CONV_STREAM_FRAME after writing the whole frame (never part of
 * one) and queuing the block again. Returns IIO_CONV_STREAM_NO_FRAME, with
 * dst untouched, when no block completed within the wait policy (libiio
 * reported -ETIMEDOUT, -EBUSY or -EAGAIN); the call can be repeated.
 * -EBADF on a transmit stream, -EINVAL for NULL, other libiio errors as is.
 */
__iio_conv_api int iio_conv_stream_read(struct iio_conv_stream *stream, void *dst);

/**
 * Transmit one frame of nb_samples samples (1 .. samples_per_frame) from src,
 * an array of the spec's type and complexity. A shorter frame is sent as a
 * shorter block (partial frame). Bytes of enabled channels that are not in
 * chans are sent as zero.
 *
 * The frame is converted and queued in this call; the first call starts the
 * buffer. If every block is in flight, the call first waits, under the wait
 * policy, for the oldest one.
 *
 * Returns IIO_CONV_STREAM_FRAME when the frame was queued;
 * IIO_CONV_STREAM_NO_FRAME when no block became free in time (nothing was
 * queued; repeat the call); -EBUSY on a cyclic stream that already has its
 * frame; -EBADF on a receive stream; -EINVAL for NULL or for nb_samples of 0
 * or more than samples_per_frame; other libiio errors as is.
 */
__iio_conv_api int iio_conv_stream_write(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples);

/**
 * As iio_conv_stream_write() for an array of another element type or
 * complexity; scale, gains, saturate and raw stay those of the spec. One
 * converter per (type, src_complex) is created on first use and kept.
 *
 * src_complex false on a complex_iq stream: src has one real column per I/Q
 * pair and the Q channels are sent as zero.
 * src_complex true on a stream without complex_iq: -EINVAL.
 * -EINVAL also when the type cannot be used with the spec's scaling (integer
 * type with a scale mode, gain or offset).
 */
__iio_conv_api int iio_conv_stream_write_typed(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples, enum iio_conv_type type, bool src_complex);

/**
 * Receive: discard the frames already captured, at most nb_blocks, so that
 * the next read waits for new data. Never waits.
 * Transmit: wait, under the wait policy, until every queued frame has been
 * sent. Returns at once on a cyclic stream and before the first write.
 * Returns IIO_CONV_STREAM_FRAME when done, IIO_CONV_STREAM_NO_FRAME when a
 * transmit wait ran out, or a negative errno.
 */
__iio_conv_api int iio_conv_stream_flush(struct iio_conv_stream *stream);

/* Getters. stream must not be NULL. */
__iio_conv_api size_t iio_conv_stream_get_samples(const struct iio_conv_stream *stream);       /* samples per frame */
__iio_conv_api unsigned int iio_conv_stream_get_columns(const struct iio_conv_stream *stream); /* nb_chans, or nb_chans / 2 with complex_iq */
__iio_conv_api size_t iio_conv_stream_get_step(const struct iio_conv_stream *stream);          /* storage bytes per sample, mask in use */
__iio_conv_api unsigned int iio_conv_stream_get_nb_blocks(const struct iio_conv_stream *stream); /* 1 when cyclic */
__iio_conv_api enum iio_conv_type iio_conv_stream_get_type(const struct iio_conv_stream *stream);
__iio_conv_api bool iio_conv_stream_is_complex(const struct iio_conv_stream *stream);
__iio_conv_api bool iio_conv_stream_is_output(const struct iio_conv_stream *stream);

#ifdef __cplusplus
}
#endif

#endif /* IIO_CONV_STREAM_H */
