/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "channel": channels and channel masks. A channel, its device
 * and its attributes are borrowed children of the context (the same libiio object always has
 * the same id). A mask made by create_channels_mask is an owned child of its device; the mask
 * of a buffer stream is a borrowed, read-only view registered by the buffer group. */
#include "mx_util.h"
#include "data_path.h"

static int channel_get_device(struct gw_call *c)
{
	const struct iio_channel *ch;
	gw_id id;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, &id);

	return ret ? ret : gw_ret_borrowed(c, 0, GW_T_DEVICE, iio_channel_get_device(ch), id);
}

static int channel_get_id(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_channel_get_id(ch));
}

/* char = channel_get_name(ch), channel_get_label(ch); [] when the channel has none */
static int channel_get_name(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_channel_get_name(ch));
}

static int channel_get_label(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_str(c, 0, iio_channel_get_label(ch));
}

static int channel_is_output(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_bool(c, 0, iio_channel_is_output(ch));
}

static int channel_is_scan_element(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_bool(c, 0, iio_channel_is_scan_element(ch));
}

/* n = channel_get_index(ch): the scan index, as it is (negative when not a scan element) */
static int channel_get_index(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : gw_ret_double(c, 0, (double)iio_channel_get_index(ch));
}

/* [name, value]: the C enumerator ('UNKNOWN' when the value has none) and the value */
static int ret_enum(struct gw_call *c, enum gw_enum which, long long value)
{
	const char *name = gw_enum_name(which, value);
	int ret = gw_ret_str(c, 0, name ? name : "UNKNOWN");

	return ret ? ret : gw_ret_double(c, 1, (double)value);
}

static int channel_get_type(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : ret_enum(c, GW_ENUM_CHAN_TYPE, (long long)iio_channel_get_type(ch));
}

static int channel_get_modifier(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : ret_enum(c, GW_ENUM_MODIFIER, (long long)iio_channel_get_modifier(ch));
}

/* the value of channel_get_type under its name in enum hwmon_chan_type */
static int hwmon_channel_get_type_cmd(struct gw_call *c)
{
	const struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : ret_enum(c, GW_ENUM_HWMON_CHAN_TYPE, (long long)hwmon_channel_get_type(ch));
}

/* List form. The result array is created first (zeroed) and the ids are written into it, so
 * nothing is allocated after the first entry was registered. */
static int channel_get_attrs(struct gw_call *c)
{
	const struct iio_channel *ch;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_channel_get_attrs_count(ch);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_ATTR, iio_channel_get_attr(ch, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register attribute %u", i);
	}
	return 0;
}

static int channel_get_event_attrs(struct gw_call *c)
{
	const struct iio_channel *ch;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_channel_get_event_attrs_count(ch);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_ATTR, iio_channel_get_event_attr(ch, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register event attribute %u", i);
	}
	return 0;
}

