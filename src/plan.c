/* SPDX-License-Identifier: MIT */
/*
 * Plan creation: validate the channel descriptors and the spec, work out the
 * per-channel constants and pick one kernel per channel.
 */
#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

size_t conv_type_size(enum iio_conv_type type)
{
	switch (type) {
	case IIO_CONV_I8:
	case IIO_CONV_U8:
		return 1;
	case IIO_CONV_I16:
	case IIO_CONV_U16:
		return 2;
	case IIO_CONV_I32:
	case IIO_CONV_U32:
	case IIO_CONV_F32:
		return 4;
	case IIO_CONV_I64:
	case IIO_CONV_U64:
	case IIO_CONV_F64:
		return 8;
	}
	return 0;
}

static bool type_is_float(enum iio_conv_type type)
{
	return type == IIO_CONV_F32 || type == IIO_CONV_F64;
}

static int plan_chan(struct chan_plan *p, const struct iio_conv_chan *ch,
		unsigned int idx, size_t step, const struct iio_conv_spec *spec)
{
	double gain = 1.0, offset = 0.0, ugain = 1.0, uoffset = 0.0;

	if (ch->length != 8 && ch->length != 16 && ch->length != 32 && ch->length != 64)
		return -ENOTSUP;
	if (!ch->bits || ch->bits + ch->shift > ch->length)
		return -EINVAL;
	if (ch->byte_offset + ch->length / 8 > step)
		return -EINVAL;

	p->length = ch->length;
	p->bits = ch->bits;
	p->shift = ch->shift;
	p->is_signed = ch->is_signed;
	p->saturate = spec->saturate;
	p->type = spec->type;
	p->src_off = ch->byte_offset;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	p->swap = ch->is_be && ch->length > 8;
#else
	p->swap = !ch->is_be && ch->length > 8;
#endif

	p->mask = ch->bits == 64 ? UINT64_MAX : (UINT64_C(1) << ch->bits) - 1;
	if (ch->is_signed) {
		p->hi = p->mask >> 1;
		p->lo = -(int64_t)p->hi - 1;
	} else {
		p->hi = p->mask;
		p->lo = 0;
	}

	switch (spec->scale) {
	case IIO_CONV_SCALE_NONE:
		break;
	case IIO_CONV_SCALE_IIO:
		gain = ch->with_scale ? ch->scale : 1.0;
		offset = ch->offset;
		break;
	case IIO_CONV_SCALE_FULLSCALE:
		gain = ldexp(1.0, -(int)(ch->is_signed ? ch->bits - 1 : ch->bits));
		break;
	default:
		return -EINVAL;
	}
	if (spec->user_gain)
		ugain = spec->user_gain[idx];
	if (spec->user_offset)
		uoffset = spec->user_offset[idx];

	p->a = gain * ugain;
	p->b = offset * gain * ugain + uoffset;
	p->inv_a = 1.0 / p->a;
	p->affine = p->a != 1.0 || p->b != 0.0;

	if (p->affine && !type_is_float(spec->type))
		return -EINVAL;
	if (p->a == 0.0 || p->a != p->a || p->b != p->b)
		return -EINVAL;

	return 0;
}

static bool pair_eligible(const struct chan_plan *i, const struct chan_plan *q)
{
	return i->length == q->length && i->bits == q->bits && i->shift == q->shift &&
		i->is_signed == q->is_signed && i->swap == q->swap &&
		i->a == q->a && i->b == q->b &&
		q->src_off == i->src_off + i->length / 8;
}

static int conv_create(const struct conv_backend *be, const struct iio_conv_chan *chans,
		unsigned int nb_chans, size_t step, const struct iio_conv_spec *spec,
		struct iio_conv **out)
{
	struct iio_conv *conv;
	size_t tsize;
	unsigned int i;
	int ret;

	if (!chans || !nb_chans || !spec || !out)
		return -EINVAL;
	tsize = conv_type_size(spec->type);
	if (!tsize)
		return -EINVAL;
	if (spec->layout != IIO_CONV_COLUMNS && spec->layout != IIO_CONV_INTERLEAVED)
		return -EINVAL;
	if (spec->complex_iq && (nb_chans & 1))
		return -EINVAL;

	conv = calloc(1, sizeof(*conv));
	if (!conv)
		return -ENOMEM;
	conv->chans = calloc(nb_chans, sizeof(*conv->chans));
	conv->rd = calloc(nb_chans, sizeof(*conv->rd));
	conv->wr = calloc(nb_chans, sizeof(*conv->wr));
	conv->rd_pair = calloc(nb_chans, sizeof(*conv->rd_pair));
	conv->wr_pair = calloc(nb_chans, sizeof(*conv->wr_pair));
	if (!conv->chans || !conv->rd || !conv->wr || !conv->rd_pair || !conv->wr_pair) {
		ret = -ENOMEM;
		goto err;
	}

	conv->nb_chans = nb_chans;
	conv->step = step;
	conv->columns = spec->layout == IIO_CONV_COLUMNS;
	conv->nb_cols = spec->complex_iq ? nb_chans / 2 : nb_chans;
	conv->elem_size = spec->complex_iq ? 2 * tsize : tsize;
	conv->host_step = conv->nb_cols * conv->elem_size;

