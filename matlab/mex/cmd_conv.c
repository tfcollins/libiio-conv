/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "conv": the fast path. One handle is one stream of the
 * libiio-conv stream layer (iio-conv-stream.h): a buffer stream, its blocks and its converters.
 * A frame goes between the hardware and a MATLAB array in one call, converted straight into
 * (receive) or out of (transmit) the array's own memory. */
#include "mx_util.h"
#include "data_path.h"

#include <iio/iio-conv-stream.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLOSE_DRAIN_MS 1000.0   /* longest wait of a close when the context timeout is infinite */
/* Most blocks of one stream. The stream layer creates every block before the open returns, in
 * one MEX call that nothing can interrupt: without a limit a mistyped count hangs MATLAB and
 * takes its memory. */
#define MAX_NB_BLOCKS 4096

/* Gateway-side state of a stream (the aux of its handle entry; malloc, freed by the destroy op). */
struct cs_aux {
	const struct iio_context *ctx;   /* alive as long as the entry: the stream is destroyed first */
	int timeout_ms;                  /* the wait policy given to iio_conv_stream_set_timeout() */
};

/* Destroy op. iio_conv_stream_close() first waits for the transmit frames in flight under the
 * wait policy of the stream. With the policy "the context timeout bounds the wait" and an
 * infinite context timeout that wait has no bound, and this runs in delete, clear all and at
 * MATLAB's exit, which nothing can interrupt. So in that one case the frames are awaited here,
 * with non-blocking dequeues 1 ms apart for at most CLOSE_DRAIN_MS; what has not completed by
 * then is cancelled by the close. The context timeout itself is never changed. */
static void cs_destroy(void *ptr, void *aux)
{
	const struct timespec pause = { 0, 1000000 };
	struct iio_conv_stream *s = ptr;
	struct cs_aux *a = aux;
	struct timespec start, now;

	if (a && iio_conv_stream_is_output(s) && a->timeout_ms < 0 &&
	    iio_context_get_params(a->ctx)->timeout_ms < 0) {
		iio_conv_stream_set_timeout(s, IIO_CONV_STREAM_TIMEOUT_NONBLOCK);
		clock_gettime(CLOCK_MONOTONIC, &start);
		while (iio_conv_stream_flush(s) == IIO_CONV_STREAM_NO_FRAME) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			if ((double)(now.tv_sec - start.tv_sec) * 1e3 +
			    (double)(now.tv_nsec - start.tv_nsec) * 1e-6 >= CLOSE_DRAIN_MS)
				break;
			nanosleep(&pause, NULL);
		}
	}
	iio_conv_stream_close(&s);
	free(a);
}

static const struct gw_ops cs_ops = { NULL, cs_destroy };

/* Overwrites the logical scalar false that gw_ret_bool() put in plhs[k] with true ("result
 * first": the result exists before the device is touched). Nothing when k > 0 and k >= nlhs. */
static void set_true(struct gw_call *c, int k)
{
	mxArray *a = k == 0 || k < c->nlhs ? c->plhs[k] : NULL;

	if (a && mxIsLogical(a) && mxGetLogicals(a))
		*mxGetLogicals(a) = true;
}

/* Bytes of one real element of a class of the data path. */
static size_t class_bytes(mxClassID cls)
{
	switch (cls) {
	case mxINT8_CLASS:
	case mxUINT8_CLASS:
		return 1;
	case mxINT16_CLASS:
	case mxUINT16_CLASS:
		return 2;
	case mxINT32_CLASS:
	case mxUINT32_CLASS:
	case mxSINGLE_CLASS:
		return 4;
	default:
		return 8;
	}
}

/* A text element of the channel list: a char row or a 2-D empty char without a NUL character
 * (the rule of gw_arg_str). *s is a UTF-8 copy from mxMalloc. */