/* attr or 0 = channel_find_attr(ch, name) */
static int channel_find_attr(struct gw_call *c)
{
	const struct iio_channel *ch;
	const struct iio_attr *attr;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	attr = iio_channel_find_attr(ch, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

static int channel_find_event_attr(struct gw_call *c)
{
	const struct iio_channel *ch;
	const struct iio_attr *attr;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	attr = iio_channel_find_event_attr(ch, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

/* struct = channel_get_data_format(ch): a copy of struct iio_data_format */
static int channel_get_data_format(struct gw_call *c)
{
	static const char *const names[] = { "length", "bits", "shift", "repeat", "is_signed",
		"is_fully_defined", "is_be", "with_scale", "is_float", "scale", "offset" };
	const struct iio_data_format *f;
	const struct iio_channel *ch;
	double numbers[4], reals[2];
	bool flags[5];
	mxArray *s, *v;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);
	if (ret)
		return ret;
	f = iio_channel_get_data_format(ch);
	numbers[0] = (double)f->length;
	numbers[1] = (double)f->bits;
	numbers[2] = (double)f->shift;
	numbers[3] = (double)f->repeat;
	flags[0] = f->is_signed;
	flags[1] = f->is_fully_defined;
	flags[2] = f->is_be;
	flags[3] = f->with_scale;
	flags[4] = f->is_float;
	reals[0] = f->scale;
	reals[1] = f->offset;
	s = mxCreateStructMatrix(1, 1, 11, (const char **)names);
	if (!s)
		return gw_fail(c, -ENOMEM, "out of memory for the result");
	c->plhs[0] = s;
	for (int i = 0; i < 11; i++) {
		if (i < 4)
			v = mxCreateDoubleScalar(numbers[i]);
		else if (i < 9)
			v = mxCreateLogicalScalar(flags[i - 4]);
		else
			v = mxCreateDoubleScalar(reals[i - 9]);
		if (!v)
			return gw_fail(c, -ENOMEM, "out of memory for the result");
		mxSetFieldByNumber(s, 0, i, v);
	}
	return 0;
}

/* channel_refresh_format(ch): the backend reads the format again (iio:ENOSYS where it cannot).
 * No numeric-locale guard: the format text holds integers only (iio_parse_format_string), and
 * the scale and offset of the channel are kept. */
static int channel_refresh_format(struct gw_call *c)
{
	struct iio_channel *ch;
	int ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);

	return ret ? ret : iio_channel_refresh_format(ch);
}

/* u8 = channel_convert(ch, u8), channel_convert_inverse(ch, u8): one libiio call per sample of
 * length/8 * repeat bytes. A channel without a storage format (not a scan element) has no
 * sample size: -EINVAL before anything is divided by it. */
static int convert(struct gw_call *c, bool inverse)
{
	const struct iio_data_format *f;
	const struct iio_channel *ch;
	const uint8_t *src;
	uint8_t *dst;
	size_t n, step;
	mxArray *a;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, &ch, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bytes(c, 1, &src, &n);
	if (ret)
		return ret;
	f = iio_channel_get_data_format(ch);
	step = (size_t)(f->length / 8) * f->repeat;
	if (!step)
		return gw_fail(c, -EINVAL, "the channel has no sample format (it is not a scan element)");
	if (n % step)
		return gw_fail(c, -EINVAL, "%zu bytes are not a multiple of the sample size (%zu bytes)", n, step);
	a = mxCreateUninitNumericMatrix(1, n, mxUINT8_CLASS, mxREAL);
	if (!a)
		return gw_fail(c, -ENOMEM, "out of memory for the result");
	c->plhs[0] = a;
	dst = n ? mxGetData(a) : NULL;
	for (size_t off = 0; off < n; off += step) {
		if (inverse)
			iio_channel_convert_inverse(ch, dst + off, src + off);
		else
			iio_channel_convert(ch, dst + off, src + off);
	}
	return 0;
}

static int channel_convert(struct gw_call *c)
{
	return convert(c, false);
}

static int channel_convert_inverse(struct gw_call *c)
{
	return convert(c, true);
}

/* The channel and the mask of arguments 0 and 1, which must belong to one device: libiio does
 * not check, and a mask of a device with fewer channels is too short for the channel's bit. */
static int chn_and_mask(struct gw_call *c, const struct iio_channel **ch, void *mask, gw_id *mask_id)
{
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CHANNEL, ch, NULL);
	if (ret)
		return ret;
	ret = gw_arg_handle(c, 1, GW_T_MASK, mask, mask_id);
	if (ret)
		return ret;
	if (dp_device_of(*mask_id) != iio_channel_get_device(*ch))
		return gw_fail(c, -EINVAL, "the channel and the mask belong to different devices");
	return 0;
}

/* channel_enable(ch, mask), channel_disable(ch, mask) */
static int set_enabled(struct gw_call *c, bool on)
{
	const struct iio_channel *ch;
	struct iio_channels_mask *mask;
	gw_id mask_id;
	int ret;

	ret = chn_and_mask(c, &ch, &mask, &mask_id);
	if (ret)
		return ret;
	if (!gw_handle_owned(mask_id))
		return gw_fail(c, -EPERM, "the mask of a stream is read-only");
	/* libiio ignores such a channel without a word */
	if (!iio_channel_is_scan_element(ch) || iio_channel_get_index(ch) < 0)
		return gw_fail(c, -EINVAL, "the channel is not a scan element");
	if (on)
		iio_channel_enable(ch, mask);
	else
		iio_channel_disable(ch, mask);
	return 0;
}

static int channel_enable(struct gw_call *c)
{
	return set_enabled(c, true);
}

static int channel_disable(struct gw_call *c)
{
	return set_enabled(c, false);
}

/* tf = channel_is_enabled(ch, mask) */
static int channel_is_enabled(struct gw_call *c)
{
	const struct iio_channels_mask *mask;
	const struct iio_channel *ch;
	gw_id mask_id;
	int ret;

	ret = chn_and_mask(c, &ch, &mask, &mask_id);
	return ret ? ret : gw_ret_bool(c, 0, iio_channel_is_enabled(ch, mask));
}

/* mask = create_channels_mask(dev): a mask with one bit per channel of the device, all clear */
static int create_channels_mask(struct gw_call *c)
{
	const struct iio_device *dev;
	struct iio_channels_mask *mask;
	unsigned int n;
	gw_id dev_id, id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &dev_id);
	if (ret)
		return ret;
	n = iio_device_get_channels_count(dev);
	if (!n)
		return gw_fail(c, -EINVAL, "the device has no channels");
	/* Result first: no mx allocation may follow the creation of the mask. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	mask = iio_create_channels_mask(n);
	if (!mask)
		return gw_fail(c, -ENOMEM, "out of memory for the mask");
	id = dp_add_mask(mask, dev_id);
	if (!id) {
		iio_channels_mask_destroy(mask);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* channels_mask_destroy(id): silent for a zero or stale id and for the mask of a stream */
static int channels_mask_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_MASK);
}

