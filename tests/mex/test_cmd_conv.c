/* SPDX-License-Identifier: MIT */
/* Command group "conv": the fast path. Receive and transmit on emu:, "no frame", the last
 * transmit frame, close order and unload on the fake backend. Contexts, devices, channels and
 * buffers come from mt_open_context and the dp_* builders; every stream is made by the commands. */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_conv);

#include <math.h>
#include <stdarg.h>
#include <time.h>

/* the table "Group conv" of the Command reference */
static const struct mt_arity arities[] = {
	{ "conv_stream_open", 3, 3, 1 },
	{ "conv_stream_read", 1, 1, 2 },
	{ "conv_stream_write", 2, 2, 1 },
	{ "conv_stream_flush", 1, 1, 1 },
	{ "conv_stream_close", 1, 1, 0 },
};

#define TX_FILE "emu-tx_buf0.bin"
#define MULTI_FILE "emu-multi_buf1.bin"

static struct mt_fixture fx;
static uint64_t ctx, rx, tx, multi, zoo;     /* the emu: context and four of its devices */
static const char *volatile fake_xml;        /* fx.xml, for dp_open_fake() */

/* ------------------------------------------------------------------ builders */

/* Scalar struct with n fields: pairs of (const char *name, mxArray *value). */
static mxArray *opts(int n, ...)
{
	const char *names[16];
	mxArray *values[16];
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

/* the two fields every receive case has, and the usual transmit stream */
#define RX(n) "output", mt_bool(false), "samples", mt_dbl(n)
#define TX4 "output", mt_bool(true), "samples", mt_dbl(4), "nb_blocks", mt_dbl(1)
#define CPLX "complex", mt_bool(true)
#define CLASS(name) "class", mt_str(name)

/* 1-by-n cell of char rows */
static mxArray *cells(int n, ...)
{
	mxArray *a = mxCreateCellMatrix(1, (mwSize)n);
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		mxSetCell(a, (mwIndex)i, mt_str(va_arg(ap, const char *)));
	va_end(ap);
	return a;
}

static mxArray *iq0(void)
{
	return cells(2, "voltage0_i", "voltage0_q");
}

static mxArray *swapped(void)
{
	return cells(4, "voltage1_i", "voltage1_q", "voltage0_i", "voltage0_q");
}

/* 1-by-n uint64 */
static mxArray *ids(int n, ...)
{
	mxArray *a = mxCreateNumericMatrix(1, (mwSize)n, mxUINT64_CLASS, mxREAL);
	uint64_t *p = mxGetData(a);
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		p[i] = va_arg(ap, uint64_t);
	va_end(ap);
	return a;
}

/* m-by-n real double with these values in storage order */
static mxArray *dmat(size_t m, size_t n, ...)
{
	mxArray *a = mxCreateDoubleMatrix(m, n, mxREAL);
	double *p = mxGetData(a);
	va_list ap;

	va_start(ap, n);
	for (size_t i = 0; i < m * n; i++)
		p[i] = va_arg(ap, double);
	va_end(ap);
	return a;
}

/* m-by-n array of class cls whose storage (interleaved when complex) is a copy of v */
static mxArray *mat(mxClassID cls, bool cplx, size_t m, size_t n, const void *v, size_t bytes)
{
	mxArray *a = mxCreateNumericMatrix(m, n, cls, cplx ? mxCOMPLEX : mxREAL);

	if (bytes)
		memcpy(mxGetData(a), v, bytes);
	return a;
}

#define MAT(cls, cplx, m, n, v) mat(cls, cplx, m, n, v, sizeof(v))

/* ------------------------------------------------------------------ calls */

static uint64_t open_cs(uint64_t dev, mxArray *chans, mxArray *o)
{
	return mt_u64_of("conv_stream_open", 3, mt_u64(dev), chans, o);
}

/* conv_stream_open raises id and leaves no handle behind */
static bool open_raises(const char *id, uint64_t dev, mxArray *chans, mxArray *o)
{
	size_t before = gw_handle_count();
	bool ok = mt_raises(id, "conv_stream_open", 3, mt_u64(dev), chans, o);

	if (gw_handle_count() != before) {
		fprintf(stderr, "conv_stream_open: a failed open left %zu handle(s)\n",
			gw_handle_count() - before);
		ok = false;
	}
	return ok;
}

/* [data, valid] = conv_stream_read(cs): data (the caller destroys it), or NULL after a raise or
 * when valid is not a logical scalar */
static mxArray *rd(uint64_t cs, bool *valid)
{
	mxArray *out[2];

	*valid = false;
	if (mt_call(2, out, "conv_stream_read", 1, mt_u64(cs))) {
		fprintf(stderr, "conv_stream_read: %s: %s\n", mock_err_id(), mock_err_msg());
		return NULL;
	}
	if (!mxIsLogical(out[1]) || !mxIsScalar(out[1])) {
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
		return NULL;
	}
	*valid = mxIsLogicalScalarTrue(out[1]);
	mxDestroyArray(out[1]);
	return out[0];
}

static bool is(const mxArray *a, size_t m, size_t n, mxClassID cls, bool cplx)
{
	return a && mxGetNumberOfDimensions(a) == 2 && mxGetM(a) == m && mxGetN(a) == n &&
		mxGetClassID(a) == cls && mxIsComplex(a) == cplx && !mxIsSparse(a);
}

/* a valid frame of that shape, or NULL (and a counted failure) */
static mxArray *frame(uint64_t cs, size_t m, size_t n, mxClassID cls, bool cplx)
{
	bool valid;
	mxArray *a = rd(cs, &valid);

	if (a && valid && is(a, m, n, cls, cplx))
		return a;
	fprintf(stderr, "conv_stream_read: not a valid %zu-by-%zu frame of the expected class\n", m, n);
	test_failures++;
	if (a)
		mxDestroyArray(a);
	return NULL;
}

static bool wr(uint64_t cs, mxArray *data)
{
	return mt_bool_of("conv_stream_write", 2, mt_u64(cs), data);
}

static bool flush(uint64_t cs)
{
	return mt_bool_of("conv_stream_flush", 1, mt_u64(cs));
}

static void close_cs(uint64_t cs)
{
	mt_ok("conv_stream_close", 1, mt_u64(cs));
}

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* the file of the fixture directory holds exactly these n 16-bit words */
static bool file_is(const char *name, const void *words, size_t n)
{
	unsigned char bytes[256];

	return fixture_read_file(fx.dir, name, bytes, sizeof(bytes)) == (long)(2 * n) &&
		!memcmp(bytes, words, 2 * n);
}

static long file_len(const char *name)
{
	unsigned char bytes[256];

	return fixture_read_file(fx.dir, name, bytes, sizeof(bytes));
}

/* number of rows of an N-by-1 complex int16 frame that are not the ramp from pair `first` on */
static unsigned int ramp_mismatches(const mxArray *a, unsigned int first)
{
	const int16_t *d = mxGetData(a);
	unsigned int bad = 0;

	for (unsigned int k = 0; k < mxGetM(a); k++)
		bad += d[2 * k] != fixture_ramp_i(first + k) || d[2 * k + 1] != fixture_ramp_q(first + k);
	return bad;
}

static unsigned int count_in(const char *s, char ch)
{
	unsigned int n = 0;

	for (; *s; s++)
		n += *s == ch;
	return n;
}

static mxArray *frame_a(void)
{
	static const int16_t A[8] = { 100, 1, -200, 2, 300, 3, -400, 4 };

	return MAT(mxINT16_CLASS, true, 4, 1, A);
}

static const int16_t file_a[8] = { 100, 1, -200, 2, 300, 3, -400, 4 };

/* ------------------------------------------------------------------ receive, emu */

static void test_rx_int16(void)
{
	uint64_t cs = open_cs(rx, iq0(), opts(4, RX(1024), CLASS("int16"), CPLX));
	mxArray *a;

	CHECK(cs != 0);
	CHECK(gw_handle_type(cs) == GW_T_CONV_STREAM && gw_handle_owned(cs));
	CHECK(gw_handle_parent(cs) == dp_buf(rx, 0));

	a = frame(cs, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		const int16_t *d = mxGetData(a);

		CHECK(d[0] == -2048 && d[1] == 2047);
		CHECK(ramp_mismatches(a, 0) == 0);      /* every element: nothing is left uninitialised */
		mxDestroyArray(a);
	}
	a = frame(cs, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		const int16_t *d = mxGetData(a);

		CHECK(d[0] == -1024 && d[1] == 1023);
		CHECK(ramp_mismatches(a, 1024) == 0);
		mxDestroyArray(a);
	}
	{       /* one output only: the data */
		mxArray *out[1];

		CHECK(mt_call(1, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
		CHECK(is(out[0], 1024, 1, mxINT16_CLASS, true));
		CHECK(ramp_mismatches(out[0], 2048) == 0);
		mxDestroyArray(out[0]);
	}
	close_cs(cs);

	/* what iio.RxStream passes when nothing is given: gain and offset as 1-by-0 doubles */
	cs = open_cs(rx, iq0(), opts(12, RX(1024), "buffer", mt_dbl(-1), "nb_blocks", mt_dbl(2),
		"cyclic", mt_bool(false), CLASS("int16"), CPLX, "scale", mt_str("none"),
		"raw", mt_bool(false), "gain", mxCreateDoubleMatrix(1, 0, mxREAL),
		"offset", mxCreateDoubleMatrix(1, 0, mxREAL), "timeout_ms", mt_dbl(-1)));
	CHECK(cs != 0);
	a = frame(cs, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 0) == 0);
		mxDestroyArray(a);
	}
	close_cs(cs);

	/* a column cell, and int64 / uint32 option values */
	{
		mxArray *col = mxCreateCellMatrix(2, 1), *n = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);
		mxArray *nb = mxCreateNumericMatrix(1, 1, mxUINT32_CLASS, mxREAL);

		mxSetCell(col, 0, mt_str("voltage0_i"));
		mxSetCell(col, 1, mt_str("voltage0_q"));
		*(int64_t *)mxGetData(n) = 16;
		*(uint32_t *)mxGetData(nb) = 1;
		cs = open_cs(rx, col, opts(5, "output", mt_bool(false), "samples", n, "nb_blocks", nb,
			CLASS("int16"), CPLX));
		CHECK(cs != 0);
		a = frame(cs, 16, 1, mxINT16_CLASS, true);
		if (a) {
			CHECK(ramp_mismatches(a, 0) == 0);
			mxDestroyArray(a);
		}
		close_cs(cs);
	}
}

