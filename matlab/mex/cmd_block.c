/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "block": a block's queue state, its bytes, libiio's
 * per-channel read and write, and the converted read and write through libiio-conv.
 *
 * Flags of a GW_T_BLOCK entry (data_path.h): BLK_ENQUEUED while the backend has the block,
 * BLK_CPU_OFF while CPU access is disabled. "Data access" (the bytes and every conversion)
 * is refused with -EPERM while either is set: libiio does not check, and the memory of an
 * enqueued block belongs to the backend.
 *
 * The converters of block_read and block_write are kept in the cache slot of the block's
 * buffer-stream entry and go when that entry is destroyed. */
#include "mx_util.h"
#include "data_path.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ the block argument */

struct blk {
	struct iio_block *ptr;
	gw_id id;
	uint8_t *start;         /* iio_block_start() */
	size_t size;            /* bytes */
};

static int arg_block(struct gw_call *c, int k, struct blk *b)
{
	int ret = gw_arg_handle(c, k, GW_T_BLOCK, &b->ptr, &b->id);

	if (ret)
		return ret;
	b->start = iio_block_start(b->ptr);
	b->size = (size_t)((uintptr_t)iio_block_end(b->ptr) - (uintptr_t)b->start);
	return 0;
}

/* -EPERM while the block's memory is not ours to touch */
static int data_access(struct gw_call *c, const struct blk *b)
{
	uint32_t flags = gw_handle_flags(b->id);

	if (flags & BLK_ENQUEUED)
		return gw_fail(c, -EPERM, "the block is enqueued: dequeue it first");
	if (flags & BLK_CPU_OFF)
		return gw_fail(c, -EPERM, "CPU access to the block is disabled");
	return 0;
}

/* A channel argument that belongs to the device of the block: libiio looks the channel up in
 * the block's mask without a check, which reads out of bounds for a channel of another device. */
static int arg_channel_of(struct gw_call *c, int k, const struct blk *b, const struct iio_channel **ch)
{
	const struct iio_device *dev = dp_device_of(b->id);
	int ret = gw_arg_handle(c, k, GW_T_CHANNEL, ch, NULL);

	if (ret)
		return ret;
	if (!dev || iio_channel_get_device(*ch) != dev)
		return gw_fail(c, -EINVAL, "argument %d: the channel and the block belong to different devices", k + 1);
	return 0;
}

/* The mask the block's buffer stream really has (it may be wider than the one it was opened with). */
static const struct iio_channels_mask *mask_of(const struct blk *b)
{
	return iio_buffer_stream_get_channels_mask(iio_block_get_buffer_stream(b->ptr));
}

/* An optional byte count or offset: absent -> dflt. */
static int arg_size(struct gw_call *c, int k, size_t dflt, size_t *v)
{
	long long n;
	int ret;

	*v = dflt;
	if (!gw_arg_given(c, k))
		return 0;
	ret = gw_arg_int(c, k, 0, LLONG_MAX, &n);
	if (ret)
		return ret;
	*v = (size_t)n;
	return 0;
}

/* ------------------------------------------------------------------ queue state */

static int block_destroy(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_BLOCK);
}

/* n = block_first(blk, ch): byte offset of the channel's first sample; block_size(blk) when the
 * channel is not enabled in the stream's mask */
static int block_first(struct gw_call *c)
{
	const struct iio_channel *ch;
	struct blk b;
	size_t off;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = arg_channel_of(c, 1, &b, &ch);
	if (ret)
		return ret;
	off = (size_t)((uintptr_t)iio_block_first(b.ptr, ch) - (uintptr_t)b.start);
	return gw_ret_double(c, 0, (double)(off < b.size ? off : b.size));
}

/* block_enqueue(blk, bytes_used, cyclic): bytes_used 0 = the whole block */
static int block_enqueue(struct gw_call *c)
{
	long long bytes_used;
	struct blk b;
	bool cyclic;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = gw_arg_int(c, 1, 0, LLONG_MAX, &bytes_used);
	if (!ret)
		ret = gw_arg_bool(c, 2, &cyclic);
	if (ret)
		return ret;
	if (!gw_handle_owned(b.id))
		return gw_fail(c, -EPERM, "the block belongs to a stream");
	if (gw_handle_flags(b.id) & BLK_ENQUEUED)
		return gw_fail(c, -EPERM, "the block is already enqueued");
	ret = iio_block_enqueue(b.ptr, (size_t)bytes_used, cyclic);
	if (ret < 0)
		return ret;
	gw_handle_set_flags(b.id, gw_handle_flags(b.id) | BLK_ENQUEUED);
	return 0;
}

