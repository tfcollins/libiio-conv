/* SPDX-License-Identifier: MIT */
/* Command group "attr": names, static values, text and byte reads with buffer growth, typed
 * reads and writes, exact 64-bit integers and doubles, binary payloads by byte count,
 * available lists and ranges. */
#include "mex_test.h"
#include <iio/iio-backend.h>
#include <locale.h>
#include <math.h>
MT_GROUPS(&gw_group_attr);

/* the table "Group attr" of the Command reference */
static const struct mt_arity arities[] = {
	{ "attr_get_name", 1, 1, 1 },
	{ "attr_get_filename", 1, 1, 1 },
	{ "attr_get_static_value", 1, 1, 1 },
	{ "attr_read_raw", 1, 2, 1 },
	{ "attr_write_raw", 2, 2, 1 },
	{ "attr_write_string", 2, 2, 1 },
	{ "attr_read_bool", 1, 1, 1 },
	{ "attr_read_longlong", 1, 1, 1 },
	{ "attr_read_double", 1, 1, 1 },
	{ "attr_write_bool", 2, 2, 0 },
	{ "attr_write_longlong", 2, 2, 0 },
	{ "attr_write_double", 2, 3, 0 },
	{ "attr_get_range", 1, 1, 1 },
	{ "attr_get_available", 1, 1, 1 },
};

/* ---- a backend with read_attr and write_attr only: what emu cannot do with a read buffer,
 * and writes that fail or count differently ---- */
enum probe_mode {
	PROBE_EFBIG_THEN_FITS,   /* -EFBIG while the buffer is too small (the local backend's answer) */
	PROBE_ALWAYS_EFBIG,      /* never fits, reported as -EFBIG */
	PROBE_ALWAYS_FULL,       /* never fits, reported as "len bytes" (emu, context attributes, binary iiod) */
	PROBE_EIO,               /* -EIO: an I/O error, and the legacy iiod's "buffer too small" */
	PROBE_NO_NUL_SHORT,      /* 5 bytes; no NUL anywhere in the buffer */
	PROBE_NO_NUL_ALMOST,     /* len - 1 bytes; no NUL anywhere in the buffer */
	PROBE_NO_NUL_ZERO,       /* 0 bytes; no NUL anywhere in the buffer */
	PROBE_HUGE_NEGATIVE,     /* a negative value that is 0 when narrowed to int */
};
#define PROBE_HUGE (-4294967296LL)
#define PROBE_VALUE_LEN 40000u   /* characters of the value of PROBE_EFBIG_THEN_FITS */
static enum probe_mode probe_mode;
static size_t probe_lens[16];    /* buffer sizes of the read_attr calls since probe_calls was reset */
static unsigned int probe_calls;

static ssize_t probe_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	(void)attr;
	if (probe_calls < 16)
		probe_lens[probe_calls] = len;
	probe_calls++;
	switch (probe_mode) {
	case PROBE_EFBIG_THEN_FITS:
		if (len < PROBE_VALUE_LEN + 1)
			return -EFBIG;
		memset(dst, 'x', PROBE_VALUE_LEN);
		dst[PROBE_VALUE_LEN] = '\0';
		return PROBE_VALUE_LEN + 1;
	case PROBE_ALWAYS_EFBIG:
		return -EFBIG;
	case PROBE_ALWAYS_FULL:
		memset(dst, 'y', len);
		return (ssize_t)len;
	case PROBE_NO_NUL_SHORT:
		memset(dst, 'z', len);
		return 5;
	case PROBE_NO_NUL_ALMOST:
		memset(dst, 'z', len);
		return (ssize_t)len - 1;
	case PROBE_NO_NUL_ZERO:
		memset(dst, 'z', len);
		return 0;
	case PROBE_HUGE_NEGATIVE:
		return (ssize_t)PROBE_HUGE;
	case PROBE_EIO:
	default:
		return -EIO;
	}
}

#define PROBE_WRITE_ALL 1                /* probe_write_ret: "every byte was written" */
static long long probe_write_ret = PROBE_WRITE_ALL;   /* else: what write_attr returns */
static unsigned int probe_writes;        /* write_attr calls */
static char probe_written[64];           /* the first bytes of the last write */
static size_t probe_written_len;         /* its length */