static void test_rx_defaults(void)
{
	uint64_t cs = open_cs(rx, iq0(), opts(2, RX(8)));
	mxArray *a = frame(cs, 8, 2, mxDOUBLE_CLASS, false);

	if (a) {
		const double *d = mxGetData(a);

		for (int k = 0; k < 8; k++) {
			CHECK(d[k] == -2048 + k);
			CHECK(d[8 + k] == 2047 - k);
		}
		mxDestroyArray(a);
	}
	close_cs(cs);
}

static void test_rx_single_fullscale(void)
{
	uint64_t cs = open_cs(rx, iq0(), opts(5, RX(1024), CLASS("single"), "scale", mt_str("fullscale"), CPLX));
	mxArray *a = frame(cs, 1024, 1, mxSINGLE_CLASS, true);

	if (a) {
		const float *d = mxGetData(a);

		CHECK(d[0] == -1.0f && d[1] == 0.99951171875f);
		CHECK(d[2] == -0.99951171875f && d[3] == 0.9990234375f);
		mxDestroyArray(a);
	}
	close_cs(cs);
}

static void test_rx_raw(void)
{
	uint64_t cs = open_cs(rx, iq0(), opts(5, RX(1024), "raw", mt_bool(true), CLASS("int16"), CPLX));
	mxArray *a = frame(cs, 1024, 1, mxINT16_CLASS, true);

	if (a) {
		const int16_t *d = mxGetData(a);

		CHECK(d[0] == 2048 && d[1] == 2047);
		CHECK(d[2] == 6145 && d[3] == 2046);
		mxDestroyArray(a);
	}
	close_cs(cs);
}

/* (1,1), (1,2) and (512,1) of the 512-by-2 frame of the list voltage1, voltage0 */
static void check_pairing(uint64_t cs)
{
	mxArray *a = frame(cs, 512, 2, mxINT16_CLASS, true);

	if (a) {
		const int16_t *d = mxGetData(a);

		CHECK(d[0] == -2047 && d[1] == 2046);
		CHECK(d[2 * 512] == -2048 && d[2 * 512 + 1] == 2047);
		CHECK(d[2 * 511] == -1025 && d[2 * 511 + 1] == 1024);
		mxDestroyArray(a);
	}
}

