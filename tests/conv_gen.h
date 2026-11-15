/* SPDX-License-Identifier: MIT */
/*
 * Random conversion cases and an independent, bit-by-bit reference for the
 * read direction. Shares no code with the library on purpose.
 */
#ifndef CONV_GEN_H
#define CONV_GEN_H

#include <iio/iio-conv.h>
#include <math.h>
#include "test_util.h"

#define GEN_MAX_CHANS 8

struct gen_case {
	struct iio_conv_chan ch[GEN_MAX_CHANS];
	double ug[GEN_MAX_CHANS], uo[GEN_MAX_CHANS];
	unsigned int nb_chans;
	size_t step, nb_samples;
	struct iio_conv_spec spec;
};

static const size_t gen_type_size[] = { 1, 1, 2, 2, 4, 4, 8, 8, 4, 8 };

static inline bool gen_type_is_float(enum iio_conv_type t)
{
	return t == IIO_CONV_F32 || t == IIO_CONV_F64;
}

static inline size_t gen_nb_cols(const struct gen_case *g)
{
	return g->spec.complex_iq ? g->nb_chans / 2 : g->nb_chans;
}

static inline size_t gen_elem_size(const struct gen_case *g)
{
	return gen_type_size[g->spec.type] * (g->spec.complex_iq ? 2 : 1);
}

static inline size_t gen_host_bytes(const struct gen_case *g)
{
	return gen_nb_cols(g) * gen_elem_size(g) * g->nb_samples;
}

/* Byte position of channel c, sample i in the host array */
static inline size_t gen_host_pos(const struct gen_case *g, unsigned int c, size_t i)
{
	size_t col = g->spec.complex_iq ? c / 2 : c;
	size_t part = g->spec.complex_iq && (c & 1) ? gen_type_size[g->spec.type] : 0;
	size_t e = gen_elem_size(g);

	if (g->spec.layout == IIO_CONV_COLUMNS)
		return col * g->nb_samples * e + i * e + part;
	return (i * gen_nb_cols(g) + col) * e + part;
}

/* pct16: percentage of channels forced to 16-bit storage */
static inline void gen_random_case(uint64_t *rng, struct gen_case *g, unsigned int pct16,
		size_t max_samples)
{
	static const unsigned int lengths[] = { 8, 16, 32, 64 };
	unsigned int i, order[GEN_MAX_CHANS];
	size_t pos = 0;
	bool same_fmt = rng_range(rng, 3) == 0;

	memset(g, 0, sizeof(*g));
	g->nb_chans = 1 + rng_range(rng, GEN_MAX_CHANS);
	g->nb_samples = rng_range(rng, max_samples + 1);

	for (i = 0; i < g->nb_chans; i++) {
		struct iio_conv_chan *ch = &g->ch[i];

		if (same_fmt && i > 0) {
			*ch = g->ch[0];
		} else {
			ch->length = rng_range(rng, 100) < pct16 ? 16 : lengths[rng_range(rng, 4)];
			ch->bits = 1 + rng_range(rng, ch->length);
			if (rng_range(rng, 3) == 0)
				ch->bits = ch->length;
			ch->shift = rng_range(rng, ch->length - ch->bits + 1);
			ch->is_signed = rng_range(rng, 2);
			ch->is_be = rng_range(rng, 4) == 0;
			ch->with_scale = rng_range(rng, 2);
			ch->scale = (double)(1 + rng_range(rng, 1000)) / 64.0;
			ch->offset = (double)rng_range(rng, 200) - 100.0;
		}
		order[i] = i;
	}

	/* storage positions in a shuffled order, with random gaps */
	for (i = g->nb_chans - 1; i > 0; i--) {
		unsigned int j = rng_range(rng, i + 1), t = order[i];
		order[i] = order[j];
		order[j] = t;
	}
	for (i = 0; i < g->nb_chans; i++) {
		struct iio_conv_chan *ch = &g->ch[order[i]];

		if (rng_range(rng, 4) == 0)
			pos += rng_range(rng, 4);
		ch->byte_offset = pos;
		pos += ch->length / 8;
	}
	if (rng_range(rng, 4) == 0)
		pos += rng_range(rng, 4);
	g->step = pos;

	g->spec.type = (enum iio_conv_type)rng_range(rng, 10);
	if (rng_range(rng, 2))
		g->spec.type = rng_range(rng, 2) ? IIO_CONV_F32 : IIO_CONV_F64;
	g->spec.layout = rng_range(rng, 2) ? IIO_CONV_COLUMNS : IIO_CONV_INTERLEAVED;
	g->spec.saturate = rng_range(rng, 2);
	g->spec.complex_iq = !(g->nb_chans & 1) && rng_range(rng, 2);
	if (gen_type_is_float(g->spec.type)) {
		g->spec.scale = (enum iio_conv_scale)rng_range(rng, 3);
		if (rng_range(rng, 3) == 0) {
			for (i = 0; i < g->nb_chans; i++) {
				g->ug[i] = (double)(1 + rng_range(rng, 4000)) / 1000.0;
				g->uo[i] = (double)rng_range(rng, 1000) / 100.0 - 5.0;
			}
			g->spec.user_gain = g->ug;
			if (rng_range(rng, 2))
				g->spec.user_offset = g->uo;
		}
	}
}

