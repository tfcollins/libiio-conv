/* SPDX-License-Identifier: MIT */
/* Command group "context": creation and its errors, identity, attribute and device lists,
 * parameters and timeout (one context's own), XML, ping, allocation failures, teardown. */
#include "mex_test.h"
#include <stdarg.h>
MT_GROUPS(&gw_group_context);

/* the table "Group context" of the Command reference */
static const struct mt_arity arities[] = {
	{ "create_context", 2, 2, 1 },
	{ "context_destroy", 1, 1, 0 },
	{ "context_get_version_major", 1, 1, 1 },
	{ "context_get_version_minor", 1, 1, 1 },
	{ "context_get_version_patch", 1, 1, 1 },
	{ "context_get_version_tag", 1, 1, 1 },
	{ "context_get_xml", 1, 1, 1 },
	{ "context_get_name", 1, 1, 1 },
	{ "context_get_description", 1, 1, 1 },
	{ "context_get_attrs", 1, 1, 1 },
	{ "context_find_attr", 2, 2, 1 },
	{ "context_get_devices", 1, 1, 1 },
	{ "context_find_device", 2, 2, 1 },
	{ "context_set_timeout", 2, 2, 0 },
	{ "context_get_params", 1, 1, 1 },
	{ "context_ping", 1, 1, 0 },
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

/* field of the struct context_get_params returns; NaN when it is missing or not a double scalar */
static double param_of(uint64_t ctx, const char *field)
{
	mxArray *out[1];
	const mxArray *f;
	double v = 0.0 / 0.0;

	if (mt_call(1, out, "context_get_params", 1, mt_u64(ctx)))
		return v;
	if (mxIsStruct(out[0]) && mxGetNumberOfElements(out[0]) == 1 && mxGetNumberOfFields(out[0]) == 5) {
		f = mxGetField(out[0], 0, field);
		if (f && mxGetClassID(f) == mxDOUBLE_CLASS && mxIsScalar(f) && !mxIsComplex(f))
			v = mxGetScalar(f);
	}
	mxDestroyArray(out[0]);
	return v;
}

/* a 1-by-n uint64 list result copied to ids; false after a failure */
static bool ids_of(const char *cmd, uint64_t arg, uint64_t *ids, size_t n)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, cmd, 1, mt_u64(arg)))
		return false;
	ok = mxGetClassID(out[0]) == mxUINT64_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 1 &&
		mxGetN(out[0]) == n;
	if (ok && n)
		memcpy(ids, mxGetData(out[0]), n * sizeof(*ids));
	mxDestroyArray(out[0]);
	return ok;
}

/* the XML of a context as a malloc string, NULL after a failure */
static char *xml_of(uint64_t ctx)
{
	mxArray *out[1];
	char *s, *copy = NULL;

	if (mt_call(1, out, "context_get_xml", 1, mt_u64(ctx)))
		return NULL;
	if (mxIsChar(out[0]) && mxGetM(out[0]) == 1) {
		s = mxArrayToUTF8String(out[0]);
		copy = strdup(s);
		mxFree(s);
	}
	mxDestroyArray(out[0]);
	return copy;
}

