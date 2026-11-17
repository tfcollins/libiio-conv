/* SPDX-License-Identifier: MIT */
/* Command group "toplevel": error text, backend queries, context scan (empty, the default
 * list, results added by hand), argument errors, allocation failures, numeric locale. */
#include "mex_test.h"
#include <limits.h>
#include <stdarg.h>
MT_GROUPS(&gw_group_toplevel);

/* libiio's hook for backends (iio/iio-backend.h): the only way to a scan with known results */
int iio_scan_add_result(struct iio_scan *ctx, const char *desc, const char *uri);

/* the table "Group toplevel" of the Command reference */
static const struct mt_arity arities[] = {
	{ "strerror", 1, 1, 1 },
	{ "has_backend", 2, 2, 1 },
	{ "get_builtin_backends", 0, 0, 1 },
	{ "scan", 2, 2, 1 },
	{ "scan_destroy", 1, 1, 0 },
	{ "scan_get_results", 1, 1, 2 },
};

/* scalar struct with n fields: n pairs (const char *name, double value) */
static mxArray *params(int n, ...)
{
	const char *names[8];
	double values[8];
	mxArray *s;
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		names[i] = va_arg(ap, const char *);
		values[i] = va_arg(ap, double);
	}
	va_end(ap);
	s = mxCreateStructMatrix(1, 1, n, names);
	for (int i = 0; i < n; i++)
		mxSetFieldByNumber(s, 0, i, mxCreateDoubleScalar(values[i]));
	return s;
}

/* the text with its character k (0-based) replaced by a NUL */
static mxArray *nul_str(const char *text, size_t k)
{
	mxArray *a = mt_str(text);

	((mxChar *)mxGetData(a))[k] = 0;
	return a;
}

/* a is a 1-by-n cell whose elements are all char rows (an empty text is the 0x0 char '') */
static bool is_text_list(const mxArray *a, size_t n)
{
	if (!a || !mxIsCell(a) || mxGetM(a) != 1 || mxGetN(a) != n)
		return false;
	for (size_t i = 0; i < n; i++) {
		const mxArray *e = mxGetCell(a, i);

		if (!e || !mxIsChar(e) || (mxGetNumberOfElements(e) != 0 && mxGetM(e) != 1))
			return false;
	}
	return true;
}

/* a is a 1-by-n cell of char rows equal to want[0 .. n-1] */
static bool list_is(const mxArray *a, const char *const *want, size_t n)
{
	bool ok = is_text_list(a, n);

	for (size_t i = 0; ok && i < n; i++) {
		char *s = mxArrayToUTF8String(mxGetCell(a, i));

		ok = s && !strcmp(s, want[i]);
		mxFree(s);
	}
	return ok;
}

/* both results of scan_get_results are 1-by-n cells of char rows; *n is their length */
static bool results_ok(uint64_t scan, size_t *n)
{
	mxArray *out[2];
	bool ok;

	if (mt_call(2, out, "scan_get_results", 1, mt_u64(scan)))
		return false;
	*n = mxGetN(out[0]);
	ok = is_text_list(out[0], *n) && is_text_list(out[1], *n);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	return ok;
}

