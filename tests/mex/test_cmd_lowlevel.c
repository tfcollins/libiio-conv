/* SPDX-License-Identifier: MIT */
/* Command group "lowlevel": sample size of a mask, trigger, hardware registers, debug and event
 * attributes of a device. emu has registers and refuses triggers; the fake backend has a
 * trigger and no register operations, so libiio falls back to the attribute direct_reg_access. */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_lowlevel);

/* the table "Group lowlevel" of the Command reference */
static const struct mt_arity arities[] = {
	{ "device_get_sample_size", 2, 2, 1 },
	{ "device_get_trigger", 1, 1, 1 },
	{ "device_set_trigger", 2, 2, 0 },
	{ "device_reg_read", 2, 2, 1 },
	{ "device_reg_write", 3, 3, 0 },
	{ "device_get_debug_attrs", 1, 1, 1 },
	{ "device_get_event_attrs", 1, 1, 1 },
	{ "device_find_debug_attr", 2, 2, 1 },
	{ "device_find_event_attr", 2, 2, 1 },
};

#define MAX_IDS 16

/* A list command: a 1-by-n uint64 copied to ids (n at most MAX_IDS), each element a borrowed
 * attribute of the context ctx. Returns n, or -1 after a failure. */
static int attrs_of(const char *cmd, uint64_t dev, uint64_t ctx, uint64_t ids[MAX_IDS])
{
	mxArray *out[1];
	int n = -1;

	if (mt_call(1, out, cmd, 1, mt_u64(dev)))
		return -1;
	if (mxGetClassID(out[0]) == mxUINT64_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 1 &&
	    mxGetN(out[0]) <= MAX_IDS) {
		n = (int)mxGetN(out[0]);
		if (n)
			memcpy(ids, mxGetData(out[0]), (size_t)n * sizeof(*ids));
		for (int i = 0; i < n; i++)
			if (gw_handle_type(ids[i]) != GW_T_ATTR || gw_handle_parent(ids[i]) != ctx ||
			    gw_handle_owned(ids[i]))
				n = -1;
	}
	mxDestroyArray(out[0]);
	return n;
}

/* device_reg_read(dev, addr) is a real uint32 scalar with this value */
static bool reg_is(uint64_t dev, mxArray *addr, uint32_t want)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "device_reg_read", 2, mt_u64(dev), addr))
		return false;
	ok = mxGetClassID(out[0]) == mxUINT32_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 1 &&
		mxGetN(out[0]) == 1 && *(const uint32_t *)mxGetData(out[0]) == want;
	mxDestroyArray(out[0]);
	return ok;
}

static double size_of(uint64_t dev, uint64_t mask)
{
	return mt_dbl_of("device_get_sample_size", 2, mt_u64(dev), mt_u64(mask));
}

/* dp_open_fake() behind a call that is not inlined: with its only call site inlined, GCC sees
 * the size of fx->xml in the snprintf of fake_create_context() and warns (-Wformat-truncation). */
static __attribute__((noinline)) gw_id open_fake(const char *xml_path)
{
	return dp_open_fake(xml_path);
}