/* tf = block_dequeue(blk, nonblock): true = the block is ours again; false = not ready (only
 * with nonblock). The three -EPERM cases below never reach libiio: emu stalls for 5 s in a
 * dequeue on a buffer that is not started, and its answer for a block that is not enqueued
 * differs from the other backends'. Result first: nothing is allocated after the dequeue. */
static int block_dequeue(struct gw_call *c)
{
	struct blk b;
	bool nonblock;
	gw_id bs;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = gw_arg_bool(c, 1, &nonblock);
	if (ret)
		return ret;
	if (!gw_handle_owned(b.id))
		return gw_fail(c, -EPERM, "the block belongs to a stream");
	if (!(gw_handle_flags(b.id) & BLK_ENQUEUED))
		return gw_fail(c, -EPERM, "the block is not enqueued");
	bs = gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(b.ptr));
	if (!(gw_handle_flags(bs) & BS_STARTED))
		return gw_fail(c, -EPERM, "the buffer stream is not started");
	ret = gw_ret_bool(c, 0, false);
	if (ret)
		return ret;

	ret = iio_block_dequeue(b.ptr, nonblock);
	if (ret == -EBUSY && nonblock)
		return 0;
	if (ret == -EPERM) {
		/* libiio says the block is not enqueued: a dequeue that failed on its generic block
		 * path has consumed it. Believe it, or the block could never be used again. */
		gw_handle_set_flags(b.id, gw_handle_flags(b.id) & ~BLK_ENQUEUED);
		return gw_fail(c, -EPERM, "libiio no longer holds the block: it can be enqueued again");
	}
	if (ret < 0)
		return ret;
	gw_handle_set_flags(b.id, gw_handle_flags(b.id) & ~BLK_ENQUEUED);
	*(mxLogical *)mxGetData(c->plhs[0]) = true;
	return 0;
}

/* bs = block_get_buffer_stream(blk): 0 when the gateway does not hold the buffer stream */
static int block_get_buffer_stream(struct gw_call *c)
{
	struct blk b;
	int ret = arg_block(c, 0, &b);

	return ret ? ret : gw_ret_id(c, 0, gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(b.ptr)));
}

static int block_get_dmabuf_fd(struct gw_call *c)
{
	struct blk b;
	int ret = arg_block(c, 0, &b);
	int fd;

	if (ret)
		return ret;
	fd = iio_block_get_dmabuf_fd(b.ptr);
	return fd < 0 ? fd : gw_ret_double(c, 0, fd);
}

static int block_disable_cpu_access(struct gw_call *c)
{
	struct blk b;
	bool disable;
	uint32_t flags;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = gw_arg_bool(c, 1, &disable);
	if (ret)
		return ret;
	if (!gw_handle_owned(b.id))
		return gw_fail(c, -EPERM, "the block belongs to a stream");
	ret = iio_block_disable_cpu_access(b.ptr, disable);
	if (ret < 0)
		return ret;
	flags = gw_handle_flags(b.id);
	gw_handle_set_flags(b.id, disable ? flags | BLK_CPU_OFF : flags & ~BLK_CPU_OFF);
	return 0;
}

/* ------------------------------------------------------------------ one channel, through libiio */

/* Bytes of one channel in the block: whole samples only. libiio's loops run while the sample
 * STARTS inside the block, so a block that ends in half a sample must not be offered in full.
 * 0 when the channel is not enabled: libiio's raw shortcut (taken when the channel is as large
 * as the whole sample) would copy the data of another channel. *sample: bytes of one sample of
 * the channel. */