int main(int argc, char **argv)
{
	static const char *const attr_names[4] = { "escaped", "fixture", "hw_model", "num" };
	static const char *const dev_ids[8] = { "hwmon0", "iio:device0", "iio:device1", "iio:device2",
		"iio:device3", "iio:device4", "iio_sysfs_trigger", "trigger0" };
	static const char xml_head[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?><!DOCTYPE context [";
	struct mt_fixture fx;
	uint64_t ctx, ctx2, xctx, scan, attrs[4], again[4], devs[8];
	const char *s;
	char *xml;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_context, arities, sizeof(arities) / sizeof(arities[0]));

	/* errors of create_context: nothing is registered */
	CHECK(mt_raises("iio:ENOSYS", "create_context", 2, mt_empty(), mt_str("bogus:foo")));
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, mt_empty(), mt_str("emu:/nonexistent.xml")));
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, params(1, "timeout_ms", -5.0), mt_str(fx.uri)));
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, params(1, "bogus", 1.0), mt_str(fx.uri)));
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());

	/* the uri [] is C NULL: libiio then takes $IIOD_REMOTE (else "local:", which depends on the
	 * machine and is not tested) */
	CHECK(setenv("IIOD_REMOTE", fx.uri, 1) == 0);
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_empty());
	CHECK(ctx != 0 && gw_handle_type(ctx) == GW_T_CONTEXT);
	s = mt_str_of("context_get_name", 1, mt_u64(ctx));
	CHECK(s && !strcmp(s, "emu"));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(setenv("IIOD_REMOTE", "bogus:foo", 1) == 0);
	CHECK(mt_raises("iio:ENOSYS", "create_context", 2, mt_empty(), mt_empty()));
	CHECK(unsetenv("IIOD_REMOTE") == 0);
	CHECK(gw_handle_count() == 0);
	/* '' is a value, not "absent": libiio gets "" and knows no such backend */
	CHECK(mt_raises("iio:ENOSYS", "create_context", 2, mt_empty(), mt_str("")));

	/* identity */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx != 0);
	s = mt_str_of("context_get_name", 1, mt_u64(ctx));
	CHECK(s && !strcmp(s, "emu"));
	s = mt_str_of("context_get_description", 1, mt_u64(ctx));
	CHECK(s && !strcmp(s, "Emulated fixture & test context"));
	CHECK(mt_dbl_of("context_get_version_major", 1, mt_u64(ctx)) == 1);
	CHECK(mt_dbl_of("context_get_version_minor", 1, mt_u64(ctx)) == 2);
	CHECK(mt_dbl_of("context_get_version_patch", 1, mt_u64(ctx)) == 3);
	s = mt_str_of("context_get_version_tag", 1, mt_u64(ctx));
	CHECK(s && !strcmp(s, "v1.2.3-fixture"));
	/* id 0: the library itself */
	CHECK(mt_dbl_of("context_get_version_major", 1, mt_u64(0)) == 1);
	CHECK(mt_dbl_of("context_get_version_minor", 1, mt_u64(0)) == iio_context_get_version_minor(NULL));
	CHECK(mt_dbl_of("context_get_version_patch", 1, mt_u64(0)) == iio_context_get_version_patch(NULL));
	s = mt_str_of("context_get_version_tag", 1, mt_u64(0));
	CHECK(s && s[0] && strcmp(s, "v1.2.3-fixture"));
	CHECK(s && !strcmp(s, iio_context_get_version_tag(NULL)));
	CHECK(mt_raises("iio:EBADF", "context_get_name", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "context_get_description", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "context_get_name", 1, mt_dbl((double)ctx)));
	CHECK(mt_raises("iio:EINVAL", "context_get_version_major", 1, mt_str("x")));
	CHECK(gw_handle_count() == 1);

	/* attributes: one borrowed id per attribute, libiio order */
	CHECK(ids_of("context_get_attrs", ctx, attrs, 4));
	for (int i = 0; i < 4; i++) {
		const struct iio_attr *a = gw_handle_ptr(attrs[i], GW_T_ATTR);

		CHECK(a && !strcmp(iio_attr_get_name(a), attr_names[i]));
		CHECK(gw_handle_parent(attrs[i]) == ctx && !gw_handle_owned(attrs[i]));
	}
	CHECK(ids_of("context_get_attrs", ctx, again, 4));
	CHECK(!memcmp(attrs, again, sizeof(attrs)));
	CHECK(mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("num")) == attrs[3]);
	CHECK(gw_handle_count() == 5);

	/* devices */
	CHECK(ids_of("context_get_devices", ctx, devs, 8));
	for (int i = 0; i < 8; i++) {
		const struct iio_device *d = gw_handle_ptr(devs[i], GW_T_DEVICE);

		CHECK(d && !strcmp(iio_device_get_id(d), dev_ids[i]));
		CHECK(gw_handle_parent(devs[i]) == ctx && !gw_handle_owned(devs[i]));
	}
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("iio:device2")) == devs[3]);
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("emu-phy")) == devs[3]);
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("phy")) == devs[3]);
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("nope")) == 0);
	CHECK(mt_raises("iio:EINVAL", "context_find_device", 2, mt_u64(ctx), mt_dbl(2)));
	CHECK(mt_raises("iio:EINVAL", "context_find_device", 2, mt_u64(ctx), mock_string_object()));
	CHECK(mt_raises("iio:EBADF", "context_find_device", 2, mt_u64(devs[3]), mt_str("phy")));
	CHECK(mt_raises("iio:EBADF", "context_get_devices", 1, mt_u64(attrs[0])));
	CHECK(mt_raises("iio:EBADF", "context_get_attrs", 1, mt_u64(devs[0])));
	CHECK(gw_handle_count() == 13);

	/* parameters */
	CHECK(param_of(ctx, "log_level") == 4);
	CHECK(param_of(ctx, "stderr_level") == 3);
	CHECK(param_of(ctx, "timestamp_level") == 5);
	CHECK(param_of(ctx, "timeout_ms") == 1000);
	CHECK(param_of(ctx, "flags") == 0);

	/* timeout */
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(2500));
	CHECK(param_of(ctx, "timeout_ms") == 2500);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(0));              /* the backend's default */
	CHECK(param_of(ctx, "timeout_ms") == 1000);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(-1));             /* no timeout */
	CHECK(param_of(ctx, "timeout_ms") == -1);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(-2147483648.0));  /* non-blocking */
	CHECK(param_of(ctx, "timeout_ms") == 0);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_i64(2500));
	CHECK(param_of(ctx, "timeout_ms") == 2500);
	CHECK(mt_raises("iio:EINVAL", "context_set_timeout", 2, mt_u64(ctx), mt_dbl(-5)));
	CHECK(param_of(ctx, "timeout_ms") == 2500);
	CHECK(mt_raises("iio:EINVAL", "context_set_timeout", 2, mt_u64(ctx), mt_dbl(2.5)));
	CHECK(mt_raises("iio:EINVAL", "context_set_timeout", 2, mt_u64(ctx), mt_dbl(2147483648.0)));
	CHECK(mt_raises("iio:EINVAL", "context_set_timeout", 2, mt_u64(ctx), mt_str("1")));
	CHECK(mt_raises("iio:EBADF", "context_set_timeout", 2, mt_u64(devs[0]), mt_dbl(1)));
	CHECK(param_of(ctx, "timeout_ms") == 2500);

	/* the timeout belongs to one context */
	ctx2 = mt_u64_of("create_context", 2,
		params(3, "log_level", 1.0, "timeout_ms", 123.0, "flags", 1.0), mt_str(fx.uri));
	CHECK(ctx2 != 0 && ctx2 != ctx);
	CHECK(param_of(ctx2, "log_level") == 1);
	CHECK(param_of(ctx2, "stderr_level") == 3);
	CHECK(param_of(ctx2, "timestamp_level") == 5);
	CHECK(param_of(ctx2, "timeout_ms") == 123);
	CHECK(param_of(ctx2, "flags") == 1);
	CHECK(param_of(ctx, "timeout_ms") == 2500);
	mt_ok("context_set_timeout", 2, mt_u64(ctx2), mt_dbl(77));
	CHECK(param_of(ctx2, "timeout_ms") == 77);
	CHECK(param_of(ctx, "timeout_ms") == 2500);

	/* XML: attribute values only with the flag IIO_CTX_XML_INCLUDE_VALUES */
	xml = xml_of(ctx);
	CHECK(xml != NULL);
	if (xml) {
		CHECK(!strncmp(xml, xml_head, sizeof(xml_head) - 1));
		CHECK(strstr(xml, "<context name=\"emu\"") != NULL);
		CHECK(strstr(xml, "emu-phy") != NULL);
		CHECK(strlen(xml) > 9000);
		CHECK(strstr(xml, "value=\"auto\"") == NULL);
	}
	free(xml);
	xml = xml_of(ctx2);
	CHECK(xml && strstr(xml, "value=\"auto\"") != NULL);
	free(xml);
	CHECK(mt_raises("iio:EBADF", "context_get_xml", 1, mt_u64(devs[0])));
	CHECK(mt_raises("iio:EBADF", "context_get_xml", 1, mt_u64(0)));

	/* ping; the xml: backend has none */
	mt_ok("context_ping", 1, mt_u64(ctx));
	xctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.xml_uri));
	CHECK(xctx != 0);
	CHECK(mt_raises("iio:ENOTSUP", "context_ping", 1, mt_u64(xctx)));
	s = mt_str_of("context_get_name", 1, mt_u64(xctx));
	CHECK(s && !strcmp(s, "xml"));
	CHECK(param_of(xctx, "timeout_ms") == 0);
	mt_ok("context_destroy", 1, mt_u64(xctx));
	mt_ok("context_destroy", 1, mt_u64(ctx2));
	CHECK(gw_handle_count() == 13);

	/* allocation failures: the XML is parked; a list creates its result before it registers */
	CHECK(mt_oom_sweep(1, "context_get_xml", 1, mt_u64(ctx)) >= 1);
	CHECK(mt_oom_sweep(1, "context_get_devices", 1, mt_u64(ctx)) >= 1);
	CHECK(mt_oom_sweep(1, "context_get_attrs", 1, mt_u64(ctx)) >= 1);
	CHECK(mt_oom_sweep(1, "context_get_params", 1, mt_u64(ctx)) >= 1);
	CHECK(mt_oom_sweep(1, "context_get_name", 1, mt_u64(ctx)) >= 1);
	CHECK(mt_oom_sweep(1, "context_find_device", 2, mt_u64(ctx), mt_str("phy")) >= 1);
	CHECK(gw_handle_count() == 13);

	/* teardown: every borrowed id goes with the context */
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0 && !mock_is_locked());
	CHECK(mt_raises("iio:EBADF", "context_get_name", 1, mt_u64(ctx)));
	for (int i = 0; i < 8; i++) {
		CHECK(gw_handle_type(devs[i]) == GW_T_NONE);
		CHECK(mt_raises("iio:EBADF", "context_get_name", 1, mt_u64(devs[i])));
	}
	for (int i = 0; i < 4; i++) {
		CHECK(gw_handle_type(attrs[i]) == GW_T_NONE);
		CHECK(mt_raises("iio:EBADF", "context_get_name", 1, mt_u64(attrs[i])));
	}
	mt_ok("context_destroy", 1, mt_u64(ctx));
	mt_ok("context_destroy", 1, mt_u64(0));
	scan = gw_handle_add(GW_T_SCAN, &fx, 0, true, NULL, NULL);     /* a live id of another type */
	CHECK(scan != 0);
	CHECK(mt_raises("iio:EBADF", "context_destroy", 1, mt_u64(scan)));
	CHECK(mt_raises("iio:EBADF", "context_get_name", 1, mt_u64(scan)));
	CHECK(gw_handle_type(scan) == GW_T_SCAN);
	gw_handle_destroy(scan);

	/* an allocation failure while a context is created leaves no context behind unregistered.
	 * (The branch "the handle table has no room" of create_context cannot be reached from a
	 * test: the table has no failure injection. It is reviewed by reading.) */
	CHECK(mt_oom_sweep(1, "create_context", 2, mt_empty(), mt_str(fx.uri)) >= 1);
	CHECK(mt_oom_sweep(1, "create_context", 2, params(1, "flags", 1.0), mt_str(fx.uri)) >= 1);
	mt_ok("mex_shutdown", 0);
	CHECK(gw_handle_count() == 0);

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
