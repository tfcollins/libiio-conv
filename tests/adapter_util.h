/* SPDX-License-Identifier: MIT */
#ifndef ADAPTER_UTIL_H
#define ADAPTER_UTIL_H

#include <iio/iio.h>
#include "internal.h"
#include "test_util.h"

static inline struct iio_context *open_ctx(const char *backend, const char *xml)
{
	char uri[1024];
	struct iio_context *ctx;

	snprintf(uri, sizeof(uri), "%s:%s", backend, xml);
	ctx = iio_create_context(NULL, uri);
	return iio_err(ctx) ? NULL : ctx;
}

static inline const struct iio_channel *find_chan(const struct iio_device *dev, const char *id,
		bool output)
{
	return iio_device_find_channel(dev, id, output);
}

#endif
