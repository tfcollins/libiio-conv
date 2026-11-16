/* SPDX-License-Identifier: MIT */
/* Data-path object builders for gateway tests. Include after "mex_test.h", in ONE source file
 * of a test program (it includes tests/fake_backend.h, whose state is static). Objects are
 * registered exactly as the gateway commands register them (dp_add_* of data_path.h), so a
 * group's test never needs a sibling group's command. Each returns the id, or 0 after
 * counting a test failure when the object does not exist. */
#ifndef DP_TEST_H
#define DP_TEST_H

#include "mex_test.h"
#include "data_path.h"
#include "fake_backend.h"

#include <stdarg.h>

/* Counts in test_failures of the including source file, as CHECK() does. */
#define DP_FAIL(...) do { \
	if (test_failures++ < 20) { \
		fprintf(stderr, "dp_test: " __VA_ARGS__); \
		fputc('\n', stderr); \
	} } while (0)

static inline void dp_context_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_context_destroy(ptr);
}

/* fake_reset(); fake_create_context(xml_path) as an owned GW_T_CONTEXT root whose destroy op is
 * iio_context_destroy */
static inline gw_id dp_open_fake(const char *xml_path)
{
	static const struct gw_ops ops = { NULL, dp_context_destroy_op };
	struct iio_context *ctx;
	gw_id id;

	/* as mt_open_context(): the gateway sets its handle table up in its first call (and in
	 * the first one after MATLAB's exit handler ran); an entry added before would be forgotten */
	mt_ok("mex_handle_count", 0);
	fake_reset();
	ctx = fake_create_context(xml_path);
	if (iio_err(ctx)) {
		DP_FAIL("no fake context from %s (%d)", xml_path, iio_err(ctx));
		return 0;
	}
	id = gw_handle_add(GW_T_CONTEXT, ctx, 0, true, &ops, NULL);
	if (!id) {
		iio_context_destroy(ctx);
		DP_FAIL("the fake context was not registered");
		return 0;
	}
	mt_ok("mex_handle_count", 0);    /* the gateway sees the handle and locks the MEX file */
	return id;
}

/* iio_context_find_device; borrowed child of ctx */
static inline gw_id dp_dev(gw_id ctx, const char *name)
{
	const struct iio_context *c = gw_handle_ptr(ctx, GW_T_CONTEXT);
	const struct iio_device *dev = c ? iio_context_find_device(c, name) : NULL;
	gw_id id = 0;

	if (!dev || gw_handle_borrow(GW_T_DEVICE, dev, ctx, &id) || !id) {
		DP_FAIL("no device %s", name);
		return 0;
	}
	return id;
}

/* iio_device_find_channel; borrowed child of the context of dev */
static inline gw_id dp_chn(gw_id dev, const char *id, bool output)
{
	const struct iio_device *d = gw_handle_ptr(dev, GW_T_DEVICE);
	const struct iio_channel *chn = d ? iio_device_find_channel(d, id, output) : NULL;
	gw_id h = 0;

	if (!chn || gw_handle_borrow(GW_T_CHANNEL, chn, gw_handle_root(dev), &h) || !h) {
		DP_FAIL("no %s channel %s", output ? "output" : "input", id);
		return 0;
	}
	return h;
}

/* iio_device_get_buffer; borrowed child of the context of dev */
static inline gw_id dp_buf(gw_id dev, unsigned int index)
{
	const struct iio_device *d = gw_handle_ptr(dev, GW_T_DEVICE);
	const struct iio_buffer *buf = d && index < iio_device_get_buffers_count(d) ?
		iio_device_get_buffer(d, index) : NULL;
	gw_id id = 0;

	if (!buf || gw_handle_borrow(GW_T_BUFFER, buf, gw_handle_root(dev), &id) || !id) {
		DP_FAIL("no buffer %u", index);
		return 0;
	}
	return id;
}

/* owned mask with these channel ids (const char *) enabled */
static inline gw_id dp_mask(gw_id dev, bool output, unsigned int nb, ...)
{
	const struct iio_device *d = gw_handle_ptr(dev, GW_T_DEVICE);
	struct iio_channels_mask *mask = d ? iio_create_channels_mask(iio_device_get_channels_count(d)) : NULL;
	bool ok = mask != NULL;
	va_list ap;
	gw_id id;

	va_start(ap, nb);
	for (unsigned int i = 0; ok && i < nb; i++) {
		const char *name = va_arg(ap, const char *);
		const struct iio_channel *chn = iio_device_find_channel(d, name, output);

		if (chn) {
			iio_channel_enable(chn, mask);
		} else {
			DP_FAIL("no %s channel %s for the mask", output ? "output" : "input", name);
			ok = false;
		}
	}
	va_end(ap);
	if (!mask) {
		DP_FAIL("no channels mask");
		return 0;
	}
	id = ok ? dp_add_mask(mask, dev) : 0;
	if (!id) {
		iio_channels_mask_destroy(mask);
		if (ok)
			DP_FAIL("the mask was not registered");
	}
	return id;
}

/* iio_buffer_open, as the command buffer_open leaves it */
static inline gw_id dp_bufstream(gw_id buf, gw_id mask)
{
	struct iio_buffer *b = gw_handle_ptr(buf, GW_T_BUFFER);
	const struct iio_channels_mask *m = gw_handle_ptr(mask, GW_T_MASK);
	struct iio_buffer_stream *bs;
	gw_id id;

	if (!b || !m) {
		DP_FAIL("no buffer stream: %s", b ? "not a mask" : "not a buffer");
		return 0;
	}
	bs = iio_buffer_open(b, m);
	if (iio_err(bs)) {
		DP_FAIL("iio_buffer_open: %d", iio_err(bs));
		return 0;
	}
	id = dp_add_bufstream(bs, buf);
	if (!id) {
		iio_buffer_close(bs);
		DP_FAIL("the buffer stream was not registered");
	}
	return id;
}

/* as the command buffer_stream_create_block leaves it */
static inline gw_id dp_block(gw_id bs, size_t nbytes)
{
	struct iio_buffer_stream *s = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	struct iio_block *blk;
	gw_id id;

	if (!s) {
		DP_FAIL("no block: not a buffer stream");
		return 0;
	}
	blk = iio_buffer_stream_create_block(s, nbytes);
	if (iio_err(blk)) {
		DP_FAIL("iio_buffer_stream_create_block(%zu): %d", nbytes, iio_err(blk));
		return 0;
	}
	id = dp_add_block(blk, bs);
	if (!id) {
		iio_block_destroy(blk);
		DP_FAIL("the block was not registered");
	}
	return id;
}

#endif
