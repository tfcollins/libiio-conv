/* SPDX-License-Identifier: MIT */
/*
 * libiio-conv streams: whole frames between an IIO buffer and a host array
 * in one call. A stream owns the buffer stream, its blocks and a converter.
 */
#ifndef IIO_CONV_STREAM_H
#define IIO_CONV_STREAM_H

#include <iio/iio-conv.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_buffer;
struct iio_conv_stream;

struct iio_conv_stream_params {
	/** Frame length in samples. Every read returns and every write takes
	 * exactly this many. */
	size_t nb_samples;
	/** Number of blocks (kernel buffers) the stream cycles through.
	 * 0 selects the default of 2. */
	unsigned int nb_blocks;
	/** Transmit only: the first frame written is repeated by the hardware
	 * until the stream is closed. Further writes fail with -EBUSY. */
	bool cyclic;
	/** How long a read or a write may wait for a block, in milliseconds.
	 * 0: do not wait. Positive: wait up to that long, whatever the
	 * context's timeout is. Negative: block in libiio, so that the
	 * context's timeout applies. */
	int timeout_ms;
	/** Host array: element type, layout, I/Q pairing, scaling */
	struct iio_conv_spec spec;
};

/** Open a stream on buf for chans, which must be scan elements of the
 * buffer's device. Only these channels are enabled; host column k is channel
 * k as passed (or the pair 2k, 2k+1 when spec.complex_iq is set), whatever
 * their order in the device. The direction is that of the buffer. A receive
 * stream starts capturing at once.
 * Returns 0 or a negative errno. */
__iio_conv_api int iio_conv_stream_open(struct iio_buffer *buf,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		const struct iio_conv_stream_params *params,
		struct iio_conv_stream **stream);

/** Receive one frame into dst, a host array of nb_samples samples.
 * Returns nb_samples, 0 when no frame arrived within the timeout (dst is
 * untouched and the call can be repeated), or a negative errno. */
__iio_conv_api ssize_t iio_conv_stream_read(struct iio_conv_stream *stream, void *dst);

/** Transmit one frame from src, a host array of nb_samples samples. The
 * frame is handed to the hardware before the call returns.
 * Returns nb_samples, 0 when no block became free within the timeout (nothing
 * was sent and the call can be repeated), or a negative errno. */
__iio_conv_api ssize_t iio_conv_stream_write(struct iio_conv_stream *stream, const void *src);

/** Receive only: drop the frames that are already captured and waiting.
 * Returns how many were dropped (at most the number of blocks) or a negative
 * errno. */
__iio_conv_api int iio_conv_stream_flush(struct iio_conv_stream *stream);

/** Close the stream. A transmit stream that is not cyclic first lets the
 * frames already written go out. NULL is ignored. */
__iio_conv_api void iio_conv_stream_close(struct iio_conv_stream *stream);

__iio_conv_api void iio_conv_stream_set_timeout(struct iio_conv_stream *stream, int timeout_ms);
__iio_conv_api bool iio_conv_stream_is_output(const struct iio_conv_stream *stream);
__iio_conv_api size_t iio_conv_stream_get_nb_samples(const struct iio_conv_stream *stream);
__iio_conv_api unsigned int iio_conv_stream_get_nb_blocks(const struct iio_conv_stream *stream);
/** Storage bytes per sample, for the channels the hardware really enabled */
__iio_conv_api size_t iio_conv_stream_get_step(const struct iio_conv_stream *stream);
/** Host bytes per sample, all columns together */
__iio_conv_api size_t iio_conv_stream_get_host_step(const struct iio_conv_stream *stream);

#ifdef __cplusplus
}
#endif

#endif /* IIO_CONV_STREAM_H */
