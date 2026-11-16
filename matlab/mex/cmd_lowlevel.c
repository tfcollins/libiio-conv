/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "lowlevel": sample size of a channels mask, trigger, hardware
 * registers, debug and event attributes of a device. Every id returned here is a borrowed child
 * of the device's context: the same libiio object always has the same id. */
#include "mx_util.h"
#include "data_path.h"

#include <stdint.h>

/* The error of a negative ssize_t result of libiio: a value outside the errno range would be
 * another number, or 0, as an int. */
static int io_err(ssize_t v)
{
	return v < -4095 ? -EIO : (int)v;
}

/* n = device_get_sample_size(dev, mask): bytes of one sample with the channels of mask enabled.
 * libiio compares only the word count of the mask with the device: with the mask of another
 * device it answers 0 or counts the wrong channels. */
static int device_get_sample_size(struct gw_call *c)
{
	const struct iio_device *dev;
	const struct iio_channels_mask *mask;
	gw_id mask_id;
	ssize_t size;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);
	if (ret)
		return ret;
	ret = gw_arg_handle(c, 1, GW_T_MASK, &mask, &mask_id);
	if (ret)
		return ret;
	if (dp_device_of(mask_id) != dev)
		return gw_fail(c, -EINVAL, "the mask belongs to another device");
	size = iio_device_get_sample_size(dev, mask);
	if (size < 0)
		return io_err(size);
	return gw_ret_double(c, 0, (double)size);
}

/* trig = device_get_trigger(dev): the id of the trigger device, 0 when none is assigned
 * (-ENODEV). The pointer libiio returns is error-encoded, never NULL on failure. */
static int device_get_trigger(struct gw_call *c)
{
	const struct iio_device *dev, *trig;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	trig = iio_device_get_trigger(dev);
	ret = iio_err(trig);
	if (ret == -ENODEV)
		trig = NULL;
	else if (ret)
		return ret;
	/* a backend must name a device of this context: an id under this context for any other
	 * pointer would outlive the object it stands for */
	if (trig && iio_device_get_context(trig) != iio_device_get_context(dev))
		return gw_fail(c, -EIO, "the backend returned a trigger of another context");
	return gw_ret_borrowed(c, 0, GW_T_DEVICE, trig, id);
}

/* device_set_trigger(dev, trig): trig is a trigger device of the same context, or 0 to remove
 * the trigger. libiio refuses a device that is no trigger (-EINVAL) but would hand a device of
 * another context to the backend. */
static int device_set_trigger(struct gw_call *c)
{
	const struct iio_device *dev, *trig;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);
	if (ret)
		return ret;
	ret = gw_arg_handle0(c, 1, GW_T_DEVICE, &trig, NULL);
	if (ret)
		return ret;
	if (trig && iio_device_get_context(trig) != iio_device_get_context(dev))
		return gw_fail(c, -EINVAL, "the trigger belongs to another context");
	return iio_device_set_trigger(dev, trig);
}

/* Register address or value: a real, integer-valued scalar in 0 .. 4294967295 */
static int arg_u32(struct gw_call *c, int k, uint32_t *v)
{
	long long x = 0;
	int ret = gw_arg_int(c, k, 0, UINT32_MAX, &x);

	if (!ret)
		*v = (uint32_t)x;
	return ret;
}

/* gw_c_numeric_enter() with the error text. Without register operations of the backend libiio
 * reads and writes a register through iio_attr_write_string / iio_attr_write_longlong on the
 * debug attribute direct_reg_access: the attribute writes that are bracketed everywhere. */
static int c_numeric(struct gw_call *c, struct gw_c_numeric *loc)
{
	int ret = gw_c_numeric_enter(loc);

	return ret ? gw_fail(c, ret, "no \"C\" numeric locale") : 0;
}

/* value = device_reg_read(dev, addr): uint32. The result array exists before the device is
 * touched (the attribute fallback of libiio writes the address): nothing can fail afterwards. */
static int device_reg_read(struct gw_call *c)
{
	struct iio_device *dev;
	struct gw_c_numeric loc;
	uint32_t addr, value = 0;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);
	if (ret)
		return ret;
	ret = arg_u32(c, 1, &addr);
	if (ret)
		return ret;
	c->plhs[0] = mxCreateNumericMatrix(1, 1, mxUINT32_CLASS, mxREAL);
	if (!c->plhs[0] || !mxGetData(c->plhs[0]))
		return gw_fail(c, -ENOMEM, "out of memory");
	ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	ret = iio_device_reg_read(dev, addr, &value);
	gw_c_numeric_leave(&loc);
	if (ret)
		return ret > 0 ? -EIO : ret;
	*(uint32_t *)mxGetData(c->plhs[0]) = value;
	return 0;
}

/* device_reg_write(dev, addr, value) */
static int device_reg_write(struct gw_call *c)
{
	struct iio_device *dev;
	struct gw_c_numeric loc;
	uint32_t addr, value;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, NULL);
	if (ret)
		return ret;
	ret = arg_u32(c, 1, &addr);
	if (ret)
		return ret;
	ret = arg_u32(c, 2, &value);
	if (ret)
		return ret;
	ret = c_numeric(c, &loc);
	if (ret)
		return ret;
	ret = iio_device_reg_write(dev, addr, value);
	gw_c_numeric_leave(&loc);
	return ret > 0 ? -EIO : ret;
}

/* List form of the debug (event) attributes. The result array is created first (zeroed) and
 * the ids are written into it, so nothing is allocated after the first entry was registered. */
static int attr_list(struct gw_call *c, bool debug)
{
	const struct iio_device *dev;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = debug ? iio_device_get_debug_attrs_count(dev) : iio_device_get_event_attrs_count(dev);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		const struct iio_attr *attr = debug ? iio_device_get_debug_attr(dev, i) :
			iio_device_get_event_attr(dev, i);

		ret = gw_handle_borrow(GW_T_ATTR, attr, root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register %s attribute %u", debug ? "debug" : "event", i);
	}
	return 0;
}

static int device_get_debug_attrs(struct gw_call *c)
{
	return attr_list(c, true);
}

static int device_get_event_attrs(struct gw_call *c)
{
	return attr_list(c, false);
}

/* attr or 0 = device_find_debug_attr(dev, name), device_find_event_attr(dev, name) */
static int attr_find(struct gw_call *c, bool debug)
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
	attr = debug ? iio_device_find_debug_attr(dev, name) : iio_device_find_event_attr(dev, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

static int device_find_debug_attr(struct gw_call *c)
{
	return attr_find(c, true);
}

static int device_find_event_attr(struct gw_call *c)
{
	return attr_find(c, false);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("device_get_sample_size", device_get_sample_size, 2, 2, 1),
	GW_CMD("device_get_trigger", device_get_trigger, 1, 1, 1),
	GW_CMD("device_set_trigger", device_set_trigger, 2, 2, 0),
	GW_CMD("device_reg_read", device_reg_read, 2, 2, 1),
	GW_CMD("device_reg_write", device_reg_write, 3, 3, 0),
	GW_CMD("device_get_debug_attrs", device_get_debug_attrs, 1, 1, 1),
	GW_CMD("device_get_event_attrs", device_get_event_attrs, 1, 1, 1),
	GW_CMD("device_find_debug_attr", device_find_debug_attr, 2, 2, 1),
	GW_CMD("device_find_event_attr", device_find_event_attr, 2, 2, 1),
};

const struct gw_group gw_group_lowlevel = GW_GROUP("lowlevel", cmds);
