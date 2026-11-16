/* SPDX-License-Identifier: MIT */
/* Command group "block": the block's bytes, its queue state, the per-channel read and write of
 * libiio and the converted read and write (block_read, block_write) with their converter cache.
 * Buffer streams and blocks come from the dp_* builders, registered as the buffer commands do. */
#include "mex_test.h"
#include "dp_test.h"

#include <math.h>
#include <stdarg.h>
#include <time.h>
MT_GROUPS(&gw_group_block);

/* the table "Group block" of the Command reference */
static const struct mt_arity arities[] = {
	{ "block_destroy", 1, 1, 0 },
	{ "block_first", 2, 2, 1 },
	{ "block_enqueue", 3, 3, 0 },
	{ "block_dequeue", 2, 2, 1 },
	{ "block_get_buffer_stream", 1, 1, 1 },
	{ "block_get_dmabuf_fd", 1, 1, 1 },
	{ "block_disable_cpu_access", 2, 2, 0 },
	{ "channel_read", 3, 3, 1 },
	{ "channel_write", 4, 4, 1 },
	{ "block_size", 1, 1, 1 },
	{ "block_get_bytes", 1, 3, 1 },
	{ "block_set_bytes", 2, 3, 1 },
	{ "block_read", 2, 2, 1 },
	{ "block_write", 3, 3, 1 },
};

static const struct mt_fixture *fx;      /* the fixture of main() */
static uint8_t ramp[32];        /* the first 32 bytes of emu-rx_buf0.bin: I(k), Q(k) words, k = 0 .. 7 */

static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec * 1e-6;
}

/* ------------------------------------------------------------------ argument builders */

/* Scalar struct with n fields: pairs of (const char *name, mxArray *value). */
static mxArray *mkstruct(int n, ...)
{
	const char *names[8];
	mxArray *values[8];
	mxArray *s;
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		names[i] = va_arg(ap, const char *);
		values[i] = va_arg(ap, mxArray *);
	}
	va_end(ap);
	s = mxCreateStructMatrix(1, 1, n, names);
	for (int i = 0; i < n; i++)
		mxSetFieldByNumber(s, 0, i, values[i]);
	return s;
}

/* m-by-n int16, real or complex, from `count` int values in storage order (re, im interleaved). */
static mxArray *i16(size_t m, size_t n, bool is_complex, int count, ...)
{
	mxArray *a = mxCreateNumericMatrix(m, n, mxINT16_CLASS, is_complex ? mxCOMPLEX : mxREAL);
	int16_t *p = mxGetData(a);
	va_list ap;

	va_start(ap, count);
	for (int i = 0; i < count; i++)
		p[i] = (int16_t)va_arg(ap, int);
	va_end(ap);
	return a;
}

/* 1-by-n uint64 */
static mxArray *ids(int n, ...)
{
	mxArray *a = mxCreateNumericMatrix(1, (size_t)n, mxUINT64_CLASS, mxREAL);
	uint64_t *p = mxGetData(a);
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		p[i] = va_arg(ap, uint64_t);
	va_end(ap);
	return a;
}

static mxArray *empty_dbl(void)     /* double.empty(1,0): what iio.Block passes for Gain, Offset, Samples */
{
	return mxCreateDoubleMatrix(1, 0, mxREAL);
}

/* ------------------------------------------------------------------ result checks */

static bool shape(const mxArray *a, mxClassID cls, size_t m, size_t n, bool is_complex)
{
	return a && mxGetClassID(a) == cls && mxGetNumberOfDimensions(a) == 2 && mxGetM(a) == m &&
		mxGetN(a) == n && mxIsComplex(a) == is_complex;
}

/* block_read(blk, opts): the result (the caller destroys it), or NULL after counting a failure */
static mxArray *rd(uint64_t blk, mxArray *opts)
{
	mxArray *out[1];

	if (mt_call(1, out, "block_read", 2, mt_u64(blk), opts)) {
		CHECK(!"block_read raised");
		fprintf(stderr, "  %s: %s\n", mock_err_id(), mock_err_msg());
		return NULL;
	}
	return out[0];
}

/* block_get_bytes(blk, off, n) is exactly these bytes */
static bool bytes_are(uint64_t blk, size_t off, const void *want, size_t n)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "block_get_bytes", 3, mt_u64(blk), mt_dbl((double)off), mt_dbl((double)n)))
		return false;
	ok = shape(out[0], mxUINT8_CLASS, 1, n, false) && (!n || !memcmp(mxGetData(out[0]), want, n));
	mxDestroyArray(out[0]);
	return ok;
}

/* channel_read(ch, blk, raw) is a 1-by-n uint8 with these bytes */
static bool chan_is(uint64_t ch, uint64_t blk, bool raw, const void *want, size_t n)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "channel_read", 3, mt_u64(ch), mt_u64(blk), mt_bool(raw)))
		return false;
	ok = shape(out[0], mxUINT8_CLASS, 1, n, false) && (!n || !memcmp(mxGetData(out[0]), want, n));
	mxDestroyArray(out[0]);
	return ok;
}

/* the file emu-tx_buf0.bin holds exactly these n int16 values */
static bool tx_file_is(const int16_t *want, size_t n)
{
	int16_t got[64];
	long len = fixture_read_file(fx->dir, "emu-tx_buf0.bin", got, sizeof(got));

	return len == (long)(n * sizeof(int16_t)) && !memcmp(got, want, n * sizeof(int16_t));
}

static unsigned int count_log(char letter)
{
	unsigned int n = 0;

	for (const char *p = fake.log; *p; p++)
		n += *p == letter;
	return n;
}

/* ------------------------------------------------------------------ receive, emu */