static void emu_tests(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, phy, dev4, trig, ids[MAX_IDS], again[MAX_IDS], ev[MAX_IDS];
	uint64_t m0, m2, m4, m5, mtx, z3, z8, bs, attr;
	const struct iio_device *phy_dev;
	size_t before;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	phy = dp_dev(ctx, "emu-phy");
	dev4 = dp_dev(ctx, "iio:device4");
	trig = dp_dev(ctx, "trigger0");
	phy_dev = gw_handle_ptr(phy, GW_T_DEVICE);

	/* ---- sample size */
	m0 = dp_mask(rx, false, 0);
	m2 = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	m4 = dp_mask(rx, false, 4, "voltage0_i", "voltage0_q", "voltage1_i", "voltage1_q");
	m5 = dp_mask(rx, false, 5, "voltage0_i", "voltage0_q", "voltage1_i", "voltage1_q", "voltage2");
	mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	z3 = dp_mask(dev4, false, 3, "voltage0", "voltage1", "timestamp");
	z8 = dp_mask(dev4, false, 8, "voltage0", "voltage1", "voltage2", "rot_quaternion", "voltage3",
		"voltage4", "voltage5", "timestamp");
	CHECK(size_of(rx, m0) == 0);
	CHECK(size_of(rx, m2) == 4);
	CHECK(size_of(rx, m4) == 8);
	CHECK(size_of(rx, m5) == 10);
	CHECK(size_of(dev4, z3) == 16);
	CHECK(size_of(dev4, z8) == 56);
	CHECK(size_of(tx, mtx) == 4);
	/* a mask of another device: libiio alone answers 0 (same word count) or reads the wrong bits */
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(rx), mt_u64(mtx)));
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(tx), mt_u64(m2)));
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(dev4), mt_u64(m5)));
	CHECK(mock_err_msg() && strstr(mock_err_msg(), "device_get_sample_size: "));
	/* the mask of a buffer stream (a view below the buffer) belongs to the device of the buffer */
	bs = dp_bufstream(dp_buf(rx, 0), m2);
	CHECK(bs != 0);
	if (bs) {
		const struct iio_channels_mask *eff = iio_buffer_stream_get_channels_mask(
			gw_handle_ptr(bs, GW_T_BUFFER_STREAM));
		gw_id view = 0;

		CHECK(gw_handle_borrow(GW_T_MASK, eff, bs, &view) == 0 && view != 0);
		CHECK(size_of(rx, view) == 4);
		CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(tx), mt_u64(view)));
		gw_handle_destroy(bs);
		CHECK(mt_raises("iio:EBADF", "device_get_sample_size", 2, mt_u64(rx), mt_u64(view)));
	}
	CHECK(mt_raises("iio:EBADF", "device_get_sample_size", 2, mt_u64(rx), mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "device_get_sample_size", 2, mt_u64(rx), mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "device_get_sample_size", 2, mt_u64(m2), mt_u64(m2)));
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(rx), mt_dbl((double)m2)));
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 1, mt_u64(rx)));
	CHECK(mt_oom_sweep(1, "device_get_sample_size", 2, mt_u64(rx), mt_u64(m5)) >= 1);

	/* ---- registers: emu keeps a table per device, empty at first */
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(16)));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(51966));
	CHECK(reg_is(phy, mt_dbl(16), 51966));
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(17)));
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(rx), mt_dbl(16)));   /* per device */
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(4294967295.0), mt_dbl(4294967295.0));
	CHECK(reg_is(phy, mt_dbl(4294967295.0), 4294967295u));
	CHECK(reg_is(phy, mt_dbl(16), 51966));
	/* integer classes are read exactly */
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_u64(0), mt_i64(2147483648LL));
	CHECK(reg_is(phy, mt_i64(0), 2147483648u));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(0));
	CHECK(reg_is(phy, mt_u64(16), 0));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(51966));
	/* out of range, not an integer, not a number */
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_dbl(4294967296.0)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_dbl(-1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_dbl(1.5)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_str("16")));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_bool(true)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_u64(4294967296ULL)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(4294967296.0), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(-1), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(1.5), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_str("a"), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(4294967296.0)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(-1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(1.5)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_str("a")));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_i64(-1)));
	CHECK(mt_raises("iio:EINVAL", "device_reg_write", 2, mt_u64(phy), mt_dbl(16)));
	CHECK(reg_is(phy, mt_dbl(16), 51966));                /* none of them wrote */
	CHECK(mt_raises("iio:EBADF", "device_reg_read", 2, mt_u64(ctx), mt_dbl(16)));
	CHECK(mt_raises("iio:EBADF", "device_reg_write", 3, mt_u64(0), mt_dbl(16), mt_dbl(1)));
	/* the result exists before the device is touched */
	CHECK(mt_oom_sweep(1, "device_reg_read", 2, mt_u64(phy), mt_dbl(16)) >= 1);
	CHECK(mt_oom_sweep(0, "device_reg_write", 3, mt_u64(phy), mt_dbl(32), mt_dbl(7)) == 0);
	CHECK(reg_is(phy, mt_dbl(32), 7));
	/* a decimal comma as the numeric locale of the process changes nothing, and the commands
	 * leave the locale as they found it (mt_comma_end checks) */
	if (mt_comma_begin()) {
		mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(48), mt_dbl(305419896));
		CHECK(reg_is(phy, mt_dbl(48), 305419896u));
		CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(49)));
		mt_comma_end();
	}

	/* ---- debug and event attributes */
	memset(ids, 0, sizeof(ids));
	CHECK(attrs_of("device_get_debug_attrs", phy, ctx, ids) == 4);
	for (unsigned int k = 0; k < 4; k++)
		CHECK(gw_handle_ptr(ids[k], GW_T_ATTR) == iio_device_get_debug_attr(phy_dev, k));
	CHECK(!strcmp(iio_attr_get_name(iio_device_get_debug_attr(phy_dev, 0)), "bist_prbs"));
	CHECK(!strcmp(iio_attr_get_name(iio_device_get_debug_attr(phy_dev, 1)), "direct_reg_access"));
	CHECK(!strcmp(iio_attr_get_name(iio_device_get_debug_attr(phy_dev, 2)), "loopback"));
	CHECK(!strcmp(iio_attr_get_name(iio_device_get_debug_attr(phy_dev, 3)), "loopback_available"));
	before = gw_handle_count();
	CHECK(attrs_of("device_get_debug_attrs", phy, ctx, again) == 4);
	CHECK(!memcmp(ids, again, 4 * sizeof(ids[0])));
	CHECK(gw_handle_count() == before);
	attr = mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("loopback"));
	CHECK(attr != 0 && attr == ids[2]);
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("bist_prbs")) == ids[0]);
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("nope")) == 0);
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("")) == 0);
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("gain")) == 0);            /* a device attribute */
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("events_enabled")) == 0);  /* an event attribute */
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(rx), mt_str("loopback")) == 0);
	CHECK(attrs_of("device_get_event_attrs", phy, ctx, ev) == 1);
	CHECK(gw_handle_ptr(ev[0], GW_T_ATTR) == iio_device_get_event_attr(phy_dev, 0));
	CHECK(ev[0] != ids[0] && ev[0] != ids[1] && ev[0] != ids[2] && ev[0] != ids[3]);
	CHECK(mt_u64_of("device_find_event_attr", 2, mt_u64(phy), mt_str("events_enabled")) == ev[0]);
	CHECK(mt_u64_of("device_find_event_attr", 2, mt_u64(phy), mt_str("loopback")) == 0);
	CHECK(mt_u64_of("device_find_event_attr", 2, mt_u64(phy), mt_str("nope")) == 0);
	CHECK(attrs_of("device_get_debug_attrs", rx, ctx, again) == 0);
	CHECK(attrs_of("device_get_event_attrs", rx, ctx, again) == 0);
	CHECK(attrs_of("device_get_debug_attrs", trig, ctx, again) == 0);
	{       /* text with an embedded NUL is refused, not cut to "loopback" / "events_enabled" */
		mxArray *dbg = mt_str("loopback?x"), *evt = mt_str("events_enabled?");

		((mxChar *)mxGetData(dbg))[8] = 0;
		((mxChar *)mxGetData(evt))[14] = 0;
		CHECK(mt_raises("iio:EINVAL", "device_find_debug_attr", 2, mt_u64(phy), dbg));
		CHECK(mt_raises("iio:EINVAL", "device_find_event_attr", 2, mt_u64(phy), evt));
	}
	CHECK(mt_raises("iio:EINVAL", "device_find_debug_attr", 2, mt_u64(phy), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "device_find_debug_attr", 2, mt_u64(phy), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "device_find_event_attr", 2, mt_u64(phy), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "device_find_event_attr", 1, mt_u64(phy)));
	CHECK(mt_raises("iio:EBADF", "device_get_debug_attrs", 1, mt_u64(ids[0])));
	CHECK(mt_raises("iio:EBADF", "device_get_event_attrs", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "device_find_debug_attr", 2, mt_u64(0), mt_str("loopback")));
	CHECK(mt_raises("iio:EBADF", "device_find_event_attr", 2, mt_u64(m2), mt_str("events_enabled")));
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "device_get_debug_attrs", 1, mt_u64(phy)) >= 1);
	CHECK(mt_oom_sweep(1, "device_get_event_attrs", 1, mt_u64(phy)) >= 1);
	CHECK(mt_oom_sweep(1, "device_find_debug_attr", 2, mt_u64(phy), mt_str("loopback")) >= 1);
	CHECK(mt_oom_sweep(1, "device_find_event_attr", 2, mt_u64(phy), mt_str("events_enabled")) >= 1);
	CHECK(gw_handle_count() == before);

	/* ---- trigger: emu has neither operation */
	CHECK(mt_raises("iio:ENOSYS", "device_get_trigger", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(phy)));   /* not a trigger */
	CHECK(mt_raises("iio:ENOSYS", "device_set_trigger", 2, mt_u64(rx), mt_u64(0)));
	CHECK(mt_raises("iio:ENOSYS", "device_set_trigger", 2, mt_u64(rx), mt_u64(trig)));
	CHECK(mt_raises("iio:EBADF", "device_get_trigger", 1, mt_u64(m2)));
	CHECK(mt_raises("iio:EBADF", "device_get_trigger", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "device_set_trigger", 2, mt_u64(0), mt_u64(trig)));
	CHECK(mt_raises("iio:EBADF", "device_set_trigger", 2, mt_u64(rx), mt_u64(m2)));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 1, mt_u64(rx)));
	CHECK(mt_oom_sweep(1, "device_get_trigger", 1, mt_u64(rx)) == 0);    /* nothing is allocated before the error */

	/* a stale device */
	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "device_get_sample_size", 2, mt_u64(rx), mt_u64(m2)));
	CHECK(mt_raises("iio:EBADF", "device_get_trigger", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "device_set_trigger", 2, mt_u64(rx), mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "device_reg_read", 2, mt_u64(phy), mt_dbl(16)));
	CHECK(mt_raises("iio:EBADF", "device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(1)));
	CHECK(mt_raises("iio:EBADF", "device_get_debug_attrs", 1, mt_u64(phy)));
	CHECK(mt_raises("iio:EBADF", "device_get_event_attrs", 1, mt_u64(phy)));
	CHECK(mt_raises("iio:EBADF", "device_find_debug_attr", 2, mt_u64(phy), mt_str("loopback")));
	CHECK(mt_raises("iio:EBADF", "device_find_event_attr", 2, mt_u64(phy), mt_str("events_enabled")));
}

static void fake_tests(const struct mt_fixture *fx)
{
	uint64_t ctx, other, rx, tx, phy, trig, sysfs_trig, other_trig, other_rx;
	size_t before;

	ctx = open_fake(fx->xml);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	phy = dp_dev(ctx, "emu-phy");
	trig = dp_dev(ctx, "trigger0");
	sysfs_trig = dp_dev(ctx, "iio_sysfs_trigger");

	/* ---- trigger */
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == 0);     /* -ENODEV: none assigned */
	mt_ok("device_set_trigger", 2, mt_u64(rx), mt_u64(trig));
	CHECK(fake.trigger == gw_handle_ptr(trig, GW_T_DEVICE));
	before = gw_handle_count();
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == trig);
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == dp_dev(ctx, "trigger0"));
	CHECK(gw_handle_count() == before);                     /* the id of the device, not a new one */
	CHECK(mt_oom_sweep(1, "device_get_trigger", 1, mt_u64(rx)) >= 1);
	CHECK(gw_handle_count() == before);
	/* refused before the backend sees it: not a trigger, a handle of another type */
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(phy)));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(sysfs_trig)));
	CHECK(mt_raises("iio:EBADF", "device_set_trigger", 2, mt_u64(rx), mt_u64(ctx)));
	CHECK(fake.trigger == gw_handle_ptr(trig, GW_T_DEVICE));
	/* a trigger of another context */
	other = mt_open_context(fx->uri);
	CHECK(other != 0);
	other_trig = dp_dev(other, "trigger0");
	other_rx = dp_dev(other, "emu-rx");
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(other_trig)));
	CHECK(mock_err_msg() && strstr(mock_err_msg(), "device_set_trigger: "));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(other_rx), mt_u64(trig)));
	CHECK(fake.trigger == gw_handle_ptr(trig, GW_T_DEVICE));
	gw_handle_destroy(other);
	CHECK(mt_raises("iio:EBADF", "device_set_trigger", 2, mt_u64(rx), mt_u64(other_trig)));
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == trig);
	/* removing it */
	mt_ok("device_set_trigger", 2, mt_u64(rx), mt_u64(0));
	CHECK(fake.trigger == NULL);
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == 0);
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(tx)) == 0);

	/* ---- registers without backend operations: libiio goes through the debug attribute
	 * direct_reg_access. One write of "0x<addr> 0x<value>" with its NUL, and nothing else. */
	CHECK(fake.attr_data == NULL);
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(51966));
	CHECK(fake.attr_len == sizeof("0x10 0xcafe") && fake.attr_data &&
	      !memcmp(fake.attr_data, "0x10 0xcafe", sizeof("0x10 0xcafe")));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(4294967295.0), mt_dbl(4294967295.0));
	CHECK(fake.attr_len == sizeof("0xffffffff 0xffffffff") && fake.attr_data &&
	      !memcmp(fake.attr_data, "0xffffffff 0xffffffff", sizeof("0xffffffff 0xffffffff")));
	/* a read writes the address, then reads the attribute: the fake's text is no number */
	CHECK(mt_raises("iio:EINVAL", "device_reg_read", 2, mt_u64(phy), mt_dbl(17)));
	CHECK(fake.attr_len == sizeof("17") && fake.attr_data && !memcmp(fake.attr_data, "17", sizeof("17")));
	/* a failed allocation of the result comes before that write, so a retry writes once */
	free(fake.attr_data);
	fake.attr_data = NULL;
	fake.attr_len = 0;
	mock_fail_alloc(1);
	{
		const mxArray *prhs[3];
		mxArray *out[1] = { NULL };
		int raised;

		prhs[0] = mt_str("device_reg_read");
		prhs[1] = mt_u64(phy);
		prhs[2] = mt_dbl(18);
		raised = mock_call(1, out, 3, prhs);
		CHECK(raised && mock_alloc_failed());
		mock_fail_alloc(0);
		CHECK(fake.attr_data == NULL && fake.attr_len == 0);
		for (int k = 0; k < 3; k++)
			mxDestroyArray((mxArray *)prhs[k]);
	}
	/* a device without that attribute */
	CHECK(mt_raises("iio:ENOENT", "device_reg_write", 3, mt_u64(rx), mt_dbl(16), mt_dbl(1)));
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(rx), mt_dbl(16)));
	CHECK(fake.attr_data == NULL);

	/* sample size does not depend on the backend */
	{
		uint64_t m = dp_mask(rx, false, 2, "voltage0_i", "voltage2");

		CHECK(size_of(rx, m) == 4);
	}
	gw_handle_destroy(ctx);
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_lowlevel, arities, sizeof(arities) / sizeof(arities[0]));

	emu_tests(&fx);
	fake_tests(&fx);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
