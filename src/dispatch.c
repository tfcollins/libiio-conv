/* SPDX-License-Identifier: MIT */
#include "internal.h"

const struct conv_backend *conv_dispatch(void)
{
	return &conv_backend_scalar;
}

const char *iio_conv_get_backend_name(void)
{
	return conv_dispatch()->name;
}