/* Six option sets for the cache test; `first` is the real part of element (1, 1) of the result. */
static mxArray *optset(int which, uint64_t v0q, double *first)
{
	switch (which) {
	case 0:
		*first = -2048;
		return mkstruct(2, "class", mt_str("int16"), "complex", mt_bool(true));
	case 1:
		*first = -2048;
		return mt_empty();
	case 2:
		*first = -1;
		return mkstruct(3, "class", mt_str("single"), "scale", mt_str("fullscale"), "complex", mt_bool(true));
	case 3:
		*first = 2048;
		return mkstruct(3, "class", mt_str("int16"), "raw", mt_bool(true), "complex", mt_bool(true));
	case 4:
		*first = 2047;
		return mkstruct(2, "class", mt_str("int32"), "channels", ids(1, v0q));
	default:
		*first = -4096;
		return mkstruct(1, "gain", mt_dbl(2));
	}
}

static double first_of(const mxArray *a)
{
	if (!a || !mxGetNumberOfElements(a))
		return NAN;
	switch (mxGetClassID(a)) {
	case mxINT16_CLASS: return *(const int16_t *)mxGetData(a);
	case mxINT32_CLASS: return *(const int32_t *)mxGetData(a);
	case mxSINGLE_CLASS: return *(const float *)mxGetData(a);
	case mxDOUBLE_CLASS: return *(const double *)mxGetData(a);
	default: return NAN;
	}
}

