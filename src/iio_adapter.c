/* SPDX-License-Identifier: MIT */
/*
 * libiio v1 entry points: build channel descriptors from iio_channel handles
 * and map blocks onto the raw conversion functions.
 */
#include "internal.h"

#include <errno.h>
#include <iio/iio.h>
#include <stdlib.h>

/* Byte position of every requested channel within one sample. Follows the
 * layout libiio uses (iio_block_first / iio_device_get_sample_size): enabled
 * scan elements in index order, each aligned to its own size, channels that
 * share an index share the bytes. */
static int find_offsets(const struct iio_device *dev, const struct iio_channels_mask *mask,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		struct iio_conv_chan *descs)
{
	unsigned int i, k, found = 0, nb = iio_device_get_channels_count(dev);
	size_t pos = 0, prev_off = 0;
	long prev_index = -1;

	for (i = 0; i < nb; i++) {
		const struct iio_channel *cur = iio_device_get_channel(dev, i);
		const struct iio_data_format *fmt = iio_channel_get_data_format(cur);
		size_t len = fmt->length / 8 * fmt->repeat, off;
		long index = iio_channel_get_index(cur);

		if (index < 0 || !iio_channel_is_scan_element(cur) ||
				!iio_channel_is_enabled(cur, mask) || !len)
			continue;

		if (index == prev_index) {
			off = prev_off;
		} else {
			if (pos % len)
				pos += len - pos % len;
			off = pos;
			pos += len;
		}
		prev_index = index;
		prev_off = off;

		for (k = 0; k < nb_chans; k++) {
			if (chans[k] == cur) {
				descs[k].byte_offset = off;
				found++;
			}
		}
	}

	return found == nb_chans ? 0 : -EINVAL;
}

int iio_conv_create(const struct iio_channels_mask *mask,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		const struct iio_conv_spec *spec, struct iio_conv **conv)
{
	const struct iio_device *dev;
	struct iio_conv_chan *descs;
	ssize_t step;
	unsigned int i;
	int ret;

	if (!mask || !chans || !nb_chans || !spec || !conv)
		return -EINVAL;

	if (!chans[0])
		return -EINVAL;
	dev = iio_channel_get_device(chans[0]);

	/* also checks that the mask was made for this device, which must be
	 * known before any channel is looked up in it */
	step = iio_device_get_sample_size(dev, mask);
	if (step < 0)
		return (int)step;

	for (i = 0; i < nb_chans; i++) {
		if (!chans[i] || iio_channel_get_device(chans[i]) != dev ||
				!iio_channel_is_scan_element(chans[i]) ||
				!iio_channel_is_enabled(chans[i], mask))
			return -EINVAL;
	}

	descs = calloc(nb_chans, sizeof(*descs));
	if (!descs)
		return -ENOMEM;

	for (i = 0; i < nb_chans; i++) {
		const struct iio_data_format *fmt = iio_channel_get_data_format(chans[i]);

		if (fmt->repeat > 1 || fmt->is_float) {
			ret = -ENOTSUP;
			goto out;
		}
		descs[i].length = fmt->length;
		descs[i].bits = fmt->bits;
		descs[i].shift = fmt->shift;
		descs[i].is_signed = fmt->is_signed;
		descs[i].is_be = fmt->is_be;
		descs[i].with_scale = fmt->with_scale;
		descs[i].scale = fmt->scale;
		descs[i].offset = fmt->offset;
	}

	ret = find_offsets(dev, mask, chans, nb_chans, descs);
	if (!ret)
		ret = iio_conv_create_raw(descs, nb_chans, (size_t)step, spec, conv);
out:
	free(descs);
	return ret;
}

static size_t block_samples(const struct iio_conv *conv, const struct iio_block *block)
{
	uintptr_t start = (uintptr_t)iio_block_start(block);
	uintptr_t end = (uintptr_t)iio_block_end(block);

	return end > start ? (end - start) / conv->step : 0;
}

ssize_t iio_conv_read(const struct iio_conv *conv, const struct iio_block *block,
		void *dst, size_t nb_samples)
{
	size_t n;

	if (!conv || !block || (!dst && nb_samples))
		return -EINVAL;

	n = block_samples(conv, block);
	if (nb_samples < n)
		n = nb_samples;

	/* the host array keeps the caller's shape even when the block is
	 * smaller */
	conv_read_cols(conv, iio_block_start(block), n, dst, nb_samples);
	return (ssize_t)n;
}

ssize_t iio_conv_write(const struct iio_conv *conv, struct iio_block *block,
		const void *src, size_t nb_samples)
{
	size_t n;

	if (!conv || !block || (!src && nb_samples))
		return -EINVAL;

	n = block_samples(conv, block);
	if (nb_samples < n)
		n = nb_samples;

	/* the host array keeps the caller's shape even when the block is
	 * smaller */
	conv_write_cols(conv, iio_block_start(block), n, src, nb_samples);
	return (ssize_t)n;
}