static int cell_text(const mxArray *e, char **s)
{
	const mxChar *w;
	size_t n;

	*s = NULL;
	if (!e || !mxIsChar(e) || mxGetNumberOfDimensions(e) != 2 ||
	    (mxGetM(e) != 1 && mxGetNumberOfElements(e) != 0))
		return -EINVAL;
	n = mxGetNumberOfElements(e);
	w = mxGetData(e);
	for (size_t i = 0; i < n; i++)
		if (!w || w[i] == 0)
			return -EINVAL;
	*s = mxArrayToUTF8String(e);
	return *s ? 0 : -EINVAL;
}

/* The channel list (argument 1): a cell vector of channel ids as text, looked up on dev in the
 * direction of the stream, or a uint64 vector of channel handles, which must be channels of dev
 * in that direction. *out gets *nb entries in the order given (mxMalloc; the caller mxFree()s it). */
static int parse_chans(struct gw_call *c, const struct iio_device *dev, bool output,
		const struct iio_channel ***out, unsigned int *nb)
{
	const mxArray *a = c->prhs[1];
	const struct iio_channel **chans;
	const uint64_t *ids = NULL;
	bool is_cell;
	size_t n;
	int ret;

	*out = NULL;
	*nb = 0;
	is_cell = a && mxIsCell(a);
	if (!is_cell && (!a || mxGetClassID(a) != mxUINT64_CLASS || mxIsComplex(a)))
		return gw_fail(c, -EINVAL, "the channels must be a cell of channel ids (char) or a uint64 vector of channel handles");
	if (mxGetNumberOfDimensions(a) != 2 || (mxGetM(a) != 1 && mxGetN(a) != 1))
		return gw_fail(c, -EINVAL, "the channels must be a vector");
	n = mxGetNumberOfElements(a);
	if (!n || n > UINT_MAX)
		return gw_fail(c, -EINVAL, "at least one channel is needed");
	if (!is_cell) {
		ids = mxGetData(a);
		if (!ids)
			return gw_fail(c, -EINVAL, "the channels must be a uint64 vector of channel handles");
	}

	chans = mxMalloc(n * sizeof(*chans));
	if (!chans)
		return gw_fail(c, -ENOMEM, "out of memory");
	for (size_t i = 0; i < n; i++) {
		if (is_cell) {
			char *name;

			ret = cell_text(mxGetCell(a, i), &name);
			if (ret) {
				mxFree(chans);
				return gw_fail(c, ret, "channel %zu of the list is not a char row vector", i + 1);
			}
			chans[i] = iio_device_find_channel(dev, name, output);
			if (!chans[i]) {
				ret = gw_fail(c, -ENOENT, "the device has no %s channel '%.160s'",
					output ? "output" : "input", name);
				mxFree(name);
				mxFree(chans);
				return ret;
			}
			mxFree(name);
		} else {
			chans[i] = gw_handle_ptr(ids[i], GW_T_CHANNEL);
			if (!chans[i]) {
				mxFree(chans);
				return gw_fail(c, -EBADF, "channel %zu of the list is not a live channel handle", i + 1);
			}
			if (iio_channel_get_device(chans[i]) != dev) {
				mxFree(chans);
				return gw_fail(c, -EINVAL, "channel %zu of the list belongs to another device", i + 1);
			}
			if (iio_channel_is_output(chans[i]) != output) {
				mxFree(chans);
				return gw_fail(c, -EINVAL, "channel %zu of the list is an %s channel", i + 1,
					output ? "input" : "output");
			}
		}
	}
	*out = chans;
	*nb = (unsigned int)n;
	return 0;
}

static bool buffer_has(const struct iio_buffer *buf, const struct iio_channel *const *chans, unsigned int nb)
{
	unsigned int nb_scan = iio_buffer_get_scan_elements_count(buf);

	for (unsigned int i = 0; i < nb; i++) {
		unsigned int k = 0;

		while (k < nb_scan && iio_buffer_get_scan_element(buf, k) != chans[i])
			k++;
		if (k == nb_scan)
			return false;
	}
	return true;
}

/* The buffer of the stream: the one at position `index` of the device, which must have the
 * direction of the stream; with index -1 the first buffer of that direction whose scan elements
 * contain every channel. */