static void test_receive(void)
{
	static const uint8_t i_conv[16] = { 0x00, 0xf8, 0x01, 0xf8, 0x02, 0xf8, 0x03, 0xf8,
		0x04, 0xf8, 0x05, 0xf8, 0x06, 0xf8, 0x07, 0xf8 };
	static const uint8_t i_raw[16] = { 0x00, 0x08, 0x01, 0x18, 0x02, 0x28, 0x03, 0x38,
		0x04, 0x48, 0x05, 0x58, 0x06, 0x68, 0x07, 0x78 };
	static const double bad_samples[] = { 0, 2.5, NAN, INFINITY, 1e300, -1 };
	uint64_t ctx, rx, tx, v0i, v0q, v2, tx_i, buf, mask, bs, blk, blk2, view = 0;
	struct iio_buffer_stream *bsp;
	struct iio_block *raw;
	const int16_t *p16;
	const double *pd;
	int16_t q_conv[8];
	mxArray *a, *out[1];
	double t0, first;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	v0i = dp_chn(rx, "voltage0_i", false);
	v0q = dp_chn(rx, "voltage0_q", false);
	v2 = dp_chn(rx, "voltage2", false);
	tx_i = dp_chn(tx, "voltage0_i", true);
	buf = dp_buf(rx, 0);
	mask = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	bs = dp_bufstream(buf, mask);
	blk = dp_block(bs, 32);
	bsp = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	if (!blk || !bsp || !v0i || !v0q || !v2 || !tx_i) {
		gw_handle_destroy(ctx);
		return;
	}
	for (int k = 0; k < 8; k++)
		q_conv[k] = (int16_t)(2047 - k);

	/* geometry */
	CHECK(mt_dbl_of("block_size", 1, mt_u64(blk)) == 32);
	CHECK(mt_dbl_of("block_first", 2, mt_u64(blk), mt_u64(v0i)) == 0);
	CHECK(mt_dbl_of("block_first", 2, mt_u64(blk), mt_u64(v0q)) == 2);
	CHECK(mt_dbl_of("block_first", 2, mt_u64(blk), mt_u64(v2)) == 32);      /* not enabled: the end */
	CHECK(mt_raises("iio:EINVAL", "block_first", 2, mt_u64(blk), mt_u64(tx_i)));   /* another device */
	CHECK(mt_raises("iio:EBADF", "block_first", 2, mt_u64(blk), mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "block_first", 2, mt_u64(bs), mt_u64(v0i)));
	CHECK(mt_raises("iio:EINVAL", "block_get_dmabuf_fd", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:ENOSYS", "block_disable_cpu_access", 2, mt_u64(blk), mt_bool(true)));
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(mt_raises("iio:EINVAL", "block_disable_cpu_access", 2, mt_u64(blk), mt_dbl(1)));
	CHECK(mt_u64_of("block_get_buffer_stream", 1, mt_u64(blk)) == bs);
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "block_size", 1, mt_dbl((double)blk)));

	/* the emu traps: none of these may reach libiio (a dequeue on a buffer that is not started
	 * stalls for 5 s) */
	t0 = now_ms();
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));   /* not enqueued */
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));   /* not started */
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(true)));
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_raises("iio:EPERM", "block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false)));
	CHECK(now_ms() - t0 < 1000);
	blk2 = dp_block(bs, 32);
	CHECK(mt_raises("iio:EINVAL", "block_enqueue", 3, mt_u64(blk2), mt_dbl(33), mt_bool(false)));
	CHECK(gw_handle_flags(blk2) == 0);
	CHECK(mt_raises("iio:EINVAL", "block_enqueue", 3, mt_u64(blk2), mt_dbl(-1), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "block_enqueue", 3, mt_u64(blk2), mt_dbl(1.5), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "block_enqueue", 3, mt_u64(blk2), mt_dbl(0), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "block_dequeue", 2, mt_u64(blk), mt_dbl(0)));
	CHECK(gw_handle_flags(blk2) == 0);

	/* data access while the block is with the backend */
	CHECK(mt_raises("iio:EPERM", "block_get_bytes", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EPERM", "block_read", 2, mt_u64(blk), mt_empty()));
	CHECK(mt_raises("iio:EPERM", "channel_read", 3, mt_u64(v0i), mt_u64(blk), mt_bool(false)));
	CHECK(mt_raises("iio:EPERM", "block_set_bytes", 2, mt_u64(blk), mt_bytes("ab", 2)));
	CHECK(mt_raises("iio:EPERM", "block_write", 3, mt_u64(blk), i16(1, 2, false, 2, 1, 2), mt_empty()));
	CHECK(mt_raises("iio:EPERM", "channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes("ab", 2), mt_bool(false)));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(blk)) == 32);                   /* not data access */

	/* start; the block comes back with the head of the ramp */
	CHECK(iio_buffer_stream_start(bsp) == 0);
	gw_handle_set_flags(bs, BS_STARTED);
	CHECK(mt_oom_sweep(1, "block_dequeue", 2, mt_u64(blk), mt_bool(false)) >= 1);   /* result first */
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));   /* once only */
	CHECK(bytes_are(blk, 0, ramp, 32));
	CHECK(mt_call(1, out, "block_get_bytes", 1, mt_u64(blk)) == 0);
	CHECK(shape(out[0], mxUINT8_CLASS, 1, 32, false) && !memcmp(mxGetData(out[0]), ramp, 32));
	mxDestroyArray(out[0]);
	CHECK(bytes_are(blk, 4, "\x01\x18\xfe\x07", 4));
	CHECK(mt_call(1, out, "block_get_bytes", 3, mt_u64(blk), mt_empty(), mt_dbl(4)) == 0);
	CHECK(shape(out[0], mxUINT8_CLASS, 1, 4, false) && !memcmp(mxGetData(out[0]), ramp, 4));
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "block_get_bytes", 2, mt_u64(blk), mt_dbl(28)) == 0);   /* to the end */
	CHECK(shape(out[0], mxUINT8_CLASS, 1, 4, false) && !memcmp(mxGetData(out[0]), ramp + 28, 4));
	mxDestroyArray(out[0]);
	CHECK(bytes_are(blk, 32, "", 0));
	CHECK(mt_raises("iio:EINVAL", "block_get_bytes", 3, mt_u64(blk), mt_dbl(30), mt_dbl(4)));
	CHECK(mt_raises("iio:EINVAL", "block_get_bytes", 3, mt_u64(blk), mt_dbl(33), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "block_get_bytes", 3, mt_u64(blk), mt_dbl(-1), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "block_get_bytes", 3, mt_u64(blk), mt_dbl(0), mt_dbl(1e300)));
	CHECK(mt_raises("iio:EINVAL", "block_get_bytes", 2, mt_u64(blk), mt_str("0")));
	CHECK(mt_oom_sweep(1, "block_get_bytes", 1, mt_u64(blk)) >= 1);

	/* one channel, as libiio converts or copies it */
	CHECK(chan_is(v0i, blk, false, i_conv, 16));
	CHECK(chan_is(v0i, blk, true, i_raw, 16));
	CHECK(chan_is(v0q, blk, false, q_conv, 16));
	CHECK(chan_is(v2, blk, false, "", 0));                  /* not enabled */
	CHECK(chan_is(v2, blk, true, "", 0));
	CHECK(mt_raises("iio:EINVAL", "channel_read", 3, mt_u64(tx_i), mt_u64(blk), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "channel_read", 3, mt_u64(blk), mt_u64(blk), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "channel_read", 3, mt_u64(v0i), mt_u64(v0i), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "channel_read", 3, mt_u64(v0i), mt_u64(blk), mt_dbl(0)));
	CHECK(mt_oom_sweep(1, "channel_read", 3, mt_u64(v0i), mt_u64(blk), mt_bool(false)) >= 1);

	/* converted read: complex int16, every byte written */
	a = rd(blk, mkstruct(2, "class", mt_str("int16"), "complex", mt_bool(true)));
	CHECK(shape(a, mxINT16_CLASS, 8, 1, true));
	if (shape(a, mxINT16_CLASS, 8, 1, true)) {
		const uint8_t *b = mxGetData(a);

		p16 = mxGetData(a);
		for (int k = 0; k < 8; k++)
			CHECK(p16[2 * k] == k - 2048 && p16[2 * k + 1] == 2047 - k);
		for (int k = 0; k < 32; k++)
			CHECK(b[k] != 0xA5);
	}
	mxDestroyArray(a);
	a = rd(blk, mkstruct(3, "class", mt_str("double"), "scale", mt_str("fullscale"), "complex", mt_bool(true)));
	CHECK(shape(a, mxDOUBLE_CLASS, 8, 1, true));
	if (shape(a, mxDOUBLE_CLASS, 8, 1, true)) {
		pd = mxGetData(a);
		CHECK(pd[0] == -1 && pd[1] == 0.99951171875);
	}
	mxDestroyArray(a);
	a = rd(blk, mkstruct(3, "raw", mt_bool(true), "class", mt_str("int16"), "complex", mt_bool(true)));
	CHECK(shape(a, mxINT16_CLASS, 8, 1, true));
	if (shape(a, mxINT16_CLASS, 8, 1, true)) {
		p16 = mxGetData(a);
		CHECK(p16[0] == 2048 && p16[1] == 2047 && p16[2] == 6145 && p16[3] == 2046);
	}
	mxDestroyArray(a);

	/* samples */
	a = rd(blk, mkstruct(3, "class", mt_str("int16"), "complex", mt_bool(true), "samples", mt_dbl(3)));
	CHECK(shape(a, mxINT16_CLASS, 3, 1, true));
	if (shape(a, mxINT16_CLASS, 3, 1, true)) {
		p16 = mxGetData(a);
		CHECK(p16[4] == -2046 && p16[5] == 2045);
	}
	mxDestroyArray(a);
	a = rd(blk, mkstruct(3, "class", mt_str("int16"), "complex", mt_bool(true), "samples", mt_dbl(100)));
	CHECK(shape(a, mxINT16_CLASS, 8, 1, true));
	mxDestroyArray(a);
	a = rd(blk, mkstruct(1, "samples", mt_i64(2)));
	CHECK(shape(a, mxDOUBLE_CLASS, 2, 2, false));
	mxDestroyArray(a);
	for (unsigned int i = 0; i < sizeof(bad_samples) / sizeof(bad_samples[0]); i++)
		CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
			mkstruct(1, "samples", mt_dbl(bad_samples[i]))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "samples", mt_str("3"))));

	/* defaults: [] and the empty doubles iio.Block passes on */
	for (int pass = 0; pass < 2; pass++) {
		a = rd(blk, pass ? mkstruct(3, "samples", empty_dbl(), "gain", empty_dbl(), "offset", empty_dbl()) :
			mt_empty());
		CHECK(shape(a, mxDOUBLE_CLASS, 8, 2, false));
		if (shape(a, mxDOUBLE_CLASS, 8, 2, false)) {
			pd = mxGetData(a);
			for (int k = 0; k < 8; k++)
				CHECK(pd[k] == k - 2048 && pd[8 + k] == 2047 - k);
		}
		mxDestroyArray(a);
	}

	/* channels: a subset, the order as passed, an empty id list */
	a = rd(blk, mkstruct(2, "channels", ids(1, v0q), "class", mt_str("int16")));
	CHECK(shape(a, mxINT16_CLASS, 8, 1, false));
	if (shape(a, mxINT16_CLASS, 8, 1, false))
		CHECK(!memcmp(mxGetData(a), q_conv, sizeof(q_conv)));
	mxDestroyArray(a);
	a = rd(blk, mkstruct(2, "channels", ids(2, v0q, v0i), "class", mt_str("int16")));
	CHECK(shape(a, mxINT16_CLASS, 8, 2, false));
	if (shape(a, mxINT16_CLASS, 8, 2, false)) {
		p16 = mxGetData(a);
		CHECK(p16[0] == 2047 && p16[7] == 2040 && p16[8] == -2048 && p16[15] == -2041);
	}
	mxDestroyArray(a);
	a = rd(blk, mkstruct(2, "channels", ids(0), "class", mt_str("int16")));
	CHECK(shape(a, mxINT16_CLASS, 8, 2, false));
	mxDestroyArray(a);
	{       /* one gain and one offset per column: host = raw * gain + offset */
		mxArray *g = mxCreateDoubleMatrix(1, 2, mxREAL), *o = mxCreateDoubleMatrix(2, 1, mxREAL);

		((double *)mxGetData(g))[0] = 2;
		((double *)mxGetData(g))[1] = 3;
		((double *)mxGetData(o))[0] = 1;
		((double *)mxGetData(o))[1] = -1;
		a = rd(blk, mkstruct(2, "gain", g, "offset", o));
		CHECK(shape(a, mxDOUBLE_CLASS, 8, 2, false));
		if (shape(a, mxDOUBLE_CLASS, 8, 2, false)) {
			pd = mxGetData(a);
			CHECK(pd[0] == -4095 && pd[8] == 6140);
		}
		mxDestroyArray(a);
	}

	/* option errors */
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(2, "complex", mt_bool(true), "channels", ids(1, v0q))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(2, "class", mt_str("int16"), "scale", mt_str("fullscale"))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "class", mt_str("int12"))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "colour", mt_dbl(1))));
	CHECK(strstr(mock_err_msg(), "colour") != NULL);
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "channels", ids(1, tx_i))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "channels", ids(1, v2))));
	CHECK(mt_raises("iio:EBADF", "block_read", 2, mt_u64(blk), mkstruct(1, "channels", ids(1, ctx))));
	CHECK(mt_raises("iio:EBADF", "block_read", 2, mt_u64(blk), mkstruct(1, "channels", ids(2, v0i, (uint64_t)0))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "channels", mt_dbl(1))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(1, "channels", mxCreateNumericMatrix(2, 2, mxUINT64_CLASS, mxREAL))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(2, "class", mt_str("int16"), "gain", mt_dbl(2))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(1, "gain", mxCreateDoubleMatrix(1, 3, mxREAL))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk),
		mkstruct(2, "raw", mt_bool(true), "scale", mt_str("iio"))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "complex", mt_dbl(1))));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mock_string_object()));
	{       /* text options with an embedded NUL are refused, not cut to 'int16' or 'none' */
		mxArray *cls = mt_str("int16?8"), *scale = mt_str("none?x");

		((mxChar *)mxGetData(cls))[5] = 0;
		((mxChar *)mxGetData(scale))[4] = 0;
		CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "class", cls)));
		CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), mkstruct(1, "scale", scale)));
	}

	/* the converter cache: six option sets in rotation over four slots, twice, then each twice in a row */
	for (int round = 0; round < 24; round++) {
		int which = round < 12 ? round % 6 : (round - 12) / 2;

		a = rd(blk, optset(which, v0q, &first));
		CHECK(first_of(a) == first);
		mxDestroyArray(a);
	}
	CHECK(gw_handle_cache(bs) != NULL);
	CHECK(mt_oom_sweep(1, "block_read", 2, mt_u64(blk), mkstruct(2, "gain", mt_dbl(3), "offset", mt_dbl(1))) >= 1);
	CHECK(mt_oom_sweep(1, "block_read", 2, mt_u64(blk), mkstruct(1, "class", mt_str("uint8"))) >= 1);

	/* a cached converter does not outlive the data format it was made for */
	{
		struct iio_data_format *fmt = (struct iio_data_format *)(uintptr_t)
			iio_channel_get_data_format(gw_handle_ptr(v0i, GW_T_CHANNEL));

		a = rd(blk, mkstruct(2, "channels", ids(1, v0i), "class", mt_str("int16")));
		CHECK(first_of(a) == -2048);
		mxDestroyArray(a);
		fmt->is_signed = false;
		a = rd(blk, mkstruct(2, "channels", ids(1, v0i), "class", mt_str("int16")));
		CHECK(first_of(a) == 2048);
		mxDestroyArray(a);
		fmt->is_signed = true;
		a = rd(blk, mkstruct(2, "channels", ids(1, v0i), "class", mt_str("int16")));
		CHECK(first_of(a) == -2048);
		mxDestroyArray(a);
	}

	/* CPU access switched off (no test backend can do it: the flag as the command leaves it) */
	gw_handle_set_flags(blk, BLK_CPU_OFF);
	CHECK(mt_raises("iio:EPERM", "block_get_bytes", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EPERM", "block_read", 2, mt_u64(blk), mt_empty()));
	CHECK(mt_raises("iio:EPERM", "channel_read", 3, mt_u64(v0i), mt_u64(blk), mt_bool(true)));
	gw_handle_set_flags(blk, 0);

	/* a view: a block that belongs to a stream */
	raw = iio_buffer_stream_create_block(bsp, 32);
	CHECK(!iio_err(raw));
	if (!iio_err(raw)) {
		CHECK(gw_handle_borrow(GW_T_BLOCK, raw, bs, &view) == 0 && view != 0);
		CHECK(mt_dbl_of("block_size", 1, mt_u64(view)) == 32);
		CHECK(mt_raises("iio:EPERM", "block_enqueue", 3, mt_u64(view), mt_dbl(0), mt_bool(false)));
		CHECK(gw_handle_flags(view) == 0);
		gw_handle_set_flags(view, BLK_ENQUEUED);
		CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(view), mt_bool(false)));
		CHECK(gw_handle_flags(view) == BLK_ENQUEUED);
		CHECK(mt_raises("iio:EPERM", "block_read", 2, mt_u64(view), mt_empty()));
		gw_handle_set_flags(view, 0);
		CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(view), mt_bool(false)));
		mt_ok("block_destroy", 1, mt_u64(view));
		CHECK(gw_handle_type(view) == GW_T_BLOCK);
		CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(view), mt_bytes(ramp, 32)) == 32);
		a = rd(view, mkstruct(1, "class", mt_str("int16")));
		CHECK(shape(a, mxINT16_CLASS, 8, 2, false) && first_of(a) == -2048);
		mxDestroyArray(a);
		CHECK(mt_u64_of("block_get_buffer_stream", 1, mt_u64(view)) == bs);
		gw_handle_destroy(view);
		iio_block_destroy(raw);
	}

	/* destroy: twice, a stale id, zero, another type */
	mt_ok("block_destroy", 1, mt_u64(blk));
	CHECK(gw_handle_type(blk) == GW_T_NONE);
	mt_ok("block_destroy", 1, mt_u64(blk));
	mt_ok("block_destroy", 1, mt_u64(0));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EBADF", "block_get_bytes", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EBADF", "block_read", 2, mt_u64(blk), mt_empty()));
	CHECK(mt_raises("iio:EBADF", "block_destroy", 1, mt_u64(bs)));
	CHECK(gw_handle_type(bs) == GW_T_BUFFER_STREAM);
	CHECK(mt_raises("iio:EINVAL", "block_destroy", 1, mt_dbl(1)));
	CHECK(gw_handle_type(blk2) == GW_T_BLOCK);

	/* the context goes with a started stream, a block and a filled cache: no leak report */
	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(blk2)));
}

