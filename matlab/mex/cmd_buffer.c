/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "buffer": what a buffer is, and the life of a buffer stream
 * (iio_buffer_open ... iio_buffer_close). A buffer, its attributes and its scan elements are
 * borrowed children of the context. A buffer stream is an owned child of its buffer; its
 * blocks and the read-only view of its mask are its children, so closing it (or destroying
 * the context) releases them first: cancel, stop if started, blocks, mask view, close
 * (dp_add_bufstream() of data_path.h). The flag BS_STARTED of a buffer stream entry is
 * written here and nowhere else. */
#include "mx_util.h"
#include "data_path.h"

#include <limits.h>

/* dev = buffer_get_device(buf) */
static int buffer_get_device(struct gw_call *c)
{
	const struct iio_buffer *buf;
	gw_id id;
	int ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &id);

	return ret ? ret : gw_ret_borrowed(c, 0, GW_T_DEVICE, iio_buffer_get_device(buf), id);
}

static int buffer_is_output(struct gw_call *c)
{
	const struct iio_buffer *buf;
	int ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, NULL);

	return ret ? ret : gw_ret_bool(c, 0, iio_buffer_is_output(buf));
}

/* List form. The result array is created first (zeroed) and the ids are written into it, so
 * nothing is allocated after the first entry was registered. */
static int buffer_get_attrs(struct gw_call *c)
{
	const struct iio_buffer *buf;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_buffer_get_attrs_count(buf);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_ATTR, iio_buffer_get_attr(buf, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register attribute %u", i);
	}
	return 0;
}

/* attr or 0 = buffer_find_attr(buf, name) */
static int buffer_find_attr(struct gw_call *c)
{
	const struct iio_buffer *buf;
	const struct iio_attr *attr;
	char *name;
	gw_id id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &id);
	if (ret)
		return ret;
	ret = gw_arg_str(c, 1, &name);
	if (ret)
		return ret;
	attr = iio_buffer_find_attr(buf, name);
	mxFree(name);
	return gw_ret_borrowed(c, 0, GW_T_ATTR, attr, id);
}

/* The scan elements are channels of the buffer's device: the ids are those of device_get_channels. */
static int buffer_get_scan_elements(struct gw_call *c)
{
	const struct iio_buffer *buf;
	unsigned int n;
	gw_id id, root, *ids;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &id);
	if (ret)
		return ret;
	root = gw_handle_root(id);
	n = iio_buffer_get_scan_elements_count(buf);
	ret = gw_ret_ids(c, 0, NULL, n);
	if (ret)
		return ret;
	ids = n ? mxGetData(c->plhs[0]) : NULL;
	for (unsigned int i = 0; i < n; i++) {
		ret = gw_handle_borrow(GW_T_CHANNEL, iio_buffer_get_scan_element(buf, i), root, &ids[i]);
		if (ret)
			return gw_fail(c, ret, "cannot register scan element %u", i);
	}
	return 0;
}

/* bs = buffer_open(buf, mask). The stream copies the mask: the caller's mask may be destroyed
 * or changed afterwards. */
static int buffer_open(struct gw_call *c)
{
	struct iio_buffer *buf;
	const struct iio_channels_mask *mask;
	struct iio_buffer_stream *bs;
	gw_id buf_id, mask_id, id;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &buf_id);
	if (ret)
		return ret;
	ret = gw_arg_handle(c, 1, GW_T_MASK, &mask, &mask_id);
	if (ret)
		return ret;
	/* libiio does not check: a mask of a device with fewer channels is read out of bounds */
	if (dp_device_of(mask_id) != iio_buffer_get_device(buf))
		return gw_fail(c, -EINVAL, "the channels mask belongs to another device");
	/* Result first: no mx allocation may follow the opening of the buffer. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	bs = iio_buffer_open(buf, mask);
	ret = iio_err(bs);             /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = dp_add_bufstream(bs, buf_id);
	if (!id) {
		iio_buffer_close(bs);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* buffer_close(id): cancel, stop if started, the blocks, the mask view, close (handle table). */
static int buffer_close(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_BUFFER_STREAM);
}

/* An owned buffer stream; -EPERM for one that belongs to an iio_stream (a borrowed entry). */
static int own_stream(struct gw_call *c, struct iio_buffer_stream **bs, gw_id *id)
{
	int ret = gw_arg_handle(c, 0, GW_T_BUFFER_STREAM, bs, id);

	if (ret)
		return ret;
	if (!gw_handle_owned(*id))
		return gw_fail(c, -EPERM, "the buffer stream belongs to a stream");
	return 0;
}

/* buffer_stream_start(bs): nothing when started (a second iio_buffer_stream_start would
 * enable the buffer of the backend again). */
