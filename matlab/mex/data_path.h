/* SPDX-License-Identifier: MIT */
/* Conventions of the data path that more than one cmd_<group>.c shares: entry flags, device
 * lookup, registration of masks, buffer streams and blocks, class and option parsing.
 * Header only; every function is static inline; errors are negative errno values (the caller
 * turns them into a message with gw_fail()). */
#ifndef IIO_MEX_DATA_PATH_H
#define IIO_MEX_DATA_PATH_H

#include "mx_util.h"
#include <iio/iio-conv.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define BS_STARTED     1u   /* GW_T_BUFFER_STREAM flags: set by buffer_stream_start, cleared by buffer_stream_stop */
#define BLK_ENQUEUED   1u   /* GW_T_BLOCK flags: set by a successful enqueue; cleared by a successful dequeue and
                               by a dequeue for which libiio returned -EPERM (libiio says: not enqueued);
                               for a stream's blocks: set on every block except the one returned last */
#define BLK_CPU_OFF    2u   /* set / cleared by a successful block_disable_cpu_access */

/* The most blocks of one stream (buffer_create_stream and conv_stream_open). libiio creates
 * every block before the call returns, in one MEX call that nothing can interrupt, and
 * destroying an output stream dequeues each one: without a limit a mistyped count hangs
 * MATLAB and takes its memory. */
#define DP_MAX_NB_BLOCKS 4096

/* ------------------------------------------------------------------ devices */

/* Device of a device, channel, buffer, mask, buffer stream, block, stream, event stream or conv
 * stream id: the entry itself (device), iio_channel_get_device, iio_buffer_get_device, else the
 * device of its parent entry. NULL if the id is zero or stale. (Also NULL for an id without a
 * device above it: a context, a scan, an attribute.) */
static inline const struct iio_device *dp_device_of(gw_id id)
{
	while (id) {
		switch (gw_handle_type(id)) {
		case GW_T_NONE:
			return NULL;
		case GW_T_DEVICE:
			return gw_handle_ptr(id, GW_T_DEVICE);
		case GW_T_CHANNEL: {
			const struct iio_channel *chn = gw_handle_ptr(id, GW_T_CHANNEL);

			return chn ? iio_channel_get_device(chn) : NULL;
		}
		case GW_T_BUFFER: {
			const struct iio_buffer *buf = gw_handle_ptr(id, GW_T_BUFFER);

			return buf ? iio_buffer_get_device(buf) : NULL;
		}
		default:
			id = gw_handle_parent(id);
			break;
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------ registration */

static inline void dp_mask_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_channels_mask_destroy(ptr);
}

static inline void dp_bufstream_pre_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	/* the entry is still live here: its flags say whether the buffer was started */
	iio_buffer_stream_cancel(ptr);
	if (gw_handle_flags(gw_handle_find(GW_T_BUFFER_STREAM, ptr)) & BS_STARTED)
		iio_buffer_stream_stop(ptr);
}

static inline void dp_bufstream_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_buffer_close(ptr);
}

static inline void dp_block_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_block_destroy(ptr);
}

/* Registration of the three object types that more than one file creates. Owned child of the
 * parent; destroy op iio_channels_mask_destroy / iio_buffer_close / iio_block_destroy. The buffer
 * stream also gets a pre_destroy op: iio_buffer_stream_cancel(), then iio_buffer_stream_stop() if
 * gw_handle_flags(gw_handle_find(GW_T_BUFFER_STREAM, bs)) has BS_STARTED. Returns 0
 * when the table cannot take it (the caller then destroys the object and returns -ENOMEM).
 * The parent must be a live GW_T_DEVICE (mask), GW_T_BUFFER (buffer stream) or
 * GW_T_BUFFER_STREAM (block) entry, else 0 too: under any other parent the object would be
 * destroyed in the wrong order (a block after its buffer stream is a use after free), and
 * under parent 0 it would be a root that outlives its context. */
static inline gw_id dp_add_mask(struct iio_channels_mask *mask, gw_id dev)
{
	static const struct gw_ops ops = { NULL, dp_mask_destroy_op };

	if (gw_handle_type(dev) != GW_T_DEVICE)
		return 0;
	return gw_handle_add(GW_T_MASK, mask, dev, true, &ops, NULL);
}

