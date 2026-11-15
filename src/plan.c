/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <errno.h>

int iio_conv_create_raw(const struct iio_conv_chan *chans, unsigned int nb_chans,
		size_t step, const struct iio_conv_spec *spec, struct iio_conv **conv)
{
	(void)step;
	if (!chans || !nb_chans || !spec || !conv)
		return -EINVAL;
	return -ENOSYS;
}