/* A channel that is not enabled, and a block whose buffer stream the gateway does not hold. */
static void test_second_context(void)
{
	static const int16_t words[8] = { -2048, 2047, -2047, 2046, -2046, 2045, -2045, 2044 };
	uint64_t ctx, rx, v0i, v0q, buf, mask, bs1, blk1, view = 0;
	struct iio_channels_mask *raw_mask;
	struct iio_buffer_stream *raw_bs, *bs1p;
	struct iio_block *raw_blk, *blk1p;
	const struct iio_device *dev;
	mxArray *a;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	v0i = dp_chn(rx, "voltage0_i", false);
	v0q = dp_chn(rx, "voltage0_q", false);
	buf = dp_buf(rx, 0);
	dev = gw_handle_ptr(rx, GW_T_DEVICE);
	if (!dev || !v0i || !v0q || !buf) {
		gw_handle_destroy(ctx);
		return;
	}

	/* a buffer stream opened behind the gateway's back: no entry, so no cache; the converter
	 * lives for the one call */
	raw_mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	iio_channel_enable(gw_handle_ptr(v0i, GW_T_CHANNEL), raw_mask);
	raw_bs = iio_buffer_open(gw_handle_ptr(buf, GW_T_BUFFER), raw_mask);
	CHECK(!iio_err(raw_bs));
	raw_blk = iio_err(raw_bs) ? NULL : iio_buffer_stream_create_block(raw_bs, 16);
	CHECK(raw_blk && !iio_err(raw_blk));
	if (raw_blk && !iio_err(raw_blk)) {
		CHECK(gw_handle_borrow(GW_T_BLOCK, raw_blk, buf, &view) == 0 && view != 0);
		CHECK(mt_u64_of("block_get_buffer_stream", 1, mt_u64(view)) == 0);
		CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(view), mt_bytes(ramp, 16)) == 16);
		for (int pass = 0; pass < 2; pass++) {
			a = rd(view, mkstruct(1, "class", mt_str("int16")));
			CHECK(shape(a, mxINT16_CLASS, 8, 1, false));
			if (shape(a, mxINT16_CLASS, 8, 1, false))
				CHECK(!memcmp(mxGetData(a), words, sizeof(words)));
			mxDestroyArray(a);
		}
		CHECK(mt_oom_sweep(1, "block_read", 2, mt_u64(view), mt_empty()) >= 1);
		CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(view),
			mkstruct(2, "class", mt_str("int16"), "scale", mt_str("fullscale"))));
		CHECK(mt_dbl_of("block_write", 3, mt_u64(view), i16(2, 1, false, 2, 1, 2), mt_empty()) == 2);
		CHECK(bytes_are(view, 0, "\x01\x00\x02\x00", 4));
		CHECK(mt_oom_sweep(1, "block_write", 3, mt_u64(view), i16(2, 1, false, 2, 3, 4), mt_empty()) >= 1);
		CHECK(bytes_are(view, 0, "\x03\x00\x04\x00", 4));
		gw_handle_destroy(view);
	}
	if (raw_blk && !iio_err(raw_blk))
		iio_block_destroy(raw_blk);
	if (!iio_err(raw_bs))
		iio_buffer_close(raw_bs);
	iio_channels_mask_destroy(raw_mask);

	/* voltage0_q is not enabled and has the storage size of the whole sample: libiio's raw
	 * shortcut would hand out, and overwrite, the data of voltage0_i */
	mask = dp_mask(rx, false, 1, "voltage0_i");
	bs1 = dp_bufstream(buf, mask);
	blk1 = dp_block(bs1, 16);
	bs1p = gw_handle_ptr(bs1, GW_T_BUFFER_STREAM);
	blk1p = gw_handle_ptr(blk1, GW_T_BLOCK);
	if (bs1p && blk1p) {
		CHECK(iio_block_enqueue(blk1p, 0, false) == 0);
		CHECK(iio_buffer_stream_start(bs1p) == 0);
		gw_handle_set_flags(bs1, BS_STARTED);
		CHECK(iio_block_dequeue(blk1p, false) == 0);
		CHECK(chan_is(v0q, blk1, true, "", 0));
		CHECK(chan_is(v0q, blk1, false, "", 0));
		CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0q), mt_u64(blk1), mt_bytes("\x01\x02", 2), mt_bool(true)) == 0);
		CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0q), mt_u64(blk1), mt_bytes("\x01\x02", 2), mt_bool(false)) == 0);
		CHECK(bytes_are(blk1, 0, ramp, 16));
		CHECK(chan_is(v0i, blk1, true, ramp, 16));              /* the shortcut, where it is right */
		CHECK(chan_is(v0i, blk1, false, words, 16));
		CHECK(mt_dbl_of("block_first", 2, mt_u64(blk1), mt_u64(v0q)) == 16);
		CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk1), mkstruct(1, "channels", ids(1, v0q))));
	}
	gw_handle_destroy(ctx);
}