static size_t channel_bytes(const struct blk *b, const struct iio_channel *ch, size_t *sample)
{
	const struct iio_channels_mask *mask = mask_of(b);
	const struct iio_data_format *fmt = iio_channel_get_data_format(ch);
	size_t length = (size_t)fmt->length / 8 * fmt->repeat;
	ssize_t step;

	if (sample)
		*sample = length;
	if (!mask || !iio_channel_is_enabled(ch, mask))
		return 0;
	step = iio_device_get_sample_size(iio_channel_get_device(ch), mask);
	if (step <= 0)
		return 0;
	return b->size / (size_t)step * length;
}

/* u8 = channel_read(ch, blk, raw): the channel's samples, converted by libiio to host format,
 * or as they are in the block (raw). 1-by-0 when the channel is not enabled. */
static int channel_read(struct gw_call *c)
{
	const struct iio_channel *ch;
	struct blk b;
	mxArray *out;
	size_t len;
	bool raw;
	int ret;

	ret = arg_block(c, 1, &b);
	if (!ret)
		ret = arg_channel_of(c, 0, &b, &ch);
	if (!ret)
		ret = gw_arg_bool(c, 2, &raw);
	if (!ret)
		ret = data_access(c, &b);
	if (ret)
		return ret;
	len = channel_bytes(&b, ch, NULL);
	out = mxCreateUninitNumericMatrix(1, len, mxUINT8_CLASS, mxREAL);
	if (!out)
		return -ENOMEM;
	c->plhs[0] = out;       /* the dispatcher destroys it if this command fails */
	if (len && iio_channel_read(ch, b.ptr, mxGetData(out), len, raw) != len)
		return gw_fail(c, -EIO, "libiio read fewer bytes than the block holds for this channel");
	return 0;
}

/* n = channel_write(ch, blk, u8, raw): bytes consumed, always whole samples of the channel
 * (libiio's raw shortcut would copy a trailing part of a sample as it comes); 0 for a channel
 * that is not enabled */
static int channel_write(struct gw_call *c)
{
	const struct iio_channel *ch;
	const uint8_t *data;
	size_t n, len, sample;
	struct blk b;
	bool raw;
	int ret;

	ret = arg_block(c, 1, &b);
	if (!ret)
		ret = arg_channel_of(c, 0, &b, &ch);
	if (!ret)
		ret = gw_arg_bytes(c, 2, &data, &n);
	if (!ret)
		ret = gw_arg_bool(c, 3, &raw);
	if (!ret)
		ret = data_access(c, &b);
	if (!ret)
		ret = gw_ret_double(c, 0, 0);
	if (ret)
		return ret;
	len = channel_bytes(&b, ch, &sample);
	if (n < len)
		len = sample ? n - n % sample : 0;
	if (len)
		gw_set_double(c, 0, (double)iio_channel_write(ch, b.ptr, data, len, raw));
	return 0;
}

/* ------------------------------------------------------------------ bytes */

static int block_size(struct gw_call *c)
{
	struct blk b;
	int ret = arg_block(c, 0, &b);

	return ret ? ret : gw_ret_double(c, 0, (double)b.size);
}

/* u8 = block_get_bytes(blk [, off0, count]): default the whole block from off0 on */
static int block_get_bytes(struct gw_call *c)
{
	size_t off, count;
	struct blk b;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = arg_size(c, 1, 0, &off);
	if (!ret && off > b.size)
		ret = gw_fail(c, -EINVAL, "offset %zu is outside the block of %zu bytes", off, b.size);
	if (!ret)
		ret = arg_size(c, 2, b.size - off, &count);
	if (!ret && count > b.size - off)
		ret = gw_fail(c, -EINVAL, "%zu bytes from offset %zu do not fit the block of %zu bytes",
			count, off, b.size);
	if (!ret)
		ret = data_access(c, &b);
	return ret ? ret : gw_ret_bytes(c, 0, b.start + off, count);
}

/* n = block_set_bytes(blk, u8 [, off0]) */
static int block_set_bytes(struct gw_call *c)
{
	const uint8_t *data;
	size_t n, off;
	struct blk b;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = gw_arg_bytes(c, 1, &data, &n);
	if (!ret)
		ret = arg_size(c, 2, 0, &off);
	if (!ret && (off > b.size || n > b.size - off))
		ret = gw_fail(c, -EINVAL, "%zu bytes at offset %zu do not fit the block of %zu bytes",
			n, off, b.size);
	if (!ret)
		ret = data_access(c, &b);
	if (!ret)
		ret = gw_ret_double(c, 0, 0);
	if (ret)
		return ret;
	if (n) {
		memcpy(b.start + off, data, n);
		gw_set_double(c, 0, (double)n);
	}
	return 0;
}

