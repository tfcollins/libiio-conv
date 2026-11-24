/* SPDX-License-Identifier: MIT */
/* iio_mex gateway, command group "test": a context on the fake backend of the tests, and what
 * that backend recorded. Compiled only with IIO_MEX_TEST_HOOKS: always for the mock-based
 * tests, into the MEX file only when CMake has IIO_CONV_MEX_TEST_HOOKS=ON (default OFF). */
#include "mx_util.h"

#ifdef IIO_MEX_TEST_HOOKS

#include <limits.h>
#include <string.h>

/* The backend and its state (one set of statics): this file is the only one of the MEX that
 * includes it. */
#include "fake_backend.h"

/* A context made by test_create_fake_context exists. Its backend state must not be reset
 * under it, so a second one is refused until the first is destroyed. */
static bool fake_alive;

/* The text fake.couple points at. */
static char couple_id[64];

static void fake_context_destroy_op(void *ptr, void *aux)
{
	(void)aux;
	iio_context_destroy(ptr);
	fake_alive = false;
}

/* ctx = test_create_fake_context(xml_file) */
static int test_create_fake_context(struct gw_call *c)
{
	static const struct gw_ops ops = { NULL, fake_context_destroy_op };
	struct gw_c_numeric loc;
	struct iio_context *ctx;
	char *path;
	gw_id id;
	int ret;

	ret = gw_arg_str(c, 0, &path);
	if (ret)
		return ret;
	if (fake_alive) {
		mxFree(path);
		return gw_fail(c, -EBUSY, "a fake context exists: destroy it first");
	}
	/* Result first: no mx allocation may follow the creation of the context. */
	ret = gw_ret_id(c, 0, 0);
	if (ret) {
		mxFree(path);
		return ret;
	}
	/* libiio reads the channel scales of the XML with strtof */
	fake_reset();
	ret = gw_c_numeric_enter(&loc);
	if (ret) {
		mxFree(path);
		return gw_fail(c, ret, "no \"C\" numeric locale");
	}
	ctx = fake_create_context(path);
	gw_c_numeric_leave(&loc);
	mxFree(path);
	ret = iio_err(ctx);            /* error-encoded, never NULL */
	if (ret)
		return ret;
	id = gw_handle_add(GW_T_CONTEXT, ctx, 0, true, &ops, NULL);
	if (!id) {
		iio_context_destroy(ctx);
		return gw_fail(c, -ENOMEM, "out of memory in the handle table");
	}
	fake_alive = true;
	gw_set_id(c, 0, id);
	return 0;
}

/* n = test_fake_last_timeout(): what the backend's set_timeout received last; -12345 if nothing */
static int test_fake_last_timeout(struct gw_call *c)
{
	return gw_ret_double(c, 0, fake.timeout);
}

/* u8 = test_fake_last_write(): the bytes the backend's write_attr received last; 1-by-0 if none */
static int test_fake_last_write(struct gw_call *c)
{
	return gw_ret_bytes(c, 0, fake.attr_data, fake.attr_data ? fake.attr_len : 0);
}

/* test_fake_set(field, value): 'rx_frames', 'tx_frames', 'busy' (whole number, 0 or more) or
 * 'couple' (channel id as text, '' = none) */
static int test_fake_set(struct gw_call *c)
{
	char *field, *text;
	long long v = 0;
	int *target = NULL;
	int ret;

	ret = gw_arg_str(c, 0, &field);
	if (ret)
		return ret;
	if (!strcmp(field, "couple")) {
		mxFree(field);
		ret = gw_arg_str(c, 1, &text);
		if (ret)
			return ret;
		if (strlen(text) >= sizeof(couple_id)) {
			mxFree(text);
			return gw_fail(c, -EINVAL, "the channel id is longer than %zu characters",
				sizeof(couple_id) - 1);
		}
		/* fake.couple is read only inside libiio calls, never while this runs */
		strcpy(couple_id, text);
		mxFree(text);
		fake.couple = couple_id[0] ? couple_id : NULL;
		return 0;
	}
	if (!strcmp(field, "rx_frames"))
		target = &fake.rx_frames;
	else if (!strcmp(field, "tx_frames"))
		target = &fake.tx_frames;
	else if (!strcmp(field, "busy"))
		target = &fake.busy;
	if (!target) {
		ret = gw_fail(c, -EINVAL, "unknown field \"%s\"", field);
		mxFree(field);
		return ret;
	}
	mxFree(field);
	ret = gw_arg_int(c, 1, 0, INT_MAX, &v);
	if (ret)
		return ret;
	*target = (int)v;
	return 0;
}

/* char = test_fake_log(): one letter per backend call since the context was created */
static int test_fake_log(struct gw_call *c)
{
	return gw_ret_str(c, 0, fake.log);
}

static const struct gw_cmd cmds[] = {
	GW_XCMD("test_create_fake_context", test_create_fake_context, 1, 1, 1),
	GW_XCMD("test_fake_last_timeout", test_fake_last_timeout, 0, 0, 1),
	GW_XCMD("test_fake_last_write", test_fake_last_write, 0, 0, 1),
	GW_XCMD("test_fake_set", test_fake_set, 2, 2, 0),
	GW_XCMD("test_fake_log", test_fake_log, 0, 0, 1),
};

const struct gw_group gw_group_test = GW_GROUP("test", cmds);

#endif /* IIO_MEX_TEST_HOOKS */