static inline gw_id dp_add_bufstream(struct iio_buffer_stream *bs, gw_id buf)
{
	static const struct gw_ops ops = { dp_bufstream_pre_destroy_op, dp_bufstream_destroy_op };

	if (gw_handle_type(buf) != GW_T_BUFFER)
		return 0;
	return gw_handle_add(GW_T_BUFFER_STREAM, bs, buf, true, &ops, NULL);
}

static inline gw_id dp_add_block(struct iio_block *blk, gw_id bs)
{
	static const struct gw_ops ops = { NULL, dp_block_destroy_op };

	if (gw_handle_type(bs) != GW_T_BUFFER_STREAM)
		return 0;
	return gw_handle_add(GW_T_BLOCK, blk, bs, true, &ops, NULL);
}

/* ------------------------------------------------------------------ classes and scales */

struct dp_class {
	const char *name;
	enum iio_conv_type type;
	mxClassID cls;
};

#define DP_NB_CLASSES 10u

/* The ten numeric classes of the data path. */
static inline const struct dp_class *dp_classes(void)
{
	static const struct dp_class table[DP_NB_CLASSES] = {
		{ "double", IIO_CONV_F64, mxDOUBLE_CLASS },
		{ "single", IIO_CONV_F32, mxSINGLE_CLASS },
		{ "int8",   IIO_CONV_I8,  mxINT8_CLASS },
		{ "uint8",  IIO_CONV_U8,  mxUINT8_CLASS },
		{ "int16",  IIO_CONV_I16, mxINT16_CLASS },
		{ "uint16", IIO_CONV_U16, mxUINT16_CLASS },
		{ "int32",  IIO_CONV_I32, mxINT32_CLASS },
		{ "uint32", IIO_CONV_U32, mxUINT32_CLASS },
		{ "int64",  IIO_CONV_I64, mxINT64_CLASS },
		{ "uint64", IIO_CONV_U64, mxUINT64_CLASS },
	};

	return table;
}

/* "double", "single", "int8" ... "uint64" (exact spelling). type and cls may be NULL. */
static inline int dp_class_from_name(const char *name, enum iio_conv_type *type, mxClassID *cls)
{
	const struct dp_class *t = dp_classes();

	for (unsigned int i = 0; name && i < DP_NB_CLASSES; i++) {
		if (strcmp(name, t[i].name))
			continue;
		if (type)
			*type = t[i].type;
		if (cls)
			*cls = t[i].cls;
		return 0;
	}
	return -EINVAL;
}

static inline int dp_class_from_id(mxClassID cls, enum iio_conv_type *type)
{
	const struct dp_class *t = dp_classes();

	for (unsigned int i = 0; i < DP_NB_CLASSES; i++) {
		if (cls != t[i].cls)
			continue;
		if (type)
			*type = t[i].type;
		return 0;
	}
	return -EINVAL;
}

/* IIO_CONV_I16 -> mxINT16_CLASS, ...; mxUNKNOWN_CLASS for a value that is no conversion type. */
static inline mxClassID dp_class_of_type(enum iio_conv_type type)
{
	const struct dp_class *t = dp_classes();

	for (unsigned int i = 0; i < DP_NB_CLASSES; i++)
		if (type == t[i].type)
			return t[i].cls;
	return mxUNKNOWN_CLASS;
}