static int pick_buffer(struct gw_call *c, const struct iio_device *dev, long long index, bool output,
		const struct iio_channel *const *chans, unsigned int nb, struct iio_buffer **buf)
{
	unsigned int count = iio_device_get_buffers_count(dev);

	*buf = NULL;
	if (index >= 0) {
		struct iio_buffer *b = index < (long long)count ? iio_device_get_buffer(dev, (unsigned int)index) : NULL;

		if (!b)
			return gw_fail(c, -EINVAL, "option 'buffer' is %lld, the device has %u buffer(s)", index, count);
		if (iio_buffer_is_output(b) != output)
			return gw_fail(c, -EINVAL, "buffer %lld of the device is an %s buffer", index,
				output ? "input" : "output");
		*buf = b;
		return 0;
	}
	for (unsigned int i = 0; i < count; i++) {
		struct iio_buffer *b = iio_device_get_buffer(dev, i);

		if (b && iio_buffer_is_output(b) == output && buffer_has(b, chans, nb)) {
			*buf = b;
			return 0;
		}
	}
	return gw_fail(c, -EINVAL, "no %s buffer of the device holds every channel of the list",
		output ? "output" : "input");
}

/* cs = conv_stream_open(dev, chans, opts). Every argument is parsed first; then the result is
 * created, and only then the stream is opened and registered: no mx allocation follows the open,
 * so no stream can exist whose id MATLAB never received. */
static int conv_stream_open(struct gw_call *c)
{
	static const char *const fields[] = {
		"output", "buffer", "samples", "nb_blocks", "cyclic", "class", "complex", "scale", "raw",
		"gain", "offset", "timeout_ms",
	};
	const mxArray *opts = c->prhs[2], *f;
	const struct iio_channel **chans = NULL;
	double *gain = NULL, *offset = NULL;
	struct iio_conv_stream *s = NULL;
	long long samples, nb_blocks, buffer, timeout_ms;
	const struct iio_device *dev;
	struct iio_conv_spec spec;
	struct iio_buffer *buf;
	struct cs_aux *aux;
	gw_id dev_id, buf_id, id;
	bool output, cyclic;
	unsigned int nb;
	char bad[64];
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_DEVICE, &dev, &dev_id);
	if (ret)
		return ret;

	if (!opts || !mxIsStruct(opts) || !mxIsScalar(opts))
		return gw_fail(c, -EINVAL, "the options must be a scalar struct");
	ret = dp_opt_check(opts, fields, sizeof(fields) / sizeof(fields[0]), bad, sizeof(bad));
	if (ret)
		return gw_fail(c, ret, "unknown option '%s'", bad);
	ret = dp_opt_field(opts, "output", &f);
	if (ret || !f)
		return gw_fail(c, -EINVAL, "option 'output' is required");
	if (dp_opt_bool(opts, "output", false, &output))
		return gw_fail(c, -EINVAL, "option 'output' must be a logical scalar");
	/* the integer options: dp_opt_int, never a cast of a double. 0 marks "samples not given" */
	if (dp_opt_int(opts, "samples", 1, LLONG_MAX, 0, &samples))
		return gw_fail(c, -EINVAL, "option 'samples' must be a positive integer");
	if (!samples)
		return gw_fail(c, -EINVAL, "option 'samples' is required");
	if (dp_opt_int(opts, "nb_blocks", 1, MAX_NB_BLOCKS, 2, &nb_blocks))
		return gw_fail(c, -EINVAL, "option 'nb_blocks' must be an integer from 1 to %d", MAX_NB_BLOCKS);
	if (dp_opt_int(opts, "buffer", -1, UINT_MAX, -1, &buffer))
		return gw_fail(c, -EINVAL, "option 'buffer' must be a buffer position (from 0) or -1");
	if (dp_opt_int(opts, "timeout_ms", -1, INT_MAX, -1, &timeout_ms))
		return gw_fail(c, -EINVAL, "option 'timeout_ms' must be -1, 0 or a time in ms up to %d", INT_MAX);
	if (dp_opt_bool(opts, "cyclic", false, &cyclic))
		return gw_fail(c, -EINVAL, "option 'cyclic' must be a logical scalar");
	if (dp_opt_spec(opts, &spec, NULL))
		return gw_fail(c, -EINVAL, "option 'class' must be the name of a numeric class, "
			"'scale' one of 'none', 'iio', 'fullscale', 'complex' and 'raw' logical scalars");
	if (cyclic && !output)
		return gw_fail(c, -EINVAL, "option 'cyclic' is for transmit streams only");

	ret = parse_chans(c, dev, output, &chans, &nb);
	if (ret)
		return ret;
	if (spec.complex_iq && nb % 2) {
		ret = gw_fail(c, -EINVAL, "option 'complex' needs an even number of channels (I then Q), not %u", nb);
		goto out;
	}
	ret = dp_opt_gains(opts, nb, &spec, &gain, &offset);
	if (ret) {
		ret = gw_fail(c, ret, "options 'gain' and 'offset' must be [], a real double scalar or one value per output column");
		goto out;
	}
	ret = pick_buffer(c, dev, buffer, output, chans, nb, &buf);
	if (ret)
		goto out;

	/* result first */
	ret = gw_ret_id(c, 0, 0);
	if (ret)
		goto out;
	ret = gw_handle_borrow(GW_T_BUFFER, buf, gw_handle_root(dev_id), &buf_id);
	if (ret) {
		ret = gw_fail(c, ret, "cannot register the buffer");
		goto out;
	}
	aux = malloc(sizeof(*aux));
	if (!aux) {
		ret = gw_fail(c, -ENOMEM, "out of memory");
		goto out;
	}
	aux->ctx = iio_device_get_context(dev);
	aux->timeout_ms = (int)timeout_ms;

	/* samples: positive and at most LLONG_MAX, which a size_t holds on every platform of the MEX
	 * file; the stream layer judges the size itself */
	ret = iio_conv_stream_open(buf, chans, nb, (size_t)samples, (unsigned int)nb_blocks, cyclic, &spec, &s);
	if (ret) {
		free(aux);
		if (ret == -EINVAL)
			ret = gw_fail(c, ret, "the channels and options do not make a stream (a channel twice or "
				"outside the buffer, a scale, gain or offset with an integer class, 'raw' with "
				"scale 'iio', a frame too large)");
		goto out;
	}
	iio_conv_stream_set_timeout(s, (int)timeout_ms);       /* -1 .. INT_MAX: cannot fail */
	id = gw_handle_add(GW_T_CONV_STREAM, s, buf_id, true, &cs_ops, aux);
	if (!id) {
		cs_destroy(s, aux);
		ret = gw_fail(c, -ENOMEM, "cannot register the stream");
		goto out;
	}
	gw_set_id(c, 0, id);
	ret = 0;
