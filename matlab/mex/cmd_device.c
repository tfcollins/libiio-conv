/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "device". Every id returned here is a borrowed child of the
 * device's context: the same libiio object always has the same id. */
#include "mx_util.h"

/* ctx = device_get_context(dev): the id under which the gateway holds the device's context */
static int device_get_context(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_id(c, 0, gw_handle_find(GW_T_CONTEXT, iio_device_get_context(dev)));
}

static int device_get_id(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_device_get_id(dev));
}

/* char = device_get_name(dev), device_get_label(dev); [] when the device has none */
static int device_get_name(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_device_get_name(dev));
}

static int device_get_label(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_device_get_label(dev));
}

/* List form. The result array is created first (zeroed) and the ids are written into it, so
 * nothing is allocated after the first entry was registered. */
static int device_get_channels(struct gw_call *c)
{
	const struct iio_device *dev;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_device_get_channels_count(dev);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_CHANNEL, iio_device_get_channel(dev, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register channel %u", i);
	}
	return 0;
}

static int device_get_attrs(struct gw_call *c)
{
	const struct iio_device *dev;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_device_get_attrs_count(dev);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_ATTR, iio_device_get_attr(dev, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register attribute %u", i);
	}
	return 0;
}

static int device_get_buffers(struct gw_call *c)
{
	const struct iio_device *dev;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_device_get_buffers_count(dev);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_BUFFER, iio_device_get_buffer(dev, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register buffer %u", i);
	}
	return 0;
}

/* ch or 0 = device_find_channel(dev, name, output): name is an id, a label or a name */
static int device_find_channel(struct gw_call *c)
{
	const struct iio_device *dev;
	const struct iio_channel *ch;
	bool output;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	ret = gw_arg_bool(c, 2, &output);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	ch = iio_device_find_channel(dev, name, output);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_CHANNEL, ch, id);
}

/* attr or 0 = device_find_attr(dev, name): plain device attributes only */
static int device_find_attr(struct gw_call *c)
{
	const struct iio_device *dev;
	const struct iio_attr *attr;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	attr = iio_device_find_attr(dev, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

static int device_is_trigger(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_bool(c, 0, iio_device_is_trigger(dev));
}

static int device_is_hwmon(struct gw_call *c)
{
	const struct iio_device *dev;
	int ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);

	return ret ? ret : gw_ret_bool(c, 0, iio_device_is_hwmon(dev));
}

static const struct gw_cmd cmds[] = {
	GW_CMD("device_get_context", device_get_context, 1, 1, 1),
	GW_CMD("device_get_id", device_get_id, 1, 1, 1),
	GW_CMD("device_get_name", device_get_name, 1, 1, 1),
	GW_CMD("device_get_label", device_get_label, 1, 1, 1),
	GW_CMD("device_get_channels", device_get_channels, 1, 1, 1),
	GW_CMD("device_get_attrs", device_get_attrs, 1, 1, 1),
	GW_CMD("device_get_buffers", device_get_buffers, 1, 1, 1),
	GW_CMD("device_find_channel", device_find_channel, 3, 3, 1),
	GW_CMD("device_find_attr", device_find_attr, 2, 2, 1),
	GW_CMD("device_is_trigger", device_is_trigger, 1, 1, 1),
	GW_CMD("device_is_hwmon", device_is_hwmon, 1, 1, 1),
};

const struct gw_group gw_group_device = GW_GROUP("device", cmds);