	for (i = 0; i < nb_chans; i++) {
		struct chan_plan *p = &conv->chans[i];
		unsigned int col = spec->complex_iq ? i / 2 : i;
		size_t part = spec->complex_iq && (i & 1) ? tsize : 0;

		ret = plan_chan(p, &chans[i], i, step, spec);
		if (ret)
			goto err;

		if (conv->columns) {
			/* the column base depends on the sample count and is
			 * added when converting */
			p->dst_off = part;
			p->dst_stride = conv->elem_size;
		} else {
			p->dst_off = col * conv->elem_size + part;
			p->dst_stride = conv->host_step;
		}
	}

	for (i = 0; i < nb_chans; i++) {
		struct chan_plan *p = &conv->chans[i];
		struct chan_plan single = *p;

		single.pair = false;

		/* An I/Q pair kernel, when the backend has one, converts
		 * channels i and i+1 in one pass. */
		if (spec->complex_iq && !(i & 1)) {
			p->pair = pair_eligible(p, p + 1);
			if (p->pair && be->pick_rd)
				conv->rd_pair[i] = be->pick_rd(p);
			if (p->pair && be->pick_wr)
				conv->wr_pair[i] = be->pick_wr(p);
		}

		if (be->pick_rd)
			conv->rd[i] = be->pick_rd(&single);
		if (!conv->rd[i])
			conv->rd[i] = conv_backend_scalar.pick_rd(&single);
		if (be->pick_wr)
			conv->wr[i] = be->pick_wr(&single);
		if (!conv->wr[i])
			conv->wr[i] = conv_backend_scalar.pick_wr(&single);
		if (!conv->rd[i] || !conv->wr[i]) {
			ret = -ENOTSUP;
			goto err;
		}
	}

	*out = conv;
	return 0;

err:
	iio_conv_destroy(conv);
	return ret;
}

int iio_conv_create_raw(const struct iio_conv_chan *chans, unsigned int nb_chans,
		size_t step, const struct iio_conv_spec *spec, struct iio_conv **conv)
{
	return conv_create(conv_dispatch(), chans, nb_chans, step, spec, conv);
}

int iio_conv_create_raw_backend(const char *backend, const struct iio_conv_chan *chans,
		unsigned int nb_chans, size_t step, const struct iio_conv_spec *spec,
		struct iio_conv **conv)
{
	const struct conv_backend *be = conv_backend_by_name(backend);

	if (!be)
		return -ENOTSUP;
	return conv_create(be, chans, nb_chans, step, spec, conv);
}

void iio_conv_destroy(struct iio_conv *conv)
{
	if (!conv)
		return;
	free(conv->chans);
	free(conv->rd);
	free(conv->wr);
	free(conv->rd_pair);
	free(conv->wr_pair);
	free(conv);
}

size_t iio_conv_get_step(const struct iio_conv *conv)
{
	return conv->step;
}

size_t iio_conv_get_host_step(const struct iio_conv *conv)
{
	return conv->host_step;
}

const struct chan_plan *iio_conv_test_get_plan(const struct iio_conv *conv,
		unsigned int chan)
{
	return &conv->chans[chan];
}

static size_t column_base(const struct iio_conv *conv, unsigned int chan, size_t col_len)
{
	unsigned int col = conv->nb_cols == conv->nb_chans ? chan : chan / 2;

	return conv->columns ? col * col_len * conv->elem_size : 0;
}

void conv_read_cols(const struct iio_conv *conv, const void *src, size_t nb_samples,
		void *dst, size_t col_len)
{
	const uint8_t *s = src;
	uint8_t *d = dst;
	unsigned int i;

	if (!nb_samples)
		return;

	for (i = 0; i < conv->nb_chans; i++) {
		const struct chan_plan *p = &conv->chans[i];
		uint8_t *out = d + column_base(conv, i, col_len) + p->dst_off;

		if (conv->rd_pair[i]) {
			conv->rd_pair[i](s + p->src_off, conv->step, out, nb_samples, p);
			i++;
		} else {
			conv->rd[i](s + p->src_off, conv->step, out, nb_samples, p);
		}
	}
}

void conv_write_cols(const struct iio_conv *conv, void *dst, size_t nb_samples,
		const void *src, size_t col_len)
{
	const uint8_t *s = src;
	uint8_t *d = dst;
	unsigned int i;

	if (!nb_samples)
		return;

	for (i = 0; i < conv->nb_chans; i++) {
		const struct chan_plan *p = &conv->chans[i];
		const uint8_t *in = s + column_base(conv, i, col_len) + p->dst_off;

		if (conv->wr_pair[i]) {
			conv->wr_pair[i](d + p->src_off, conv->step, in, nb_samples, p);
			i++;
		} else {
			conv->wr[i](d + p->src_off, conv->step, in, nb_samples, p);
		}
	}
}

void iio_conv_read_raw(const struct iio_conv *conv, const void *src, size_t nb_samples,
		void *dst)
{
	conv_read_cols(conv, src, nb_samples, dst, nb_samples);
}

void iio_conv_write_raw(const struct iio_conv *conv, void *dst, size_t nb_samples,
		const void *src)
{
	conv_write_cols(conv, dst, nb_samples, src, nb_samples);
}