out:
	if (gain)
		mxFree(gain);
	if (offset)
		mxFree(offset);
	if (chans)
		mxFree(chans);
	return ret;
}

/* [data, valid] = conv_stream_read(cs). Both results exist before the frame is taken: a read
 * that ends for lack of memory has consumed nothing. data is created uninitialised and every
 * byte of it is written: by the converter, or as zero when there was no frame in time. */
static int conv_stream_read(struct gw_call *c)
{
	struct iio_conv_stream *s;
	size_t samples, columns, bytes;
	mxClassID cls;
	bool is_complex;
	mxArray *out;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONV_STREAM, &s, NULL);
	if (ret)
		return ret;
	if (iio_conv_stream_is_output(s))
		return gw_fail(c, -EBADF, "this is a transmit stream");

	samples = iio_conv_stream_get_samples(s);
	columns = iio_conv_stream_get_columns(s);
	cls = dp_class_of_type(iio_conv_stream_get_type(s));
	is_complex = iio_conv_stream_is_complex(s);
	bytes = samples * columns * class_bytes(cls) * (is_complex ? 2 : 1);

	out = mxCreateUninitNumericMatrix(samples, columns, cls, is_complex ? mxCOMPLEX : mxREAL);
	if (!out)
		return gw_fail(c, -ENOMEM, "out of memory");
	c->plhs[0] = out;       /* the dispatcher destroys it if the call fails */
	ret = gw_ret_bool(c, 1, false);
	if (ret)
		return ret;
	if (!mxGetData(out))
		return gw_fail(c, -ENOMEM, "the frame has no memory");

	ret = iio_conv_stream_read(s, mxGetData(out));
	if (ret < 0)
		return ret;     /* the text is libiio's */
	if (ret == IIO_CONV_STREAM_NO_FRAME)
		memset(mxGetData(out), 0, bytes);
	else
		set_true(c, 1);
	return 0;
}