/* ------------------------------------------------------------------ converter cache
 * One per buffer stream, in the cache slot of its entry; four converters, the least recently
 * used one is replaced. A converter is found again by everything it was made from: the
 * channels in order, a copy of each channel's data format (libiio reads scale and offset
 * again on iio_channel_refresh_format), the host type and the spec. The layout always comes
 * from the stream's own mask, which does not change while the stream is open. */

#define CACHE_SLOTS 4u

struct conv_entry {
	struct iio_conv *conv;                  /* NULL: free */
	void *mem;                              /* holds the four arrays below */
	const struct iio_channel **chans;
	struct iio_data_format *fmts;
	double *gain, *offset;                  /* NULL: none given */
	unsigned int nb_chans;
	enum iio_conv_type type;
	enum iio_conv_scale scale;
	bool complex_iq, raw;
	uint64_t used;                          /* value of the cache's clock at the last use */
};

struct conv_cache {
	struct conv_entry e[CACHE_SLOTS];
	uint64_t clock;
};

static void entry_release(struct conv_entry *e)
{
	if (e->conv)
		iio_conv_destroy(e->conv);
	free(e->mem);
	memset(e, 0, sizeof(*e));
}

static void cache_free(void *p)
{
	struct conv_cache *cache = p;

	for (unsigned int i = 0; i < CACHE_SLOTS; i++)
		entry_release(&cache->e[i]);
	free(cache);
}

/* field by field: the struct has padding */
static bool same_format(const struct iio_data_format *a, const struct iio_data_format *b)
{
	return a->length == b->length && a->bits == b->bits && a->shift == b->shift &&
		a->is_signed == b->is_signed && a->is_fully_defined == b->is_fully_defined &&
		a->is_be == b->is_be && a->with_scale == b->with_scale &&
		!memcmp(&a->scale, &b->scale, sizeof(a->scale)) && a->repeat == b->repeat &&
		!memcmp(&a->offset, &b->offset, sizeof(a->offset)) && a->is_float == b->is_float;
}

static bool same_doubles(const double *a, const double *b, unsigned int n)
{
	return (!a && !b) || (a && b && !memcmp(a, b, n * sizeof(*a)));
}

static bool entry_matches(const struct conv_entry *e, const struct iio_channel *const *chans,
		unsigned int nb_chans, const struct iio_conv_spec *spec)
{
	if (!e->conv || e->nb_chans != nb_chans || e->type != spec->type || e->scale != spec->scale ||
	    e->complex_iq != spec->complex_iq || e->raw != spec->raw ||
	    memcmp(e->chans, chans, nb_chans * sizeof(*chans)) ||
	    !same_doubles(e->gain, spec->user_gain, nb_chans) ||
	    !same_doubles(e->offset, spec->user_offset, nb_chans))
		return false;
	for (unsigned int i = 0; i < nb_chans; i++)
		if (!same_format(&e->fmts[i], iio_channel_get_data_format(chans[i])))
			return false;
	return true;
}

/* Takes conv over; on failure (-ENOMEM) conv is still the caller's. */
static int entry_fill(struct conv_entry *e, struct iio_conv *conv, const struct iio_channel *const *chans,
		unsigned int nb_chans, const struct iio_conv_spec *spec)
{
	size_t n = nb_chans;
	char *mem = malloc(n * (2 * sizeof(double) + sizeof(*e->chans) + sizeof(*e->fmts)));

	if (!mem)
		return -ENOMEM;
	entry_release(e);
	e->mem = mem;
	/* doubles first, then pointers, then the formats: each part is aligned for its type */
	if (spec->user_gain) {
		e->gain = (double *)(void *)mem;
		memcpy(e->gain, spec->user_gain, n * sizeof(double));
	}
	if (spec->user_offset) {
		e->offset = (double *)(void *)(mem + n * sizeof(double));
		memcpy(e->offset, spec->user_offset, n * sizeof(double));
	}
	e->chans = (const struct iio_channel **)(void *)(mem + 2 * n * sizeof(double));
	memcpy(e->chans, chans, n * sizeof(*chans));
	e->fmts = (struct iio_data_format *)(void *)(mem + n * (2 * sizeof(double) + sizeof(*e->chans)));
	for (unsigned int i = 0; i < nb_chans; i++)
		e->fmts[i] = *iio_channel_get_data_format(chans[i]);
	e->nb_chans = nb_chans;
	e->type = spec->type;
	e->scale = spec->scale;
	e->complex_iq = spec->complex_iq;
	e->raw = spec->raw;
	e->conv = conv;
	return 0;
}

