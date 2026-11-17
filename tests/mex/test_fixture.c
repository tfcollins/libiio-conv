/* SPDX-License-Identifier: MIT */
/* The gateway test harness itself (mex_test.h): the fixture directory, a context opened
 * without the context commands, and the allocation-failure sweep. */
#include "mex_test.h"

const struct gw_group *const gw_groups[] = { &gw_group_mex, NULL };

int main(int argc, char **argv)
{
	static unsigned char buf[20000];
	struct mt_fixture fx;
	struct iio_context *ctx;
	const struct iio_device *dev;
	struct stat st;
	size_t live;
	gw_id id, id2;

	if (argc < 2)
		return 2;
	CHECK(mt_fixture_setup(&fx, "/no/such/dir/iio_fixture.xml") == -ENOENT);
	CHECK(mt_fixture_setup(&fx, argv[1]) == 0);
	if (test_failures)
		TEST_MAIN_RETURN();

	CHECK(!strncmp(fx.uri, "emu:/", 5));
	CHECK(!strncmp(fx.xml_uri, "xml:/", 5));
	CHECK(fx.dir[0] == '/' && !strncmp(fx.xml, fx.dir, strlen(fx.dir)));
	CHECK(!strcmp(fx.xml + strlen(fx.dir), "/iio_fixture.xml"));
	CHECK(!strcmp(fx.uri + 4, fx.xml) && !strcmp(fx.xml_uri + 4, fx.xml));
	CHECK(stat(fx.xml, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);

	ctx = iio_create_context(NULL, fx.uri);
	CHECK(iio_err(ctx) == 0);
	if (!iio_err(ctx)) {
		CHECK(iio_context_get_devices_count(ctx) == 8);
		dev = iio_context_find_device(ctx, "emu-phy");
		CHECK(dev && iio_device_get_attrs_count(dev) == 14);
		iio_context_destroy(ctx);
	}
	ctx = iio_create_context(NULL, fx.xml_uri);
	CHECK(iio_err(ctx) == 0);
	if (!iio_err(ctx)) {
		CHECK(iio_context_get_devices_count(ctx) == 8);
		iio_context_destroy(ctx);
	}

	CHECK(fixture_read_file(fx.dir, "emu-rx_buf0.bin", buf, sizeof(buf)) == 16384);
	CHECK(buf[0] == 0x00 && buf[1] == 0x08 && buf[2] == 0xff && buf[3] == 0x07);

	/* a context in the handle table, without any context command */
	CHECK(!mock_is_locked());
	id = mt_open_context(fx.uri);
	CHECK(id != 0);
	CHECK(gw_handle_type(id) == GW_T_CONTEXT);
	CHECK(gw_handle_owned(id) && gw_handle_parent(id) == 0);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	CHECK(mock_is_locked());
	ctx = gw_handle_ptr(id, GW_T_CONTEXT);
	CHECK(ctx && iio_context_get_devices_count(ctx) == 8);
	id2 = mt_open_context(fx.xml_uri);
	CHECK(id2 != 0 && id2 != id);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 2);
	gw_handle_destroy(id2);
	gw_handle_destroy(id);                 /* iio_context_destroy: LeakSanitizer would see a leak */
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());

	CHECK(mt_open_context("bogus:foo") == 0);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);

	/* a context that is still open at MATLAB exit is closed by the gateway */
	id = mt_open_context(fx.uri);
	CHECK(id != 0);
	mock_exit_matlab();
	CHECK(gw_handle_count() == 0);
	id = mt_open_context(fx.uri);          /* also the first thing after a new start */
	CHECK(id != 0 && gw_handle_type(id) == GW_T_CONTEXT);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	mt_ok("mex_shutdown", 0);
	CHECK(gw_handle_type(id) == GW_T_NONE);

	/* the sweep: one failed attempt per allocation of the command, then one that succeeds */
	live = mock_live();
	CHECK(mt_oom_sweep(1, "mex_handle_count", 0) >= 1);
	CHECK(!mock_alloc_failed());
	CHECK(mock_live() == live);
	CHECK(mt_oom_sweep(0, "mex_shutdown", 0) == 0);     /* a command without allocations */
	CHECK(mt_oom_sweep(0, "mex_handle_count", 1, mt_dbl(1)) == 0);   /* fails before any allocation */
	CHECK(!strcmp(mock_err_id(), "iio:EINVAL"));
	CHECK(mock_live() == live);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);        /* the injection is switched off again */

	/* mt_raises compares the identifier exactly */
	CHECK(mt_raises("iio:ENOSYS", "no_such", 0));
	CHECK(!mt_raises("iio:EINVAL", "no_such", 0));
	CHECK(!mt_raises("iio:ENOSY", "no_such", 0));
	CHECK(!mt_raises("iio:ENOSYS", "mex_handle_count", 0));

	mt_fixture_teardown(&fx);
	CHECK(stat(fx.dir, &st) != 0 && errno == ENOENT);
	MT_MAIN_RETURN();
}
