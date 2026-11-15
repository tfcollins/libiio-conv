/* SPDX-License-Identifier: MIT */
/*
 * libiio-conv - fast conversion between IIO buffer samples and host arrays
 */
#ifndef IIO_CONV_H
#define IIO_CONV_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __iio_conv_api
#define __iio_conv_api __attribute__((visibility("default")))
#endif

struct iio_block;
struct iio_channel;
struct iio_channels_mask;
struct iio_conv;

/** Element type of the host array */
enum iio_conv_type {
	IIO_CONV_I8,
	IIO_CONV_U8,
	IIO_CONV_I16,
	IIO_CONV_U16,
	IIO_CONV_I32,
	IIO_CONV_U32,
	IIO_CONV_I64,
	IIO_CONV_U64,
	IIO_CONV_F32,
	IIO_CONV_F64,
};

/** Arrangement of the host array.
 * With N samples, C columns and element size E (doubled for complex):
 * COLUMNS puts column c at byte c*N*E (MATLAB / Fortran order);
 * INTERLEAVED puts sample i of column c at byte (i*C + c)*E (C / NumPy order). */
enum iio_conv_layout {
	IIO_CONV_COLUMNS,
	IIO_CONV_INTERLEAVED,
};

/** Scaling applied between raw sample and host value: host = (raw + offset) * gain */
enum iio_conv_scale {
	IIO_CONV_SCALE_NONE,      /**< gain 1, offset 0 */
	IIO_CONV_SCALE_IIO,       /**< the channel's IIO scale and offset */
	IIO_CONV_SCALE_FULLSCALE, /**< gain 1/2^(bits-1) signed, 1/2^bits unsigned */
};

struct iio_conv_spec {
	enum iio_conv_type type;
	enum iio_conv_layout layout;
	enum iio_conv_scale scale;
	/** Channels 2k and 2k+1 become real and imaginary part of column k */
	bool complex_iq;
	/** Integer narrowing saturates instead of wrapping */
	bool saturate;
	/** Per channel extra gain, applied after the scale mode. NULL = 1.0 */
	const double *user_gain;
	/** Per channel extra offset, added after all gains. NULL = 0.0 */
	const double *user_offset;
};

/** Storage description of one channel inside a sample */
struct iio_conv_chan {
	unsigned int length; /**< storage size in bits: 8, 16, 32 or 64 */
	unsigned int bits;   /**< valid bits */
	unsigned int shift;  /**< right shift to apply to the storage word */
	bool is_signed;
	bool is_be;
	bool with_scale;
	double scale;
	double offset;
	size_t byte_offset;  /**< position within one sample */
};

/** Build a converter from plain descriptors. step = bytes per sample.
 * Returns 0 or a negative errno. */
__iio_conv_api int iio_conv_create_raw(const struct iio_conv_chan *chans,
		unsigned int nb_chans, size_t step,
		const struct iio_conv_spec *spec, struct iio_conv **conv);

__iio_conv_api void iio_conv_destroy(struct iio_conv *conv);

/** Storage bytes per sample */
__iio_conv_api size_t iio_conv_get_step(const struct iio_conv *conv);

/** Host bytes per sample, all columns together */
__iio_conv_api size_t iio_conv_get_host_step(const struct iio_conv *conv);

/** Convert nb_samples samples at src into the host array dst */
__iio_conv_api void iio_conv_read_raw(const struct iio_conv *conv, const void *src,
		size_t nb_samples, void *dst);

/** Convert the host array src into nb_samples samples at dst.
 * Only the bytes of the converter's channels are written. */
__iio_conv_api void iio_conv_write_raw(const struct iio_conv *conv, void *dst,
		size_t nb_samples, const void *src);

/** "avx512", "avx2", "sse41", "neon" or "scalar". Override with the
 * environment variable IIO_CONV_FORCE. */
__iio_conv_api const char *iio_conv_get_backend_name(void);

/* libiio v1 entry points (library built with IIO_CONV_WITH_LIBIIO) */

/** Build a converter for chans, which must all be enabled in mask and belong
 * to the same device. mask is the mask the buffer was opened with. */
__iio_conv_api int iio_conv_create(const struct iio_channels_mask *mask,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		const struct iio_conv_spec *spec, struct iio_conv **conv);

/** Convert a block into dst. Returns the number of samples converted, which is
 * limited by both the block and dst_bytes, or a negative errno. With the
 * COLUMNS layout the columns are that many samples long.
 * The block must come from a buffer opened with the converter's mask. */
__iio_conv_api ssize_t iio_conv_read(const struct iio_conv *conv,
		const struct iio_block *block, void *dst, size_t dst_bytes);

/** Fill a block from src, a host array of nb_samples samples. Returns the
 * number of samples written, at most the block capacity, or a negative errno.
 * Pass that count times iio_conv_get_step() to iio_block_enqueue(). */
__iio_conv_api ssize_t iio_conv_write(const struct iio_conv *conv,
		struct iio_block *block, const void *src, size_t nb_samples);

#ifdef __cplusplus
}
#endif

#endif /* IIO_CONV_H */