/* ------------------------------------------------------------------ transmit, emu */

static void test_transmit(void)
{
	static const int16_t first[8] = { 100, 1, -200, 2, 300, 3, -400, 4 };
	static const int16_t real_iq[8] = { 7, 0, -8, 0, 9, 0, -10, 0 };
	static const int16_t chan_i[4] = { 7, 8, 9, 10 };
	static const int16_t rounded[8] = { 32767, 0, -32768, 0, 2, 0, 2, 0 };
	static const uint8_t four[4] = { 1, 2, 3, 4 };
	uint64_t ctx, tx, rx, v0i, v0q, rx_i, buf, mask, bs, blk;
	struct iio_buffer_stream *bsp;
	int16_t got[8];
	mxArray *out[1], *d;
	double *pd;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	tx = dp_dev(ctx, "emu-tx");
	rx = dp_dev(ctx, "emu-rx");
	v0i = dp_chn(tx, "voltage0_i", true);
	v0q = dp_chn(tx, "voltage0_q", true);
	rx_i = dp_chn(rx, "voltage0_i", false);
	buf = dp_buf(tx, 0);
	mask = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	bs = dp_bufstream(buf, mask);
	blk = dp_block(bs, 16);
	bsp = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	if (!blk || !bsp || !v0i || !v0q) {
		gw_handle_destroy(ctx);
		return;
	}

	/* complex int16 into the I/Q pair; an allocation failure writes nothing twice */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk),
		i16(4, 1, true, 8, 100, 1, -200, 2, 300, 3, -400, 4), mt_empty()) == 4);
	CHECK(bytes_are(blk, 0, first, 16));
	CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(blk), mt_bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16)) == 16);
	CHECK(mt_oom_sweep(1, "block_write", 3, mt_u64(blk),
		i16(4, 1, true, 8, 100, 1, -200, 2, 300, 3, -400, 4), mt_empty()) >= 1);
	CHECK(bytes_are(blk, 0, first, 16));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(16), mt_bool(false));
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(iio_buffer_stream_start(bsp) == 0);
	gw_handle_set_flags(bs, BS_STARTED);
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)) == true);
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(tx_file_is(first, 8));

	/* shapes and classes */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk),
		i16(6, 1, true, 12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12), mt_empty()) == 4);   /* the capacity */
	for (int k = 0; k < 8; k++)
		got[k] = (int16_t)(k + 1);
	CHECK(bytes_are(blk, 0, got, 16));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 2, true, 0), mt_empty()));
	CHECK(strstr(mock_err_msg(), "columns") != NULL);
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), mt_str("abcd"), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), mxCreateLogicalMatrix(4, 2), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), mock_sparse_double(4, 2), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk),
		mock_nd_array(mxINT16_CLASS, mxREAL, 4, 1, 2), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), mxCreateCellMatrix(1, 1), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), mock_string_object(), mt_empty()));
	CHECK(bytes_are(blk, 0, got, 16));

	/* the option complex, not the complexity of the array as the MEX sees it, selects the layout */
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 1, false, 4, 7, -8, 9, -10), mt_empty()));
	CHECK(strstr(mock_err_msg(), "columns") != NULL);
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk),
		i16(4, 1, true, 8, 100, 1, -200, 2, 300, 3, -400, 4), mkstruct(1, "complex", mt_bool(false))));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk),
		i16(4, 1, false, 4, 7, -8, 9, -10), mkstruct(1, "complex", mt_dbl(1))));
	CHECK(bytes_are(blk, 0, got, 16));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), i16(4, 1, false, 4, 7, -8, 9, -10),
		mkstruct(1, "complex", mt_bool(true))) == 4);
	CHECK(bytes_are(blk, 0, real_iq, 16));          /* the Q channel is zero, not what was there */
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(16), mt_bool(false));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)) == true);
	CHECK(tx_file_is(real_iq, 8));
	/* fewer rows than the block holds: only those rows are touched */
	CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(blk), mt_bytes(first, 16)) == 16);
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), i16(2, 1, false, 2, 7, -8),
		mkstruct(1, "complex", mt_bool(true))) == 2);
	CHECK(bytes_are(blk, 0, real_iq, 8) && bytes_are(blk, 8, first + 4, 8));
	/* complex true with complex data is the plain case; a gain is applied per column */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), i16(4, 1, true, 8, 100, 1, -200, 2, 300, 3, -400, 4),
		mkstruct(1, "complex", mt_bool(true))) == 4);
	CHECK(bytes_are(blk, 0, first, 16));
	d = mxCreateDoubleMatrix(4, 1, mxREAL);
	pd = mxGetData(d);
	pd[0] = 14; pd[1] = -16; pd[2] = 18; pd[3] = -20;      /* raw = host / gain */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), d,
		mkstruct(2, "complex", mt_bool(true), "gain", mt_dbl(2))) == 4);
	CHECK(bytes_are(blk, 0, real_iq, 16));

	/* the empty-double defaults of iio.Block.write */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), i16(4, 1, true, 8, 100, 1, -200, 2, 300, 3, -400, 4),
		mkstruct(2, "gain", empty_dbl(), "offset", empty_dbl())) == 4);
	CHECK(bytes_are(blk, 0, first, 16));

	/* nothing to write: no data pointer is touched */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk),
		mxCreateNumericMatrix(0, 1, mxINT16_CLASS, mxCOMPLEX), mt_empty()) == 0);
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk),
		mxCreateNumericMatrix(0, 2, mxINT16_CLASS, mxREAL), mt_empty()) == 0);
	CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(blk), mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL)) == 0);
	CHECK(mt_dbl_of("block_set_bytes", 3, mt_u64(blk), mxCreateNumericMatrix(1, 0, mxUINT8_CLASS, mxREAL),
		mt_dbl(16)) == 0);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0i), mt_u64(blk),
		mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL), mt_bool(false)) == 0);
	CHECK(bytes_are(blk, 0, first, 16));

	/* bytes */
	CHECK(mt_dbl_of("block_set_bytes", 3, mt_u64(blk), mt_bytes(four, 4), mt_dbl(12)) == 4);
	CHECK(bytes_are(blk, 12, four, 4) && bytes_are(blk, 0, first, 12));
	CHECK(mt_raises("iio:EINVAL", "block_set_bytes", 3, mt_u64(blk), mt_bytes(four, 4), mt_dbl(14)));
	CHECK(mt_raises("iio:EINVAL", "block_set_bytes", 3, mt_u64(blk), mt_bytes(four, 4), mt_dbl(17)));
	CHECK(mt_raises("iio:EINVAL", "block_set_bytes", 3, mt_u64(blk), mt_bytes(four, 4), mt_dbl(-1)));
	CHECK(mt_raises("iio:EINVAL", "block_set_bytes", 2, mt_u64(blk), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "block_set_bytes", 2, mt_u64(blk), mt_str("ab")));
	CHECK(bytes_are(blk, 12, four, 4) && bytes_are(blk, 0, first, 12));
	CHECK(mt_oom_sweep(1, "block_set_bytes", 3, mt_u64(blk), mt_bytes(four, 4), mt_dbl(0)) >= 1);
	CHECK(bytes_are(blk, 0, four, 4));

	/* one channel through libiio */
	CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(blk), mt_bytes(first, 16)) == 16);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes(chan_i, 8), mt_bool(false)) == 8);
	CHECK(mt_call(1, out, "block_get_bytes", 1, mt_u64(blk)) == 0);
	if (shape(out[0], mxUINT8_CLASS, 1, 16, false)) {
		memcpy(got, mxGetData(out[0]), 16);
		CHECK(got[0] == 7 && got[1] == 1 && got[2] == 8 && got[3] == 2 && got[6] == 10 && got[7] == 4);
	} else {
		CHECK(!"block_get_bytes is 1-by-16 uint8");
	}
	mxDestroyArray(out[0]);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0q), mt_u64(blk), mt_bytes(chan_i, 8), mt_bool(true)) == 8);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0q), mt_u64(blk), mt_bytes(chan_i, 3), mt_bool(false)) == 2);
	/* more bytes than the block has samples for: the block's share */
	for (int k = 0; k < 8; k++)
		got[k] = (int16_t)(20 + k);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes(got, 16), mt_bool(false)) == 8);
	CHECK(mt_oom_sweep(1, "channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes(chan_i, 8), mt_bool(false)) >= 1);
	CHECK(mt_raises("iio:EINVAL", "channel_write", 4, mt_u64(rx_i), mt_u64(blk), mt_bytes(chan_i, 8), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_dbl(1), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes(chan_i, 8), mt_dbl(0)));
	CHECK(mt_raises("iio:EBADF", "channel_write", 4, mt_u64(v0i), mt_u64(bs), mt_bytes(chan_i, 8), mt_bool(false)));

	/* double data: saturated, rounded half to even; one channel of the pair */
	d = mxCreateDoubleMatrix(4, 1, mxREAL);
	pd = mxGetData(d);
	pd[0] = 1e6; pd[1] = -1e6; pd[2] = 1.5; pd[3] = 2.5;
	CHECK(mt_dbl_of("block_set_bytes", 2, mt_u64(blk), mt_bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16)) == 16);
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), d, mkstruct(1, "channels", ids(1, v0i))) == 4);
	CHECK(bytes_are(blk, 0, rounded, 16));

	/* option errors */
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 2, false, 0),
		mkstruct(1, "scale", mt_str("fullscale"))));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 2, false, 0),
		mkstruct(1, "class", mt_str("int16"))));
	CHECK(strstr(mock_err_msg(), "class") != NULL);
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 2, false, 0),
		mkstruct(1, "samples", mt_dbl(2))));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 2, false, 0), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 1, false, 0),
		mkstruct(1, "channels", ids(1, rx_i))));
	CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), i16(4, 1, true, 0),
		mkstruct(1, "channels", ids(1, v0i))));         /* complex data, one channel */
	CHECK(mt_raises("iio:EBADF", "block_write", 3, mt_u64(bs), i16(4, 2, false, 0), mt_empty()));
	CHECK(bytes_are(blk, 0, rounded, 16));

	mt_ok("block_destroy", 1, mt_u64(blk));
	mt_ok("block_destroy", 1, mt_u64(blk));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(blk)));
	gw_handle_destroy(ctx);
}

