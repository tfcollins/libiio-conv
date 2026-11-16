/* SPDX-License-Identifier: MIT */
/* Command group "device": identity, the lists of channels, attributes and buffers, the find
 * commands and the two flags. Every id a command returns is a borrowed child of the context. */
#include "mex_test.h"
MT_GROUPS(&gw_group_device);

/* the table "Group device" of the Command reference */
static const struct mt_arity arities[] = {
	{ "device_get_context", 1, 1, 1 },
	{ "device_get_id", 1, 1, 1 },
	{ "device_get_name", 1, 1, 1 },
	{ "device_get_label", 1, 1, 1 },
	{ "device_get_channels", 1, 1, 1 },
	{ "device_get_attrs", 1, 1, 1 },
	{ "device_get_buffers", 1, 1, 1 },
	{ "device_find_channel", 3, 3, 1 },
	{ "device_find_attr", 2, 2, 1 },
	{ "device_is_trigger", 1, 1, 1 },
	{ "device_is_hwmon", 1, 1, 1 },
};

#define MAX_IDS 16

static uint64_t ctx;
static const struct iio_context *c;

/* borrowed id of the device that libiio finds under name */
static uint64_t dev_id(const char *name)
{
	const struct iio_device *d = iio_context_find_device(c, name);
	gw_id id = 0;

	CHECK(d != NULL);
	CHECK(gw_handle_borrow(GW_T_DEVICE, d, ctx, &id) == 0);
	CHECK(id != 0);
	return id;
}

/* A list command: a 1-by-n uint64 copied to ids (n at most MAX_IDS), each element a borrowed
 * child of the context with that type. Returns n, or -1 after a failure. */
static int list_of(const char *cmd, uint64_t dev, enum gw_type type, uint64_t ids[MAX_IDS])
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
			if (gw_handle_type(ids[i]) != type || gw_handle_parent(ids[i]) != ctx ||
			    gw_handle_owned(ids[i]))
				n = -1;
	}
	mxDestroyArray(out[0]);
	return n;
}

