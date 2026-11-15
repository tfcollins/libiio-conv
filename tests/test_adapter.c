/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "adapter_util.h"

int main(int argc, char **argv)
{
	static const char *const ids[5] = { "voltage0_i", "voltage0_q", "voltage1", "temp", "voltage2" };
	static const size_t offs_all[5] = { 0, 2, 4, 8, 10 };
	struct iio_conv_spec i64 = { .type = IIO_CONV_I64 };
	struct iio_conv_spec f64 = { .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_IIO };
	const struct iio_channel *ch[5], *two[2], *tx0, *raw_only;
	struct iio_channels_mask *mask;
	const struct iio_device *rx, *tx;
	struct iio_context *ctx;
	struct iio_conv *c = NULL;
	unsigned int i;

	if (argc < 2)
		return 2;
	ctx = open_ctx("xml", argv[1]);
	CHECK(ctx != NULL);
	if (!ctx)
		TEST_MAIN_RETURN();

	rx = iio_context_find_device(ctx, "conv-rx");
	tx = iio_context_find_device(ctx, "conv-tx");
	CHECK(rx && tx);
	for (i = 0; i < 5; i++) {
		ch[i] = find_chan(rx, ids[i], false);
		CHECK(ch[i] != NULL);
	}
	tx0 = find_chan(tx, "voltage0", true);
	raw_only = find_chan(rx, "voltage9", false);
	CHECK(tx0 && raw_only);

	/* all five enabled, all five requested */
	mask = iio_create_channels_mask(iio_device_get_channels_count(rx));
	for (i = 0; i < 5; i++)
		iio_channel_enable(ch[i], mask);
	CHECK(iio_conv_create(mask, ch, 5, &i64, &c) == 0);
	if (c) {
		CHECK(iio_device_get_sample_size(rx, mask) == 12);
		CHECK(iio_conv_get_step(c) == (size_t)iio_device_get_sample_size(rx, mask));
		for (i = 0; i < 5; i++)
			CHECK(iio_conv_test_get_plan(c, i)->src_off == offs_all[i]);
		CHECK(iio_conv_test_get_plan(c, 2)->swap);
		CHECK(iio_conv_test_get_plan(c, 2)->bits == 24);
		CHECK(iio_conv_test_get_plan(c, 2)->shift == 8);
		CHECK(!iio_conv_test_get_plan(c, 2)->is_signed);
		CHECK(iio_conv_test_get_plan(c, 4)->is_signed);
	}
	iio_conv_destroy(c); c = NULL;

	/* all five enabled, two requested out of scan order */
	two[0] = ch[4]; two[1] = ch[0];
	CHECK(iio_conv_create(mask, two, 2, &i64, &c) == 0);
	if (c) {
		CHECK(iio_conv_get_step(c) == 12);
		CHECK(iio_conv_test_get_plan(c, 0)->src_off == 10);
		CHECK(iio_conv_test_get_plan(c, 1)->src_off == 0);
	}
	iio_conv_destroy(c); c = NULL;

	/* IIO scale reaches the plan */
	CHECK(iio_conv_create(mask, &ch[4], 1, &f64, &c) == 0);
	if (c)
		CHECK(iio_conv_test_get_plan(c, 0)->a == 0.25);
	iio_conv_destroy(c); c = NULL;

	/* wrong device, not a scan element */
	two[0] = ch[0]; two[1] = tx0;
	CHECK(iio_conv_create(mask, two, 2, &i64, &c) == -EINVAL);
	CHECK(iio_conv_create(mask, &raw_only, 1, &i64, &c) == -EINVAL);
	CHECK(iio_conv_create(mask, ch, 0, &i64, &c) == -EINVAL);
	CHECK(iio_conv_create(NULL, ch, 1, &i64, &c) == -EINVAL);
	iio_channels_mask_destroy(mask);

	/* only temp and voltage2 enabled */
	mask = iio_create_channels_mask(iio_device_get_channels_count(rx));
	iio_channel_enable(ch[3], mask);
	iio_channel_enable(ch[4], mask);
	CHECK(iio_conv_create(mask, &ch[3], 2, &i64, &c) == 0);
	if (c) {
		CHECK(iio_conv_get_step(c) == 4);
		CHECK(iio_conv_test_get_plan(c, 0)->src_off == 0);
		CHECK(iio_conv_test_get_plan(c, 1)->src_off == 2);
	}
	iio_conv_destroy(c); c = NULL;

	/* requested channel not enabled */
	CHECK(iio_conv_create(mask, &ch[0], 1, &i64, &c) == -EINVAL);
	CHECK(c == NULL);
	iio_channels_mask_destroy(mask);

	iio_context_destroy(ctx);
	TEST_MAIN_RETURN();
}
