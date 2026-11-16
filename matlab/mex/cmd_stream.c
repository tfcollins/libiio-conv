/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "stream": libiio's iio_stream helper.
 *
 * A stream is an owned child of its buffer. It owns a buffer stream and nb_blocks blocks; the
 * gateway shows them as borrowed children of the stream ("views"): the buffer stream from the
 * first successful stream_get_next_block on, each block from the call that returns it first.
 * The flag BLK_ENQUEUED of a block view says "not the caller's": it is clear on the block
 * returned last and set on every other one, and on all of them after a call that failed. */
#include "mx_util.h"
#include "data_path.h"

#include <stdlib.h>

/* Gateway-side state of a GW_T_STREAM entry (its aux pointer; malloc, freed by the destroy op) */
struct stream_aux {
	const struct iio_buffer *buf;   /* the buffer the stream was created on */
	size_t nb_blocks;
	size_t nb_views;                /* block views registered so far, at most nb_blocks */
	gw_id views[];
};

/* The children (the views) are gone when this runs; the context is still alive. */
static void stream_destroy_op(void *ptr, void *aux_ptr)
{
	struct stream_aux *aux = aux_ptr;
	bool cancel = true;

	if (aux && iio_buffer_is_output(aux->buf)) {
		/* iio_stream_destroy() dequeues every block, which sends what is still queued.
		 * With an infinite context timeout that wait has no bound: cancel first. */
		const struct iio_context *ctx = iio_device_get_context(iio_buffer_get_device(aux->buf));

		cancel = iio_context_get_params(ctx)->timeout_ms < 0;
	}
	if (cancel)
		iio_stream_cancel(ptr);
	iio_stream_destroy(ptr);
	free(aux);
}

/* BLK_ENQUEUED on every block view of the stream, clear on mine (0: on every one) */
static void mark_views(const struct stream_aux *aux, gw_id mine)
{
	for (size_t i = 0; i < aux->nb_views; i++) {
		gw_id id = aux->views[i];
		uint32_t flags = gw_handle_flags(id);

		gw_handle_set_flags(id, id == mine ? flags & ~BLK_ENQUEUED : flags | BLK_ENQUEUED);
	}
}

/* st = buffer_create_stream(buf, nb_blocks, samples_count, mask) */
static int buffer_create_stream(struct gw_call *c)
{
	static const struct gw_ops ops = { NULL, stream_destroy_op };
	struct iio_channels_mask *mask;
	struct iio_buffer *buf;
	struct iio_stream *st;
	struct stream_aux *aux;
	long long nb_blocks, samples;
	const struct iio_device *dev;
	gw_id buf_id, mask_id, id;
	ssize_t sample_size;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_BUFFER, &buf, &buf_id);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 1, 1, (long long)((SIZE_MAX - sizeof(*aux)) / sizeof(aux->views[0]) / 2), &nb_blocks);
	if (ret)
		return ret;
	ret = gw_arg_int(c, 2, 1, LLONG_MAX, &samples);
	if (ret)
		return ret;
	ret = gw_arg_handle(c, 3, GW_T_MASK, &mask, &mask_id);
	if (ret)
		return ret;
	/* libiio does not compare the devices and would read the mask with the wrong width */
	dev = dp_device_of(buf_id);
	if (!dev || dev != dp_device_of(mask_id))
		return gw_fail(c, -EINVAL, "the channels mask belongs to another device");
	/* the block size is samples_count * sample size, computed by libiio without a check */
	sample_size = iio_device_get_sample_size(dev, mask);
	if (sample_size <= 0)
		return gw_fail(c, -EINVAL, "no channel of the mask can be captured");
	if ((unsigned long long)samples > (SIZE_MAX / 2) / (size_t)sample_size)
		return gw_fail(c, -EINVAL, "samples_count is too large");

	/* Result first: no mx allocation may follow the creation of the stream. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;
	aux = calloc(1, sizeof(*aux) + (size_t)nb_blocks * sizeof(aux->views[0]));
	if (!aux)
		return gw_fail(c, -ENOMEM, "out of memory");
	aux->buf = buf;
	aux->nb_blocks = (size_t)nb_blocks;

	st = iio_buffer_create_stream(buf, (size_t)nb_blocks, (size_t)samples, mask);
	ret = iio_err(st);              /* error-encoded, never NULL */
	if (ret) {
		free(aux);
		return ret;
	}
	id = gw_handle_add(GW_T_STREAM, st, buf_id, true, &ops, aux);
	if (!id) {
		iio_stream_destroy(st);     /* nothing is queued */
		free(aux);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	gw_set_id(c, 0, id);
	return 0;
}

/* blk = stream_get_next_block(st): a block view, the caller's until the next call */
static int stream_get_next_block(struct gw_call *c)
{
	const struct iio_block *blk;
	struct iio_stream *st;
	struct stream_aux *aux;
	gw_id id, bs_id, blk_id;
	size_t i;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_STREAM, &st, &id);
	if (ret)
		return ret;
	aux = gw_handle_aux(id);
	if (!aux)
		return gw_fail(c, -EBADF, "not a stream of this gateway");
	/* Result first: the call moves blocks, so nothing may fail after it. */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		return ret;

	blk = iio_stream_get_next_block(st);
	ret = iio_err(blk);             /* error-encoded, never NULL: nothing is registered */
	if (ret) {
		/* which blocks libiio holds now is unknown: none is the caller's */
		mark_views(aux, 0);
		return ret;
	}
	/* the stream's buffer stream, on first use (Block.read keeps its converters there) */
	ret = gw_handle_borrow(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(blk), id, &bs_id);
	if (!ret) {
		/* libiio started it in the first successful call: buffer_stream_is_started says so */
		gw_handle_set_flags(bs_id, gw_handle_flags(bs_id) | BS_STARTED);
		ret = gw_handle_borrow(GW_T_BLOCK, blk, id, &blk_id);
	}
	if (!ret) {
		for (i = 0; i < aux->nb_views && aux->views[i] != blk_id; i++)
			;
		if (i == aux->nb_views) {
			if (i < aux->nb_blocks)
				aux->views[aux->nb_views++] = blk_id;
			else
				ret = -EFAULT;      /* more blocks than the stream has: a view of something else */
		}
	}
	if (ret) {
		mark_views(aux, 0);
		return gw_fail(c, ret, "cannot register the block");
	}
	mark_views(aux, blk_id);
	gw_set_id(c, 0, blk_id);
	return 0;
}

/* stream_cancel(st) */
static int stream_cancel(struct gw_call *c)
{
	struct iio_stream *st;
	int ret = gw_arg_handle(c, 0, GW_T_STREAM, &st, NULL);

	if (ret)
		return ret;
	iio_stream_cancel(st);
	return 0;
}

/* stream_destroy(id): the views go first (handle table), then cancel / destroy as the op says */
static int stream_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_STREAM);
}

static const struct gw_cmd cmds[] = {
	GW_CMD("buffer_create_stream", buffer_create_stream, 4, 4, 1),
	GW_CMD("stream_get_next_block", stream_get_next_block, 1, 1, 1),
	GW_CMD("stream_cancel", stream_cancel, 1, 1, 0),
	GW_CMD("stream_destroy", stream_destroy, 1, 1, 0),
};

const struct gw_group gw_group_stream = GW_GROUP("stream", cmds);