/* tf = channels_mask_get(mask): 1-by-N logical, one element per channel of the device */
static int channels_mask_get(struct gw_call *c)
{
	const struct iio_channels_mask *mask;
	const struct iio_device *dev;
	mxLogical *bits;
	unsigned int n;
	mxArray *a;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_MASK, &mask, &id);
	if (ret)
		return ret;
	dev = dp_device_of(id);
	if (!dev)
		return gw_fail(c, -EBADF, "the mask has no device");
	n = iio_device_get_channels_count(dev);
	a = mxCreateLogicalMatrix(1, n);
	if (!a)
		return gw_fail(c, -ENOMEM, "out of memory for the result");
	c->plhs[0] = a;
	bits = n ? mxGetLogicals(a) : NULL;
	for (unsigned int i = 0; i < n; i++)
		bits[i] = iio_channel_is_enabled(iio_device_get_channel(dev, i), mask);
	return 0;
}

static const struct gw_cmd cmds[] = {
	GW_CMD("channel_get_device", channel_get_device, 1, 1, 1),
	GW_CMD("channel_get_id", channel_get_id, 1, 1, 1),
	GW_CMD("channel_get_name", channel_get_name, 1, 1, 1),
	GW_CMD("channel_get_label", channel_get_label, 1, 1, 1),
	GW_CMD("channel_is_output", channel_is_output, 1, 1, 1),
	GW_CMD("channel_is_scan_element", channel_is_scan_element, 1, 1, 1),
	GW_CMD("channel_get_index", channel_get_index, 1, 1, 1),
	GW_CMD("channel_get_type", channel_get_type, 1, 1, 2),
	GW_CMD("channel_get_modifier", channel_get_modifier, 1, 1, 2),
	GW_CMD("hwmon_channel_get_type", hwmon_channel_get_type_cmd, 1, 1, 2),
	GW_CMD("channel_get_attrs", channel_get_attrs, 1, 1, 1),
	GW_CMD("channel_get_event_attrs", channel_get_event_attrs, 1, 1, 1),
	GW_CMD("channel_find_attr", channel_find_attr, 2, 2, 1),
	GW_CMD("channel_find_event_attr", channel_find_event_attr, 2, 2, 1),
	GW_CMD("channel_get_data_format", channel_get_data_format, 1, 1, 1),
	GW_CMD("channel_refresh_format", channel_refresh_format, 1, 1, 0),
	GW_CMD("channel_convert", channel_convert, 2, 2, 1),
	GW_CMD("channel_convert_inverse", channel_convert_inverse, 2, 2, 1),
	GW_CMD("channel_enable", channel_enable, 2, 2, 0),
	GW_CMD("channel_disable", channel_disable, 2, 2, 0),
	GW_CMD("channel_is_enabled", channel_is_enabled, 2, 2, 1),
	GW_CMD("create_channels_mask", create_channels_mask, 1, 1, 1),
	GW_CMD("channels_mask_destroy", channels_mask_destroy, 1, 1, 0),
	GW_XCMD("channels_mask_get", channels_mask_get, 1, 1, 1),
};

const struct gw_group gw_group_channel = GW_GROUP("channel", cmds);
