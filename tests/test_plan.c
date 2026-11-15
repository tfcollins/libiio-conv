/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <math.h>
#include "internal.h"
#include "test_util.h"

static const struct iio_conv_chan D16 = {
	.length = 16, .bits = 12, .shift = 0, .is_signed = true, .is_be = false,
	.with_scale = true, .scale = 0.5, .offset = 3.0, .byte_offset = 0,
};

static int create1(struct iio_conv_chan ch, size_t step, struct iio_conv_spec spec,
		struct iio_conv **c)
{
	*c = NULL;
	return iio_conv_create_raw(&ch, 1, step, &spec, c);
}

static void test_errors(void)
{
	struct iio_conv_spec f64 = { .type = IIO_CONV_F64 };
	struct iio_conv_spec spec;
	struct iio_conv_chan ch, three[3] = { D16, D16, D16 };
	struct iio_conv *c;
	double gain = 2.0;

	ch = D16; ch.length = 24;
	CHECK(create1(ch, 4, f64, &c) == -ENOTSUP);
	ch = D16; ch.bits = 0;
	CHECK(create1(ch, 2, f64, &c) == -EINVAL);
	ch = D16; ch.shift = 5; /* 12 + 5 > 16 */
	CHECK(create1(ch, 2, f64, &c) == -EINVAL);
	ch = D16; ch.byte_offset = 1;
	CHECK(create1(ch, 2, f64, &c) == -EINVAL);

	/* values chosen to wrap the range checks */
	ch = D16; ch.bits = 0xFFFFFFFFu; ch.shift = 2;
	CHECK(create1(ch, 2, f64, &c) == -EINVAL);
	ch = D16; ch.byte_offset = SIZE_MAX - 1;
	CHECK(create1(ch, 2, f64, &c) == -EINVAL);
	/* constants that are not finite, or vanish in the host precision */
	{
		double inf = INFINITY, tiny = 1e-60, huge = 1e300;
		struct iio_conv_spec sp = f64;

		sp.user_offset = &inf;
		CHECK(create1(D16, 2, sp, &c) == -EINVAL);
		sp = f64; sp.scale = IIO_CONV_SCALE_IIO; sp.user_gain = &huge;
		ch = D16; ch.scale = 1e300;
		CHECK(create1(ch, 2, sp, &c) == -EINVAL);
		sp = (struct iio_conv_spec){ .type = IIO_CONV_F32, .user_gain = &tiny };
		CHECK(create1(D16, 2, sp, &c) == -EINVAL);
		sp.type = IIO_CONV_F64; /* representable as a double */
		CHECK(create1(D16, 2, sp, &c) == 0);
		iio_conv_destroy(c);
	}

	three[1].byte_offset = 2; three[2].byte_offset = 4;
	spec = f64; spec.complex_iq = true;
	c = NULL;
	CHECK(iio_conv_create_raw(three, 3, 6, &spec, &c) == -EINVAL);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_I16, .scale = IIO_CONV_SCALE_IIO };
	CHECK(create1(D16, 2, spec, &c) == -EINVAL);
	spec = (struct iio_conv_spec){ .type = IIO_CONV_I16, .user_gain = &gain };
	CHECK(create1(D16, 2, spec, &c) == -EINVAL);

	c = NULL;
	CHECK(iio_conv_create_raw(&D16, 0, 2, &f64, &c) == -EINVAL);
	CHECK(c == NULL);
}