static ssize_t probe_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	(void)attr;
	probe_writes++;
	probe_written_len = len;
	memset(probe_written, 0, sizeof(probe_written));
	memcpy(probe_written, src, len < sizeof(probe_written) ? len : sizeof(probe_written));
	return probe_write_ret == PROBE_WRITE_ALL ? (ssize_t)len : (ssize_t)probe_write_ret;
}

static const struct iio_backend_ops probe_ops = {
	.read_attr = probe_read_attr,
	.write_attr = probe_write_attr,
};
static const struct iio_backend probe_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "probe",
	.uri_prefix = "probe:",
	.ops = &probe_ops,
	.default_timeout_ms = 1000,
};

static void probe_destroy(void *ptr, void *aux)
{
	(void)aux;
	iio_context_destroy(ptr);
}

/* a context with the fixture's structure on the probe backend, as an owned root; 0 on failure */
static gw_id probe_open(const char *xml_uri)
{
	static const struct gw_ops ops = { NULL, probe_destroy };
	static const struct iio_context_params params;
	struct iio_context *ctx;
	gw_id id;

	mt_ok("mex_handle_count", 0);
	ctx = iio_create_context_from_xml(&params, xml_uri, &probe_backend, "(probe)", NULL, NULL, 0);
	if (iio_err(ctx))
		return 0;
	id = gw_handle_add(GW_T_CONTEXT, ctx, 0, true, &ops, NULL);
	if (!id)
		iio_context_destroy(ctx);
	return id;
}

/* ---- helpers ---- */
static uint64_t attr_id(uint64_t ctx, const struct iio_attr *a)
{
	gw_id id = 0;

	CHECK(a != NULL);
	CHECK(gw_handle_borrow(GW_T_ATTR, a, ctx, &id) == 0);
	CHECK(id != 0);
	return id;
}

/* the text attr_read_raw returns; "<error>" after a raise or for [] */
static const char *text_of(uint64_t a)
{
	const char *s = mt_str_of("attr_read_raw", 1, mt_u64(a));

	return s ? s : "<error>";
}

/* cmd(a) returns a 1-by-3 double equal to want */
static bool range_is(uint64_t a, double lo, double step, double hi)
{
	mxArray *out[1];
	const double *v;
	bool ok;

	if (mt_call(1, out, "attr_get_range", 1, mt_u64(a)))
		return false;
	ok = mxGetClassID(out[0]) == mxDOUBLE_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 1 &&
		mxGetN(out[0]) == 3;
	v = ok ? mxGetData(out[0]) : NULL;
	ok = ok && v[0] == lo && v[1] == step && v[2] == hi;
	mxDestroyArray(out[0]);
	return ok;
}

/* attr_write_double(a, v [, exact]) and the text the attribute then holds */
static const char *after_double(uint64_t a, double v, int exact)
{
	if (exact < 0)
		mt_ok("attr_write_double", 2, mt_u64(a), mt_dbl(v));
	else
		mt_ok("attr_write_double", 3, mt_u64(a), mt_dbl(v), mt_bool(exact != 0));
	return text_of(a);
}

/* the text with its character k (0-based) replaced by a NUL */
static mxArray *nul_str(const char *text, size_t k)
{
	mxArray *a = mt_str(text);

	((mxChar *)mxGetData(a))[k] = 0;
	return a;
}

static mxArray *sgl(float v)
{
	mxArray *a = mxCreateNumericMatrix(1, 1, mxSINGLE_CLASS, mxREAL);

	memcpy(mxGetData(a), &v, sizeof(v));
	return a;
}

static mxArray *i32(int32_t v)
{
	mxArray *a = mxCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);

	memcpy(mxGetData(a), &v, sizeof(v));
	return a;
}