static void test_rx_pairing(void)
{
	uint64_t cs = open_cs(rx, swapped(), opts(4, RX(512), CLASS("int16"), CPLX));

	check_pairing(cs);
	close_cs(cs);

	/* the same list as channel handles */
	cs = open_cs(rx, ids(4, dp_chn(rx, "voltage1_i", false), dp_chn(rx, "voltage1_q", false),
		dp_chn(rx, "voltage0_i", false), dp_chn(rx, "voltage0_q", false)),
		opts(4, RX(512), CLASS("int16"), CPLX));
	check_pairing(cs);
	close_cs(cs);

	/* one channel, IIO scale: (v - 100) * 0.25 of voltage2 */
	cs = open_cs(rx, cells(1, "voltage2"), opts(4, RX(6), CLASS("single"), "scale", mt_str("iio")));
	{
		mxArray *a = frame(cs, 6, 1, mxSINGLE_CLASS, false);

		if (a) {
			const float *d = mxGetData(a);

			CHECK(d[0] == 103.0f && d[1] == 102.75f && d[2] == 359.0f && d[3] == 102.75f);
			mxDestroyArray(a);
		}
	}
	close_cs(cs);
}

static void test_rx_gain(void)
{
	uint64_t cs = open_cs(rx, iq0(), opts(5, RX(1024), CLASS("double"), CPLX, "gain", mt_dbl(2)));
	mxArray *a = frame(cs, 1024, 1, mxDOUBLE_CLASS, true);

	if (a) {
		const double *d = mxGetData(a);

		CHECK(d[0] == -4096 && d[1] == 4094);
		mxDestroyArray(a);
	}
	close_cs(cs);

	cs = open_cs(rx, swapped(), opts(5, RX(512), CLASS("double"), CPLX, "gain", dmat(1, 2, 2.0, 3.0)));
	a = frame(cs, 512, 2, mxDOUBLE_CLASS, true);
	if (a) {
		const double *d = mxGetData(a);

		CHECK(d[0] == -4094 && d[1] == 4092);
		CHECK(d[2 * 512] == -6144 && d[2 * 512 + 1] == 6141);
		mxDestroyArray(a);
	}
	close_cs(cs);

	/* gain and offset: (v + offset) is not the rule, v * gain + offset is */
	cs = open_cs(rx, iq0(), opts(4, RX(4), "gain", dmat(2, 1, 2.0, 3.0), "offset", dmat(1, 2, 1.0, -1.0)));
	a = frame(cs, 4, 2, mxDOUBLE_CLASS, false);
	if (a) {
		const double *d = mxGetData(a);

		CHECK(d[0] == -4095 && d[4] == 6140);
		mxDestroyArray(a);
	}
	close_cs(cs);

	CHECK(open_raises("iio:EINVAL", rx, swapped(), opts(5, RX(512), CLASS("double"), CPLX,
		"gain", dmat(1, 3, 1.0, 2.0, 3.0))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(4, RX(1024), CLASS("int16"), "gain", mt_dbl(2))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(1024), "gain", mt_str("2"))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(1024), "offset", dmat(2, 2, 1.0, 2.0, 3.0, 4.0))));
}