static int buffer_stream_start(struct gw_call *c)
{
	struct iio_buffer_stream *bs;
	gw_id id;
	int ret;

	ret = own_stream(c, &bs, &id);
	if (ret)
		return ret;
	if (gw_handle_flags(id) & BS_STARTED)
		return 0;
	ret = iio_buffer_stream_start(bs);
	if (ret < 0)
		return ret;
	gw_handle_set_flags(id, gw_handle_flags(id) | BS_STARTED);
	return 0;
}

/* buffer_stream_stop(bs): nothing when not started */
static int buffer_stream_stop(struct gw_call *c)
{
	struct iio_buffer_stream *bs;
	gw_id id;
	int ret;

	ret = own_stream(c, &bs, &id);
	if (ret)
		return ret;
	if (!(gw_handle_flags(id) & BS_STARTED))
		return 0;
	ret = iio_buffer_stream_stop(bs);
	if (ret < 0)
		return ret;
	gw_handle_set_flags(id, gw_handle_flags(id) & ~BS_STARTED);
	return 0;
}

/* buffer_stream_cancel(bs): also for a stream's buffer stream. BS_STARTED stays as it is:
 * the backend's buffer is still enabled, and the close still has to stop it. */
static int buffer_stream_cancel(struct gw_call *c)
{
	struct iio_buffer_stream *bs;
	int ret = gw_arg_handle(c, 0, GW_T_BUFFER_STREAM, &bs, NULL);

	if (ret)
		return ret;
	iio_buffer_stream_cancel(bs);
	return 0;
}

/* mask = buffer_stream_get_channels_mask(bs): the channels the backend really enabled, as a
 * borrowed (read-only) child of the buffer stream; the same id on every call. */
static int buffer_stream_get_channels_mask(struct gw_call *c)
{
	struct iio_buffer_stream *bs;
	gw_id bs_id, id = 0;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER_STREAM, &bs, &bs_id);
	if (ret)
		return ret;
	/* Result first: the first call registers the view. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	ret = gw_handle_borrow(GW_T_MASK, iio_buffer_stream_get_channels_mask(bs), bs_id, &id);
	if (ret)
		return gw_fail(c, ret, "cannot register the channels mask");
	gw_set_id(c, 0, id);
	return 0;
}

/* blk = buffer_stream_create_block(bs, nbytes) */
static int buffer_stream_create_block(struct gw_call *c)
{
	struct iio_buffer_stream *bs;
	struct iio_block *blk;
	long long nbytes;
	gw_id bs_id, id;
	int ret;

	ret = own_stream(c, &bs, &bs_id);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 1, 0, LLONG_MAX, &nbytes);
	if (ret)
		return ret;
	/* Result first: no mx allocation may follow the creation of the block. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	blk = iio_buffer_stream_create_block(bs, (size_t)nbytes);
	ret = iio_err(blk);            /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = dp_add_block(blk, bs_id);
	if (!id) {
		iio_block_destroy(blk);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* tf = buffer_stream_is_started(bs): the flag BS_STARTED */
static int buffer_stream_is_started(struct gw_call *c)
{
	gw_id id;
	int ret = gw_arg_handle(c, 0, GW_T_BUFFER_STREAM, NULL, &id);

	return ret ? ret : gw_ret_bool(c, 0, (gw_handle_flags(id) & BS_STARTED) != 0);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("buffer_get_device", buffer_get_device, 1, 1, 1),
	GW_CMD("buffer_is_output", buffer_is_output, 1, 1, 1),
	GW_CMD("buffer_get_attrs", buffer_get_attrs, 1, 1, 1),
	GW_CMD("buffer_find_attr", buffer_find_attr, 2, 2, 1),
	GW_CMD("buffer_get_scan_elements", buffer_get_scan_elements, 1, 1, 1),
	GW_CMD("buffer_open", buffer_open, 2, 2, 1),
	GW_CMD("buffer_close", buffer_close, 1, 1, 0),
	GW_CMD("buffer_stream_start", buffer_stream_start, 1, 1, 0),
	GW_CMD("buffer_stream_stop", buffer_stream_stop, 1, 1, 0),
	GW_CMD("buffer_stream_cancel", buffer_stream_cancel, 1, 1, 0),
	GW_CMD("buffer_stream_get_channels_mask", buffer_stream_get_channels_mask, 1, 1, 1),
	GW_CMD("buffer_stream_create_block", buffer_stream_create_block, 2, 2, 1),
	GW_XCMD("buffer_stream_is_started", buffer_stream_is_started, 1, 1, 1),
};

const struct gw_group gw_group_buffer = GW_GROUP("buffer", cmds);
