/* SPDX-License-Identifier: MIT */
/* Command group "channel": identity, type and modifier names, attribute lists, the data format,
 * sample conversion, and channel masks (owned masks and the read-only view of a stream's mask). */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_channel);

/* the table "Group channel" of the Command reference */
static const struct mt_arity arities[] = {
	{ "channel_get_device", 1, 1, 1 },
	{ "channel_get_id", 1, 1, 1 },
	{ "channel_get_name", 1, 1, 1 },
	{ "channel_get_label", 1, 1, 1 },
	{ "channel_is_output", 1, 1, 1 },
	{ "channel_is_scan_element", 1, 1, 1 },
	{ "channel_get_index", 1, 1, 1 },
	{ "channel_get_type", 1, 1, 2 },
	{ "channel_get_modifier", 1, 1, 2 },
	{ "hwmon_channel_get_type", 1, 1, 2 },
	{ "channel_get_attrs", 1, 1, 1 },
	{ "channel_get_event_attrs", 1, 1, 1 },
	{ "channel_find_attr", 2, 2, 1 },
	{ "channel_find_event_attr", 2, 2, 1 },
	{ "channel_get_data_format", 1, 1, 1 },
	{ "channel_refresh_format", 1, 1, 0 },
	{ "channel_convert", 2, 2, 1 },
	{ "channel_convert_inverse", 2, 2, 1 },
	{ "channel_enable", 2, 2, 0 },
	{ "channel_disable", 2, 2, 0 },
	{ "channel_is_enabled", 2, 2, 1 },
	{ "create_channels_mask", 1, 1, 1 },
	{ "channels_mask_destroy", 1, 1, 0 },
	{ "channels_mask_get", 1, 1, 1 },
};

#define MAX_IDS 16

static uint64_t ctx;

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

/* [name, value] = cmd(ch): a char row and a real double scalar with these values */
static bool enum_is(const char *cmd, uint64_t ch, const char *name, double value)
{
	mxArray *out[2];
	bool ok = false;
	char *s;

	if (mt_call(2, out, cmd, 1, mt_u64(ch)))
		return false;
	if (mxIsChar(out[0]) && mxGetM(out[0]) == 1 && mxGetClassID(out[1]) == mxDOUBLE_CLASS &&
	    mxIsScalar(out[1]) && !mxIsComplex(out[1])) {
		s = mxArrayToUTF8String(out[0]);
		ok = s && !strcmp(s, name) && mxGetScalar(out[1]) == value;
		if (!ok)
			fprintf(stderr, "%s: '%s', %g (expected '%s', %g)\n", cmd, s ? s : "(null)",
				mxGetScalar(out[1]), name, value);
		mxFree(s);
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	return ok;
}

/* A list command: a 1-by-n uint64 copied to ids (n at most MAX_IDS), each element a borrowed
 * attribute of the context. Returns n, or -1 after a failure. */
static int attrs_of(const char *cmd, uint64_t ch, uint64_t ids[MAX_IDS])
{
	mxArray *out[1];
	int n = -1;

	if (mt_call(1, out, cmd, 1, mt_u64(ch)))
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

static bool field_dbl(const mxArray *s, const char *name, double want)
{
	const mxArray *f = mxGetField(s, 0, name);
	bool ok = f && mxGetClassID(f) == mxDOUBLE_CLASS && mxIsScalar(f) && !mxIsComplex(f) &&
		mxGetScalar(f) == want;

	if (!ok)
		fprintf(stderr, "data format: field %s is not the double %g\n", name, want);
	return ok;
}

static bool field_bool(const mxArray *s, const char *name, bool want)
{
	const mxArray *f = mxGetField(s, 0, name);
	bool ok = f && mxIsLogical(f) && mxIsScalar(f) && mxIsLogicalScalarTrue(f) == want;

	if (!ok)
		fprintf(stderr, "data format: field %s is not the logical %d\n", name, (int)want);
	return ok;
}

struct fmt {
	double length, bits, shift, repeat;
	bool is_signed, is_fully_defined, is_be, with_scale, is_float;
	double scale, offset;
};

/* channel_get_data_format(ch) is a scalar struct with exactly these eleven fields, in this order */
static bool fmt_is(uint64_t ch, struct fmt want)
{
	static const char *const order[] = { "length", "bits", "shift", "repeat", "is_signed",
		"is_fully_defined", "is_be", "with_scale", "is_float", "scale", "offset" };
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "channel_get_data_format", 1, mt_u64(ch)))
		return false;
	ok = mxIsStruct(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 1 && mxGetNumberOfFields(out[0]) == 11;
	for (int i = 0; ok && i < 11; i++)
		ok = !strcmp(mxGetFieldNameByNumber(out[0], i), order[i]);
	ok = ok && field_dbl(out[0], "length", want.length) && field_dbl(out[0], "bits", want.bits) &&
		field_dbl(out[0], "shift", want.shift) && field_dbl(out[0], "repeat", want.repeat) &&
		field_bool(out[0], "is_signed", want.is_signed) &&
		field_bool(out[0], "is_fully_defined", want.is_fully_defined) &&
		field_bool(out[0], "is_be", want.is_be) && field_bool(out[0], "with_scale", want.with_scale) &&
		field_bool(out[0], "is_float", want.is_float) && field_dbl(out[0], "scale", want.scale) &&
		field_dbl(out[0], "offset", want.offset);
	mxDestroyArray(out[0]);
	return ok;
}

/* cmd(ch, uint8 in[0..n-1]) is a 1-by-n real uint8 with the bytes of want */
static bool conv_is(const char *cmd, uint64_t ch, const uint8_t *in, size_t n, const uint8_t *want)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, cmd, 2, mt_u64(ch), mt_bytes(in, n)))
		return false;
	ok = mxGetClassID(out[0]) == mxUINT8_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 1 &&
		mxGetN(out[0]) == n && (!n || !memcmp(mxGetData(out[0]), want, n));
	if (!ok && mxGetClassID(out[0]) == mxUINT8_CLASS) {
		fprintf(stderr, "%s: %zux%zu:", cmd, mxGetM(out[0]), mxGetN(out[0]));
		for (size_t i = 0; i < mxGetNumberOfElements(out[0]); i++)
			fprintf(stderr, " %02x", ((const uint8_t *)mxGetData(out[0]))[i]);
		fputc('\n', stderr);
	}
	mxDestroyArray(out[0]);
	return ok;
}

