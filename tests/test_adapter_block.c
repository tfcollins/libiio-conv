/* SPDX-License-Identifier: MIT */
/* Block entry points against the emulation backend. Exits 77 (skip) when
 * that backend cannot provide a block. */
#include <errno.h>
#include "adapter_util.h"

#define NB 64

int main(int argc, char **argv)
{
	static const char *const ids[5] = { "voltage0_i", "voltage0_q", "voltage1", "temp", "voltage2" };
	struct iio_conv_spec spec = { .type = IIO_CONV_F32, .scale = IIO_CONV_SCALE_FULLSCALE };
	const struct iio_channel *ch[5];
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *stream;
	const struct iio_device *rx;
	struct iio_context *ctx;
	struct iio_buffer *buf;
	struct iio_block *block;
	struct iio_conv *c = NULL;
	size_t step, host_step, i, col, block_len;
	uint8_t *mem, *shadow;
	float *a, *b;

	if (argc < 2)
		return 2;
	ctx = open_ctx("emu", argv[1]);
	if (!ctx) {
		fprintf(stderr, "no emu context\n");
		return 77;
	}
	rx = iio_context_find_device(ctx, "conv-rx");
	if (!rx || !iio_device_get_buffers_count(rx))
		return 77;
	for (i = 0; i < 5; i++)
		ch[i] = iio_device_find_channel(rx, ids[i], false);
	mask = iio_create_channels_mask(iio_device_get_channels_count(rx));
	for (i = 0; i < 5; i++)
		iio_channel_enable(ch[i], mask);

	buf = iio_device_get_buffer(rx, 0);
	stream = iio_buffer_open(buf, mask);
	if (iio_err(stream)) {
		fprintf(stderr, "emu: cannot open buffer (%d)\n", iio_err(stream));
		return 77;
	}
	block = iio_buffer_stream_create_block(stream, 12 * NB);
	if (iio_err(block)) {
		fprintf(stderr, "emu: cannot create block (%d)\n", iio_err(block));
		return 77;
	}

	CHECK(iio_conv_create(iio_buffer_stream_get_channels_mask(stream), ch, 5, &spec, &c) == 0);
	if (!c)
		TEST_MAIN_RETURN();
	step = iio_conv_get_step(c);
	host_step = iio_conv_get_host_step(c);
	CHECK(step == 12 && host_step == 20);

	mem = iio_block_start(block);
	block_len = (size_t)((uint8_t *)iio_block_end(block) - mem);
	CHECK(block_len == 12 * NB);
	for (i = 0; i < block_len; i++)
		mem[i] = (uint8_t)(i * 7 + 3);

	/* whole block equals the raw entry point on the same bytes */
	a = malloc(NB * host_step + 4);
	b = malloc(NB * host_step + 4);
	memset(a, 0, NB * host_step + 4);
	CHECK(iio_conv_read(c, block, a, NB * host_step) == NB);
	iio_conv_read_raw(c, mem, NB, b);
	CHECK(memcmp(a, b, NB * host_step) == 0);

	/* channel positions agree with libiio */
	for (i = 0; i < 5; i++)
		CHECK((size_t)((uint8_t *)iio_block_first(block, ch[i]) - mem) ==
				iio_conv_test_get_plan(c, i)->src_off);

	/* a small destination limits the count and is not overrun */
	memset(a, 0x7F, NB * host_step + 4);
	CHECK(iio_conv_read(c, block, a, 5 * host_step + 3) == 5);
	CHECK(((uint8_t *)a)[5 * host_step] == 0x7F);
	iio_conv_read_raw(c, mem, 5, b);
	CHECK(memcmp(a, b, 5 * host_step) == 0);
	CHECK(iio_conv_read(c, block, a, 0) == 0);

	/* writing more samples than the block holds stops at its capacity; the
	 * host array keeps the caller's shape (2*NB rows per column) */
	free(a);
	a = malloc(2 * NB * host_step);
	for (i = 0; i < 2 * NB * 5; i++)
		a[i] = (float)(i % 197) / 197.0f - 0.5f;
	for (col = 0; col < 5; col++) /* b: the first NB rows of every column */
		memcpy(b + col * NB, a + col * 2 * NB, NB * sizeof(float));
	shadow = malloc(block_len);
	memset(mem, 0, block_len);
	memset(shadow, 0, block_len);
	CHECK(iio_conv_write(c, block, a, 2 * NB) == NB);
	iio_conv_write_raw(c, shadow, NB, b);
	CHECK(memcmp(mem, shadow, block_len) == 0);
	CHECK(iio_conv_write(c, block, a, 0) == 0);
	iio_conv_destroy(c);

	free(a); free(b); free(shadow);
	iio_block_destroy(block);
	iio_buffer_close(stream);
	iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);
	TEST_MAIN_RETURN();
}