/* The converter for these channels and this spec on the buffer stream bs. *temp: the gateway
 * does not hold the buffer stream (bs is 0), so there is nowhere to keep the converter: the
 * caller destroys it, and allocates nothing with mx* while it has it. */
static int get_conv(struct gw_call *c, gw_id bs, const struct iio_channels_mask *mask,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		const struct iio_conv_spec *spec, struct iio_conv **conv, bool *temp)
{
	struct conv_cache *cache = bs ? gw_handle_cache(bs) : NULL;
	struct conv_entry *victim;
	int ret;

	*temp = false;
	for (unsigned int i = 0; cache && i < CACHE_SLOTS; i++) {
		if (entry_matches(&cache->e[i], chans, nb_chans, spec)) {
			cache->e[i].used = ++cache->clock;
			*conv = cache->e[i].conv;
			return 0;
		}
	}

	ret = iio_conv_create(mask, chans, nb_chans, spec, conv);
	if (ret)
		return gw_fail(c, ret, "no converter for these channels with this class, scale and raw setting");
	if (!bs) {
		*temp = true;
		return 0;
	}
	if (!cache) {
		cache = calloc(1, sizeof(*cache));
		if (!cache) {
			iio_conv_destroy(*conv);
			return -ENOMEM;
		}
		gw_handle_set_cache(bs, cache, cache_free);
	}
	victim = &cache->e[0];
	for (unsigned int i = 1; i < CACHE_SLOTS && victim->conv; i++)
		if (!cache->e[i].conv || cache->e[i].used < victim->used)
			victim = &cache->e[i];
	ret = entry_fill(victim, *conv, chans, nb_chans, spec);
	if (ret) {
		iio_conv_destroy(*conv);
		return ret;
	}
	victim->used = ++cache->clock;
	return 0;
}

/* ------------------------------------------------------------------ converted read and write */

/* What block_read and block_write share. chans, gain and offset are per-call scratch (mxMalloc). */
struct xfer {
	const struct iio_channels_mask *mask;
	gw_id bs;                               /* 0: the gateway does not hold the buffer stream */
	size_t step, capacity;                  /* bytes per sample; whole samples in the block */
	struct iio_conv_spec spec;
	mxClassID cls;
	const struct iio_channel **chans;
	unsigned int nb_chans;
	double *gain, *offset;
};

static void xfer_end(struct xfer *x)
{
	if (x->chans)
		mxFree((void *)x->chans);
	if (x->gain)
		mxFree(x->gain);
	if (x->offset)
		mxFree(x->offset);
	x->chans = NULL;
	x->gain = x->offset = NULL;
}

/* The option `channels`: a uint64 vector of channel ids, columns in that order, none twice (as
 * on the fast path: on a write the later column would overwrite the earlier one); not given or
 * empty: every scan element enabled in the stream's mask, in device order. */