/* channels_mask_get(mask) is a 1-by-n logical whose elements are the bits of want (bit k = channel k) */
static bool mask_is(uint64_t mask, size_t n, unsigned int want)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "channels_mask_get", 1, mt_u64(mask)))
		return false;
	ok = mxIsLogical(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == n;
	for (size_t i = 0; ok && i < n; i++)
		ok = mxGetLogicals(out[0])[i] == (bool)((want >> i) & 1u);
	mxDestroyArray(out[0]);
	return ok;
}

int main(int argc, char **argv)
{
	static const uint8_t raw2[] = { 0x00, 0xf0 }, host2[] = { 0x00, 0xff };
	static const uint8_t raw2x2[] = { 0x00, 0xf0, 0x00, 0xf0 }, host2x2[] = { 0x00, 0xff, 0x00, 0xff };
	static const uint8_t three[] = { 0x00, 0xf0, 0x00 };
	static const uint8_t raw1[] = { 0x12, 0x34, 0x56, 0x78 }, host1[] = { 0x56, 0x34, 0x12, 0x00 };
	static const uint8_t back1[] = { 0x12, 0x34, 0x56, 0x00 };
	static const uint8_t quat_raw[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	static const uint8_t be64[] = { 0x3f, 0xf0, 0, 0, 0, 0, 0, 0 }, le64[] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x3f };
	struct mt_fixture fx;
	uint64_t rx, tx, phy, d4, hw, trig;
	uint64_t v0i, v0q, v1q, v2, tx_v0i, tx_alt, phy_in, phy_out, accel, temp1, in1, d4_v1, d4_v2, d4_quat, d4_f64;
	uint64_t ids[MAX_IDS], again[MAX_IDS], scale, m, m2, mtx, view, bs, a;
	const struct iio_buffer_stream *stream;
	mxArray *out[2];
	size_t before;
	const char *s;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_channel, arities, sizeof(arities) / sizeof(arities[0]));

	ctx = mt_open_context(fx.uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	phy = dp_dev(ctx, "emu-phy");
	d4 = dp_dev(ctx, "iio:device4");
	hw = dp_dev(ctx, "hwmon0");
	trig = dp_dev(ctx, "trigger0");
	v0i = dp_chn(rx, "voltage0_i", false);
	v0q = dp_chn(rx, "voltage0_q", false);
	v1q = dp_chn(rx, "voltage1_q", false);
	v2 = dp_chn(rx, "voltage2", false);
	tx_v0i = dp_chn(tx, "voltage0_i", true);
	tx_alt = dp_chn(tx, "altvoltage0", true);
	phy_in = dp_chn(phy, "voltage0", false);
	phy_out = dp_chn(phy, "voltage0", true);
	accel = dp_chn(phy, "accel_x", false);
	temp1 = dp_chn(hw, "temp1", false);
	in1 = dp_chn(hw, "in1", false);
	d4_v1 = dp_chn(d4, "voltage1", false);
	d4_v2 = dp_chn(d4, "voltage2", false);
	d4_quat = dp_chn(d4, "rot_quaternion", false);
	d4_f64 = dp_chn(d4, "voltage4", false);

	/* ---- identity, one command per value ---- */
	before = gw_handle_count();
	CHECK(mt_u64_of("channel_get_device", 1, mt_u64(v2)) == rx);
	CHECK(mt_u64_of("channel_get_device", 1, mt_u64(tx_alt)) == tx);
	CHECK(mt_u64_of("channel_get_device", 1, mt_u64(temp1)) == hw);
	CHECK(gw_handle_count() == before);         /* the device ids existed: nothing new */
	s = mt_str_of("channel_get_id", 1, mt_u64(v2));
	CHECK(s && !strcmp(s, "voltage2"));
	s = mt_str_of("channel_get_name", 1, mt_u64(v2));
	CHECK(s && !strcmp(s, "aux"));
	s = mt_str_of("channel_get_label", 1, mt_u64(v2));
	CHECK(s && !strcmp(s, "aux-adc"));
	CHECK(mt_bool_of("channel_is_output", 1, mt_u64(v2)) == false);
	CHECK(mt_bool_of("channel_is_scan_element", 1, mt_u64(v2)) == true);
	CHECK(mt_dbl_of("channel_get_index", 1, mt_u64(v2)) == 4);
	CHECK(enum_is("channel_get_type", v2, "IIO_VOLTAGE", 0));
	CHECK(enum_is("channel_get_modifier", v2, "IIO_NO_MOD", 0));

	s = mt_str_of("channel_get_id", 1, mt_u64(v0i));
	CHECK(s && !strcmp(s, "voltage0_i"));
	CHECK(is_null("channel_get_name", v0i));     /* a missing name or label is [], not '' */
	CHECK(is_null("channel_get_label", v0i));
	CHECK(mt_dbl_of("channel_get_index", 1, mt_u64(v0i)) == 0);
	CHECK(enum_is("channel_get_modifier", v0i, "IIO_MOD_I", 32));
	CHECK(enum_is("channel_get_modifier", v0q, "IIO_MOD_Q", 33));

	CHECK(mt_dbl_of("channel_get_index", 1, mt_u64(tx_alt)) == -2);
	CHECK(mt_bool_of("channel_is_scan_element", 1, mt_u64(tx_alt)) == false);
	CHECK(enum_is("channel_get_type", tx_alt, "IIO_ALTVOLTAGE", 15));
	s = mt_str_of("channel_get_name", 1, mt_u64(tx_alt));
	CHECK(s && !strcmp(s, "TX1_I_F1"));
	CHECK(is_null("channel_get_label", tx_alt));
	CHECK(mt_bool_of("channel_is_output", 1, mt_u64(tx_alt)) == true);
	CHECK(mt_bool_of("channel_is_output", 1, mt_u64(phy_out)) == true);
	CHECK(mt_bool_of("channel_is_output", 1, mt_u64(phy_in)) == false);

	{       /* the order the M tests rely on: emu-phy has no scan element and keeps the file order */
		static const char *const order[] = { "voltage0", "voltage0", "altvoltage0", "altvoltage1", "temp0", "accel_x" };
		static const bool is_out[] = { false, true, true, true, false, false };
		const struct iio_device *d = gw_handle_ptr(phy, GW_T_DEVICE);

		CHECK(iio_device_get_channels_count(d) == 6);
		for (unsigned int k = 0; k < 6 && iio_device_get_channels_count(d) == 6; k++) {
			gw_id id = 0;

			CHECK(gw_handle_borrow(GW_T_CHANNEL, iio_device_get_channel(d, k), ctx, &id) == 0);
			s = mt_str_of("channel_get_id", 1, mt_u64(id));
			CHECK(s && !strcmp(s, order[k]));
			CHECK(mt_bool_of("channel_is_output", 1, mt_u64(id)) == is_out[k]);
			CHECK(mt_bool_of("channel_is_scan_element", 1, mt_u64(id)) == false);
			CHECK(mt_dbl_of("channel_get_index", 1, mt_u64(id)) < 0);
		}
		s = mt_str_of("channel_get_type", 1, mt_u64(dp_chn(phy, "temp0", false)));
		CHECK(s && !strcmp(s, "IIO_TEMP"));
	}
	CHECK(enum_is("channel_get_type", accel, "IIO_ACCEL", 3));
	CHECK(enum_is("channel_get_modifier", accel, "IIO_MOD_X", 1));

	/* hwmon: the same number under the name of the hwmon table */
	CHECK(enum_is("hwmon_channel_get_type", temp1, "HWMON_TEMP", 3));
	CHECK(enum_is("channel_get_type", temp1, "IIO_ACCEL", 3));
	CHECK(enum_is("hwmon_channel_get_type", in1, "HWMON_VOLTAGE", 0));

	/* one output: the name only */
	CHECK(mt_call(1, out, "channel_get_type", 1, mt_u64(v2)) == 0);
	CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == strlen("IIO_VOLTAGE"));
	mxDestroyArray(out[0]);
	s = mt_str_of("channel_get_modifier", 1, mt_u64(v0i));
	CHECK(s && !strcmp(s, "IIO_MOD_I"));
	s = mt_str_of("hwmon_channel_get_type", 1, mt_u64(temp1));
	CHECK(s && !strcmp(s, "HWMON_TEMP"));
	mt_ok("channel_get_type", 1, mt_u64(v2));    /* no output requested */

	/* ---- attributes ---- */
	CHECK(attrs_of("channel_get_attrs", v0i, ids) == 2);
	CHECK(gw_handle_ptr(ids[1], GW_T_ATTR) == iio_channel_get_attr(gw_handle_ptr(v0i, GW_T_CHANNEL), 1));
	CHECK(gw_handle_ptr(ids[0], GW_T_ATTR) == iio_channel_get_attr(gw_handle_ptr(v0i, GW_T_CHANNEL), 0));
	before = gw_handle_count();
	CHECK(attrs_of("channel_get_attrs", v0i, again) == 2);
	CHECK(!memcmp(ids, again, 2 * sizeof(ids[0])));
	CHECK(gw_handle_count() == before);          /* the same object has the same id */
	CHECK(mt_call(1, out, "channel_get_attrs", 1, mt_u64(v0q)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT64_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);
	CHECK(mt_call(1, out, "channel_get_event_attrs", 1, mt_u64(v0q)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT64_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);

	memset(ids, 0, sizeof(ids));
	CHECK(attrs_of("channel_get_attrs", v2, ids) == 3);
	CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[0], GW_T_ATTR)), "offset"));
	CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[1], GW_T_ATTR)), "raw"));
	CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[2], GW_T_ATTR)), "scale"));
	scale = mt_u64_of("channel_find_attr", 2, mt_u64(v2), mt_str("scale"));
	CHECK(scale != 0 && scale == ids[2]);
	CHECK(gw_handle_type(scale) == GW_T_ATTR && gw_handle_parent(scale) == ctx && !gw_handle_owned(scale));
	CHECK(mt_u64_of("channel_find_attr", 2, mt_u64(v2), mt_str("nope")) == 0);
	CHECK(mt_u64_of("channel_find_attr", 2, mt_u64(v2), mt_str("")) == 0);
	CHECK(mt_u64_of("channel_find_event_attr", 2, mt_u64(v2), mt_str("scale")) == 0);

	a = mt_u64_of("channel_find_attr", 2, mt_u64(phy_in), mt_str("hardwaregain"));
	CHECK(a != 0);
	CHECK(mt_u64_of("channel_find_attr", 2, mt_u64(phy_in), mt_str("RX1_hardwaregain")) == a);
	CHECK(mt_u64_of("channel_find_attr", 2, mt_u64(phy_out), mt_str("hardwaregain")) != a);
	CHECK(attrs_of("channel_get_event_attrs", phy_in, ids) == 3);
	a = mt_u64_of("channel_find_event_attr", 2, mt_u64(phy_in), mt_str("thresh_rising_en"));
	CHECK(a != 0 && (a == ids[0] || a == ids[1] || a == ids[2]));
	CHECK(gw_handle_ptr(ids[2], GW_T_ATTR) ==
		iio_channel_get_event_attr(gw_handle_ptr(phy_in, GW_T_CHANNEL), 2));
	CHECK(mt_u64_of("channel_find_attr", 2, mt_u64(phy_in), mt_str("thresh_rising_en")) == 0);
	CHECK(mt_u64_of("channel_find_event_attr", 2, mt_u64(phy_in), mt_str("hardwaregain")) == 0);
	CHECK(attrs_of("channel_get_event_attrs", accel, ids) == 1);

	{       /* text with an embedded NUL is refused, not cut to "scale" or "thresh_rising_en" */
		mxArray *name = mt_str("scaleXx"), *ev = mt_str("thresh_rising_enX");

		((mxChar *)mxGetData(name))[5] = 0;
		((mxChar *)mxGetData(ev))[16] = 0;
		CHECK(mt_raises("iio:EINVAL", "channel_find_attr", 2, mt_u64(v2), name));
		CHECK(mt_raises("iio:EINVAL", "channel_find_event_attr", 2, mt_u64(phy_in), ev));
	}
	CHECK(mt_raises("iio:EINVAL", "channel_find_attr", 2, mt_u64(v2), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "channel_find_attr", 2, mt_u64(v2), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "channel_find_event_attr", 2, mt_u64(v2), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "channel_find_attr", 1, mt_u64(v2)));

	/* ---- data format: a copy ---- */
	CHECK(fmt_is(v0i, (struct fmt){ 16, 12, 0, 1, true, false, false, false, false, 0, 0 }));
	CHECK(fmt_is(v2, (struct fmt){ 16, 14, 2, 1, true, false, false, true, false, 0.25, -100 }));
	CHECK(fmt_is(tx_v0i, (struct fmt){ 16, 16, 0, 1, true, true, false, false, false, 0, 0 }));
	CHECK(fmt_is(d4_v1, (struct fmt){ 32, 24, 8, 1, false, false, true, false, false, 0, 0 }));
	CHECK(fmt_is(d4_quat, (struct fmt){ 16, 16, 0, 4, true, true, false, false, false, 0, 0 }));
	CHECK(fmt_is(d4_f64, (struct fmt){ 64, 64, 0, 1, false, true, true, false, true, 0, 0 }));
	/* not a scan element: no storage format (length 0); emu leaves repeat at 1 */
	CHECK(fmt_is(phy_in, (struct fmt){ 0, 0, 0, 1, false, false, false, false, false, 0, 0 }));
	CHECK(mt_raises("iio:ENOSYS", "channel_refresh_format", 1, mt_u64(v0i)));
	CHECK(mt_raises("iio:EINVAL", "channel_refresh_format", 2, mt_u64(v0i), mt_dbl(1)));

	/* ---- convert: one C call per sample of length/8 * repeat bytes ---- */
	CHECK(conv_is("channel_convert", d4_v2, raw2, 2, host2));
	CHECK(conv_is("channel_convert_inverse", d4_v2, host2, 2, raw2));
	CHECK(conv_is("channel_convert", d4_v2, raw2x2, 4, host2x2));
	CHECK(conv_is("channel_convert_inverse", d4_v2, host2x2, 4, raw2x2));
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(d4_v2), mt_bytes(three, 3)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert_inverse", 2, mt_u64(d4_v2), mt_bytes(three, 3)));
	CHECK(conv_is("channel_convert", d4_v1, raw1, 4, host1));
	CHECK(conv_is("channel_convert_inverse", d4_v1, host1, 4, back1));
	CHECK(conv_is("channel_convert", d4_f64, be64, 8, le64));
	CHECK(conv_is("channel_convert_inverse", d4_f64, le64, 8, be64));
	/* repeat 4: a sample is 8 bytes, 4 are not a whole sample */
	CHECK(conv_is("channel_convert", d4_quat, quat_raw, 8, quat_raw));
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(d4_quat), mt_bytes(quat_raw, 4)));
	/* a column vector is a vector too; the result is a row */
	out[1] = mxCreateNumericMatrix(2, 1, mxUINT8_CLASS, mxREAL);
	memcpy(mxGetData(out[1]), raw2, 2);
	CHECK(mt_call(1, out, "channel_convert", 2, mt_u64(d4_v2), out[1]) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 2 &&
		!memcmp(mxGetData(out[0]), host2, 2));
	mxDestroyArray(out[0]);
	/* empty input: 1-by-0 uint8 */
	CHECK(conv_is("channel_convert", d4_v2, NULL, 0, NULL));
	CHECK(conv_is("channel_convert_inverse", d4_v2, NULL, 0, NULL));
	CHECK(mt_call(1, out, "channel_convert", 2, mt_u64(d4_v2),
		mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 0);
	mxDestroyArray(out[0]);
	/* not a scan element (format length 0): an error, not a division by zero */
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(phy_in), mt_bytes(raw2, 2)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert_inverse", 2, mt_u64(phy_in), mt_bytes(raw2, 2)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(phy_in), mt_bytes(raw1, 4)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert_inverse", 2, mt_u64(phy_in), mt_bytes(raw1, 4)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(phy_in), mt_bytes(NULL, 0)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert_inverse", 2, mt_u64(phy_in), mt_bytes(NULL, 0)));
	/* not bytes */
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(d4_v2), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert", 2, mt_u64(d4_v2),
		mxCreateNumericMatrix(2, 2, mxUINT8_CLASS, mxREAL)));
	CHECK(mt_raises("iio:EINVAL", "channel_convert_inverse", 2, mt_u64(d4_v2), mt_str("ab")));

	/* ---- masks ---- */
	before = gw_handle_count();
	m = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	CHECK(m != 0 && gw_handle_count() == before + 1);
	CHECK(gw_handle_type(m) == GW_T_MASK && gw_handle_parent(m) == rx && gw_handle_owned(m));
	CHECK(mask_is(m, 5, 0x00));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(m));
	mt_ok("channel_enable", 2, mt_u64(v0q), mt_u64(m));
	CHECK(mask_is(m, 5, 0x03));
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(v0i), mt_u64(m)) == true);
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(v2), mt_u64(m)) == false);
	mt_ok("channel_disable", 2, mt_u64(v0i), mt_u64(m));
	CHECK(mask_is(m, 5, 0x02));
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(v0i), mt_u64(m)) == false);
	mt_ok("channel_disable", 2, mt_u64(v0i), mt_u64(m));          /* twice: no change */
	mt_ok("channel_enable", 2, mt_u64(v2), mt_u64(m));
	CHECK(mask_is(m, 5, 0x12));
	mt_ok("channel_disable", 2, mt_u64(v2), mt_u64(m));
	CHECK(mask_is(m, 5, 0x02));

	/* an object of another device never reaches libiio */
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(tx_v0i), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "channel_disable", 2, mt_u64(tx_v0i), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "channel_is_enabled", 2, mt_u64(tx_v0i), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(temp1), mt_u64(m)));
	CHECK(mask_is(m, 5, 0x02));
	/* a channel that is not a scan element cannot be enabled; asking about it is allowed */
	mtx = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	CHECK(mtx != 0 && mtx != m);
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(tx_alt), mt_u64(mtx)));
	CHECK(mt_raises("iio:EINVAL", "channel_disable", 2, mt_u64(tx_alt), mt_u64(mtx)));
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(tx_alt), mt_u64(mtx)) == false);
	mt_ok("channel_enable", 2, mt_u64(tx_v0i), mt_u64(mtx));
	CHECK(mask_is(mtx, 5, 0x01));
	CHECK(mt_raises("iio:EINVAL", "channel_is_enabled", 2, mt_u64(v0i), mt_u64(mtx)));
	/* a device without channels has no mask */
	before = gw_handle_count();
	CHECK(mt_raises("iio:EINVAL", "create_channels_mask", 1, mt_u64(trig)));
	CHECK(gw_handle_count() == before);
	/* a mask of a device without scan elements exists and stays empty */
	m2 = mt_u64_of("create_channels_mask", 1, mt_u64(phy));
	CHECK(m2 != 0 && mask_is(m2, 6, 0x00));
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(phy_in), mt_u64(m2)));
	CHECK(mask_is(m2, 6, 0x00));
	mt_ok("channels_mask_destroy", 1, mt_u64(m2));
	/* wrong handle types */
	CHECK(mt_raises("iio:EBADF", "create_channels_mask", 1, mt_u64(v0i)));
	CHECK(mt_raises("iio:EBADF", "create_channels_mask", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "channel_enable", 2, mt_u64(v0i), mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_enable", 2, mt_u64(m), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "channel_disable", 2, mt_u64(v0i), mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "channel_is_enabled", 2, mt_u64(rx), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "channels_mask_get", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channels_mask_destroy", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EINVAL", "channels_mask_destroy", 1, mt_dbl(1)));
	CHECK(gw_handle_type(rx) == GW_T_DEVICE);

	/* result first: an allocation failure leaves no mask that MATLAB never heard of */
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "create_channels_mask", 1, mt_u64(rx)) >= 1);
	CHECK(gw_handle_count() == before + 1);      /* the attempt that succeeded; released with the context */
	CHECK(mt_oom_sweep(1, "channels_mask_get", 1, mt_u64(m)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_get_attrs", 1, mt_u64(v2)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_get_event_attrs", 1, mt_u64(phy_in)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_find_attr", 2, mt_u64(v2), mt_str("scale")) >= 1);
	CHECK(mt_oom_sweep(1, "channel_find_event_attr", 2, mt_u64(phy_in), mt_str("thresh_rising_en")) >= 1);
	CHECK(mt_oom_sweep(1, "channel_get_data_format", 1, mt_u64(v2)) >= 1);
	CHECK(mt_oom_sweep(2, "channel_get_type", 1, mt_u64(v2)) >= 2);
	CHECK(mt_oom_sweep(2, "channel_get_modifier", 1, mt_u64(v0i)) >= 2);
	CHECK(mt_oom_sweep(2, "hwmon_channel_get_type", 1, mt_u64(temp1)) >= 2);
	CHECK(mt_oom_sweep(1, "channel_convert", 2, mt_u64(d4_v2), mt_bytes(raw2x2, 4)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_convert_inverse", 2, mt_u64(d4_v2), mt_bytes(host2x2, 4)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_get_device", 1, mt_u64(v1q)) >= 1);
	CHECK(mt_oom_sweep(1, "channel_get_name", 1, mt_u64(v2)) >= 1);
	CHECK(gw_handle_count() == before + 1);

	/* the mask of a buffer stream: a read-only view */
	bs = dp_bufstream(dp_buf(rx, 0), m);        /* voltage0_q */
	CHECK(bs != 0);
	stream = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	view = 0;
	CHECK(stream && gw_handle_borrow(GW_T_MASK, iio_buffer_stream_get_channels_mask(stream), bs, &view) == 0);
	CHECK(view != 0 && !gw_handle_owned(view));
	CHECK(mask_is(view, 5, 0x02));
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(v0q), mt_u64(view)) == true);
	CHECK(mt_bool_of("channel_is_enabled", 2, mt_u64(v0i), mt_u64(view)) == false);
	CHECK(mt_raises("iio:EPERM", "channel_enable", 2, mt_u64(v0i), mt_u64(view)));
	CHECK(mt_raises("iio:EPERM", "channel_disable", 2, mt_u64(v0q), mt_u64(view)));
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(tx_v0i), mt_u64(view)));
	CHECK(mt_raises("iio:EINVAL", "channel_is_enabled", 2, mt_u64(tx_v0i), mt_u64(view)));
	CHECK(mask_is(view, 5, 0x02));
	mt_ok("channels_mask_destroy", 1, mt_u64(view));          /* not the caller's to destroy */
	CHECK(gw_handle_type(view) == GW_T_MASK);
	CHECK(mask_is(view, 5, 0x02));
	gw_handle_destroy(bs);
	{       /* the order of the checks: "read-only" (EPERM) before "not a scan element" (EINVAL) */
		uint64_t tmask = dp_mask(tx, true, 1, "voltage0_i");
		uint64_t tbs = dp_bufstream(dp_buf(tx, 0), tmask), tview = 0;
		struct iio_buffer_stream *ts = gw_handle_ptr(tbs, GW_T_BUFFER_STREAM);

		CHECK(ts && gw_handle_borrow(GW_T_MASK, iio_buffer_stream_get_channels_mask(ts), tbs, &tview) == 0);
		CHECK(mt_raises("iio:EPERM", "channel_enable", 2, mt_u64(tx_alt), mt_u64(tview)));
		CHECK(mt_raises("iio:EPERM", "channel_disable", 2, mt_u64(tx_alt), mt_u64(tview)));
		CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(tx_alt), mt_u64(tmask)));
		gw_handle_destroy(tbs);
		mt_ok("channels_mask_destroy", 1, mt_u64(tmask));
	}
	CHECK(mt_raises("iio:EBADF", "channels_mask_get", 1, mt_u64(view)));
	CHECK(mt_raises("iio:EBADF", "channel_is_enabled", 2, mt_u64(v0q), mt_u64(view)));
	mt_ok("channels_mask_destroy", 1, mt_u64(view));          /* stale: silent */

	/* destroy: twice, then every use is EBADF */
	before = gw_handle_count();
	mt_ok("channels_mask_destroy", 1, mt_u64(m));
	CHECK(gw_handle_count() == before - 1);
	mt_ok("channels_mask_destroy", 1, mt_u64(m));
	mt_ok("channels_mask_destroy", 1, mt_u64(0));
	CHECK(gw_handle_count() == before - 1);
	CHECK(mt_raises("iio:EBADF", "channels_mask_get", 1, mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "channel_enable", 2, mt_u64(v0i), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "channel_is_enabled", 2, mt_u64(v0i), mt_u64(m)));

	/* a handle of another type, none, a wrong class */
	CHECK(mt_raises("iio:EBADF", "channel_get_device", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_id", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_name", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_label", 1, mt_u64(scale)));
	CHECK(mt_raises("iio:EBADF", "channel_is_output", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "channel_is_scan_element", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_index", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_type", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_modifier", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "hwmon_channel_get_type", 1, mt_u64(hw)));
	CHECK(mt_raises("iio:EBADF", "channel_get_attrs", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_get_event_attrs", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_find_attr", 2, mt_u64(rx), mt_str("scale")));
	CHECK(mt_raises("iio:EBADF", "channel_find_event_attr", 2, mt_u64(rx), mt_str("scale")));
	CHECK(mt_raises("iio:EBADF", "channel_get_data_format", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_refresh_format", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "channel_convert", 2, mt_u64(d4), mt_bytes(raw2, 2)));
	CHECK(mt_raises("iio:EBADF", "channel_convert_inverse", 2, mt_u64(d4), mt_bytes(raw2, 2)));
	CHECK(mt_raises("iio:EINVAL", "channel_get_id", 1, mt_dbl((double)v2)));

	/* the context goes with masks still open (mtx and the one of the sweep): nothing leaks */
	CHECK(gw_handle_type(mtx) == GW_T_MASK);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EBADF", "channel_get_id", 1, mt_u64(v2)));
	CHECK(mt_raises("iio:EBADF", "channels_mask_get", 1, mt_u64(mtx)));
	CHECK(mt_raises("iio:EBADF", "create_channels_mask", 1, mt_u64(rx)));
	mt_ok("channels_mask_destroy", 1, mt_u64(mtx));

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