int main(int argc, char **argv)
{
	static const char *const backends[4] = { "local", "ip", "emu", "xml" };
	static const char *const uris[3] = { "ip:192.168.2.1", "usb:1.2.3", "" };
	static const char *const descs[3] = { "first, with a comma", "Gr\xc3\xb6\xc3\x9f" "e (non-ASCII)", "" };
	struct mt_fixture fx;
	struct iio_scan *sp;
	mxArray *out[2];
	uint64_t s, ctx;
	const char *t;
	size_t live, n;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_toplevel, arities, sizeof(arities) / sizeof(arities[0]));

	/* strerror */
	t = mt_str_of("strerror", 1, mt_dbl(-22));
	CHECK(t && !strcmp(t, "Invalid argument (-22)"));
	t = mt_str_of("strerror", 1, mt_dbl(22));
	CHECK(t && !strcmp(t, "Invalid argument (22)"));
	t = mt_str_of("strerror", 1, mt_dbl(0));
	CHECK(t && !strcmp(t, "Success (0)"));
	t = mt_str_of("strerror", 1, mt_dbl(-99999));
	CHECK(t && !strcmp(t, "Unknown error -99999"));
	t = mt_str_of("strerror", 1, mt_i64(-110));
	CHECK(t && !strcmp(t, "Connection timed out (-110)"));
	t = mt_str_of("strerror", 1, mt_dbl(INT_MAX));
	CHECK(t && !strcmp(t, "Unknown error 2147483647"));
	t = mt_str_of("strerror", 1, mt_dbl(-INT_MAX));
	CHECK(t && !strcmp(t, "Unknown error -2147483647"));
	CHECK(mt_raises("iio:EINVAL", "strerror", 1, mt_str("x")));
	CHECK(mt_raises("iio:EINVAL", "strerror", 1, mt_dbl(-22.5)));
	CHECK(mt_raises("iio:EINVAL", "strerror", 1, mt_dbl(2147483648.0)));
	/* INT_MIN cannot be negated: libiio would compute -err */
	CHECK(mt_raises("iio:EINVAL", "strerror", 1, mt_dbl(-2147483648.0)));
	CHECK(mt_raises("iio:EINVAL", "strerror", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "strerror", 0));
	CHECK(mt_oom_sweep(1, "strerror", 1, mt_dbl(-22)) >= 1);

	/* has_backend */
	CHECK(mt_bool_of("has_backend", 2, mt_empty(), mt_str("emu")));
	CHECK(mt_bool_of("has_backend", 2, mt_empty(), mt_str("xml")));
	CHECK(mt_bool_of("has_backend", 2, mt_empty(), mt_str("local")));
	CHECK(mt_bool_of("has_backend", 2, mt_empty(), mt_str("ip")));
	CHECK(!mt_bool_of("has_backend", 2, mt_empty(), mt_str("usb")));
	CHECK(!mt_bool_of("has_backend", 2, mt_empty(), mt_str("bogus")));
	CHECK(!mt_bool_of("has_backend", 2, mt_empty(), mt_str("")));
	CHECK(!mt_bool_of("has_backend", 2, mt_empty(), mt_str("emu:")));
	CHECK(mt_bool_of("has_backend", 2, params(1, "log_level", 1.0), mt_str("emu")));
	CHECK(!mt_bool_of("has_backend", 2, params(1, "log_level", 1.0), mt_str("usb")));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_empty(), mt_dbl(3)));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_empty(), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_empty(), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, params(1, "bogus", 1.0), mt_str("emu")));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_dbl(1), mt_str("emu")));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 1, mt_str("emu")));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_empty(), nul_str("emu", 1)));
	CHECK(mt_raises("iio:EINVAL", "has_backend", 2, mt_empty(), nul_str("emu?", 3)));
	CHECK(mt_oom_sweep(1, "has_backend", 2, params(1, "log_level", 1.0), mt_str("emu")) >= 1);

	/* get_builtin_backends: list form, libiio order */
	CHECK(mt_call(1, out, "get_builtin_backends", 0) == 0);
	CHECK(list_is(out[0], backends, 4));
	mxDestroyArray(out[0]);
	CHECK(mt_raises("iio:EINVAL", "get_builtin_backends", 1, mt_dbl(0)));
	CHECK(mt_oom_sweep(1, "get_builtin_backends", 0) >= 1);
	CHECK(gw_handle_count() == 0 && !mock_is_locked());

	/* scan of a backend that cannot scan: an owned root with no result */
	s = mt_u64_of("scan", 2, params(1, "log_level", 1.0), mt_str("emu"));
	CHECK(s != 0);
	CHECK(gw_handle_type(s) == GW_T_SCAN && gw_handle_owned(s) && gw_handle_parent(s) == 0);
	CHECK(gw_handle_count() == 1 && mock_is_locked());
	CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
	CHECK(is_text_list(out[0], 0) && is_text_list(out[1], 0));
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	live = mock_live();
	CHECK(mt_call(1, out, "scan_get_results", 1, mt_u64(s)) == 0);
	CHECK(is_text_list(out[0], 0));
	CHECK(mock_live() == live + 1);
	mxDestroyArray(out[0]);
	CHECK(mock_live() == live);
	mt_ok("scan_get_results", 1, mt_u64(s));                  /* no output requested */
	CHECK(mock_live() == live);
	CHECK(mt_raises("iio:EINVAL", "scan_get_results", 1, mt_dbl((double)s)));
	CHECK(mt_raises("iio:EBADF", "scan_get_results", 1, mt_u64(0)));
	mt_ok("scan_destroy", 1, mt_u64(s));
	CHECK(gw_handle_count() == 0 && !mock_is_locked());
	mt_ok("scan_destroy", 1, mt_u64(s));
	mt_ok("scan_destroy", 1, mt_u64(0));
	CHECK(mt_raises("iio:EBADF", "scan_get_results", 1, mt_u64(s)));
	CHECK(mt_raises("iio:EINVAL", "scan_destroy", 1, mt_dbl(1)));

	/* results: libiio fills them only from real hardware or the network, so the test adds
	 * three through libiio's backend hook, the last with empty texts */
	s = mt_u64_of("scan", 2, params(1, "log_level", 1.0), mt_str("emu"));
	sp = gw_handle_ptr(s, GW_T_SCAN);
	CHECK(sp != NULL);
	if (sp) {
		for (int i = 0; i < 3; i++)
			CHECK(iio_scan_add_result(sp, descs[i], uris[i]) == 0);
		CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
		CHECK(list_is(out[0], uris, 3));            /* output 0: the URIs */
		CHECK(list_is(out[1], descs, 3));           /* output 1: the descriptions */
		CHECK(mxGetM(mxGetCell(out[0], 2)) == 0 && mxGetN(mxGetCell(out[0], 2)) == 0);
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
		CHECK(mt_call(1, out, "scan_get_results", 1, mt_u64(s)) == 0);
		CHECK(list_is(out[0], uris, 3));
		mxDestroyArray(out[0]);
		CHECK(mt_oom_sweep(2, "scan_get_results", 1, mt_u64(s)) >= 8);
		CHECK(mt_oom_sweep(1, "scan_get_results", 1, mt_u64(s)) >= 4);
	}
	CHECK(gw_handle_count() == 1);
	mt_ok("scan_destroy", 1, mt_u64(s));
	CHECK(gw_handle_count() == 0);

	/* libiio's default list ("local" in the pinned build): the number of results depends on
	 * the machine */
	s = mt_u64_of("scan", 2, params(1, "log_level", 1.0), mt_empty());
	CHECK(s != 0 && gw_handle_type(s) == GW_T_SCAN);
	CHECK(results_ok(s, &n));
	mt_ok("scan_destroy", 1, mt_u64(s));
	/* no parameters at all; '' is a value (an empty list), not "absent"; unknown names and
	 * backend arguments are skipped by libiio */
	s = mt_u64_of("scan", 2, mt_empty(), mt_str(""));
	CHECK(s != 0 && results_ok(s, &n) && n == 0);
	mt_ok("scan_destroy", 1, mt_u64(s));
	s = mt_u64_of("scan", 2, params(2, "log_level", 1.0, "timeout_ms", 100.0), mt_str("bogus,emu=x,xml"));
	CHECK(s != 0 && results_ok(s, &n) && n == 0);
	mt_ok("scan_destroy", 1, mt_u64(s));
	CHECK(gw_handle_count() == 0 && !mock_is_locked());

	/* argument errors of scan: nothing is registered */
	CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_empty(), mt_dbl(3)));
	CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_empty(), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "scan", 2, params(1, "bogus", 1.0), mt_str("emu")));
	CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_str("emu"), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "scan", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_empty(), nul_str("emu", 1)));
	CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_empty(), nul_str("emu?", 3)));
	CHECK(gw_handle_count() == 0 && !mock_is_locked());

	/* a live id of another type is no scan */
	ctx = mt_open_context(fx.uri);
	CHECK(ctx != 0);
	CHECK(mt_raises("iio:EBADF", "scan_destroy", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "scan_get_results", 1, mt_u64(ctx)));
	CHECK(gw_handle_type(ctx) == GW_T_CONTEXT && gw_handle_count() == 1);
	s = mt_u64_of("scan", 2, params(1, "log_level", 1.0), mt_str("emu"));
	CHECK(s != 0 && s != ctx && gw_handle_count() == 2);
	mt_ok("scan_destroy", 1, mt_u64(s));
	CHECK(gw_handle_type(ctx) == GW_T_CONTEXT && gw_handle_count() == 1);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);

	/* The numeric locale of the process has a decimal comma: iio_scan runs under the gateway's
	 * "C" guard. No scan result depends on the locale on this machine; the block proves that
	 * the command restores the locale (mt_comma_end fails otherwise). */
	if (mt_comma_begin()) {
		s = mt_u64_of("scan", 2, params(1, "log_level", 1.0), mt_str("emu"));
		CHECK(s != 0);
		CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
		CHECK(is_text_list(out[0], 0) && is_text_list(out[1], 0));
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
		mt_ok("scan_destroy", 1, mt_u64(s));
		/* an argument error after the guard was never entered, and one more scan */
		CHECK(mt_raises("iio:EINVAL", "scan", 2, mt_empty(), mt_dbl(3)));
		s = mt_u64_of("scan", 2, mt_empty(), mt_str(""));
		CHECK(s != 0);
		mt_ok("scan_destroy", 1, mt_u64(s));
		t = mt_str_of("strerror", 1, mt_dbl(-22));
		CHECK(t && !strcmp(t, "Invalid argument (-22)"));
		mt_comma_end();
	}
	CHECK(gw_handle_count() == 0);

	/* allocation failures: the result of scan exists before the scan is made, so a failed
	 * attempt creates nothing and the last one leaves exactly one registered scan */
	CHECK(mt_oom_sweep(1, "scan", 2, params(1, "log_level", 1.0), mt_str("emu")) >= 2);
	CHECK(gw_handle_count() == 1 && mock_is_locked());
	CHECK(mt_oom_sweep(1, "scan", 2, mt_empty(), mt_str("")) >= 2);
	CHECK(gw_handle_count() == 2);
	CHECK(mt_oom_sweep(0, "scan", 2, params(1, "log_level", 1.0), mt_empty()) >= 1);
	CHECK(gw_handle_count() == 3);
	mt_ok("mex_shutdown", 0);
	CHECK(gw_handle_count() == 0 && !mock_is_locked());

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