static int xfer_channels(struct gw_call *c, const mxArray *opts, const struct iio_device *dev, struct xfer *x)
{
	const struct iio_channel *ch;
	const mxArray *f;
	const gw_id *ids;
	unsigned int n;

	if (dp_opt_field(opts, "channels", &f))
		return gw_fail(c, -EINVAL, "the options are [] or a scalar struct");
	if (f && (mxGetClassID(f) != mxUINT64_CLASS || mxIsComplex(f) || mxIsSparse(f) ||
	    mxGetNumberOfDimensions(f) != 2 ||
	    (mxGetNumberOfElements(f) && mxGetM(f) != 1 && mxGetN(f) != 1)))
		return gw_fail(c, -EINVAL, "option channels: a uint64 vector of channel ids is expected");
	if (f && mxGetNumberOfElements(f)) {
		if (mxGetNumberOfElements(f) > UINT_MAX / 2)
			return gw_fail(c, -EINVAL, "option channels: too many ids");
		n = (unsigned int)mxGetNumberOfElements(f);
		ids = mxGetData(f);
		x->chans = mxMalloc(n * sizeof(*x->chans));
		if (!ids || !x->chans)
			return -ENOMEM;
		for (unsigned int i = 0; i < n; i++) {
			ch = gw_handle_ptr(ids[i], GW_T_CHANNEL);
			if (!ch)
				return gw_fail(c, -EBADF, "option channels: element %u is not a channel", i + 1);
			if (iio_channel_get_device(ch) != dev)
				return gw_fail(c, -EINVAL, "option channels: element %u is a channel of another device", i + 1);
			for (unsigned int k = 0; k < i; k++)
				if (x->chans[k] == ch)
					return gw_fail(c, -EINVAL, "option channels: element %u is given twice", i + 1);
			x->chans[i] = ch;
		}
		x->nb_chans = n;
		return 0;
	}
	n = iio_device_get_channels_count(dev);
	if (n)
		x->chans = mxMalloc(n * sizeof(*x->chans));
	for (unsigned int i = 0; x->chans && i < n; i++) {
		ch = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(ch) && iio_channel_is_enabled(ch, x->mask))
			x->chans[x->nb_chans++] = ch;
	}
	if (!x->nb_chans)
		return gw_fail(c, -EINVAL, "no channel is enabled in the mask of the buffer stream");
	return 0;
}

/* Checks the option names, reads class, complex, scale, raw (dp_opt_spec) and the channels.
 * On an error nothing is left allocated. */
static int xfer_begin(struct gw_call *c, const struct blk *b, const mxArray *opts,
		const char *const *fields, unsigned int nb_fields, struct xfer *x)
{
	const struct iio_device *dev = dp_device_of(b->id);
	char bad[64];
	ssize_t step;
	int ret;

	memset(x, 0, sizeof(*x));
	ret = dp_opt_check(opts, fields, nb_fields, bad, sizeof(bad));
	if (ret)
		return bad[0] ? gw_fail(c, ret, "unknown option '%s'", bad) :
			gw_fail(c, ret, "the options are [] or a scalar struct");
	ret = dp_opt_spec(opts, &x->spec, &x->cls);
	if (ret)
		return gw_fail(c, ret, "one of the options class, complex, scale, raw has a wrong value");
	x->mask = mask_of(b);
	if (!dev || !x->mask)
		return gw_fail(c, -EINVAL, "the block has no device or no channels mask");
	step = iio_device_get_sample_size(dev, x->mask);
	if (step <= 0)
		return gw_fail(c, -EINVAL, "the buffer stream has no sample size");
	x->step = (size_t)step;
	x->capacity = b->size / x->step;
	x->bs = gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(b->ptr));
	ret = xfer_channels(c, opts, dev, x);
	if (ret)
		xfer_end(x);
	return ret;
}

/* After spec.type and spec.complex_iq are final: the pairing check and the options gain, offset. */
static int xfer_gains(struct gw_call *c, const mxArray *opts, struct xfer *x)
{
	int ret;

	if (x->spec.complex_iq && x->nb_chans % 2)
		return gw_fail(c, -EINVAL, "complex needs an even number of channels (I/Q pairs), not %u", x->nb_chans);
	ret = dp_opt_gains(opts, x->nb_chans, &x->spec, &x->gain, &x->offset);
	if (ret)
		return gw_fail(c, ret, "options gain, offset: a real double scalar or one value per column is expected");
	return 0;
}

/* get_conv() and the check that the converter and the gateway agree on the sample size, on
 * which every bound below rests. */