/* cmd(id) is [] (a 0x0 double): the gateway's NULL */
static bool is_null(const char *cmd, uint64_t id)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, cmd, 1, mt_u64(id)))
		return false;
	ok = mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0;
	mxDestroyArray(out[0]);
	return ok;
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;
	const struct iio_device *rx_dev, *multi_dev;
	uint64_t phy, rx, tx, multi, dev4, trig, sysfs_trig, hwmon, ids[MAX_IDS], again[MAX_IDS];
	uint64_t in0, out0, a;
	size_t before;
	const char *s;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_device, arities, sizeof(arities) / sizeof(arities[0]));

	ctx = mt_open_context(fx.uri);
	CHECK(ctx != 0);
	c = gw_handle_ptr(ctx, GW_T_CONTEXT);
	phy = dev_id("emu-phy");
	rx = dev_id("emu-rx");
	tx = dev_id("emu-tx");
	multi = dev_id("emu-multi");
	dev4 = dev_id("iio:device4");
	trig = dev_id("trigger0");
	sysfs_trig = dev_id("iio_sysfs_trigger");
	hwmon = dev_id("hwmon0");
	rx_dev = gw_handle_ptr(rx, GW_T_DEVICE);
	multi_dev = gw_handle_ptr(multi, GW_T_DEVICE);

	/* identity: a missing name or label is [], not '' */
	s = mt_str_of("device_get_id", 1, mt_u64(phy));
	CHECK(s && !strcmp(s, "iio:device2"));
	s = mt_str_of("device_get_name", 1, mt_u64(phy));
	CHECK(s && !strcmp(s, "emu-phy"));
	s = mt_str_of("device_get_label", 1, mt_u64(phy));
	CHECK(s && !strcmp(s, "phy"));
	s = mt_str_of("device_get_id", 1, mt_u64(dev4));
	CHECK(s && !strcmp(s, "iio:device4"));
	CHECK(is_null("device_get_name", dev4));
	CHECK(is_null("device_get_label", dev4));
	CHECK(is_null("device_get_label", tx));
	s = mt_str_of("device_get_name", 1, mt_u64(tx));
	CHECK(s && !strcmp(s, "emu-tx"));
	CHECK(mt_u64_of("device_get_context", 1, mt_u64(phy)) == ctx);
	CHECK(mt_u64_of("device_get_context", 1, mt_u64(hwmon)) == ctx);

	/* list sizes */
	CHECK(list_of("device_get_channels", phy, GW_T_CHANNEL, ids) == 6);
	CHECK(list_of("device_get_attrs", phy, GW_T_ATTR, ids) == 14);
	CHECK(list_of("device_get_buffers", phy, GW_T_BUFFER, ids) == 0);
	CHECK(list_of("device_get_channels", rx, GW_T_CHANNEL, ids) == 5);
	CHECK(list_of("device_get_attrs", rx, GW_T_ATTR, ids) == 0);
	CHECK(list_of("device_get_buffers", rx, GW_T_BUFFER, ids) == 1);
	CHECK(list_of("device_get_channels", multi, GW_T_CHANNEL, ids) == 4);
	CHECK(list_of("device_get_attrs", multi, GW_T_ATTR, ids) == 0);
	CHECK(list_of("device_get_buffers", multi, GW_T_BUFFER, ids) == 2);
	CHECK(list_of("device_get_channels", trig, GW_T_CHANNEL, ids) == 0);
	CHECK(list_of("device_get_attrs", trig, GW_T_ATTR, ids) == 1);
	CHECK(list_of("device_get_buffers", trig, GW_T_BUFFER, ids) == 0);

	/* channels: libiio order, one id per channel */
	CHECK(list_of("device_get_channels", rx, GW_T_CHANNEL, ids) == 5);
	for (unsigned int k = 0; k < 5; k++)
		CHECK(gw_handle_ptr(ids[k], GW_T_CHANNEL) == iio_device_get_channel(rx_dev, k));
	before = gw_handle_count();
	CHECK(list_of("device_get_channels", rx, GW_T_CHANNEL, again) == 5);
	CHECK(!memcmp(ids, again, 5 * sizeof(ids[0])));
	CHECK(gw_handle_count() == before);
	in0 = mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("voltage0"), mt_bool(false));
	out0 = mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("voltage0"), mt_bool(true));
	CHECK(in0 != 0 && out0 != 0 && in0 != out0);
	CHECK(gw_handle_type(in0) == GW_T_CHANNEL && gw_handle_parent(in0) == ctx && !gw_handle_owned(in0));
	CHECK(gw_handle_type(out0) == GW_T_CHANNEL && gw_handle_parent(out0) == ctx);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("RX1"), mt_bool(false)) == in0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("rx1-port"), mt_bool(false)) == in0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("TX1"), mt_bool(true)) == out0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(rx), mt_str("voltage0_i"), mt_bool(false)) == ids[0]);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(rx), mt_str("voltage2"), mt_bool(false)) == ids[4]);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(rx), mt_str("voltage0_i"), mt_bool(true)) == 0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str("nope"), mt_bool(false)) == 0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str(""), mt_bool(false)) == 0);
	CHECK(mt_raises("iio:EINVAL", "device_find_channel", 3, mt_u64(phy), mt_str("voltage0"), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "device_find_channel", 3, mt_u64(phy), mt_dbl(0), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "device_find_channel", 3, mt_u64(phy), mock_string_object(), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "device_find_channel", 2, mt_u64(phy), mt_str("voltage0")));

	/* attributes: plain device attributes only */
	memset(ids, 0, sizeof(ids));
	if (list_of("device_get_attrs", phy, GW_T_ATTR, ids) == 14) {
		CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[0], GW_T_ATTR)), "calib_mode"));
		CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[9], GW_T_ATTR)), "gain"));
		CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[13], GW_T_ATTR)), "text"));
	} else {
		CHECK(!"device_get_attrs(phy) is a list of 14 attributes");
	}
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("calib_mode")) == ids[0]);
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("text")) == ids[13]);
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("bist_prbs")) == 0);   /* a debug attribute */
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("events_enabled")) == 0);   /* an event attribute */
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(rx), mt_str("calib_mode")) == 0);
	CHECK(mt_raises("iio:EINVAL", "device_find_attr", 2, mt_u64(phy), mt_dbl(1)));
	a = ids[0];

	/* buffers */
	CHECK(list_of("device_get_buffers", multi, GW_T_BUFFER, ids) == 2);
	CHECK(gw_handle_ptr(ids[0], GW_T_BUFFER) == iio_device_get_buffer(multi_dev, 0));
	CHECK(gw_handle_ptr(ids[1], GW_T_BUFFER) == iio_device_get_buffer(multi_dev, 1));
	CHECK(ids[0] != ids[1]);

	/* flags */
	CHECK(mt_bool_of("device_is_trigger", 1, mt_u64(trig)) == true);
	CHECK(mt_bool_of("device_is_trigger", 1, mt_u64(sysfs_trig)) == false);
	CHECK(mt_bool_of("device_is_trigger", 1, mt_u64(phy)) == false);
	CHECK(mt_bool_of("device_is_hwmon", 1, mt_u64(hwmon)) == true);
	CHECK(mt_bool_of("device_is_hwmon", 1, mt_u64(rx)) == false);

	/* allocation failures: nothing leaks, the ids stay the same */
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "device_get_channels", 1, mt_u64(phy)) >= 1);
	CHECK(mt_oom_sweep(1, "device_get_attrs", 1, mt_u64(phy)) >= 1);
	CHECK(mt_oom_sweep(1, "device_get_buffers", 1, mt_u64(multi)) >= 1);
	CHECK(mt_oom_sweep(1, "device_find_channel", 3, mt_u64(phy), mt_str("voltage0"), mt_bool(false)) >= 1);
	CHECK(mt_oom_sweep(1, "device_find_attr", 2, mt_u64(phy), mt_str("gain")) >= 1);
	CHECK(mt_oom_sweep(1, "device_get_name", 1, mt_u64(phy)) >= 1);
	CHECK(gw_handle_count() == before);

	/* a handle of another type, none, a stale one */
	CHECK(mt_raises("iio:EBADF", "device_get_id", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "device_get_name", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "device_get_label", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "device_get_context", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "device_get_channels", 1, mt_u64(in0)));
	CHECK(mt_raises("iio:EBADF", "device_get_attrs", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "device_get_buffers", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "device_find_channel", 3, mt_u64(a), mt_str("voltage0"), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "device_find_attr", 2, mt_u64(ctx), mt_str("gain")));
	CHECK(mt_raises("iio:EBADF", "device_is_trigger", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "device_is_hwmon", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EINVAL", "device_get_id", 1, mt_dbl((double)phy)));
	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "device_get_id", 1, mt_u64(phy)));
	CHECK(mt_raises("iio:EBADF", "device_get_channels", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "device_is_trigger", 1, mt_u64(trig)));

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
