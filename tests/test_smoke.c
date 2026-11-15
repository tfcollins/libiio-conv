/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <iio/iio-conv.h>
#include "test_util.h"

int main(void)
{
	struct iio_conv *c = NULL;

	setenv("IIO_CONV_FORCE", "scalar", 1);
	CHECK(strcmp(iio_conv_get_backend_name(), "scalar") == 0);
	CHECK(iio_conv_create_raw(NULL, 0, 0, NULL, &c) == -EINVAL);
	CHECK(c == NULL);
	TEST_MAIN_RETURN();
}
