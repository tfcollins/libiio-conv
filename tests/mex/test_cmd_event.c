/* SPDX-License-Identifier: MIT */
/* Command group "event": decoding of event ids (on emu, with synthetic ids), and the event stream
 * of the fake backend (two events, then "no event"). emu has no event streams (-ENOSYS). */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_event);

/* the table "Group event" of the Command reference */
static const struct mt_arity arities[] = {
	{ "device_create_event_stream", 1, 1, 1 },
	{ "event_stream_destroy", 1, 1, 0 },
	{ "event_stream_read", 2, 2, 3 },
	{ "event_get_type", 1, 1, 2 },
	{ "event_get_direction", 1, 1, 2 },
	{ "event_get_channel", 3, 3, 1 },
};

/* [name, value] = cmd(uint64 id) is this pair: a char row and a double scalar */
static bool pair_is(const char *cmd, uint64_t id, const char *name, double value)
{
	mxArray *out[2] = { NULL, NULL };
	char *s;
	bool ok;

	if (mt_call(2, out, cmd, 1, mt_u64(id)))
		return false;
	s = mxIsChar(out[0]) && mxGetM(out[0]) == 1 ? mxArrayToUTF8String(out[0]) : NULL;
	ok = s && !strcmp(s, name);
	if (s)
		mxFree(s);
	ok = ok && mxGetClassID(out[1]) == mxDOUBLE_CLASS && !mxIsComplex(out[1]) && mxIsScalar(out[1]) &&
		mxGetScalar(out[1]) == value;
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	return ok;
}

/* [tf, id, timestamp] = event_stream_read(es, nonblock): a logical scalar, a real uint64 scalar
 * and a real int64 scalar with these values */
static bool read_is(uint64_t es, bool nonblock, bool tf, uint64_t id, int64_t timestamp)
{
	mxArray *out[3] = { NULL, NULL, NULL };
	bool ok;

	if (mt_call(3, out, "event_stream_read", 2, mt_u64(es), mt_bool(nonblock)))
		return false;
	ok = mxIsLogical(out[0]) && mxIsScalar(out[0]) && mxIsLogicalScalarTrue(out[0]) == tf;
	ok = ok && mxGetClassID(out[1]) == mxUINT64_CLASS && !mxIsComplex(out[1]) && mxIsScalar(out[1]) &&
		*(const uint64_t *)mxGetData(out[1]) == id;
	ok = ok && mxGetClassID(out[2]) == mxINT64_CLASS && !mxIsComplex(out[2]) && mxIsScalar(out[2]) &&
		*(const int64_t *)mxGetData(out[2]) == timestamp;
	for (int i = 0; i < 3; i++)
		mxDestroyArray(out[i]);
	return ok;
}

static uint64_t chan_of(uint64_t id, uint64_t dev, bool diff)
{
	return mt_u64_of("event_get_channel", 3, mt_u64(id), mt_u64(dev), mt_bool(diff));
}

struct row {
	uint64_t id;
	const char *type;
	double type_id;
	const char *dir;
	double dir_id;
	const char *chn;        /* channel id; NULL: the command returns 0 */
	const char *chn_diff;
};