static void test_rx_errors(void)
{
	static const char *const int_fields[4] = { "samples", "nb_blocks", "buffer", "timeout_ms" };
	static const double bad_numbers[4] = { NAN, INFINITY, 2.5, 1e300 };
	uint64_t xml_ctx, xml_rx, stale, cs;
	mxArray *a;

	/* buffers are borrowed handles of their context: registered here, so that "no handle is
	 * left" can be counted for the opens that fail after the buffer was chosen */
	(void)dp_buf(rx, 0);
	(void)dp_buf(zoo, 0);

	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), CLASS("int12"))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "scale", mt_str("full"))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "colour", mt_dbl(1))));
	CHECK(strstr(mock_err_msg(), "colour") != NULL);
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, RX(0))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "nb_blocks", mt_dbl(0))));
	for (unsigned int f = 0; f < 4; f++) {
		for (unsigned int v = 0; v < 4; v++) {
			if (f == 0)
				CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, RX(bad_numbers[v]))));
			else
				CHECK(open_raises("iio:EINVAL", rx, iq0(),
					opts(3, RX(8), int_fields[f], mt_dbl(bad_numbers[v]))));
			CHECK(strstr(mock_err_msg(), int_fields[f]) != NULL);
		}
	}
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "buffer", mt_dbl(-2))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "timeout_ms", mt_dbl(-2))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "nb_blocks", mt_dbl(4294967296.0))));
	/* every block is created inside the open, which nothing can interrupt: at most 4096 */
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "nb_blocks", mt_dbl(4097))));
	CHECK(strstr(mock_err_msg(), "nb_blocks") != NULL && strstr(mock_err_msg(), "4096") != NULL);
	{
		uint64_t big = open_cs(rx, iq0(), opts(3, RX(1), "nb_blocks", mt_dbl(4096)));

		CHECK(big != 0);
		close_cs(big);
	}
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "timeout_ms", mt_dbl(2147483648.0))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "buffer", mt_dbl(4294967296.0))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_bool(false),
		"samples", mxCreateDoubleMatrix(1, 0, mxREAL))));
	CHECK(strstr(mock_err_msg(), "samples") != NULL);
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(1, "output", mt_bool(false))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(1, "samples", mt_dbl(8))));
	CHECK(strstr(mock_err_msg(), "output") != NULL);
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_dbl(0), "samples", mt_dbl(8))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_empty(), "samples", mt_dbl(8))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_bool(false), "samples", mt_str("8"))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_bool(false), "samples", mt_bool(true))));
	CHECK(open_raises("iio:EINVAL", rx, cells(3, "voltage0_i", "voltage0_q", "voltage1_i"),
		opts(3, RX(8), CPLX)));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "cyclic", mt_bool(true))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "cyclic", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "buffer", mt_dbl(3))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "buffer", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, ids(2, dp_chn(rx, "voltage0_i", false), dp_chn(rx, "voltage0_q", false)),
		opts(2, "output", mt_bool(true), "samples", mt_dbl(8))));
	/* the direction of the stream is that of the buffer */
	CHECK(open_raises("iio:EINVAL", tx, iq0(), opts(3, "output", mt_bool(true), "samples", mt_dbl(8),
		"buffer", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", multi, cells(1, "voltage0"), opts(3, RX(2), "buffer", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(4, RX(8), "scale", mt_str("fullscale"), CLASS("int16"))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(4, RX(8), "scale", mt_str("iio"), "raw", mt_bool(true))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "raw", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "complex", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "class", mt_dbl(1))));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "class", mock_string_object())));

	/* the options argument itself */
	CHECK(open_raises("iio:EINVAL", rx, iq0(), mt_empty()));
	CHECK(open_raises("iio:EINVAL", rx, iq0(), mt_dbl(8)));
	{
		const char *name = "samples";

		CHECK(open_raises("iio:EINVAL", rx, iq0(), mxCreateStructMatrix(1, 2, 1, &name)));
		CHECK(open_raises("iio:EINVAL", rx, iq0(), mxCreateStructMatrix(0, 0, 1, &name)));
	}
	CHECK(open_raises("iio:EINVAL", rx, iq0(), iq0()));

	/* the channel list */
	CHECK(open_raises("iio:ENOENT", rx, cells(2, "voltage0_i", "voltage9"), opts(2, RX(8))));
	CHECK(strstr(mock_err_msg(), "voltage9") != NULL);
	CHECK(open_raises("iio:ENOENT", rx, cells(1, ""), opts(2, RX(8))));
	CHECK(open_raises("iio:ENOENT", tx, iq0(), opts(2, RX(8))));     /* outputs only on emu-tx */
	CHECK(open_raises("iio:EINVAL", rx, mxCreateCellMatrix(1, 0), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mxCreateNumericMatrix(1, 0, mxUINT64_CLASS, mxREAL), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mxCreateCellMatrix(2, 2), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mxCreateCellMatrix(1, 2), opts(2, RX(8))));    /* unset elements */
	CHECK(open_raises("iio:EINVAL", rx, mt_str("voltage0_i"), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mock_string_object(), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mt_empty(), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, mt_dbl(1), opts(2, RX(8))));
	CHECK(open_raises("iio:EINVAL", rx, cells(2, "voltage0_i", "voltage0_i"), opts(2, RX(8))));
	a = mxCreateCellMatrix(1, 2);
	mxSetCell(a, 0, mt_str("voltage0_i"));
	mxSetCell(a, 1, mt_dbl(1));
	CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));
	a = mxCreateCellMatrix(1, 2);
	mxSetCell(a, 0, mt_str("voltage0_i"));
	mxSetCell(a, 1, mock_string_object());
	CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));
	a = mxCreateCellMatrix(1, 1);
	mxSetCell(a, 0, mock_char_array(2, 5, 1, "vvoollttaa"));
	CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));
	{       /* text with an embedded NUL is refused, not cut to "voltage0_q" */
		mxArray *name = mt_str("voltage0_q?x"), *cls = mt_str("int16?"), *scale = mt_str("iio?x");

		((mxChar *)mxGetData(name))[10] = 0;
		((mxChar *)mxGetData(cls))[5] = 0;
		((mxChar *)mxGetData(scale))[3] = 0;
		a = mxCreateCellMatrix(1, 2);
		mxSetCell(a, 0, mt_str("voltage0_i"));
		mxSetCell(a, 1, name);
		CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));
		CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "class", cls)));
		CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(3, RX(8), "scale", scale)));
	}
	/* handles: a channel of another device, something that is no channel, a stale id */
	CHECK(open_raises("iio:EINVAL", rx, ids(2, dp_chn(rx, "voltage0_i", false), dp_chn(tx, "voltage0_q", true)),
		opts(2, RX(8))));
	CHECK(open_raises("iio:EBADF", rx, ids(2, dp_chn(rx, "voltage0_i", false), rx), opts(2, RX(8))));
	CHECK(open_raises("iio:EBADF", rx, ids(2, dp_chn(rx, "voltage0_i", false), (uint64_t)0), opts(2, RX(8))));
	a = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxCOMPLEX);
	CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));
	a = mxCreateNumericMatrix(2, 2, mxUINT64_CLASS, mxREAL);
	CHECK(open_raises("iio:EINVAL", rx, a, opts(2, RX(8))));

	/* a format the converter does not support, a backend without buffers */
	CHECK(open_raises("iio:ENOTSUP", zoo, cells(1, "rot_quaternion"), opts(2, RX(8))));
	xml_ctx = mt_open_context(fx.xml_uri);
	CHECK(xml_ctx != 0);
	xml_rx = dp_dev(xml_ctx, "emu-rx");
	(void)dp_buf(xml_rx, 0);
	CHECK(open_raises("iio:ENOSYS", xml_rx, iq0(), opts(2, RX(8))));
	gw_handle_destroy(xml_ctx);
	stale = xml_rx;
	CHECK(open_raises("iio:EBADF", stale, iq0(), opts(2, RX(8))));
	CHECK(open_raises("iio:EBADF", ctx, iq0(), opts(2, RX(8))));
	CHECK(open_raises("iio:EBADF", 0, iq0(), opts(2, RX(8))));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_open", 3, mt_dbl(1), iq0(), opts(2, RX(8))));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_open", 2, mt_u64(rx), iq0()));

	/* a frame whose byte count does not fit a size_t: the stream layer's -EINVAL */
	{
		mxArray *n = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);

		*(int64_t *)mxGetData(n) = INT64_MAX;
		CHECK(open_raises("iio:EINVAL", rx, iq0(), opts(2, "output", mt_bool(false), "samples", n)));
	}

	/* after all that the device still opens */
	cs = open_cs(rx, iq0(), opts(4, RX(8), CLASS("int16"), CPLX));
	a = frame(cs, 8, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 0) == 0);
		mxDestroyArray(a);
	}
	close_cs(cs);
}

static void test_rx_flush_close(void)
{
	static const int16_t A[8] = { 100, 1, -200, 2, 300, 3, -400, 4 };
	uint64_t cs = open_cs(rx, iq0(), opts(5, RX(1024), "nb_blocks", mt_dbl(2), CLASS("int16"), CPLX));
	size_t before;
	mxArray *a;

	a = frame(cs, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 0) == 0);
		mxDestroyArray(a);
	}
	CHECK(flush(cs) == true);
	a = frame(cs, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		const int16_t *d = mxGetData(a);

		CHECK(d[0] == 1024 && d[1] == -1025);
		CHECK(ramp_mismatches(a, 3072) == 0);
		mxDestroyArray(a);
	}

	/* a receive stream does not transmit; the wrong handle type is no stream */
	CHECK(mt_raises("iio:EBADF", "conv_stream_write", 2, mt_u64(cs), MAT(mxINT16_CLASS, true, 4, 1, A)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_flush", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_close", 1, mt_u64(rx)));     /* live, another type */
	CHECK(mt_raises("iio:EINVAL", "conv_stream_read", 1, mt_dbl((double)cs)));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_close", 1, mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_read", 0));
	CHECK(gw_handle_type(cs) == GW_T_CONV_STREAM);

	before = gw_handle_count();
	close_cs(cs);
	CHECK(gw_handle_count() == before - 1);
	close_cs(cs);
	close_cs(0);
	CHECK(gw_handle_count() == before - 1);
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_flush", 1, mt_u64(cs)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_write", 2, mt_u64(cs), MAT(mxINT16_CLASS, true, 4, 1, A)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(0)));

	/* release and re-create with another frame size: the first frame again */
	cs = open_cs(rx, iq0(), opts(4, RX(64), CLASS("int16"), CPLX));
	a = frame(cs, 64, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 0) == 0);
		mxDestroyArray(a);
	}
	close_cs(cs);
}