static void test_constants(void)
{
	struct iio_conv_spec spec = { .type = IIO_CONV_F64, .scale = IIO_CONV_SCALE_IIO };
	struct iio_conv_chan ch;
	struct iio_conv *c;
	double gain = 4.0, off = 1.0;

	CHECK(create1(D16, 2, spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 0)->a == 0.5);
		CHECK(iio_conv_test_get_plan(c, 0)->b == 1.5);
		CHECK(iio_conv_test_get_plan(c, 0)->inv_a == 2.0);
		CHECK(iio_conv_get_step(c) == 2);
		CHECK(iio_conv_get_host_step(c) == 8);
	}
	iio_conv_destroy(c);

	ch = D16; ch.with_scale = false; /* scale ignored, offset still applies */
	CHECK(create1(ch, 2, spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 0)->a == 1.0);
		CHECK(iio_conv_test_get_plan(c, 0)->b == 3.0);
	}
	iio_conv_destroy(c);

	spec.scale = IIO_CONV_SCALE_FULLSCALE;
	CHECK(create1(D16, 2, spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 0)->a == 1.0 / 2048);
		CHECK(iio_conv_test_get_plan(c, 0)->b == 0.0);
		CHECK(iio_conv_test_get_plan(c, 0)->lo == -2048);
		CHECK(iio_conv_test_get_plan(c, 0)->hi == 2047);
	}
	iio_conv_destroy(c);

	ch = D16; ch.is_signed = false;
	CHECK(create1(ch, 2, spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 0)->a == 1.0 / 4096);
		CHECK(iio_conv_test_get_plan(c, 0)->lo == 0);
		CHECK(iio_conv_test_get_plan(c, 0)->hi == 4095);
	}
	iio_conv_destroy(c);

	spec.scale = IIO_CONV_SCALE_IIO;
	spec.user_gain = &gain; spec.user_offset = &off;
	CHECK(create1(D16, 2, spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 0)->a == 2.0);
		CHECK(iio_conv_test_get_plan(c, 0)->b == 7.0);
		CHECK(iio_conv_test_get_plan(c, 0)->affine);
	}
	iio_conv_destroy(c);

	spec = (struct iio_conv_spec){ .type = IIO_CONV_I16 };
	CHECK(create1(D16, 2, spec, &c) == 0);
	if (c)
		CHECK(!iio_conv_test_get_plan(c, 0)->affine);
	iio_conv_destroy(c);
}

static void test_layout(void)
{
	struct iio_conv_chan chans[4] = { D16, D16, D16, D16 };
	struct iio_conv_spec spec = { .type = IIO_CONV_F32 };
	struct iio_conv *c = NULL;
	unsigned int i;

	for (i = 0; i < 4; i++)
		chans[i].byte_offset = 2 * i;

	spec.layout = IIO_CONV_COLUMNS;
	CHECK(iio_conv_create_raw(chans, 4, 8, &spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_get_host_step(c) == 16);
		CHECK(iio_conv_test_get_plan(c, 2)->dst_stride == 4);
		CHECK(iio_conv_test_get_plan(c, 2)->dst_off == 0);
		CHECK(iio_conv_test_get_plan(c, 2)->src_off == 4);
	}
	iio_conv_destroy(c); c = NULL;

	spec.layout = IIO_CONV_INTERLEAVED;
	CHECK(iio_conv_create_raw(chans, 4, 8, &spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 2)->dst_off == 8);
		CHECK(iio_conv_test_get_plan(c, 2)->dst_stride == 16);
	}
	iio_conv_destroy(c); c = NULL;

	spec.layout = IIO_CONV_COLUMNS;
	spec.complex_iq = true;
	CHECK(iio_conv_create_raw(chans, 4, 8, &spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_get_host_step(c) == 16);
		CHECK(iio_conv_test_get_plan(c, 1)->dst_off == 4);
		CHECK(iio_conv_test_get_plan(c, 1)->dst_stride == 8);
		CHECK(iio_conv_test_get_plan(c, 2)->dst_off == 0);
	}
	iio_conv_destroy(c); c = NULL;

	spec.layout = IIO_CONV_INTERLEAVED;
	CHECK(iio_conv_create_raw(chans, 4, 8, &spec, &c) == 0);
	if (c) {
		CHECK(iio_conv_test_get_plan(c, 3)->dst_off == 12);
		CHECK(iio_conv_test_get_plan(c, 3)->dst_stride == 16);
	}
	iio_conv_destroy(c);
}

static void test_swap(void)
{
	const uint16_t probe = 1;
	bool host_le = *(const uint8_t *)&probe == 1;
	struct iio_conv_spec spec = { .type = IIO_CONV_I16 };
	struct iio_conv_chan ch;
	struct iio_conv *c;

	ch = D16; ch.is_be = true;
	CHECK(create1(ch, 2, spec, &c) == 0);
	if (c) CHECK(iio_conv_test_get_plan(c, 0)->swap == host_le);
	iio_conv_destroy(c);
	ch = D16; ch.is_be = false;
	CHECK(create1(ch, 2, spec, &c) == 0);
	if (c) CHECK(iio_conv_test_get_plan(c, 0)->swap == !host_le);
	iio_conv_destroy(c);
	ch = D16; ch.is_be = true; ch.length = 8; ch.bits = 8;
	CHECK(create1(ch, 1, spec, &c) == 0);
	if (c) CHECK(!iio_conv_test_get_plan(c, 0)->swap);
	iio_conv_destroy(c);
}

int main(void)
{
	test_errors();
	test_constants();
	test_layout();
	test_swap();
	TEST_MAIN_RETURN();
}