/* "none", "iio", "fullscale" */
static inline int dp_scale_from_name(const char *name, enum iio_conv_scale *scale)
{
	if (!name)
		return -EINVAL;
	if (!strcmp(name, "none"))
		*scale = IIO_CONV_SCALE_NONE;
	else if (!strcmp(name, "iio"))
		*scale = IIO_CONV_SCALE_IIO;
	else if (!strcmp(name, "fullscale"))
		*scale = IIO_CONV_SCALE_FULLSCALE;
	else
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ arrays */

/* "Absent", for an ARGUMENT (the options argument itself): no array, or [] (0x0 double, as
 * gw_arg_given() defines it). A 1x0 or 0x3 double, a 0x0x0 double, a sparse [] are not absent. */
static inline bool dp_is_absent(const mxArray *a)
{
	return !a || (mxGetClassID(a) == mxDOUBLE_CLASS && !mxIsSparse(a) &&
		mxGetNumberOfDimensions(a) == 2 && mxGetM(a) == 0 && mxGetN(a) == 0);
}

/* "Not given", for an option value: no array, or a full 2-D double without elements ([], and
 * also 1x0, 0x1, 0x3: MATLAB code writes double.empty(1,0) and zeros(1,0) for "none"). Wider than
 * dp_is_absent() on purpose; an N-D, a sparse or a non-double empty array is a value. */
static inline bool dp_is_unset(const mxArray *a)
{
	return !a || (mxGetClassID(a) == mxDOUBLE_CLASS && !mxIsSparse(a) &&
		mxGetNumberOfDimensions(a) == 2 && mxGetNumberOfElements(a) == 0);
}

/* a: not given (dp_is_unset(): [] or another empty double matrix; then *out = NULL and nothing
 * else is checked), a real double scalar, or a real double vector with one entry per output
 * column. *out gets nb_chans entries (mxMalloc, the caller mxFree()s it: per-call scratch; the
 * converter and the stream layer copy the values; a complex pair gets its column's value twice).
 * -EINVAL (and *out = NULL) for anything else, for nb_chans of 0 and for an odd nb_chans with
 * is_complex. */
static inline int dp_per_channel(const mxArray *a, unsigned int nb_chans, bool is_complex, double **out)
{
	unsigned int per = is_complex ? 2 : 1;
	const double *v;
	double *p;
	size_t n;

	*out = NULL;
	if (dp_is_unset(a))
		return 0;
	if (mxGetClassID(a) != mxDOUBLE_CLASS || mxIsSparse(a) || mxIsComplex(a) ||
	    mxGetNumberOfDimensions(a) != 2 || (mxGetM(a) != 1 && mxGetN(a) != 1))
		return -EINVAL;
	if (!nb_chans || nb_chans % per)
		return -EINVAL;
	n = mxGetNumberOfElements(a);
	v = mxGetData(a);
	if (!v || (n != 1 && n != nb_chans / per))
		return -EINVAL;
	p = mxMalloc(nb_chans * sizeof(*p));
	if (!p)
		return -ENOMEM;
	for (unsigned int i = 0; i < nb_chans; i++)
		p[i] = v[n == 1 ? 0 : i / per];
	*out = p;
	return 0;
}

/* full numeric 2-D array of one of the ten classes; tests mxGetNumberOfDimensions(a) == 2 itself
 * (mxGetM / mxGetN alone take a 4-by-1-by-2 array for a 4-by-2 matrix). An empty one (0-by-2,
 * 0-by-0) is accepted: the caller decides about row and column counts. type and is_complex may
 * be NULL. */
static inline int dp_check_matrix(const mxArray *a, enum iio_conv_type *type, bool *is_complex)
{
	enum iio_conv_type t;

	if (!a || mxIsSparse(a) || mxGetNumberOfDimensions(a) != 2 || dp_class_from_id(mxGetClassID(a), &t))
		return -EINVAL;
	if (type)
		*type = t;
	if (is_complex)
		*is_complex = mxIsComplex(a);
	return 0;
}

/* ------------------------------------------------------------------ options
 * Options are ONE scalar struct argument; [] (or an argument that was not given: NULL) means
 * "no options". Anything else (a struct array, an empty struct, a number, a cell) is -EINVAL
 * from every function below. A field that is missing or holds an empty double matrix
 * (dp_is_unset(): [], double.empty(1,0), zeros(0,1)) takes its default: the M classes declare
 * double.empty(1,0) as the default of Gain, Offset and Samples and pass it on unchanged. */

/* *f = the value of the field, or NULL when it takes its default. */
static inline int dp_opt_field(const mxArray *opts, const char *field, const mxArray **f)
{
	const mxArray *v;

	*f = NULL;
	if (dp_is_absent(opts))
		return 0;
	if (!mxIsStruct(opts) || !mxIsScalar(opts))
		return -EINVAL;
	v = mxGetField(opts, 0, field);
	if (!dp_is_unset(v))
		*f = v;
	return 0;
}

/* opts: [] or a scalar struct. A field outside `allowed` is -EINVAL, its name copied to bad.
 * bad (may be NULL when len is 0) is "" in every other case. */
static inline int dp_opt_check(const mxArray *opts, const char *const *allowed, unsigned int nb, char *bad, size_t len)
{
	int nfields;

	if (bad && len)
		bad[0] = '\0';
	if (dp_is_absent(opts))
		return 0;
	if (!mxIsStruct(opts) || !mxIsScalar(opts))
		return -EINVAL;
	nfields = mxGetNumberOfFields(opts);
	for (int i = 0; i < nfields; i++) {
		const char *name = mxGetFieldNameByNumber(opts, i);
		unsigned int k = 0;

		while (name && k < nb && strcmp(name, allowed[k]))
			k++;
		if (name && k < nb)
			continue;
		if (bad && len) {
			strncpy(bad, name ? name : "", len - 1);
			bad[len - 1] = '\0';
		}
		return -EINVAL;
	}
	return 0;
}

/* A real, full scalar of one of the ten numeric classes (not logical, not char), as a double:
 * NaN, Inf and fractions included, and an int64 / uint64 rounded to a double. For a value that
 * is a double in C too (a frequency, a gain); for a count or an index use dp_opt_int().
 * No command has such an option yet: kept, and tested, for the first one that does. */
static inline int dp_opt_number(const mxArray *opts, const char *field, double dflt, double *out)
{
	const mxArray *f;
	int ret = dp_opt_field(opts, field, &f);

	if (ret)
		return ret;
	if (!f) {
		*out = dflt;
		return 0;
	}
	if (!mxIsNumeric(f) || mxIsSparse(f) || mxIsComplex(f) || !mxIsScalar(f) || !mxGetData(f))
		return -EINVAL;
	*out = mxGetScalar(f);
	return 0;
}

/* A real, full, integer-valued scalar of one of the ten numeric classes in [lo, hi], else -EINVAL
 * (and *out untouched): NaN, Inf, a fraction, a value outside the range. int64 and uint64 are read
 * from the integer data, never through double. dflt is returned as it is (it may lie outside the
 * range, as a marker for "not given"). THE way from an option to a C integer (samples, nb_blocks,
 * buffer, timeout_ms): converting a double that dp_opt_number() returned is undefined behaviour
 * for NaN and for 1e300, and no sanitizer of the mock tests reports it. */
static inline int dp_opt_int(const mxArray *opts, const char *field, long long lo, long long hi,
		long long dflt, long long *out)
{
	const mxArray *f;
	int ret = dp_opt_field(opts, field, &f);
	long long v;

	if (ret)
		return ret;
	if (!f) {
		*out = dflt;
		return 0;
	}
	if (!mxIsNumeric(f) || mxIsSparse(f) || mxIsComplex(f) || !mxIsScalar(f) || !mxGetData(f))
		return -EINVAL;
	if (mxGetClassID(f) == mxINT64_CLASS) {
		v = *(const int64_t *)mxGetData(f);
	} else if (mxGetClassID(f) == mxUINT64_CLASS) {
		uint64_t u = *(const uint64_t *)mxGetData(f);

		if (u > (uint64_t)LLONG_MAX)
			return -EINVAL;
		v = (long long)u;
	} else {
		double d = mxGetScalar(f);

		/* -2^63 <= d < 2^63 (false for NaN), so that the conversion is defined */
		if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
			return -EINVAL;
		v = (long long)d;
		if ((double)v != d)
			return -EINVAL;
	}
	if (v < lo || v > hi)
		return -EINVAL;
	*out = v;
	return 0;
}

/* A logical scalar. */
static inline int dp_opt_bool(const mxArray *opts, const char *field, bool dflt, bool *out)
{
	const mxArray *f;
	int ret = dp_opt_field(opts, field, &f);

	if (ret)
		return ret;
	if (!f) {
		*out = dflt;
		return 0;
	}
	if (!mxIsLogical(f) || mxIsSparse(f) || !mxIsScalar(f))
		return -EINVAL;
	*out = mxIsLogicalScalarTrue(f);
	return 0;
}

/* Text (a char row or a 2-D empty char) as UTF-8 in out, which holds len bytes; -EINVAL when it
 * does not fit (the default too; nothing fits a len of 0, and out is then not written) or holds
 * a NUL character anywhere. dflt may be NULL ("" then). */
static inline int dp_opt_string(const mxArray *opts, const char *field, const char *dflt, char *out, size_t len)
{
	const mxArray *f;
	int ret = dp_opt_field(opts, field, &f);
	const mxChar *w;
	size_t n, nc;
	char *s;

	if (ret)
		return ret;
	if (!len)
		return -EINVAL;
	out[0] = '\0';
	if (!f) {
		if (!dflt)
			return 0;
		if (strlen(dflt) >= len)
			return -EINVAL;
		strcpy(out, dflt);
		return 0;
	}
	if (!mxIsChar(f) || mxGetNumberOfDimensions(f) != 2 ||
	    (mxGetM(f) != 1 && mxGetNumberOfElements(f) != 0))
		return -EINVAL;
	/* a NUL would end the C string early: 'iio' followed by NUL and more is not 'iio'. Looked for
	 * in the characters, not in the byte count of the UTF-8 text (non-ASCII characters before
	 * the NUL make that count up) */
	nc = mxGetNumberOfElements(f);
	w = mxGetData(f);
	for (size_t i = 0; i < nc; i++)
		if (!w || w[i] == 0)
			return -EINVAL;
	s = mxArrayToUTF8String(f);
	if (!s)
		return -EINVAL;
	n = strlen(s);
	if (n >= len) {
		mxFree(s);
		return -EINVAL;
	}
	memcpy(out, s, n + 1);
	mxFree(s);
	return 0;
}

/* reads the fields class, complex, scale, raw; sets layout COLUMNS, saturate true, no gains. cls may be NULL.
 * Defaults: class 'double', complex false, scale 'none', raw false. What the fields mean together
 * (a scale with an integer class, raw with scale 'iio') is the converter's decision, not checked here. */
static inline int dp_opt_spec(const mxArray *opts, struct iio_conv_spec *spec, mxClassID *cls)
{
	char name[16];
	mxClassID id;
	int ret;

	memset(spec, 0, sizeof(*spec));
	spec->layout = IIO_CONV_COLUMNS;
	spec->saturate = true;

	ret = dp_opt_string(opts, "class", "double", name, sizeof(name));
	if (!ret)
		ret = dp_class_from_name(name, &spec->type, &id);
	if (!ret)
		ret = dp_opt_bool(opts, "complex", false, &spec->complex_iq);
	if (!ret)
		ret = dp_opt_string(opts, "scale", "none", name, sizeof(name));
	if (!ret)
		ret = dp_scale_from_name(name, &spec->scale);
	if (!ret)
		ret = dp_opt_bool(opts, "raw", false, &spec->raw);
	if (ret)
		return ret;
	if (cls)
		*cls = id;
	return 0;
}

/* reads the fields gain, offset with dp_per_channel(.., nb_chans, spec->complex_iq, ..) into *gain / *offset
 * (mxMalloc, the caller mxFree()s both) and points spec->user_gain / user_offset at them. Call after the
 * caller has fixed spec->type and spec->complex_iq (block_write takes the type from the data array and
 * complex_iq from its option `complex`, else from the data array). A field that is not given leaves
 * its pointers NULL; on an error all four are NULL and nothing is left allocated. */
static inline int dp_opt_gains(const mxArray *opts, unsigned int nb_chans, struct iio_conv_spec *spec,
		double **gain, double **offset)
{
	const mxArray *g, *o;
	int ret;

	*gain = NULL;
	*offset = NULL;
	spec->user_gain = NULL;
	spec->user_offset = NULL;

	ret = dp_opt_field(opts, "gain", &g);
	if (!ret)
		ret = dp_opt_field(opts, "offset", &o);
	if (!ret)
		ret = dp_per_channel(g, nb_chans, spec->complex_iq, gain);
	if (ret)
		return ret;
	ret = dp_per_channel(o, nb_chans, spec->complex_iq, offset);
	if (ret) {
		if (*gain)
			mxFree(*gain);
		*gain = NULL;
		return ret;
	}
	spec->user_gain = *gain;
	spec->user_offset = *offset;
	return 0;
}

#endif