/* ------------------------------------------------------------------ transmit, emu */

static uint64_t open_tx(void)
{
	return open_cs(tx, iq0(), opts(4, TX4, CPLX));
}

static void test_tx_basic(void)
{
	uint64_t cs = open_tx();

	CHECK(cs != 0);
	CHECK(gw_handle_parent(cs) == dp_buf(tx, 0));
	CHECK(flush(cs) == true);                 /* nothing in flight before the first write */
	CHECK(wr(cs, frame_a()) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, file_a, 8));
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
	CHECK(gw_handle_type(cs) == GW_T_CONV_STREAM);
	close_cs(cs);
}

/* a complex array with a zero imaginary part arrives real */
static void test_tx_real_input(void)
{
	static const int16_t R[4] = { 7, -8, 9, -10 };
	static const int16_t sent[8] = { 7, 0, -8, 0, 9, 0, -10, 0 };
	uint64_t cs = open_tx();

	CHECK(wr(cs, frame_a()) == true);         /* Q of the block is not what frame A left there */
	CHECK(wr(cs, MAT(mxINT16_CLASS, false, 4, 1, R)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent, 8));
	close_cs(cs);
}

static void test_tx_classes(void)
{
	static const double D[8] = { 100.4, -200.6, 32767.9, -40000, 0.5, 1.5, 2.5, -0.5 };
	static const int16_t sent_d[8] = { 100, -201, 32767, -32768, 0, 2, 2, 0 };
	static const int32_t L[8] = { 40000, -40000, 5, -5, 32768, -32769, 0, 1 };
	static const int16_t sent_l[8] = { 32767, -32768, 5, -5, 32767, -32768, 0, 1 };
	static const float F[8] = { 1.5f, -1.5f, 3, 4, -5, 6, 70000, -70000 };
	static const int16_t sent_f[8] = { 2, -2, 3, 4, -5, 6, 32767, -32768 };
	static const uint8_t U[8] = { 1, 2, 3, 4, 255, 0, 7, 8 };
	static const int16_t sent_u[8] = { 1, 2, 3, 4, 255, 0, 7, 8 };
	uint64_t cs = open_tx();

	CHECK(wr(cs, MAT(mxDOUBLE_CLASS, true, 4, 1, D)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent_d, 8));
	CHECK(wr(cs, MAT(mxINT32_CLASS, true, 4, 1, L)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent_l, 8));
	CHECK(wr(cs, MAT(mxSINGLE_CLASS, true, 4, 1, F)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent_f, 8));
	CHECK(wr(cs, MAT(mxUINT8_CLASS, true, 4, 1, U)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent_u, 8));
	close_cs(cs);
}

static void test_tx_shape(void)
{
	static const int16_t two[4] = { 5, 6, 7, 8 };
	uint64_t cs = open_tx();
	mxArray *a;

	CHECK(wr(cs, frame_a()) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, file_a, 8));

	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mxCreateNumericMatrix(4, 2, mxINT16_CLASS, mxCOMPLEX)));
	CHECK(strstr(mock_err_msg(), "columns") != NULL);
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mxCreateNumericMatrix(5, 1, mxINT16_CLASS, mxCOMPLEX)));
	CHECK(strstr(mock_err_msg(), "rows") != NULL);
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mxCreateNumericMatrix(0, 1, mxINT16_CLASS, mxCOMPLEX)));
	CHECK(strstr(mock_err_msg(), "rows") != NULL);
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mock_nd_array(mxINT16_CLASS, mxCOMPLEX, 4, 1, 2)));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mt_str("abcd")));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mock_char_array(4, 1, 1, "abcd")));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mxCreateLogicalMatrix(4, 1)));
	a = mxCreateCellMatrix(4, 1);
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), a));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mock_sparse_double(4, 1)));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), opts(1, "a", mt_dbl(1))));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 1, mt_u64(cs)));
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, file_a, 8));       /* nothing was sent by a rejected call */

	CHECK(wr(cs, MAT(mxINT16_CLASS, true, 2, 1, two)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_len(TX_FILE) == 8);
	CHECK(file_is(TX_FILE, two, 4));
	close_cs(cs);
}

static void test_tx_real_stream(void)
{
	static const int16_t R[8] = { 100, -200, 300, -400, 1, 2, 3, 4 };    /* [100 1; -200 2; 300 3; -400 4] */
	uint64_t cs = open_cs(tx, iq0(), opts(3, TX4));

	CHECK(wr(cs, MAT(mxINT16_CLASS, false, 4, 2, R)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, file_a, 8));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mxCreateNumericMatrix(4, 2, mxINT16_CLASS, mxCOMPLEX)));
	CHECK(strstr(mock_err_msg(), "complex") != NULL);
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs),
		mxCreateNumericMatrix(4, 1, mxINT16_CLASS, mxREAL)));
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, file_a, 8));
	close_cs(cs);
}

static void test_tx_scaled(void)
{
	static const double D[8] = { 1, -1, 0.5, -0.5, 0.25, 0, 0, 0 };
	static const int16_t sent[8] = { 32767, -32768, 16384, -16384, 8192, 0, 0, 0 };
	uint64_t cs = open_cs(tx, iq0(), opts(5, TX4, CPLX, "scale", mt_str("fullscale")));

	CHECK(wr(cs, MAT(mxDOUBLE_CLASS, true, 4, 1, D)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent, 8));
	CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), frame_a()));
	CHECK(flush(cs) == true);
	CHECK(file_is(TX_FILE, sent, 8));
	close_cs(cs);
}

static void test_tx_cyclic(void)
{
	uint64_t cs = open_cs(tx, iq0(), opts(5, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(4), "cyclic", mt_bool(true), CPLX));
	double t0;

	CHECK(wr(cs, frame_a()) == true);
	CHECK(mt_raises("iio:EBUSY", "conv_stream_write", 2, mt_u64(cs), frame_a()));
	CHECK(flush(cs) == true);
	t0 = now_s();
	close_cs(cs);
	CHECK(now_s() - t0 < 1.0);
}

