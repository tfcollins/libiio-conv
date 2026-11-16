/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "event": event streams of a device, and the decoding of an
 * event id. An event is passed as its 64-bit id (uint64); nothing here keeps an event. */
#include "mx_util.h"

#include <stdint.h>

static void event_stream_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_event_stream_destroy(ptr);
}

/* es = device_create_event_stream(dev): an owned child of the device */
static int device_create_event_stream(struct gw_call *c)
{
	static const struct gw_ops ops = { NULL, event_stream_destroy_op };
	const struct iio_device *dev;
	struct iio_event_stream *es;
	gw_id dev_id, id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &dev_id);
	if (ret)
		return ret;
	/* Result first: no mx allocation may follow the creation of the stream. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	es = iio_device_create_event_stream(dev);
	ret = iio_err(es);              /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = gw_handle_add(GW_T_EVENT_STREAM, es, dev_id, true, &ops, NULL);
	if (!id) {
		iio_event_stream_destroy(es);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* event_stream_destroy(id) */
static int event_stream_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_EVENT_STREAM);
}

/* [tf, id, timestamp] = event_stream_read(es, nonblock): tf false, id 0 and timestamp 0 when
 * there is no event (-EBUSY, -EAGAIN, -ETIMEDOUT). The three results are created first: a read
 * takes the event out of the stream, so no allocation may fail after it. */
static int event_stream_read(struct gw_call *c)
{
	struct iio_event_stream *es;
	struct iio_event ev = { 0, 0 };
	bool nonblock;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_EVENT_STREAM, &es, NULL);
	if (ret)
		return ret;
	ret = gw_arg_bool(c, 1, &nonblock);
	if (ret)
		return ret;
	ret = gw_ret_bool(c, 0, false);
	if (!ret)
		ret = gw_ret_uint64(c, 1, 0);
	if (!ret)
		ret = gw_ret_int64(c, 2, 0);
	if (ret)
		return ret;
	ret = iio_event_stream_read(es, &ev, nonblock);
	if (ret == -EBUSY || ret == -EAGAIN || ret == -ETIMEDOUT)
		return 0;
	if (ret < 0)
		return ret;
	*mxGetLogicals(c->plhs[0]) = true;
	if (c->nlhs > 1)
		*(uint64_t *)mxGetData(c->plhs[1]) = ev.id;
	if (c->nlhs > 2)
		*(int64_t *)mxGetData(c->plhs[2]) = ev.timestamp;
	return 0;
}

/* The event id of argument k as a struct iio_event. */
static int arg_event(struct gw_call *c, int k, struct iio_event *ev)
{
	gw_id id = 0;
	int ret = gw_arg_id(c, k, &id);    /* a real uint64 scalar; an event id is no handle */

	ev->id = id;
	ev->timestamp = 0;
	return ret;
}

/* [name, value] = cmd(id): the C enumerator, or 'UNKNOWN' for a value that has none */
static int ret_enum(struct gw_call *c, enum gw_enum which, long long value)
{
	const char *name = gw_enum_name(which, value);
	int ret = gw_ret_str(c, 0, name ? name : "UNKNOWN");

	return ret ? ret : gw_ret_double(c, 1, (double)value);
}

/* [name, value] = event_get_type(id) */
static int event_get_type(struct gw_call *c)
{
	struct iio_event ev;
	int ret = arg_event(c, 0, &ev);

	return ret ? ret : ret_enum(c, GW_ENUM_EVENT_TYPE, (long long)iio_event_get_type(&ev));
}

/* [name, value] = event_get_direction(id) */
static int event_get_direction(struct gw_call *c)
{
	struct iio_event ev;
	int ret = arg_event(c, 0, &ev);

	return ret ? ret : ret_enum(c, GW_ENUM_EVENT_DIRECTION, (long long)iio_event_get_direction(&ev));
}

/* Is chn the channel that the event names? libiio returns the LAST channel of the device when
 * none matches (its search loop ends with the pointer still set), so its answer is checked
 * against the event id: channel type (bits 39..32), modifier (bits 47..40) and the decimal
 * number after the leading letters of the channel id (0 when there is none), which must be the
 * channel number of the event: bits 15..0, or bits 31..16 for the second channel of a
 * differential event. */
static bool event_names_channel(const struct iio_event *ev, const struct iio_channel *chn, bool diff)
{
	const char *p = iio_channel_get_id(chn);
	uint16_t want = (uint16_t)(ev->id >> (diff ? 16 : 0));
	unsigned long number = 0;

	if ((unsigned int)iio_channel_get_type(chn) != (unsigned int)((ev->id >> 32) & 0xff) ||
	    (unsigned int)iio_channel_get_modifier(chn) != (unsigned int)((ev->id >> 40) & 0xff))
		return false;
	while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
		p++;
	for (; *p >= '0' && *p <= '9'; p++) {
		number = number * 10 + (unsigned long)(*p - '0');
		if (number > UINT16_MAX)
			return false;
	}
	return number == want;
}

/* ch or 0 = event_get_channel(id, dev, diff): the channel of the device that the event is about;
 * with diff true the second channel of a differential event */
static int event_get_channel(struct gw_call *c)
{
	const struct iio_device *dev;
	const struct iio_channel *chn;
	struct iio_event ev;
	gw_id dev_id;
	bool diff;
	int ret;

	ret = arg_event(c, 0, &ev);
	if (ret)
		return ret;
	ret = gw_arg_handle(c, 1, GW_T_DEVICE, &dev, &dev_id);
	if (ret)
		return ret;
	ret = gw_arg_bool(c, 2, &diff);
	if (ret)
		return ret;
	chn = iio_event_get_channel(&ev, dev, diff);
	if (chn && !event_names_channel(&ev, chn, diff))
		chn = NULL;
	return gw_ret_borrowed(c, 0, GW_T_CHANNEL, chn, dev_id);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("device_create_event_stream", device_create_event_stream, 1, 1, 1),
	GW_CMD("event_stream_destroy", event_stream_destroy, 1, 1, 0),
	GW_CMD("event_stream_read", event_stream_read, 2, 2, 3),
	GW_CMD("event_get_type", event_get_type, 1, 1, 2),
	GW_CMD("event_get_direction", event_get_direction, 1, 1, 2),
	GW_CMD("event_get_channel", event_get_channel, 3, 3, 1),
};

const struct gw_group gw_group_event = GW_GROUP("event", cmds);