static int xfer_conv(struct gw_call *c, const struct xfer *x, unsigned int nb_chans, struct iio_conv **conv, bool *temp)
{
	int ret = get_conv(c, x->bs, x->mask, x->chans, nb_chans, &x->spec, conv, temp);

	if (!ret && iio_conv_get_step(*conv) != x->step) {
		if (*temp)
			iio_conv_destroy(*conv);
		*temp = false;
		ret = gw_fail(c, -EFAULT, "gateway bug: the converter has another sample size");
	}
	return ret;
}

/* data = block_read(blk, opts): N-by-C array of opts.class, N = min(opts.samples, capacity).
 * The output is created once, uninitialised, and every element is written. */
static int block_read(struct gw_call *c)
{
	static const char *const fields[] = { "channels", "class", "complex", "scale", "raw", "gain", "offset", "samples" };
	const mxArray *opts = c->prhs[1];
	struct iio_conv *conv;
	long long samples;
	size_t rows, cols;
	struct xfer x;
	struct blk b;
	mxArray *out;
	ssize_t got;
	bool temp;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = data_access(c, &b);
	if (!ret)
		ret = xfer_begin(c, &b, opts, fields, sizeof(fields) / sizeof(fields[0]), &x);
	if (ret)
		return ret;
	ret = xfer_gains(c, opts, &x);
	if (!ret && dp_opt_int(opts, "samples", 1, LLONG_MAX, 0, &samples))
		ret = gw_fail(c, -EINVAL, "option samples: an integer of at least 1 is expected");
	if (ret)
		goto out;
	rows = x.capacity;
	if (samples && (unsigned long long)samples < rows)
		rows = (size_t)samples;
	cols = x.spec.complex_iq ? x.nb_chans / 2 : x.nb_chans;

	/* the last mx allocation of this command: a converter that is not cached is never held
	 * across one */
	out = mxCreateUninitNumericMatrix(rows, cols, x.cls, x.spec.complex_iq ? mxCOMPLEX : mxREAL);
	if (!out) {
		ret = -ENOMEM;
		goto out;
	}
	c->plhs[0] = out;       /* the dispatcher destroys it if this command fails */
	ret = xfer_conv(c, &x, x.nb_chans, &conv, &temp);
	if (ret)
		goto out;
	if (rows && cols) {
		got = iio_conv_read(conv, b.ptr, mxGetData(out), rows);
		if (got < 0)
			ret = got < -4095 ? -EIO : (int)got;
		else if ((size_t)got != rows)
			ret = gw_fail(c, -EIO, "%zd of %zu samples were converted", got, rows);
	}
	if (temp)
		iio_conv_destroy(conv);
out:
	xfer_end(&x);
	return ret;
}

/* n = block_write(blk, data, opts): rows written = min(rows of data, capacity). The class is
 * that of data. opts.complex (default: data is complex) says whether a column is an I/Q pair;
 * with real data such a column goes to the I channel and the Q channel is written as zero
 * (MATLAB hands a complex array whose imaginary part is all zero to a MEX file as a real one).
 * For that reason the pair list is checked in full, with the converter complex data would use,
 * before anything is written: what is an error must not depend on the imaginary part. */