/* emu-multi: the output buffer is the second one; 12 bits in the upper part of 16-bit words */
static void test_tx_buffer_choice(void)
{
	static const double V[4] = { 0.5, -0.5, 1, -1 };      /* [0.5 1; -0.5 -1] */
	static const int16_t R[4] = { 32767, 1, -32768, -1 }; /* [32767 -32768; 1 -1] */
	static const uint16_t sent_v[4] = { 0x4000, 0x7ff0, 0xc000, 0x8000 };
	static const uint16_t sent_r[4] = { 0x7ff0, 0x8000, 0x0010, 0xfff0 };
	static const uint16_t sent_raw[4] = { 0x7fff, 0x8000, 0x0001, 0xffff };
	uint64_t cs;

	cs = open_cs(multi, cells(2, "voltage0", "voltage1"), opts(3, "output", mt_bool(true),
		"samples", mt_dbl(2), "scale", mt_str("fullscale")));
	CHECK(cs != 0);
	CHECK(gw_handle_parent(cs) == dp_buf(multi, 1));
	CHECK(wr(cs, MAT(mxDOUBLE_CLASS, false, 2, 2, V)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(MULTI_FILE, sent_v, 4));
	close_cs(cs);

	cs = open_cs(multi, cells(2, "voltage0", "voltage1"), opts(2, "output", mt_bool(true),
		"samples", mt_dbl(2)));
	CHECK(wr(cs, MAT(mxINT16_CLASS, false, 2, 2, R)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(MULTI_FILE, sent_r, 4));
	close_cs(cs);

	/* the buffer named by its position */
	cs = open_cs(multi, cells(2, "voltage0", "voltage1"), opts(4, "output", mt_bool(true),
		"samples", mt_dbl(2), "raw", mt_bool(true), "buffer", mt_dbl(1)));
	CHECK(gw_handle_parent(cs) == dp_buf(multi, 1));
	CHECK(wr(cs, MAT(mxINT16_CLASS, false, 2, 2, R)) == true);
	CHECK(flush(cs) == true);
	CHECK(file_is(MULTI_FILE, sent_raw, 4));
	close_cs(cs);

	/* and the input side of the same device is its first buffer (emu wants its data file) */
	CHECK(fixture_write_file(fx.dir, "emu-multi_buf0.bin", sent_v, sizeof(sent_v)) == 0);
	cs = open_cs(multi, cells(1, "voltage0"), opts(2, RX(2)));
	CHECK(cs != 0 && gw_handle_parent(cs) == dp_buf(multi, 0));
	close_cs(cs);
	CHECK(open_raises("iio:EINVAL", multi, cells(2, "voltage0", "voltage1"), opts(3, "output", mt_bool(true),
		"samples", mt_dbl(2), "buffer", mt_dbl(0))));

	/* the automatic choice looks at the scan elements: altvoltage0 of emu-tx is in no buffer.
	 * Refused by the choice itself, before a buffer is registered. */
	{
		size_t before = gw_handle_count();

		CHECK(open_raises("iio:EINVAL", tx, cells(1, "altvoltage0"), opts(3, "output", mt_bool(true),
			"samples", mt_dbl(2), "nb_blocks", mt_dbl(1))));
		CHECK(strstr(mock_err_msg(), "holds every channel") != NULL);
		CHECK(open_raises("iio:EINVAL", tx, cells(2, "voltage0_i", "altvoltage0"), opts(3, "output", mt_bool(true),
			"samples", mt_dbl(2), "nb_blocks", mt_dbl(1))));
		CHECK(strstr(mock_err_msg(), "holds every channel") != NULL);
		CHECK(gw_handle_count() == before);
	}
}

/* a receive and a transmit stream at once on one context */
static void test_rx_and_tx(void)
{
	uint64_t r = open_cs(rx, iq0(), opts(4, RX(1024), CLASS("int16"), CPLX));
	uint64_t t = open_cs(tx, iq0(), opts(4, "output", mt_bool(true), "samples", mt_dbl(4), CPLX,
		"nb_blocks", mt_dbl(2)));
	mxArray *a;

	a = frame(r, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 0) == 0);
		mxDestroyArray(a);
	}
	CHECK(wr(t, frame_a()) == true);
	a = frame(r, 1024, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(ramp_mismatches(a, 1024) == 0);
		mxDestroyArray(a);
	}
	CHECK(flush(t) == true);
	CHECK(file_is(TX_FILE, file_a, 8));
	/* the last frame written goes out when the stream is closed */
	{
		static const int16_t B[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };
		static const int16_t C[8] = { 21, 22, 23, 24, 25, 26, 27, 28 };

		CHECK(wr(t, MAT(mxINT16_CLASS, true, 4, 1, B)) == true);
		CHECK(wr(t, MAT(mxINT16_CLASS, true, 4, 1, C)) == true);
		close_cs(t);
		CHECK(file_is(TX_FILE, C, 8));
	}
	close_cs(r);
}

/* The numeric locale of the process has a decimal comma: the fast path parses and prints no
 * number as text, and leaves the locale alone. The context was made before the block. */
static void test_comma_locale(void)
{
	if (mt_comma_begin()) {
		uint64_t cs = open_cs(rx, cells(1, "voltage2"), opts(5, RX(6), CLASS("single"),
			"scale", mt_str("iio"), "gain", mt_dbl(0.5)));
		mxArray *a = frame(cs, 6, 1, mxSINGLE_CLASS, false);

		if (a) {
			const float *d = mxGetData(a);

			CHECK(d[0] == 51.5f && d[1] == 51.375f);
			mxDestroyArray(a);
		}
		CHECK(flush(cs) == true);
		close_cs(cs);
		mt_comma_end();
	}
}

/* ------------------------------------------------------------------ fake backend */