static inline void gen_fill(uint64_t *rng, uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		buf[i] = (uint8_t)rng_next(rng);
}

/* Reference conversion of one stored sample of channel c into out
 * (gen_type_size[type] bytes). */
static inline void ref_read_one(const struct gen_case *g, unsigned int c,
		const uint8_t *sample, void *out)
{
	const struct iio_conv_chan *ch = &g->ch[c];
	unsigned int i, nbytes = ch->length / 8;
	unsigned __int128 w = 0;
	__int128 val;
	double gain = 1.0, offset = 0.0, ug = 1.0, uo = 0.0, a, b;
	size_t tsize = gen_type_size[g->spec.type];

	for (i = 0; i < nbytes; i++) {
		unsigned int sh = ch->is_be ? 8 * (nbytes - 1 - i) : 8 * i;

		w |= (unsigned __int128)sample[ch->byte_offset + i] << sh;
	}
	w >>= ch->shift;
	w &= ((unsigned __int128)1 << ch->bits) - 1;
	val = (__int128)w;
	if (ch->is_signed && (w >> (ch->bits - 1)))
		val -= (__int128)1 << ch->bits;

	if (gen_type_is_float(g->spec.type)) {
		switch (g->spec.scale) {
		case IIO_CONV_SCALE_IIO:
			gain = ch->with_scale ? ch->scale : 1.0;
			offset = ch->offset;
			break;
		case IIO_CONV_SCALE_FULLSCALE:
			gain = 1.0 / pow(2.0, ch->is_signed ? ch->bits - 1 : ch->bits);
			break;
		default:
			break;
		}
		if (g->spec.user_gain)
			ug = g->spec.user_gain[c];
		if (g->spec.user_offset)
			uo = g->spec.user_offset[c];
		a = gain * ug;
		b = offset * gain * ug + uo;

		if (g->spec.type == IIO_CONV_F64) {
			volatile double x = ch->is_signed ? (double)(int64_t)val : (double)(uint64_t)val;
			volatile double m = x * a;
			double r = m + b;
			memcpy(out, &r, sizeof(r));
		} else {
			volatile float x = ch->is_signed ? (float)(int64_t)val : (float)(uint64_t)val;
			volatile float m = x * (float)a;
			float r = m + (float)b;
			memcpy(out, &r, sizeof(r));
		}
		return;
	}

	{
		bool tsigned = !(g->spec.type & 1); /* I8, I16, I32, I64 are even */
		unsigned int tbits = 8 * tsize;
		__int128 tmax = tsigned ? ((__int128)1 << (tbits - 1)) - 1 : ((__int128)1 << tbits) - 1;
		__int128 tmin = tsigned ? -((__int128)1 << (tbits - 1)) : 0;
		uint64_t o;

		if (g->spec.saturate) {
			if (val > tmax)
				val = tmax;
			if (val < tmin)
				val = tmin;
		}
		o = (uint64_t)val; /* low bits; little-endian host stores them first */
		memcpy(out, &o, tsize);
	}
}

/* Reference conversion of a whole buffer into ref (gen_host_bytes(g) bytes) */
static inline void ref_read(const struct gen_case *g, const uint8_t *src, uint8_t *ref)
{
	unsigned int c;
	size_t i;

	for (c = 0; c < g->nb_chans; c++)
		for (i = 0; i < g->nb_samples; i++)
			ref_read_one(g, c, src + i * g->step, ref + gen_host_pos(g, c, i));
}

#endif /* CONV_GEN_H */