/* ------------------------------------------------------------------ fake backend */

static void test_fake(void)
{
	uint64_t ctx, rx, tx, buf, mask, bs, blk, tbuf, tmask, tbs, tblk;
	struct iio_buffer_stream *bsp;
	struct iio_block *blkp;
	char xml[PATH_MAX];     /* fake_create_context() has room for a path of PATH_MAX bytes */
	size_t len = strlen(fx->xml);

	CHECK(len < sizeof(xml));
	if (len >= sizeof(xml))
		return;
	memcpy(xml, fx->xml, len + 1);
	ctx = dp_open_fake(xml);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	buf = dp_buf(rx, 0);
	mask = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	bs = dp_bufstream(buf, mask);
	blk = dp_block(bs, 32);
	bsp = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	blkp = gw_handle_ptr(blk, GW_T_BLOCK);
	if (!bsp || !blkp) {
		gw_handle_destroy(ctx);
		return;
	}

	/* three frames, then "not ready": false without waiting, a timeout when waiting */
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(iio_buffer_stream_start(bsp) == 0);
	gw_handle_set_flags(bs, BS_STARTED);
	for (int k = 0; k < 3; k++) {
		CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)) == true);
		CHECK(bytes_are(blk, 0, k == 0 ? "\x00\x00\x01\x00" : k == 1 ? "\x00\x01\x01\x01" : "\x00\x02\x01\x02", 4));
		mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	}
	CHECK(!strcmp(fake.log, "OBESDEDEDE"));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(true)) == false);
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_raises("iio:ETIMEDOUT", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_raises("iio:EPERM", "block_get_bytes", 1, mt_u64(blk)));
	CHECK(!strcmp(fake.log, "OBESDEDEDEdd"));

	/* a block libiio no longer holds: the gateway believes libiio and clears its flag */
	fake.rx_frames = 100;
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)) == true);
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(iio_block_dequeue(blkp, false) == 0);
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(bytes_are(blk, 0, "\x00\x04\x01\x04", 4));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(gw_handle_flags(blk) == BLK_ENQUEUED);
	fake.busy = 1;
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(true)) == false);
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(true)) == true);

	/* allocation failures: the block is queued once and taken back once */
	fake_clear_log();
	mt_oom_sweep(0, "block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(count_log('E') == 1 && gw_handle_flags(blk) == BLK_ENQUEUED);
	CHECK(mt_oom_sweep(1, "block_dequeue", 2, mt_u64(blk), mt_bool(false)) >= 1);
	CHECK(count_log('D') == 1 && count_log('d') == 0 && gw_handle_flags(blk) == 0);
	CHECK(!strcmp(fake.log, "ED"));

	/* bytes_used and cyclic reach the backend */
	tx = dp_dev(ctx, "emu-tx");
	tbuf = dp_buf(tx, 0);
	tmask = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	tbs = dp_bufstream(tbuf, tmask);
	tblk = dp_block(tbs, 16);
	if (tblk) {
		CHECK(mt_dbl_of("block_write", 3, mt_u64(tblk), i16(4, 1, true, 8, 1, 2, 3, 4, 5, 6, 7, 8), mt_empty()) == 4);
		fake_clear_log();
		mt_ok("block_enqueue", 3, mt_u64(tblk), mt_dbl(8), mt_bool(true));
		CHECK(!strcmp(fake.log, "e") && fake.tx_bytes == 8 && fake.tx_cyclic);
		CHECK(!memcmp(fake.tx_data, "\x01\x00\x02\x00\x03\x00\x04\x00", 8));
		CHECK(mt_raises("iio:EPERM", "block_write", 3, mt_u64(tblk), i16(4, 1, true, 0), mt_empty()));
		CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(tblk), mt_bool(true)));   /* not started */
		CHECK(!strcmp(fake.log, "e"));
	}

	/* an enqueued block and a started stream go with the context */
	gw_handle_destroy(ctx);
	CHECK(gw_handle_type(blk) == GW_T_NONE);
}

int main(int argc, char **argv)
{
	struct mt_fixture fixture;
	double t0 = now_ms();

	if (argc < 2 || mt_fixture_setup(&fixture, argv[1]))
		return 2;
	fx = &fixture;
	for (unsigned int k = 0; k < 8; k++) {
		ramp[4 * k] = (uint8_t)fixture_ramp_word_i(k);
		ramp[4 * k + 1] = (uint8_t)(fixture_ramp_word_i(k) >> 8);
		ramp[4 * k + 2] = (uint8_t)fixture_ramp_word_q(k);
		ramp[4 * k + 3] = (uint8_t)(fixture_ramp_word_q(k) >> 8);
	}
	CHECK(!memcmp(ramp, "\x00\x08\xff\x07\x01\x18\xfe\x07", 8));       /* "Test data" */

	mt_check_arities(&gw_group_block, arities, sizeof(arities) / sizeof(arities[0]));

	test_receive();
	test_second_context();
	test_transmit();
	test_fake();
	CHECK(now_ms() - t0 < 5000);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fixture);
	MT_MAIN_RETURN();
}