static void test_rx_no_frame(void)
{
	static const int16_t first[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const unsigned char zero[16], zero32[32];
	uint64_t f, frx, cs;
	double t0, dt;
	bool valid;
	mxArray *a;

	f = dp_open_fake(fake_xml);
	frx = dp_dev(f, "emu-rx");
	cs = open_cs(frx, iq0(), opts(6, RX(4), "nb_blocks", mt_dbl(2), CLASS("int16"), CPLX,
		"timeout_ms", mt_dbl(0)));
	CHECK(cs != 0);
	a = frame(cs, 4, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(!memcmp(mxGetData(a), first, sizeof(first)));
		mxDestroyArray(a);
	}
	a = frame(cs, 4, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(((const int16_t *)mxGetData(a))[0] == 256 && ((const int16_t *)mxGetData(a))[1] == 257);
		mxDestroyArray(a);
	}
	a = frame(cs, 4, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(((const int16_t *)mxGetData(a))[0] == 512 && ((const int16_t *)mxGetData(a))[1] == 513);
		mxDestroyArray(a);
	}
	/* the fourth: no error, not valid, a frame of zeros */
	a = rd(cs, &valid);
	CHECK(a != NULL && !valid);
	CHECK(is(a, 4, 1, mxINT16_CLASS, true));
	if (a) {
		CHECK(!memcmp(mxGetData(a), zero, sizeof(zero)));
		mxDestroyArray(a);
	}
	/* and with the data alone */
	{
		mxArray *out[1];

		CHECK(mt_call(1, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
		CHECK(is(out[0], 4, 1, mxINT16_CLASS, true));
		CHECK(!memcmp(mxGetData(out[0]), zero, sizeof(zero)));
		mxDestroyArray(out[0]);
	}
	CHECK(flush(cs) == true);
	/* data again: the stream goes on */
	fake.rx_frames = 4;
	a = frame(cs, 4, 1, mxINT16_CLASS, true);
	if (a) {
		CHECK(((const int16_t *)mxGetData(a))[0] == 768);
		mxDestroyArray(a);
	}

	/* allocation failures: a read that fails for lack of memory has not taken a frame */
	fake.rx_frames = 100;
	fake_clear_log();
	CHECK(mt_oom_sweep(2, "conv_stream_read", 1, mt_u64(cs)) >= 2);
	CHECK(count_in(fake.log, 'D') == 1 && fake.rx_count == 5);
	fake_clear_log();
	CHECK(mt_oom_sweep(1, "conv_stream_flush", 1, mt_u64(cs)) >= 1);
	CHECK(count_in(fake.log, 'D') == 2);       /* one flush: both blocks */
	gw_handle_destroy(f);

	/* a time limit of the stream's own, a double class: 4-by-1 complex double zeros */
	f = dp_open_fake(fake_xml);
	frx = dp_dev(f, "emu-rx");
	cs = open_cs(frx, iq0(), opts(5, RX(4), "nb_blocks", mt_dbl(2), CPLX, "timeout_ms", mt_dbl(30)));
	for (int i = 0; i < 3; i++) {
		a = frame(cs, 4, 1, mxDOUBLE_CLASS, true);
		if (a)
			mxDestroyArray(a);
	}
	t0 = now_s();
	a = rd(cs, &valid);
	dt = now_s() - t0;
	CHECK(dt >= 0.030 && dt < 1.0);
	CHECK(a != NULL && !valid);
	CHECK(is(a, 4, 1, mxDOUBLE_CLASS, true));
	if (a) {
		const double *d = mxGetData(a);

		for (int i = 0; i < 8; i++)
			CHECK(d[i] == 0);
		mxDestroyArray(a);
	}
	CHECK(fake.timeout == FAKE_NO_TIMEOUT);    /* the context timeout was never touched */
	close_cs(cs);

	/* every byte of a frame that did not come is zero, whatever the class: single and int8,
	 * complex (the mock fills an uninitialised array with 0xA5) */
	fake.rx_frames = 0;
	cs = open_cs(frx, iq0(), opts(5, RX(4), CLASS("single"), CPLX, "timeout_ms", mt_dbl(0)));
	a = rd(cs, &valid);
	CHECK(a != NULL && !valid);
	CHECK(is(a, 4, 1, mxSINGLE_CLASS, true));
	if (a) {
		CHECK(!memcmp(mxGetData(a), zero32, 32));
		mxDestroyArray(a);
	}
	close_cs(cs);
	cs = open_cs(frx, iq0(), opts(5, RX(4), CLASS("int8"), CPLX, "timeout_ms", mt_dbl(0)));
	a = rd(cs, &valid);
	CHECK(a != NULL && !valid);
	CHECK(is(a, 4, 1, mxINT8_CLASS, true));
	if (a) {
		CHECK(!memcmp(mxGetData(a), zero32, 8));
		mxDestroyArray(a);
	}
	gw_handle_destroy(f);
}

static void test_tx_no_block(void)
{
	uint64_t f = dp_open_fake(fake_xml);
	uint64_t ftx = dp_dev(f, "emu-tx");
	uint64_t cs = open_cs(ftx, iq0(), opts(5, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(2), CPLX, "timeout_ms", mt_dbl(0)));
	int16_t v[8];

	for (int16_t k = 1; k <= 5; k++) {
		for (int i = 0; i < 8; i++)
			v[i] = (int16_t)(10 * k + i);
		CHECK(wr(cs, MAT(mxINT16_CLASS, true, 4, 1, v)) == (k <= 4));
	}
	for (int i = 0; i < 8; i++)
		v[i] = (int16_t)(40 + i);
	CHECK(fake.tx_bytes == 16 && !memcmp(fake.tx_data, v, 16));     /* still frame 4 */
	CHECK(count_in(fake.log, 'E') == 4);
	CHECK(flush(cs) == false);                                     /* and it does not complete */

	/* allocation failures: a write that fails for lack of memory has queued nothing */
	fake.tx_frames = 100;
	fake_clear_log();
	CHECK(mt_oom_sweep(1, "conv_stream_write", 2, mt_u64(cs), frame_a()) >= 1);
	CHECK(count_in(fake.log, 'E') == 1);
	CHECK(fake.tx_bytes == 16 && !memcmp(fake.tx_data, file_a, 16));
	CHECK(flush(cs) == true);

	/* cyclic: the flag reaches the backend, the block is never dequeued */
	close_cs(cs);
	fake_clear_log();
	cs = open_cs(ftx, iq0(), opts(5, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(4), "cyclic", mt_bool(true), CPLX));
	CHECK(wr(cs, frame_a()) == true);
	CHECK(fake.tx_cyclic);
	CHECK(mt_raises("iio:EBUSY", "conv_stream_write", 2, mt_u64(cs), frame_a()));
	close_cs(cs);
	CHECK(!strcmp(fake.log, "OBeSXsFXC"));
	gw_handle_destroy(f);
}

/* MATLAB unloads the MEX file while streams are open */
static void test_unload(void)
{
	uint64_t f = dp_open_fake(fake_xml);
	uint64_t r = open_cs(dp_dev(f, "emu-rx"), iq0(), opts(4, RX(4), CLASS("int16"), CPLX));
	uint64_t t = open_cs(dp_dev(f, "emu-tx"), iq0(), opts(4, TX4, CPLX));
	size_t len;

	CHECK(r != 0 && t != 0);
	CHECK(wr(t, frame_a()) == true);
	CHECK(mock_is_locked());
	mock_exit_matlab();
	len = strlen(fake.log);
	CHECK(len > 0 && fake.log[len - 1] == 'C');
	CHECK(count_in(fake.log, 'C') == 2);
	CHECK(count_in(fake.log, 'B') == 3 && count_in(fake.log, 'F') == 3);
	CHECK(count_in(fake.log, 'D') == 1);      /* the transmit frame was awaited before the cancel */
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());
}

/* a destructor never blocks without bound: with an infinite context timeout the frames in flight
 * are polled for a limited time, not awaited by blocking dequeues */
static void test_unload_tx_infinite(void)
{
	uint64_t f = dp_open_fake(fake_xml);
	struct iio_context *c = gw_handle_ptr(f, GW_T_CONTEXT);
	uint64_t t;
	double t0, dt;

	CHECK(iio_context_set_timeout(c, -1) == 0);
	CHECK(iio_context_get_params(c)->timeout_ms < 0);
	fake.tx_frames = 0;
	t = open_cs(dp_dev(f, "emu-tx"), iq0(), opts(4, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(2), CPLX));
	CHECK(wr(t, frame_a()) == true);
	CHECK(wr(t, frame_a()) == true);
	fake_clear_log();
	t0 = now_s();
	mock_exit_matlab();
	dt = now_s() - t0;
	CHECK(dt < 2.0);
	CHECK(count_in(fake.log, 'd') >= 3);      /* polled (the log is full of it) */
	CHECK(gw_handle_count() == 0);

	/* frames that do complete end the wait at once */
	f = dp_open_fake(fake_xml);
	c = gw_handle_ptr(f, GW_T_CONTEXT);
	CHECK(iio_context_set_timeout(c, -1) == 0);
	fake.tx_frames = 100;
	t = open_cs(dp_dev(f, "emu-tx"), iq0(), opts(4, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(2), CPLX));
	CHECK(wr(t, frame_a()) == true);
	CHECK(wr(t, frame_a()) == true);
	fake_clear_log();
	t0 = now_s();
	close_cs(t);
	CHECK(now_s() - t0 < 0.5);
	CHECK(!strcmp(fake.log, "DDXsFFXC"));
	gw_handle_destroy(f);

	/* the bounded poll is for that one case only. A finite context timeout: the close asks
	 * once per frame in flight, with a dequeue that may wait (the fake answers at once) */
	f = dp_open_fake(fake_xml);
	c = gw_handle_ptr(f, GW_T_CONTEXT);
	CHECK(iio_context_set_timeout(c, 250) == 0);
	CHECK(iio_context_get_params(c)->timeout_ms == 250);
	fake.tx_frames = 0;
	t = open_cs(dp_dev(f, "emu-tx"), iq0(), opts(4, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(2), CPLX));
	CHECK(wr(t, frame_a()) == true);
	CHECK(wr(t, frame_a()) == true);
	fake_clear_log();
	t0 = now_s();
	close_cs(t);
	CHECK(now_s() - t0 < 0.5);
	CHECK(count_in(fake.log, 'd') <= 4 && count_in(fake.log, 'D') == 0);
	if (count_in(fake.log, 'd') > 4)
		fprintf(stderr, "fake log (close, finite context timeout): %s\n", fake.log);
	gw_handle_destroy(f);

	/* a stream that never waits (timeout_ms 0) on a context without timeout: no poll either */
	f = dp_open_fake(fake_xml);
	c = gw_handle_ptr(f, GW_T_CONTEXT);
	CHECK(iio_context_set_timeout(c, -1) == 0);
	fake.tx_frames = 0;
	t = open_cs(dp_dev(f, "emu-tx"), iq0(), opts(5, "output", mt_bool(true), "samples", mt_dbl(4),
		"nb_blocks", mt_dbl(2), CPLX, "timeout_ms", mt_dbl(0)));
	CHECK(wr(t, frame_a()) == true);
	CHECK(wr(t, frame_a()) == true);
	fake_clear_log();
	t0 = now_s();
	close_cs(t);
	CHECK(now_s() - t0 < 0.5);
	CHECK(count_in(fake.log, 'd') <= 4 && count_in(fake.log, 'D') == 0);
	gw_handle_destroy(f);
}

static void test_oom_open(void)
{
	uint64_t f = dp_open_fake(fake_xml);
	uint64_t frx = dp_dev(f, "emu-rx");
	size_t before;

	(void)dp_buf(frx, 0);
	before = gw_handle_count();
	fake_clear_log();
	CHECK(mt_oom_sweep(1, "conv_stream_open", 3, mt_u64(frx), iq0(), opts(7, RX(4), CLASS("double"), CPLX,
		"gain", mt_dbl(1), "offset", mt_dbl(0), "scale", mt_str("none"))) >= 1);
	CHECK(count_in(fake.log, 'O') == 1);
	CHECK(gw_handle_count() == before + 1);   /* the one stream of the attempt that succeeded */
	gw_handle_destroy(f);
	CHECK(count_in(fake.log, 'C') == 1);
}

int main(int argc, char **argv)
{
	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	fake_xml = fx.xml;
	mt_check_arities(&gw_group_conv, arities, sizeof(arities) / sizeof(arities[0]));

	ctx = mt_open_context(fx.uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	multi = dp_dev(ctx, "emu-multi");
	zoo = dp_dev(ctx, "iio:device4");

	test_rx_int16();
	test_rx_defaults();
	test_rx_single_fullscale();
	test_rx_raw();
	test_rx_pairing();
	test_rx_gain();
	test_rx_errors();
	test_rx_flush_close();

	test_tx_basic();
	test_tx_real_input();
	test_tx_classes();
	test_tx_shape();
	test_tx_real_stream();
	test_tx_scaled();
	test_tx_cyclic();
	test_tx_buffer_choice();
	test_rx_and_tx();
	test_comma_locale();

	/* the context goes while a stream is open: the stream goes first, its id is stale */
	{
		uint64_t cs = open_cs(rx, iq0(), opts(2, RX(8)));
		uint64_t t = open_tx();

		CHECK(wr(t, frame_a()) == true);
		gw_handle_destroy(ctx);
		CHECK(gw_handle_type(cs) == GW_T_NONE && gw_handle_type(t) == GW_T_NONE);
		CHECK(file_is(TX_FILE, file_a, 8));       /* the frame in flight went out first */
		CHECK(mt_raises("iio:EBADF", "conv_stream_write", 2, mt_u64(t), frame_a()));
		CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
		close_cs(cs);
		CHECK(gw_handle_count() == 0);
	}

	test_rx_no_frame();
	test_tx_no_block();
	test_unload();
	test_unload_tx_infinite();
	test_oom_open();

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