static int block_write(struct gw_call *c)
{
	static const char *const fields[] = { "channels", "complex", "scale", "raw", "gain", "offset" };
	const mxArray *data = c->prhs[1], *opts = c->prhs[2];
	const struct iio_channel **iq = NULL;
	unsigned int nb_conv, pairs;
	struct iio_conv *conv;
	bool data_complex, want_complex, temp;
	size_t rows, cols;
	struct xfer x;
	struct blk b;
	ssize_t n;
	int ret;

	ret = arg_block(c, 0, &b);
	if (!ret)
		ret = data_access(c, &b);
	if (ret)
		return ret;
	ret = xfer_begin(c, &b, opts, fields, sizeof(fields) / sizeof(fields[0]), &x);
	if (ret)
		return ret;
	if (dp_check_matrix(data, &x.spec.type, &data_complex)) {
		ret = gw_fail(c, -EINVAL, "argument 2: a full numeric matrix is expected");
		goto out;
	}
	if (dp_opt_bool(opts, "complex", data_complex, &want_complex)) {
		ret = gw_fail(c, -EINVAL, "option complex: a logical scalar is expected");
		goto out;
	}
	if (data_complex && !want_complex) {
		ret = gw_fail(c, -EINVAL, "argument 2: complex data, but the option complex is false");
		goto out;
	}
	x.spec.complex_iq = want_complex;
	ret = xfer_gains(c, opts, &x);
	if (ret)
		goto out;
	rows = mxGetM(data);
	cols = want_complex ? x.nb_chans / 2 : x.nb_chans;
	if (mxGetN(data) != cols) {
		ret = gw_fail(c, -EINVAL, "argument 2: the data has %zu columns, the channels need %zu%s",
			mxGetN(data), cols, want_complex ? " (one per I/Q pair)" : "");
		goto out;
	}

	nb_conv = x.nb_chans;
	pairs = want_complex && !data_complex ? x.nb_chans / 2 : 0;
	if (pairs) {
		iq = mxMalloc(x.nb_chans * sizeof(*iq));
		if (!iq) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* result first: the last mx allocation, before anything is written to the block */
	ret = gw_ret_double(c, 0, 0);
	if (ret || !rows)
		goto out;
	if (pairs) {
		/* real columns on I/Q pairs. First the converter complex data would get: it refuses
		 * a list that is not made of pairs (a Q channel that is no scan element, is not in
		 * the stream's mask or has another format than its I channel). Then a converter for
		 * the I channels alone; the Q channels, kept behind them in the list, are cleared
		 * below. */
		ret = xfer_conv(c, &x, x.nb_chans, &conv, &temp);
		if (ret)
			goto out;
		if (temp)
			iio_conv_destroy(conv);
		for (unsigned int k = 0; k < pairs; k++) {
			iq[k] = x.chans[2 * k];
			iq[pairs + k] = x.chans[2 * k + 1];
			if (x.gain)
				x.gain[k] = x.gain[2 * k];
			if (x.offset)
				x.offset[k] = x.offset[2 * k];
		}
		mxFree((void *)x.chans);
		x.chans = iq;
		iq = NULL;
		x.spec.complex_iq = false;
		nb_conv = pairs;
	}
	ret = xfer_conv(c, &x, nb_conv, &conv, &temp);
	if (ret)
		goto out;
	n = iio_conv_write(conv, b.ptr, mxGetData(data), rows);
	if (temp)
		iio_conv_destroy(conv);
	if (n < 0) {
		ret = n < -4095 ? -EIO : (int)n;
		goto out;
	}
	for (unsigned int k = 0; k < pairs; k++) {
		const struct iio_channel *q = x.chans[pairs + k];
		size_t len = iio_channel_get_data_format(q)->length / 8;
		size_t off = (size_t)((uintptr_t)iio_block_first(b.ptr, q) - (uintptr_t)b.start);

		for (size_t s = 0; s < (size_t)n && off + s * x.step + len <= b.size; s++)
			memset(b.start + off + s * x.step, 0, len);
	}
	gw_set_double(c, 0, (double)n);
out:
	if (iq)
		mxFree((void *)iq);
	xfer_end(&x);
	return ret;
}

static const struct gw_cmd cmds[] = {
	GW_CMD("block_destroy", block_destroy, 1, 1, 0),
	GW_CMD("block_first", block_first, 2, 2, 1),
	GW_CMD("block_enqueue", block_enqueue, 3, 3, 0),
	GW_CMD("block_dequeue", block_dequeue, 2, 2, 1),
	GW_CMD("block_get_buffer_stream", block_get_buffer_stream, 1, 1, 1),
	GW_CMD("block_get_dmabuf_fd", block_get_dmabuf_fd, 1, 1, 1),
	GW_CMD("block_disable_cpu_access", block_disable_cpu_access, 2, 2, 0),
	GW_CMD("channel_read", channel_read, 3, 3, 1),
	GW_CMD("channel_write", channel_write, 4, 4, 1),
	GW_XCMD("block_size", block_size, 1, 1, 1),
	GW_XCMD("block_get_bytes", block_get_bytes, 1, 3, 1),
	GW_XCMD("block_set_bytes", block_set_bytes, 2, 3, 1),
	GW_XCMD("block_read", block_read, 2, 2, 1),
	GW_XCMD("block_write", block_write, 3, 3, 1),
};

const struct gw_group gw_group_block = GW_GROUP("block", cmds);