int main(int argc, char **argv)
{
	static const char *const modes[4] = { "auto", "manual", "tx_quad", "rf_dc_offs" };
	static const size_t growth[6] = { 32768, 131072, 524288, 2097152, 8388608, 33554432 };
	enum { BIG = 47000 };
	struct mt_fixture fx;
	const struct iio_context *c;
	const struct iio_device *phy;
	const struct iio_channel *rx1;
	uint64_t ctx, xctx, pctx, dev_id = 0;
	uint64_t calib, calib_avail, gcm, hw_model, empty, novalue, flag, frac, frac_avail, hex, gain,
		gain_avail, dbl, text, xa, pa;
	mxArray *out[1];
	const char *s;
	char *big;
	double n;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_attr, arities, sizeof(arities) / sizeof(arities[0]));

	ctx = mt_open_context(fx.uri);
	CHECK(ctx != 0);
	c = gw_handle_ptr(ctx, GW_T_CONTEXT);
	phy = iio_context_find_device(c, "emu-phy");
	rx1 = iio_device_find_channel(phy, "voltage0", false);
	calib = attr_id(ctx, iio_device_find_attr(phy, "calib_mode"));
	calib_avail = attr_id(ctx, iio_device_find_attr(phy, "calib_mode_available"));
	gcm = attr_id(ctx, iio_channel_find_attr(rx1, "gain_control_mode"));
	hw_model = attr_id(ctx, iio_context_find_attr(c, "hw_model"));
	empty = attr_id(ctx, iio_device_find_attr(phy, "empty"));
	novalue = attr_id(ctx, iio_device_find_attr(phy, "novalue"));
	flag = attr_id(ctx, iio_device_find_attr(phy, "flag"));
	frac = attr_id(ctx, iio_device_find_attr(phy, "frac"));
	frac_avail = attr_id(ctx, iio_device_find_attr(phy, "frac_available"));
	hex = attr_id(ctx, iio_device_find_attr(phy, "hex"));
	gain = attr_id(ctx, iio_device_find_attr(phy, "gain"));
	gain_avail = attr_id(ctx, iio_device_find_attr(phy, "gain_available"));
	dbl = attr_id(ctx, iio_device_find_attr(phy, "dbl"));
	text = attr_id(ctx, iio_device_find_attr(phy, "text"));
	CHECK(gw_handle_borrow(GW_T_DEVICE, phy, ctx, &dev_id) == 0 && dev_id != 0);

	/* names */
	s = mt_str_of("attr_get_name", 1, mt_u64(calib));
	CHECK(s && !strcmp(s, "calib_mode"));
	s = mt_str_of("attr_get_filename", 1, mt_u64(calib));
	CHECK(s && !strcmp(s, "calib_mode"));
	s = mt_str_of("attr_get_name", 1, mt_u64(gcm));
	CHECK(s && !strcmp(s, "gain_control_mode"));
	s = mt_str_of("attr_get_filename", 1, mt_u64(gcm));
	CHECK(s && !strcmp(s, "in_voltage0_gain_control_mode"));

	/* static value: only context attributes have one */
	s = mt_str_of("attr_get_static_value", 1, mt_u64(hw_model));
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	CHECK(mt_call(1, out, "attr_get_static_value", 1, mt_u64(calib)) == 0);
	CHECK(mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);

	/* reads */
	CHECK(!strcmp(text_of(calib), "auto"));
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(calib), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 5);
	CHECK(mxGetN(out[0]) == 5 && !memcmp(mxGetData(out[0]), "auto", 5));
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(hw_model), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 19);
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "attr_read_raw", 1, mt_u64(empty)) == 0);       /* "" is a 0x0 char */
	CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(empty), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 1);
	mxDestroyArray(out[0]);
	CHECK(mt_raises("iio:ENOENT", "attr_read_raw", 1, mt_u64(novalue)));
	CHECK(mt_raises("iio:ENOENT", "attr_read_raw", 2, mt_u64(novalue), mt_bool(true)));
	CHECK(mt_raises("iio:EINVAL", "attr_read_raw", 2, mt_u64(calib), mt_dbl(3)));
	CHECK(mt_raises("iio:EINVAL", "attr_read_raw", 3, mt_u64(calib), mt_bool(false), mt_bool(false)));

	/* typed reads */
	CHECK(mt_bool_of("attr_read_bool", 1, mt_u64(flag)) == true);
	CHECK(mt_bool_of("attr_read_bool", 1, mt_u64(frac)) == false);    /* text "0.5": libiio's rule */
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(hex)) == 16);     /* text "0x10" */
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(gain)) == 10);
	CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(dbl)) == 3.25);     /* text "3.25 dB" */
	CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(frac)) == 0.5);
	CHECK(mt_raises("iio:EINVAL", "attr_read_longlong", 1, mt_u64(text)));
	CHECK(mt_raises("iio:EINVAL", "attr_read_double", 1, mt_u64(text)));
	CHECK(mt_raises("iio:EINVAL", "attr_read_bool", 1, mt_u64(text)));
	CHECK(mt_raises("iio:ENOENT", "attr_read_longlong", 1, mt_u64(novalue)));
	CHECK(mt_raises("iio:ENOENT", "attr_read_double", 1, mt_u64(novalue)));
	CHECK(mt_raises("iio:ENOENT", "attr_read_bool", 1, mt_u64(novalue)));

	/* string writes */
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(calib), mt_str("manual")) == 7);   /* with the NUL */
	CHECK(!strcmp(text_of(calib), "manual"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(calib), mt_str("bogus")));
	CHECK(!strcmp(text_of(calib), "manual"));
	CHECK(mt_raises("iio:EACCES", "attr_write_string", 2, mt_u64(calib_avail), mt_str("x")));
	CHECK(mt_raises("iio:EPERM", "attr_write_string", 2, mt_u64(hw_model), mt_str("x")));
	CHECK(mt_raises("iio:EPERM", "attr_write_raw", 2, mt_u64(hw_model), mt_bytes("x", 1)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(text), mt_dbl(5)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(text), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(text), mt_bytes("x", 1)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(text), nul_str("ab?cd", 2)));
	CHECK(strstr(mock_err_msg(), "embedded NUL") != NULL);
	CHECK(!strcmp(text_of(text), "hello world"));
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(text), mt_str("")) == 1);
	CHECK(mt_call(1, out, "attr_read_raw", 1, mt_u64(text)) == 0);
	CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);

	/* typed writes */
	mt_ok("attr_write_bool", 2, mt_u64(flag), mt_bool(false));
	CHECK(!strcmp(text_of(flag), "0"));
	mt_ok("attr_write_bool", 2, mt_u64(flag), mt_bool(true));
	CHECK(!strcmp(text_of(flag), "1"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_bool", 2, mt_u64(flag), mt_dbl(1)));
	mt_ok("attr_write_longlong", 2, mt_u64(hex), mt_i64(9007199254740993LL));   /* 2^53 + 1 */
	CHECK(!strcmp(text_of(hex), "9007199254740993"));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(hex)) == 9007199254740993LL);
	mt_ok("attr_write_longlong", 2, mt_u64(hex), mt_i64(INT64_MIN));
	CHECK(!strcmp(text_of(hex), "-9223372036854775808"));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(hex)) == INT64_MIN);
	mt_ok("attr_write_longlong", 2, mt_u64(hex), mt_i64(INT64_MAX));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(hex)) == INT64_MAX);
	mt_ok("attr_write_longlong", 2, mt_u64(hex), mt_u64(2450000000u));
	CHECK(!strcmp(text_of(hex), "2450000000"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_longlong", 2, mt_u64(hex), mt_u64(UINT64_MAX)));
	mt_ok("attr_write_longlong", 2, mt_u64(hex), mt_dbl(17));
	CHECK(!strcmp(text_of(hex), "17"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_longlong", 2, mt_u64(hex), mt_dbl(2.5)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_longlong", 2, mt_u64(hex), mt_str("1")));
	CHECK(!strcmp(text_of(hex), "17"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_longlong", 2, mt_u64(gain), mt_dbl(999)));   /* emu: gain_available */
	CHECK(!strcmp(text_of(gain), "10"));

	/* doubles: exact fixed notation by default, libiio's "%f" on request */
	CHECK(!strcmp(after_double(dbl, 1e-9, -1), "0.000000001"));
	CHECK(!strcmp(after_double(dbl, 0.1, -1), "0.1"));
	CHECK(!strcmp(after_double(dbl, 2.4e9, -1), "2400000000"));
	CHECK(!strcmp(after_double(dbl, 3.141592653589793, -1), "3.141592653589793"));
	CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(dbl)) == 3.141592653589793);
	CHECK(!strcmp(after_double(dbl, -0.5, -1), "-0.5"));
	CHECK(!strcmp(after_double(dbl, 123456789.125, 1), "123456789.125"));
	CHECK(!strcmp(after_double(dbl, 0.0, -1), "0"));
	/* up to 40 decimals in fixed notation; a value that needs more is sent as "%.17g" */
	CHECK(!strcmp(after_double(dbl, 1e-30, -1), "0.000000000000000000000000000001"));
	CHECK(!strcmp(after_double(dbl, 1e-40, -1), "0.0000000000000000000000000000000000000001"));
	CHECK(!strcmp(after_double(dbl, 1.5e-40, -1), "1.5000000000000001e-40"));
	CHECK(!strcmp(after_double(dbl, 2.9999999999999996, -1), "2.9999999999999996"));   /* "%f": 3.000000 */
	CHECK(!strcmp(after_double(dbl, 5e-324, -1), "4.9406564584124654e-324"));
	CHECK(!strcmp(after_double(dbl, 1e-9, 0), "0.000000"));
	CHECK(!strcmp(after_double(dbl, 5.0, 0), "5.000000"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_dbl(NAN)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_dbl(INFINITY)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_dbl(-INFINITY)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 3, mt_u64(dbl), mt_dbl(NAN), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_str("1")));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 3, mt_u64(dbl), mt_dbl(1), mt_dbl(1)));
	/* a floating-point class only: an integer class would be rounded to a double without notice */
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_i64(9007199254740993LL)));
	CHECK(strstr(mock_err_msg(), "attr_write_longlong") != NULL);
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), mt_u64(1)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(dbl), i32(3)));
	CHECK(mt_raises("iio:EINVAL", "attr_write_double", 3, mt_u64(dbl), i32(3), mt_bool(false)));
	CHECK(!strcmp(text_of(dbl), "5.000000"));
	mt_ok("attr_write_double", 2, mt_u64(dbl), sgl(0.5f));
	CHECK(!strcmp(text_of(dbl), "0.5"));
	mt_ok("attr_write_double", 3, mt_u64(dbl), mt_dbl(0.25), mt_empty());    /* [] = absent: exact */
	CHECK(!strcmp(text_of(dbl), "0.25"));

	/* The numeric locale of the process has a decimal comma (MATLAB's may). The texts sent
	 * and the numbers read do not depend on it, and every command leaves the locale of the
	 * thread and of the process as it found them (mt_comma_end checks both). */
	CHECK(uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
	if (mt_comma_begin()) {
		CHECK(!strcmp(after_double(dbl, 0.1, -1), "0.1"));
		CHECK(uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
		CHECK(!strcmp(after_double(dbl, -1234.5, -1), "-1234.5"));
		CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(dbl)) == -1234.5);
		CHECK(!strcmp(after_double(dbl, 1e-9, -1), "0.000000001"));
		CHECK(!strcmp(after_double(dbl, 2.5, 0), "2.500000"));       /* libiio's own "%f" */
		CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(frac)) == 0.5);
		/* libiio parses a range with sscanf("%lf"): without the "C" guard a range with a
		 * fraction is iio:ENOTSUP here */
		CHECK(range_is(frac_avail, 0, 0.25, 1));
		CHECK(range_is(gain_avail, 0, 2, 70));
		CHECK(mt_raises("iio:ENOTSUP", "attr_get_range", 1, mt_u64(calib_avail)));
		/* emu checks a write against the attribute's "_available" range with sscanf too */
		CHECK(!strcmp(after_double(frac, 0.75, -1), "0.75"));
		CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(frac), mt_str("0.25")) == 5);
		CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(frac), mt_bytes("0.5", 4)) == 4);
		CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(frac), mt_str("0.3")));
		CHECK(mt_raises("iio:EINVAL", "attr_write_double", 2, mt_u64(frac), mt_dbl(1.25)));
		CHECK(!strcmp(text_of(frac), "0.5"));
		mt_comma_end();
	}
	CHECK(uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
	CHECK(!strcmp(after_double(dbl, 0.25, -1), "0.25"));

	/* raw writes: the byte count is the numel of the input */
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes("abcdef", 6)) == 6);
	CHECK(!strcmp(text_of(text), "abcdef"));
	big = malloc(BIG);
	for (int i = 0; i < BIG; i++)
		big[i] = (char)('a' + i % 26);
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(big, BIG)) == BIG);
	s = mt_str_of("attr_read_raw", 1, mt_u64(text));              /* the read buffer grew */
	CHECK(s && strlen(s) == BIG && !memcmp(s, big, BIG));
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(text), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == BIG + 1);
	if (mxGetN(out[0]) == BIG + 1) {
		CHECK(!memcmp(mxGetData(out[0]), big, BIG));
		CHECK(((const uint8_t *)mxGetData(out[0]))[BIG] == 0);
	}
	mxDestroyArray(out[0]);
	CHECK(mt_oom_sweep(1, "attr_read_raw", 1, mt_u64(text)) >= 1);
	/* a value of first-buffer-size - 1 characters fills the first buffer exactly: emu reports
	 * that as it reports a truncated value, so the command reads once more and loses nothing */
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(big, 32767)) == 32767);
	s = mt_str_of("attr_read_raw", 1, mt_u64(text));
	CHECK(s && strlen(s) == 32767 && !memcmp(s, big, 32767));
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(big, 32766)) == 32766);
	s = mt_str_of("attr_read_raw", 1, mt_u64(text));
	CHECK(s && strlen(s) == 32766);
	/* NULs inside: emu keeps a C string, but the count proves the length was passed, not strlen */
	big[10] = '\0';
	big[20000] = '\0';
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(big, BIG)) == BIG);
	s = mt_str_of("attr_read_raw", 1, mt_u64(text));
	CHECK(s && strlen(s) == 10 && !memcmp(s, big, 10));
	free(big);
	out[0] = mxCreateDoubleMatrix(1, 3, mxREAL);
	CHECK(mt_raises("iio:EINVAL", "attr_write_raw", 2, mt_u64(text), out[0]));
	CHECK(mt_raises("iio:EINVAL", "attr_write_raw", 2, mt_u64(text), mt_str("abc")));
	CHECK(mt_raises("iio:EINVAL", "attr_write_raw", 2, mt_u64(text),
		mxCreateNumericMatrix(2, 2, mxUINT8_CLASS, mxREAL)));
	/* an empty payload: a valid pointer and length 0 reach libiio; emu stores "" and returns 0 */
	n = mt_dbl_of("attr_write_raw", 2, mt_u64(text), mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL));
	CHECK(n == 0);
	n = mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(NULL, 0));
	CHECK(n == 0);

	/* available lists and ranges */
	CHECK(mt_oom_sweep(1, "attr_get_available", 1, mt_u64(calib_avail)) >= 1);   /* parked: no leak */
	CHECK(mt_call(1, out, "attr_get_available", 1, mt_u64(calib_avail)) == 0);
	CHECK(mxIsCell(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 4);
	for (size_t i = 0; i < 4 && mxIsCell(out[0]) && mxGetN(out[0]) == 4; i++) {
		const mxArray *e = mxGetCell(out[0], i);
		char *t = e && mxIsChar(e) ? mxArrayToUTF8String(e) : NULL;

		CHECK(t && !strcmp(t, modes[i]));
		mxFree(t);
	}
	mxDestroyArray(out[0]);
	CHECK(range_is(gain_avail, 0, 2, 70));
	CHECK(range_is(frac_avail, 0, 0.25, 1));
	CHECK(mt_raises("iio:ENOTSUP", "attr_get_range", 1, mt_u64(calib_avail)));
	CHECK(mt_raises("iio:ENOTSUP", "attr_get_available", 1, mt_u64(gain_avail)));
	CHECK(mt_raises("iio:ENXIO", "attr_get_available", 1, mt_u64(calib)));
	CHECK(mt_raises("iio:ENXIO", "attr_get_range", 1, mt_u64(calib)));

	/* a handle of another type, a stale one, none */
	CHECK(mt_raises("iio:EBADF", "attr_get_name", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_write_string", 2, mt_u64(ctx), mt_str("x")));
	CHECK(mt_raises("iio:EBADF", "attr_write_raw", 2, mt_u64(0), mt_bytes("x", 1)));
	CHECK(mt_raises("iio:EBADF", "attr_read_bool", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_read_longlong", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_read_double", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_write_bool", 2, mt_u64(dev_id), mt_bool(true)));
	CHECK(mt_raises("iio:EBADF", "attr_write_longlong", 2, mt_u64(dev_id), mt_dbl(1)));
	CHECK(mt_raises("iio:EBADF", "attr_write_double", 2, mt_u64(dev_id), mt_dbl(1)));
	CHECK(mt_raises("iio:EBADF", "attr_get_range", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_get_available", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_get_filename", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EBADF", "attr_get_static_value", 1, mt_u64(dev_id)));
	CHECK(mt_raises("iio:EINVAL", "attr_get_name", 1, mt_dbl((double)calib)));
	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "attr_get_name", 1, mt_u64(calib)));
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(calib)));

	/* the xml: backend has the structure and no I/O */
	xctx = mt_open_context(fx.xml_uri);
	CHECK(xctx != 0);
	c = gw_handle_ptr(xctx, GW_T_CONTEXT);
	xa = attr_id(xctx, iio_device_find_attr(iio_context_find_device(c, "emu-phy"), "calib_mode"));
	CHECK(mt_raises("iio:ENOSYS", "attr_read_raw", 1, mt_u64(xa)));
	CHECK(mt_raises("iio:ENOSYS", "attr_write_string", 2, mt_u64(xa), mt_str("auto")));
	s = mt_str_of("attr_get_name", 1, mt_u64(xa));
	CHECK(s && !strcmp(s, "calib_mode"));
	gw_handle_destroy(xctx);

	/* the read buffer against a backend that says "too small" in each of libiio's ways */
	pctx = probe_open(fx.xml_uri);
	CHECK(pctx != 0);
	c = gw_handle_ptr(pctx, GW_T_CONTEXT);
	pa = attr_id(pctx, iio_device_find_attr(iio_context_find_device(c, "emu-phy"), "text"));
	probe_mode = PROBE_EFBIG_THEN_FITS;
	probe_calls = 0;
	s = mt_str_of("attr_read_raw", 1, mt_u64(pa));
	CHECK(s && strlen(s) == PROBE_VALUE_LEN && s[0] == 'x' && s[PROBE_VALUE_LEN - 1] == 'x');
	CHECK(probe_calls == 2 && probe_lens[0] == growth[0] && probe_lens[1] == growth[1]);
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(pa), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetN(out[0]) == PROBE_VALUE_LEN + 1);
	mxDestroyArray(out[0]);
	CHECK(mt_oom_sweep(1, "attr_read_raw", 1, mt_u64(pa)) >= 2);     /* both buffers, then the result */
	/* a value that fits no buffer is iio:EFBIG, however the backend says so; the buffer grows
	 * by four from 32 KiB and the first size of 16 MiB or more is the last one tried */
	probe_mode = PROBE_ALWAYS_EFBIG;
	probe_calls = 0;
	CHECK(mt_raises("iio:EFBIG", "attr_read_raw", 1, mt_u64(pa)));
	CHECK(probe_calls == 6 && !memcmp(probe_lens, growth, sizeof(growth)));
	probe_mode = PROBE_ALWAYS_FULL;
	probe_calls = 0;
	CHECK(mt_raises("iio:EFBIG", "attr_read_raw", 1, mt_u64(pa)));
	CHECK(probe_calls == 6 && !memcmp(probe_lens, growth, sizeof(growth)));
	probe_calls = 0;
	CHECK(mt_raises("iio:EFBIG", "attr_read_raw", 2, mt_u64(pa), mt_bool(true)));
	CHECK(probe_calls == 6);
	/* -EIO is an error and is not retried (a legacy iiod reports a too-small buffer this way:
	 * there, a value of 32767 bytes or more cannot be read) */
	probe_mode = PROBE_EIO;
	probe_calls = 0;
	CHECK(mt_raises("iio:EIO", "attr_read_raw", 1, mt_u64(pa)));
	CHECK(probe_calls == 1 && probe_lens[0] == growth[0]);
	CHECK(mt_raises("iio:EIO", "attr_read_longlong", 1, mt_u64(pa)));
	/* a read that returns fewer bytes than the buffer holds and no NUL: the text ends at the
	 * count, wherever the backend left the rest of the buffer */
	probe_mode = PROBE_NO_NUL_SHORT;
	s = mt_str_of("attr_read_raw", 1, mt_u64(pa));
	CHECK(s && !strcmp(s, "zzzzz"));
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(pa), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 5);
	mxDestroyArray(out[0]);
	probe_mode = PROBE_NO_NUL_ALMOST;
	probe_calls = 0;
	s = mt_str_of("attr_read_raw", 1, mt_u64(pa));
	CHECK(s && strlen(s) == 32767 && s[0] == 'z' && s[32766] == 'z');
	CHECK(probe_calls == 1);
	probe_mode = PROBE_NO_NUL_ZERO;
	CHECK(mt_call(1, out, "attr_read_raw", 1, mt_u64(pa)) == 0);
	CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(pa), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);
	/* a negative value outside the errno range is an I/O error, not "success without a result" */
	probe_mode = PROBE_HUGE_NEGATIVE;
	CHECK(mt_raises("iio:EIO", "attr_read_raw", 1, mt_u64(pa)));
	CHECK(mt_raises("iio:EIO", "attr_read_raw", 2, mt_u64(pa), mt_bool(true)));

	/* writes: the bytes and the count the backend saw */
	probe_writes = 0;
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(pa), mt_bytes("ab\0def", 6)) == 6);
	CHECK(probe_writes == 1 && probe_written_len == 6 && !memcmp(probe_written, "ab\0def", 6));
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(pa), mt_str("xyz")) == 4);
	CHECK(probe_writes == 2 && probe_written_len == 4 && !memcmp(probe_written, "xyz", 4));
	mt_ok("attr_write_double", 2, mt_u64(pa), mt_dbl(0.1));
	CHECK(probe_writes == 3 && probe_written_len == 4 && !memcmp(probe_written, "0.1", 4));
	mt_ok("attr_write_longlong", 2, mt_u64(pa), mt_i64(-7));
	CHECK(probe_writes == 4 && !strcmp(probe_written, "-7"));
	mt_ok("attr_write_bool", 2, mt_u64(pa), mt_bool(true));
	CHECK(probe_writes == 5 && !strcmp(probe_written, "1"));
	/* the count is the backend's, not the size of the input */
	probe_write_ret = 2;
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(pa), mt_bytes("abcdef", 6)) == 2);
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(pa), mt_str("abcdef")) == 2);
	/* a write the backend refuses is an error of every write command */
	probe_write_ret = -EBUSY;
	probe_writes = 0;
	CHECK(mt_raises("iio:EBUSY", "attr_write_raw", 2, mt_u64(pa), mt_bytes("x", 1)));
	CHECK(mt_raises("iio:EBUSY", "attr_write_string", 2, mt_u64(pa), mt_str("x")));
	CHECK(mt_raises("iio:EBUSY", "attr_write_bool", 2, mt_u64(pa), mt_bool(true)));
	CHECK(mt_raises("iio:EBUSY", "attr_write_longlong", 2, mt_u64(pa), mt_dbl(1)));
	CHECK(mt_raises("iio:EBUSY", "attr_write_double", 2, mt_u64(pa), mt_dbl(1)));
	CHECK(mt_raises("iio:EBUSY", "attr_write_double", 3, mt_u64(pa), mt_dbl(1), mt_bool(false)));
	CHECK(probe_writes == 6);
	probe_write_ret = PROBE_HUGE;
	CHECK(mt_raises("iio:EIO", "attr_write_raw", 2, mt_u64(pa), mt_bytes("x", 1)));
	CHECK(mt_raises("iio:EIO", "attr_write_string", 2, mt_u64(pa), mt_str("x")));
	CHECK(mt_raises("iio:EIO", "attr_write_double", 2, mt_u64(pa), mt_dbl(1)));
	/* result first: when the allocation of the returned count fails, nothing was written yet,
	 * so the one write of the sweep is the one of the attempt that succeeded */
	probe_write_ret = PROBE_WRITE_ALL;
	probe_writes = 0;
	CHECK(mt_oom_sweep(1, "attr_write_raw", 2, mt_u64(pa), mt_bytes("abc", 3)) >= 1);
	CHECK(probe_writes == 1);
	probe_writes = 0;
	CHECK(mt_oom_sweep(1, "attr_write_string", 2, mt_u64(pa), mt_str("abc")) >= 2);
	CHECK(probe_writes == 1);
	gw_handle_destroy(pctx);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