static const struct row rows[] = {
	{ UINT64_C(0x0001000000000000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_RISING", 1, "voltage0", NULL },
	{ UINT64_C(0x0102010300000000), "IIO_EV_TYPE_MAG", 1, "IIO_EV_DIR_FALLING", 2, "accel_x", NULL },
	{ UINT64_C(0x0000000900000000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, "temp0", NULL },
	/* pressure: no such channel (libiio returns the last channel, accel_x) */
	{ UINT64_C(0x0000001100000000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, NULL, NULL },
	/* voltage 5: the type exists, the number does not (libiio: the last channel again) */
	{ UINT64_C(0x0000000000000005), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, NULL, NULL },
	/* a number that is not below the channel count: libiio warns and returns NULL */
	{ UINT64_C(0x0000000000000007), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, NULL, NULL },
	/* differential, altvoltage 0 against altvoltage 1 */
	{ UINT64_C(0x0080000f00010000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, "altvoltage0", "altvoltage1" },
	/* acceleration with modifier Y: the last channel, accel_x, has the type but not the modifier */
	{ UINT64_C(0x0000020300000000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, NULL, NULL },
	/* voltage with modifier X: accel_x has the modifier and the number but not the type */
	{ UINT64_C(0x0000010000000000), "IIO_EV_TYPE_THRESH", 0, "IIO_EV_DIR_EITHER", 0, NULL, NULL },
	/* type 200 has no name */
	{ UINT64_C(0xC800000000000000), "UNKNOWN", 200, "IIO_EV_DIR_EITHER", 0, "voltage0", NULL },
};

static void row_check(size_t i, const char *what, bool ok)
{
	if (!ok)
		fprintf(stderr, "row %zu of the event table: %s\n", i + 1, what);
	CHECK(ok);
}

/* event_stream_read(es, true) with three outputs, repeated with the 1st, 2nd, ... mx allocation
 * failing until one attempt gets through. Returns the number of failed attempts when that
 * attempt returned this event, else -1. An event that a failed attempt had taken from the
 * stream would be missing here: the results are created before the read. */
static int read_under_oom(uint64_t es, uint64_t id, int64_t timestamp)
{
	for (unsigned int failed = 0; failed < 100; failed++) {
		mxArray *out[3] = { NULL, NULL, NULL };
		bool ok;
		int raised;

		mock_fail_alloc(failed + 1);
		raised = mt_call(3, out, "event_stream_read", 2, mt_u64(es), mt_bool(true));
		mock_fail_alloc(0);
		if (raised && mock_alloc_failed())
			continue;
		if (raised)
			return -1;
		ok = mxIsLogicalScalarTrue(out[0]) && *(const uint64_t *)mxGetData(out[1]) == id &&
			*(const int64_t *)mxGetData(out[2]) == timestamp;
		for (int i = 0; i < 3; i++)
			mxDestroyArray(out[i]);
		return ok ? (int)failed : -1;
	}
	return -1;
}

/* the channel of phy that the row names; the altvoltage channels are outputs */
static uint64_t row_chn(uint64_t phy, const char *id)
{
	return id ? dp_chn(phy, id, !strncmp(id, "altvoltage", 10)) : 0;
}

/* ---- emu: no event streams; decoding with synthetic ids ---- */
static void test_emu(const struct mt_fixture *fx)
{
	uint64_t ctx, phy, rx, in0, out0;
	const char *name;
	size_t before;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	phy = dp_dev(ctx, "emu-phy");
	rx = dp_dev(ctx, "emu-rx");
	before = gw_handle_count();
	CHECK(mt_raises("iio:ENOSYS", "device_create_event_stream", 1, mt_u64(phy)));
	CHECK(gw_handle_count() == before);

	in0 = dp_chn(phy, "voltage0", false);
	out0 = dp_chn(phy, "voltage0", true);
	CHECK(in0 != out0);
	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		const struct row *r = &rows[i];
		uint64_t want = row_chn(phy, r->chn), want_diff = row_chn(phy, r->chn_diff);

		row_check(i, "event_get_type", pair_is("event_get_type", r->id, r->type, r->type_id));
		row_check(i, "event_get_direction", pair_is("event_get_direction", r->id, r->dir, r->dir_id));
		row_check(i, "event_get_channel, diff false", chan_of(r->id, phy, false) == want);
		row_check(i, "event_get_channel, diff true", chan_of(r->id, phy, true) == want_diff);
	}
	/* the input channel, not the output channel of the same id; a borrowed child of the context */
	CHECK(chan_of(rows[0].id, phy, false) == in0);
	CHECK(gw_handle_type(in0) == GW_T_CHANNEL && gw_handle_parent(in0) == ctx && !gw_handle_owned(in0));
	/* a device that has no channel of the event (emu-rx: voltage0_i ... are not "voltage 0";
	 * voltage2 is "voltage 2") */
	CHECK(chan_of(UINT64_C(0x0000000000000002), rx, false) == dp_chn(rx, "voltage2", false));
	CHECK(chan_of(UINT64_C(0x0000000000000001), rx, false) == 0);
	CHECK(chan_of(UINT64_C(0x0000000900000000), rx, false) == 0);
	/* a negative channel number (bit 15): no channel */
	CHECK(chan_of(UINT64_C(0x0000000000008000), phy, false) == 0);
	/* the differential bit with diff = false still names the first channel; without it diff = true is 0 */
	CHECK(chan_of(UINT64_C(0x0000000f00010000), phy, true) == 0);
	/* one output is enough; the name alone */
	name = mt_str_of("event_get_type", 1, mt_u64(UINT64_C(0x0800000000000000)));
	CHECK(name && !strcmp(name, "IIO_EV_TYPE_FAULT"));
	name = mt_str_of("event_get_direction", 1, mt_u64(UINT64_C(0x0006000000000000)));
	CHECK(name && !strcmp(name, "IIO_EV_DIR_FAULT_OPENWIRE"));
	/* bit 55 is the differential flag, not part of the direction */
	CHECK(pair_is("event_get_direction", UINT64_C(0x00ff000000000000), "UNKNOWN", 127));
	CHECK(pair_is("event_get_type", UINT64_C(0xff00000000000000), "UNKNOWN", 255));

	/* arguments */
	CHECK(mt_raises("iio:EINVAL", "event_get_type", 1, mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "event_get_direction", 1, mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "event_get_channel", 3, mt_dbl(0), mt_u64(phy), mt_bool(false)));
	CHECK(mt_raises("iio:EINVAL", "event_get_type", 1, mt_i64(0)));
	CHECK(mt_raises("iio:EINVAL", "event_get_type", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "event_get_channel", 3, mt_u64(0), mt_u64(phy), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "event_get_channel", 2, mt_u64(0), mt_u64(phy)));
	CHECK(mt_raises("iio:EBADF", "event_get_channel", 3, mt_u64(0), mt_u64(ctx), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "event_get_channel", 3, mt_u64(0), mt_u64(in0), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "event_get_channel", 3, mt_u64(0), mt_u64(0), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "device_create_event_stream", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "device_create_event_stream", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "device_create_event_stream", 1, mt_dbl((double)phy)));
	CHECK(mt_raises("iio:EBADF", "event_stream_read", 2, mt_u64(phy), mt_bool(true)));
	CHECK(mt_raises("iio:EBADF", "event_stream_read", 2, mt_u64(0), mt_bool(true)));
	CHECK(mt_raises("iio:EBADF", "event_stream_destroy", 1, mt_u64(phy)));    /* live, another type */
	CHECK(gw_handle_type(phy) == GW_T_DEVICE);
	CHECK(mt_raises("iio:EINVAL", "event_stream_destroy", 1, mt_dbl(0)));
	mt_ok("event_stream_destroy", 1, mt_u64(0));

	/* allocation failures in the decoders: nothing is registered twice, nothing leaks */
	before = gw_handle_count();
	CHECK(mt_oom_sweep(2, "event_get_type", 1, mt_u64(rows[1].id)) >= 1);
	CHECK(mt_oom_sweep(2, "event_get_direction", 1, mt_u64(rows[1].id)) >= 1);
	CHECK(mt_oom_sweep(1, "event_get_channel", 3, mt_u64(rows[1].id), mt_u64(phy), mt_bool(false)) >= 1);
	CHECK(gw_handle_count() == before);
	CHECK(mt_oom_sweep(1, "device_create_event_stream", 1, mt_u64(phy)) >= 1);    /* fails after the sweep too */
	CHECK(gw_handle_count() == before);

	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "event_get_channel", 3, mt_u64(rows[0].id), mt_u64(phy), mt_bool(false)));
	CHECK(mt_raises("iio:EBADF", "device_create_event_stream", 1, mt_u64(phy)));
	CHECK(gw_handle_count() == 0);
}

/* ---- fake backend: an event stream on emu-phy only ---- */
static void test_fake(const struct mt_fixture *fx)
{
	uint64_t ctx, phy, rx, es, es2;
	size_t before, len = strlen(fx->xml);
	char xml[PATH_MAX];     /* fake_create_context() takes a path of less than PATH_MAX bytes */
	mxArray *out[1];

	CHECK(len < sizeof(xml));
	if (len >= sizeof(xml))
		return;
	memcpy(xml, fx->xml, len + 1);
	ctx = dp_open_fake(xml);
	CHECK(ctx != 0);
	phy = dp_dev(ctx, "emu-phy");
	rx = dp_dev(ctx, "emu-rx");
	before = gw_handle_count();
	CHECK(mt_raises("iio:ENODEV", "device_create_event_stream", 1, mt_u64(rx)));
	CHECK(gw_handle_count() == before);

	es = mt_u64_of("device_create_event_stream", 1, mt_u64(phy));
	CHECK(es != 0);
	CHECK(gw_handle_type(es) == GW_T_EVENT_STREAM);
	CHECK(gw_handle_owned(es));
	CHECK(gw_handle_parent(es) == phy);
	CHECK(gw_handle_root(es) == ctx);
	CHECK(gw_handle_count() == before + 1);

	CHECK(mt_raises("iio:EINVAL", "event_stream_read", 2, mt_u64(es), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "event_stream_read", 1, mt_u64(es)));
	CHECK(read_is(es, true, true, UINT64_C(0x0001000000000000), 1000));
	/* an allocation that fails does not cost an event: the second one is still there */
	CHECK(read_under_oom(es, UINT64_C(0x0102010300000000), 1001) >= 3);
	CHECK(read_is(es, true, false, 0, 0));
	/* and with "no event" as the answer */
	CHECK(mt_oom_sweep(3, "event_stream_read", 2, mt_u64(es), mt_bool(true)) >= 3);
	es2 = mt_u64_of("device_create_event_stream", 1, mt_u64(phy));
	CHECK(es2 != 0 && es2 != es);
	CHECK(read_under_oom(es2, UINT64_C(0x0001000000000000), 1000) >= 3);
	CHECK(read_is(es2, true, true, UINT64_C(0x0102010300000000), 1001));
	mt_ok("event_stream_destroy", 1, mt_u64(es2));
	mt_ok("event_stream_destroy", 1, mt_u64(es));

	/* the plain sequence: two events, then "no event" for a non-blocking and for a blocking read */
	es = mt_u64_of("device_create_event_stream", 1, mt_u64(phy));
	CHECK(es != 0);
	CHECK(read_is(es, true, true, UINT64_C(0x0001000000000000), 1000));
	CHECK(read_is(es, true, true, UINT64_C(0x0102010300000000), 1001));
	fake_clear_log();
	CHECK(read_is(es, true, false, 0, 0));          /* the fake's -EBUSY */
	CHECK(!strcmp(fake.log, "n"));                  /* nonblock reaches the backend as given: */
	CHECK(read_is(es, false, false, 0, 0));         /* the fake's -ETIMEDOUT */
	CHECK(!strcmp(fake.log, "nb"));                 /* a read that must not wait, one that may */
	/* "no event" as the real backends say it: -EAGAIN (local and network, non-blocking) and
	 * -EBUSY; any other error is raised, with the three results still unset */
	fake.ev_none = EAGAIN;
	CHECK(read_is(es, true, false, 0, 0));
	CHECK(read_is(es, false, false, 0, 0));
	fake.ev_none = EBUSY;
	CHECK(read_is(es, false, false, 0, 0));
	fake.ev_none = ETIMEDOUT;
	CHECK(read_is(es, true, false, 0, 0));
	fake.ev_none = EIO;
	CHECK(mt_raises("iio:EIO", "event_stream_read", 2, mt_u64(es), mt_bool(true)));
	fake.ev_none = 0;
	CHECK(!strcmp(fake.log, "nbnbbnn"));
	/* fewer outputs */
	CHECK(mt_bool_of("event_stream_read", 2, mt_u64(es), mt_bool(true)) == false);
	mt_ok("event_stream_read", 2, mt_u64(es), mt_bool(true));

	/* the events decode on the fake context as on emu */
	CHECK(chan_of(UINT64_C(0x0001000000000000), phy, false) == dp_chn(phy, "voltage0", false));
	CHECK(chan_of(UINT64_C(0x0102010300000000), phy, false) == dp_chn(phy, "accel_x", false));

	/* destroy twice; then the id is stale */
	before = gw_handle_count();
	mt_ok("event_stream_destroy", 1, mt_u64(es));
	CHECK(gw_handle_count() == before - 1);
	mt_ok("event_stream_destroy", 1, mt_u64(es));
	CHECK(gw_handle_count() == before - 1);
	CHECK(mt_raises("iio:EBADF", "event_stream_read", 2, mt_u64(es), mt_bool(true)));

	/* result first: a failed allocation leaves no stream behind, the sweep's last attempt makes one */
	before = gw_handle_count();
	CHECK(mt_call(1, out, "device_create_event_stream", 1, mt_u64(phy)) == 0);
	es = *(const uint64_t *)mxGetData(out[0]);
	mxDestroyArray(out[0]);
	mt_ok("event_stream_destroy", 1, mt_u64(es));
	CHECK(mt_oom_sweep(1, "device_create_event_stream", 1, mt_u64(phy)) >= 1);
	CHECK(gw_handle_count() == before + 1);         /* exactly one: the successful attempt */

	/* a second event stream, read once, left open when the context is destroyed: no leak report */
	es2 = mt_u64_of("device_create_event_stream", 1, mt_u64(phy));
	CHECK(es2 != 0);
	CHECK(read_is(es2, true, true, UINT64_C(0x0001000000000000), 1000));
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EBADF", "event_stream_read", 2, mt_u64(es2), mt_bool(true)));
	mt_ok("event_stream_destroy", 1, mt_u64(es2));
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_event, arities, sizeof(arities) / sizeof(arities[0]));

	test_emu(&fx);
	test_fake(&fx);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
