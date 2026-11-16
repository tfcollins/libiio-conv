/* SPDX-License-Identifier: MIT */
/* The first vertical slice through the gateway: create an emu: context, find and read one
 * context attribute, destroy the context, "clear mex" and MATLAB exit. The same program runs
 * as the test mex_bundled_libs with libiio bound to MATLAB's bundled libxml2 and libzstd. */
#include "mex_test.h"
MT_GROUPS(&gw_group_context, &gw_group_attr);

int main(int argc, char **argv)
{
	struct mt_fixture fx;
	mxArray *out[1];
	uint64_t ctx, ctx2, a, num;
	const char *s;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	/* errors of create_context: nothing is registered, nothing is locked */
	CHECK(mt_raises("iio:EINVAL", "create_context", 1, mt_str(fx.uri)));
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, mt_empty(), mt_dbl(3)));
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, mt_empty(), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "create_context", 2, mt_dbl(1), mt_str(fx.uri)));
	CHECK(mt_raises("iio:EINVAL", "create_context", 3, mt_empty(), mt_str(fx.uri), mt_empty()));
	CHECK(mt_raises("iio:ENOSYS", "create_context", 2, mt_empty(), mt_str("bogus:foo")));
	CHECK(!strncmp(mock_err_msg(), "create_context: ", 16));
	CHECK(strlen(mock_err_msg()) > 16);
	CHECK(!mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);

	/* the context */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx != 0);
	CHECK(mock_is_locked());
	CHECK(gw_handle_type(ctx) == GW_T_CONTEXT);
	CHECK(gw_handle_owned(ctx) && gw_handle_parent(ctx) == 0);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);

	/* a context attribute: a borrowed child of the context, one id per attribute */
	a = mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("hw_model"));
	CHECK(a != 0 && a != ctx);
	CHECK(gw_handle_type(a) == GW_T_ATTR);
	CHECK(!gw_handle_owned(a) && gw_handle_parent(a) == ctx);
	CHECK(mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("hw_model")) == a);
	CHECK(mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("nope")) == 0);
	CHECK(mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("")) == 0);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 2);
	CHECK(mt_raises("iio:EINVAL", "context_find_attr", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EINVAL", "context_find_attr", 2, mt_u64(ctx), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "context_find_attr", 2, mt_u64(ctx), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "context_find_attr", 2, mt_dbl((double)ctx), mt_str("hw_model")));
	CHECK(mt_raises("iio:EBADF", "context_find_attr", 2, mt_u64(a), mt_str("hw_model")));
	CHECK(mt_raises("iio:EBADF", "context_find_attr", 2, mt_u64(0), mt_str("hw_model")));

	/* its value */
	CHECK(mt_call(1, out, "attr_read_raw", 1, mt_u64(a)) == 0);
	CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 19);
	mxDestroyArray(out[0]);
	s = mt_str_of("attr_read_raw", 1, mt_u64(a));
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	num = mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("num"));
	CHECK(num != 0 && num != a && num != ctx);
	s = mt_str_of("attr_read_raw", 1, mt_u64(num));
	CHECK(s && !strcmp(s, "42"));
	CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(a), mt_bool(true)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 19);
	CHECK(!memcmp(mxGetData(out[0]), "Emulated Test Board", 19));
	mxDestroyArray(out[0]);
	s = mt_str_of("attr_read_raw", 2, mt_u64(a), mt_bool(false));
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	s = mt_str_of("attr_read_raw", 2, mt_u64(a), mt_empty());
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(ctx)));
	CHECK(strstr(mock_err_msg(), "attr_read_raw: ") == mock_err_msg());
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "attr_read_raw", 1, mt_str("hw_model")));
	CHECK(mt_raises("iio:EINVAL", "attr_read_raw", 0));
	CHECK(mt_raises("iio:EINVAL", "attr_read_raw", 2, mt_u64(a), mt_dbl(3)));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 3);
	CHECK(mt_raises("iio:EBADF", "context_destroy", 1, mt_u64(a)));   /* a live id of another type */
	CHECK(mt_raises("iio:EBADF", "context_destroy", 1, mt_u64(num)));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 3);

	/* allocation failures inside the two commands that read */
	CHECK(mt_oom_sweep(1, "attr_read_raw", 1, mt_u64(a)) >= 1);
	CHECK(mt_oom_sweep(1, "context_find_attr", 2, mt_u64(ctx), mt_str("hw_model")) >= 1);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 3);

	/* destroy: the attributes go with their context */
	CHECK(mt_raises("iio:EINVAL", "context_destroy", 0));
	CHECK(mt_raises("iio:EINVAL", "context_destroy", 1, mt_dbl((double)ctx)));
	CHECK(mt_raises("iio:EINVAL", "context_destroy", 1, mt_str("x")));
	CHECK(mock_is_locked());
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(!mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(num)));
	CHECK(mt_raises("iio:EBADF", "context_find_attr", 2, mt_u64(ctx), mt_str("hw_model")));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	mt_ok("context_destroy", 1, mt_u64(0));
	mt_ok("context_destroy", 1, mt_u64(a));
	CHECK(!mock_is_locked());

	/* an allocation failure while a context is created leaves no context behind unregistered */
	CHECK(mt_oom_sweep(1, "create_context", 2, mt_empty(), mt_str(fx.uri)) >= 1);
	mt_ok("mex_shutdown", 0);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());

	/* the xml: backend reads the same attribute */
	ctx2 = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.xml_uri));
	CHECK(ctx2 != 0);
	a = mt_u64_of("context_find_attr", 2, mt_u64(ctx2), mt_str("hw_model"));
	s = mt_str_of("attr_read_raw", 1, mt_u64(a));
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	mt_ok("context_destroy", 1, mt_u64(ctx2));

	/* a new context has a new id; "clear mex" does nothing while it lives; MATLAB exit closes it */
	ctx2 = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx2 != 0 && ctx2 != ctx);
	CHECK(mt_raises("iio:EBADF", "context_find_attr", 2, mt_u64(ctx), mt_str("hw_model")));
	mock_clear_mex();
	CHECK(mock_is_locked());
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	a = mt_u64_of("context_find_attr", 2, mt_u64(ctx2), mt_str("num"));
	s = mt_str_of("attr_read_raw", 1, mt_u64(a));
	CHECK(s && !strcmp(s, "42"));
	mock_exit_matlab();
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());

	/* load again after "clear mex" with nothing alive */
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	mock_clear_mex();
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx != 0);
	a = mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("hw_model"));
	s = mt_str_of("attr_read_raw", 1, mt_u64(a));
	CHECK(s && !strcmp(s, "Emulated Test Board"));
	mt_ok("context_destroy", 1, mt_u64(ctx));

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