/* valid = conv_stream_write(cs, data). The array is read in place. The result exists before
 * the frame is queued: a write that ends for lack of memory has sent nothing. */
static int conv_stream_write(struct gw_call *c)
{
	const mxArray *data = c->prhs[1];
	struct iio_conv_stream *s;
	enum iio_conv_type type;
	size_t samples, rows;
	unsigned int columns;
	bool is_complex;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONV_STREAM, &s, NULL);
	if (ret)
		return ret;
	if (!iio_conv_stream_is_output(s))
		return gw_fail(c, -EBADF, "this is a receive stream");
	if (dp_check_matrix(data, &type, &is_complex))
		return gw_fail(c, -EINVAL, "the data must be a full numeric matrix (double, single or an integer class)");
	samples = iio_conv_stream_get_samples(s);
	columns = iio_conv_stream_get_columns(s);
	rows = mxGetM(data);
	if (mxGetN(data) != columns)
		return gw_fail(c, -EINVAL, "the data must have %u columns, not %zu", columns, mxGetN(data));
	if (!rows || rows > samples)
		return gw_fail(c, -EINVAL, "the data must have 1 to %zu rows, not %zu", samples, rows);
	if (is_complex && !iio_conv_stream_is_complex(s))
		return gw_fail(c, -EINVAL, "complex data on a stream that was opened without 'complex'");
	if (!mxGetData(data))
		return gw_fail(c, -EINVAL, "the data has no memory");

	ret = gw_ret_bool(c, 0, false);
	if (ret)
		return ret;
	ret = iio_conv_stream_write_typed(s, mxGetData(data), rows, type, is_complex);
	if (ret == -EINVAL)
		return gw_fail(c, ret, "data of this class cannot be sent on this stream (an integer class with a scale, gain or offset)");
	if (ret == -EBUSY)
		return gw_fail(c, ret, "a cyclic stream takes one frame");
	if (ret < 0)
		return ret;
	if (ret == IIO_CONV_STREAM_FRAME)
		set_true(c, 0);
	return 0;
}

/* tf = conv_stream_flush(cs): receive: the captured frames are dropped (always true);
 * transmit: true when every queued frame was sent within the wait policy. */
static int conv_stream_flush(struct gw_call *c)
{
	struct iio_conv_stream *s;
	int ret;

	ret = gw_arg_handle(c, 0, GW_T_CONV_STREAM, &s, NULL);
	if (ret)
		return ret;
	ret = gw_ret_bool(c, 0, false);
	if (ret)
		return ret;
	ret = iio_conv_stream_flush(s);
	if (ret < 0)
		return ret;
	if (ret == IIO_CONV_STREAM_FRAME)
		set_true(c, 0);
	return 0;
}

/* conv_stream_close(id): a zero or stale id is a silent no-op */
static int conv_stream_close(struct gw_call *c)
{
	return gw_arg_destroy(c, 0, GW_T_CONV_STREAM);
}

static const struct gw_cmd cmds[] = {
	GW_XCMD("conv_stream_open", conv_stream_open, 3, 3, 1),
	GW_XCMD("conv_stream_read", conv_stream_read, 1, 1, 2),
	GW_XCMD("conv_stream_write", conv_stream_write, 2, 2, 1),
	GW_XCMD("conv_stream_flush", conv_stream_flush, 1, 1, 1),
	GW_XCMD("conv_stream_close", conv_stream_close, 1, 1, 0),
};

const struct gw_group gw_group_conv = GW_GROUP("conv", cmds);
