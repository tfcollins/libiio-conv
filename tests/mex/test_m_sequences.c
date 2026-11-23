/* SPDX-License-Identifier: MIT */
/* The gateway call sequences of the MATLAB classes where one command group hands an object to
 * another: every object here is made by gateway commands, in the order and with the arguments
 * the M code of matlab/+iio uses, and the values are the ones matlab/tests asserts. A
 * test_cmd_<group>.c links one group and builds its prerequisites with libiio directly, so it
 * cannot show that, say, a block of an iio_stream is readable through block_read, or that the
 * mask of a stream's buffer stream has a device for device_get_sample_size. */
#include "mex_test.h"
MT_GROUPS(&gw_group_toplevel, &gw_group_context, &gw_group_attr, &gw_group_device, &gw_group_channel, &gw_group_buffer, &gw_group_block, &gw_group_stream, &gw_group_event, &gw_group_lowlevel, &gw_group_conv, &gw_group_test);

#include <math.h>
#include <stdarg.h>

static struct mt_fixture fx;

/* ------------------------------------------------------------------ builders */

/* Scalar struct with n fields: pairs of (const char *name, mxArray *value). */
static mxArray *opts(int n, ...)
{
	const char *names[16];
	mxArray *values[16];
	mxArray *s;
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		names[i] = va_arg(ap, const char *);
		values[i] = va_arg(ap, mxArray *);
	}
	va_end(ap);
	s = mxCreateStructMatrix(1, 1, n, names);
	for (int i = 0; i < n; i++)
		mxSetFieldByNumber(s, 0, i, values[i]);
	return s;
}

/* double.empty(1, 0): the default of Gain, Offset and Samples in the M classes */
static mxArray *none(void)
{
	return mxCreateDoubleMatrix(1, 0, mxREAL);
}

/* iio.internal.Object.handles(iio.Channel.empty(1, 0)) */
static mxArray *no_ids(void)
{
	return mxCreateNumericMatrix(1, 0, mxUINT64_CLASS, mxREAL);
}

static mxArray *iq0(void)
{
	mxArray *a = mxCreateCellMatrix(1, 2);

	mxSetCell(a, 0, mt_str("voltage0_i"));
	mxSetCell(a, 1, mt_str("voltage0_q"));
	return a;
}

/* complex int16 n-by-1 from n (re, im) pairs */
static mxArray *ciq(size_t n, const int16_t *v)
{
	mxArray *a = mxCreateNumericMatrix(n, 1, mxINT16_CLASS, mxCOMPLEX);

	memcpy(mxGetData(a), v, n * 2 * sizeof(*v));
	return a;
}

/* the struct iio.RxStream builds */
static mxArray *rx_opts(double samples, double nb_blocks, const char *cls, bool cplx, double timeout_ms)
{
	return opts(11, "output", mt_bool(false), "buffer", mt_dbl(-1), "samples", mt_dbl(samples),
		"nb_blocks", mt_dbl(nb_blocks), "class", mt_str(cls), "complex", mt_bool(cplx),
		"scale", mt_str("none"), "raw", mt_bool(false), "gain", none(), "offset", none(),
		"timeout_ms", mt_dbl(timeout_ms));
}

/* the struct iio.TxStream builds */
static mxArray *tx_opts(double samples, double nb_blocks, bool cplx, double timeout_ms)
{
	return opts(11, "output", mt_bool(true), "buffer", mt_dbl(-1), "samples", mt_dbl(samples),
		"nb_blocks", mt_dbl(nb_blocks), "cyclic", mt_bool(false), "complex", mt_bool(cplx),
		"scale", mt_str("none"), "raw", mt_bool(false), "gain", none(), "offset", none(),
		"timeout_ms", mt_dbl(timeout_ms));
}

/* the struct iio.Block.read builds */
static mxArray *read_opts(const char *cls, bool cplx)
{
	return opts(8, "channels", no_ids(), "class", mt_str(cls), "complex", mt_bool(cplx),
		"scale", mt_str("none"), "raw", mt_bool(false), "gain", none(), "offset", none(),
		"samples", none());
}

/* the struct iio.Block.write builds */
static mxArray *write_opts(bool cplx)
{
	return opts(6, "channels", no_ids(), "complex", mt_bool(cplx), "scale", mt_str("none"),
		"raw", mt_bool(false), "gain", none(), "offset", none());
}

/* ------------------------------------------------------------------ what the M classes do */

static uint64_t find_dev(uint64_t ctx, const char *name)
{
	return mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str(name));
}

static uint64_t find_chn(uint64_t dev, const char *name, bool output)
{
	return mt_u64_of("device_find_channel", 3, mt_u64(dev), mt_str(name), mt_bool(output));
}

/* element k of a command that returns a list of ids; 0 when there is none */
static uint64_t id_at(const char *cmd, uint64_t owner, size_t k)
{
	mxArray *out[1] = { NULL };
	uint64_t id = 0;

	if (mt_call(1, out, cmd, 1, mt_u64(owner)) == 0 && out[0]) {
		if (mxGetClassID(out[0]) == mxUINT64_CLASS && k < mxGetNumberOfElements(out[0]))
			id = ((uint64_t *)mxGetData(out[0]))[k];
		mxDestroyArray(out[0]);
	}
	return id;
}

/* dev.buffers(1) */
static uint64_t buf0(uint64_t dev)
{
	return id_at("device_get_buffers", dev, 0);
}

/* the logical row of channels_mask_get as bits (bit k = channel k), -1 on failure */
static int mask_bits(uint64_t mask)
{
	mxArray *out[1] = { NULL };
	int bits = -1;

	if (mt_call(1, out, "channels_mask_get", 1, mt_u64(mask)) == 0 && out[0]) {
		if (mxIsLogical(out[0]) && mxGetM(out[0]) == 1) {
			bits = 0;
			for (size_t k = 0; k < mxGetN(out[0]); k++)
				if (mxGetLogicals(out[0])[k])
					bits |= 1 << k;
		}
		mxDestroyArray(out[0]);
	}
	return bits;
}

/* mask.channels = [chans]: iio.ChannelsMask/set.channels */
static void mask_assign(uint64_t mask, uint64_t dev, int n, ...)
{
	uint64_t ch[8];
	va_list ap;
	int bits;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		ch[i] = va_arg(ap, uint64_t);
	va_end(ap);
	for (int i = 0; i < n; i++)
		(void)mt_bool_of("channel_is_enabled", 2, mt_u64(ch[i]), mt_u64(mask));
	bits = mask_bits(mask);
	for (size_t k = 0; bits > 0 && k < 8; k++)
		if (bits & (1 << k))
			mt_ok("channel_disable", 2, mt_u64(id_at("device_get_channels", dev, k)), mt_u64(mask));
	for (int i = 0; i < n; i++)
		mt_ok("channel_enable", 2, mt_u64(ch[i]), mt_u64(mask));
}

/* mask.sample_size (iio.ChannelsMask) and dev.sample_size(mask) */
static double mask_sample_size(uint64_t dev, uint64_t mask)
{
	return mt_dbl_of("device_get_sample_size", 2, mt_u64(dev), mt_u64(mask));
}

/* bs.sample_size (iio.BufferStream/get.sample_size); buf is the Parent of bs */
static double bs_sample_size(uint64_t bs, uint64_t buf)
{
	uint64_t m = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs));
	uint64_t dev = mt_u64_of("buffer_get_device", 1, mt_u64(buf));

	return mt_dbl_of("device_get_sample_size", 2, mt_u64(dev), mt_u64(m));
}

/* numel(bs.mask.channels), and the delete of the temporary iio.ChannelsMask */
static int bs_mask_bits(uint64_t bs)
{
	uint64_t m = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs));
	int bits = mask_bits(m);

	mt_ok("channels_mask_destroy", 1, mt_u64(m));      /* a view: left alone */
	CHECK(gw_handle_type(m) == GW_T_MASK);
	return bits;
}

/* int16 content of <dir>/<file>; the number of values, -1 if the file cannot be read */
static int file_i16(const char *file, int16_t *v, size_t max)
{
	char path[PATH_MAX + 64];
	FILE *f;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", fx.dir, file);
	f = fopen(path, "rb");
	if (!f)
		return -1;
	n = fread(v, sizeof(*v), max, f);
	fclose(f);
	return (int)n;
}

static bool file_is(const char *file, size_t n, const int16_t *want)
{
	int16_t got[64];

	return file_i16(file, got, 64) == (int)n && !memcmp(got, want, n * sizeof(*want));
}

/* x = blk.read(OutputDataType = "int16", Complex = true): n rows whose row r is ramp sample k0 + r */
static bool block_is_ramp(uint64_t blk, size_t n, int k0)
{
	mxArray *out[1] = { NULL };
	bool ok = false;

	if (mt_call(1, out, "block_read", 2, mt_u64(blk), read_opts("int16", true)) == 0 && out[0]) {
		const int16_t *p = mxGetData(out[0]);

		ok = mxGetClassID(out[0]) == mxINT16_CLASS && mxIsComplex(out[0]) &&
			mxGetM(out[0]) == n && mxGetN(out[0]) == 1;
		for (size_t r = 0; ok && r < n; r++)
			ok = p[2 * r] == k0 + (int)r - 2048 && p[2 * r + 1] == 2047 - k0 - (int)r;
		mxDestroyArray(out[0]);
	}
	return ok;
}

/* ------------------------------------------------------------------ cases */

/* EmuFixture.fakeContext and tFakeBackend: adopt, timeout, attribute read, binary write */
static void fake_adopt(void)
{
	static uint8_t payload[47000];
	mxArray *out[1] = { NULL };
	uint64_t ctx, phy, text;

	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(ctx != 0);
	/* the constructor of iio.Context(AdoptHandle = h) */
	CHECK(!strcmp(mt_str_of("context_get_name", 1, mt_u64(ctx)), "fake"));
	CHECK(!strcmp(mt_str_of("context_get_description", 1, mt_u64(ctx)), "Emulated fixture & test context (fake)"));
	(void)mt_dbl_of("context_get_version_major", 1, mt_u64(ctx));
	(void)mt_dbl_of("context_get_version_minor", 1, mt_u64(ctx));
	(void)mt_dbl_of("context_get_version_patch", 1, mt_u64(ctx));
	CHECK(mt_str_of("context_get_version_tag", 1, mt_u64(ctx)) != NULL);
	CHECK(id_at("context_get_devices", ctx, 7) != 0 && id_at("context_get_devices", ctx, 8) == 0);

	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -12345);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(250));
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == 250);
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(-1));       /* set_timeout(Inf) */
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -1);

	phy = find_dev(ctx, "emu-phy");
	text = mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("text"));
	CHECK(text != 0);
	CHECK(!strcmp(mt_str_of("attr_get_name", 1, mt_u64(text)), "text"));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "fake:text"));
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i % 256);
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes(payload, sizeof(payload))) == 47000);
	CHECK(mt_call(1, out, "test_fake_last_write", 0) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == sizeof(payload));
	CHECK(mxGetN(out[0]) == sizeof(payload) && !memcmp(mxGetData(out[0]), payload, sizeof(payload)));
	mxDestroyArray(out[0]);

	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);
}

/* tFakeBackend.coupledChannelWidensSample, then proof that the next fake context is not coupled */
static void fake_coupled(void)
{
	static const int16_t want[] = { 0, 3, 6, 9, 1, 4, 7, 10 };      /* [0 1; 3 4; 6 7; 9 10] */
	static const int16_t plain[] = { 0, 1, 2, 3, 4, 5, 6, 7 };      /* [0+1i; 2+3i; 4+5i; 6+7i] */
	mxArray *out[2] = { NULL, NULL };
	uint64_t ctx, rx, mask, buf, bs, cs;

	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	rx = find_dev(ctx, "emu-rx");
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str("voltage1_i"));
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mask_assign(mask, rx, 2, find_chn(rx, "voltage0_i", false), find_chn(rx, "voltage0_q", false));
	CHECK(mask_bits(mask) == 0x3);
	CHECK(mask_sample_size(rx, mask) == 4);
	buf = buf0(rx);
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	CHECK(bs != 0);
	CHECK(bs_sample_size(bs, buf) == 6);
	CHECK(bs_mask_bits(bs) == 0x7);                 /* numel(bs.mask.channels) == 3 */
	CHECK(mask_sample_size(rx, mask) == 4);         /* the caller's mask is not the stream's */
	mt_ok("buffer_close", 1, mt_u64(bs));

	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(4, 1, "int16", false, 0));
	CHECK(cs != 0);
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[1]));
	CHECK(mxGetClassID(out[0]) == mxINT16_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 4 && mxGetN(out[0]) == 2);
	CHECK(mxGetNumberOfElements(out[0]) == 8 && !memcmp(mxGetData(out[0]), want, sizeof(want)));
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	/* the locals of the test method go, in any order */
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	CHECK(gw_handle_count() == 0);

	/* the next test method: a new fake context starts without the coupled channel */
	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	rx = find_dev(ctx, "emu-rx");
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(4, 2, "int16", true, 0));
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[1]));
	CHECK(mxIsComplex(out[0]) && mxGetM(out[0]) == 4 && mxGetN(out[0]) == 1);
	CHECK(mxGetNumberOfElements(out[0]) == 4 && !memcmp(mxGetData(out[0]), plain, sizeof(plain)));
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);
}

/* tFakeBackend.closeOrderOnShutdown */
static void fake_shutdown(void)
{
	uint64_t ctx, rx, cs, again;

	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	rx = find_dev(ctx, "emu-rx");
	CHECK(mt_str_of("test_fake_log", 0) && !strcmp(mt_str_of("test_fake_log", 0), ""));
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(4, 2, "int16", true, -1));
	CHECK(cs != 0);
	CHECK(!strcmp(mt_str_of("test_fake_log", 0), "OBBEES"));
	mt_ok("mex_shutdown", 0);
	CHECK(!strcmp(mt_str_of("test_fake_log", 0), "OBBEESXsFFXC"));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
	again = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(again != 0 && again != ctx);
	CHECK(!strcmp(mt_str_of("context_get_name", 1, mt_u64(again)), "fake"));
	CHECK(!strcmp(mt_str_of("test_fake_log", 0), ""));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	/* the stale objects of the test method are deleted while the new context lives */
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 1);
	mt_ok("context_destroy", 1, mt_u64(again));
	CHECK(gw_handle_count() == 0);
}

/* tTxStream.noFreeBlockGivesInvalid, and the events and the trigger of the fake through commands */
static void fake_tx_events_trigger(void)
{
	static const int16_t frame[] = { 100, 1, -200, 2, 300, 3, -400, 4 };
	mxArray *out[3] = { NULL, NULL, NULL };
	uint64_t ctx, tx, rx, phy, trg, cs, es;

	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	tx = find_dev(ctx, "emu-tx");
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(tx), iq0(), tx_opts(4, 2, true, 0));
	CHECK(cs != 0);
	for (int k = 0; k < 4; k++)
		CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(4, frame)));
	CHECK(!mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(4, frame)));
	CHECK(!mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));

	/* iio.EventStream(phy), es.read(), iio.EventStream.decode */
	phy = find_dev(ctx, "emu-phy");
	rx = find_dev(ctx, "emu-rx");
	CHECK(mt_raises("iio:ENODEV", "device_create_event_stream", 1, mt_u64(rx)));
	es = mt_u64_of("device_create_event_stream", 1, mt_u64(phy));
	CHECK(es != 0);
	CHECK(mt_call(3, out, "event_stream_read", 2, mt_u64(es), mt_bool(true)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[0]));
	CHECK(mxGetClassID(out[1]) == mxUINT64_CLASS && *(uint64_t *)mxGetData(out[1]) == 0x0001000000000000u);
	CHECK(mxGetClassID(out[2]) == mxINT64_CLASS && *(int64_t *)mxGetData(out[2]) == 1000);
	CHECK(mt_u64_of("event_get_channel", 3, mt_u64(0x0001000000000000u), mt_u64(phy), mt_bool(false)) ==
		find_chn(phy, "voltage0", false));
	CHECK(mt_u64_of("event_get_channel", 3, mt_u64(0x0001000000000000u), mt_u64(phy), mt_bool(true)) == 0);
	for (int i = 0; i < 3; i++)
		mxDestroyArray(out[i]);
	/* tEventStream.readFake: es.read(true), es.read() without an event, es.read(false); the
	 * backend saw the flag of each read */
	CHECK(mt_bool_of("event_stream_read", 2, mt_u64(es), mt_bool(true)));
	CHECK(!mt_bool_of("event_stream_read", 2, mt_u64(es), mt_bool(true)));
	CHECK(!mt_bool_of("event_stream_read", 2, mt_u64(es), mt_bool(false)));
	{
		const char *log = mt_str_of("test_fake_log", 0);
		size_t len = log ? strlen(log) : 0;

		CHECK(len >= 4 && !strcmp(log + len - 4, "nnnb"));
	}

	/* tDeviceLowLevel.triggerFake */
	trg = find_dev(ctx, "trigger0");
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == 0);
	mt_ok("device_set_trigger", 2, mt_u64(rx), mt_u64(trg));
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == trg);
	CHECK(mt_u64_of("device_get_context", 1, mt_u64(trg)) == ctx);
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(phy)));
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == trg);
	mt_ok("device_set_trigger", 2, mt_u64(rx), mt_u64(0));
	CHECK(mt_u64_of("device_get_trigger", 1, mt_u64(rx)) == 0);

	/* the context goes with the transmit stream (two frames in flight) and the event stream open */
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);
}

/* tStream on emu-rx: the blocks and the buffer stream of an iio_stream in the hands of groups
 * block, buffer, channel and lowlevel */
static void stream_views(uint64_t ctx)
{
	mxArray *out[1] = { NULL };
	uint64_t rx, buf, mask, st, a, b, c, d, bsv, m;

	rx = find_dev(ctx, "emu-rx");
	buf = buf0(rx);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage0_i", false)), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage0_q", false)), mt_u64(mask));
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(buf), mt_dbl(3), mt_dbl(16), mt_u64(mask));
	CHECK(st != 0);

	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(a)) == 64);
	CHECK(block_is_ramp(a, 16, 0));
	CHECK(block_is_ramp(a, 16, 0));         /* again: the cached converter */
	/* tStream.cpuAccessOfStreamBlock */
	CHECK(mt_raises("iio:EPERM", "block_disable_cpu_access", 2, mt_u64(a), mt_bool(true)));
	CHECK(mt_raises("iio:EPERM", "block_disable_cpu_access", 2, mt_u64(a), mt_bool(false)));
	CHECK(block_is_ramp(a, 16, 0));

	/* blk.buffer_stream, bs.sample_size, bs.mask, bs.started, bs.start(), delete(bs) */
	bsv = mt_u64_of("block_get_buffer_stream", 1, mt_u64(a));
	CHECK(bsv != 0 && !gw_handle_owned(bsv));
	CHECK(bs_sample_size(bsv, buf) == 4);
	CHECK(bs_mask_bits(bsv) == 0x3);
	m = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bsv));
	CHECK(mt_raises("iio:EPERM", "channel_disable", 2, mt_u64(find_chn(rx, "voltage0_i", false)), mt_u64(m)));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bsv)));  /* libiio started it in the first next */
	CHECK(mt_raises("iio:EPERM", "buffer_stream_start", 1, mt_u64(bsv)));
	CHECK(mt_raises("iio:EPERM", "buffer_stream_stop", 1, mt_u64(bsv)));
	CHECK(mt_raises("iio:EPERM", "buffer_stream_create_block", 2, mt_u64(bsv), mt_dbl(64)));
	mt_ok("buffer_close", 1, mt_u64(bsv));
	CHECK(gw_handle_type(bsv) == GW_T_BUFFER_STREAM);
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bsv)));

	/* the block belongs to the stream */
	CHECK(mt_raises("iio:EPERM", "block_enqueue", 3, mt_u64(a), mt_dbl(0), mt_bool(false)));
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(a), mt_bool(false)));
	mt_ok("block_destroy", 1, mt_u64(a));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(a)) == 64);
	CHECK(mt_dbl_of("block_first", 2, mt_u64(a), mt_u64(find_chn(rx, "voltage0_q", false))) == 2);
	/* v0q.read(blk): 16 samples of 2 bytes */
	CHECK(mt_call(1, out, "channel_read", 3, mt_u64(find_chn(rx, "voltage0_q", false)), mt_u64(a), mt_bool(false)) == 0);
	CHECK(mxGetN(out[0]) == 32 && ((int16_t *)mxGetData(out[0]))[0] == 2047 && ((int16_t *)mxGetData(out[0]))[15] == 2032);
	mxDestroyArray(out[0]);

	/* the first block went back to the stream with the second next() */
	b = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(b != a);
	CHECK(mt_raises("iio:EPERM", "block_read", 2, mt_u64(a), read_opts("int16", true)));
	CHECK(mt_raises("iio:EPERM", "block_get_bytes", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EPERM", "channel_read", 3, mt_u64(find_chn(rx, "voltage0_q", false)), mt_u64(a), mt_bool(false)));
	CHECK(block_is_ramp(b, 16, 16));
	CHECK(mt_call(1, out, "block_read", 2, mt_u64(b), read_opts("double", false)) == 0);
	CHECK(mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxGetM(out[0]) == 16 && mxGetN(out[0]) == 2);
	CHECK(mxGetNumberOfElements(out[0]) == 32 && ((double *)mxGetData(out[0]))[0] == -2032 && ((double *)mxGetData(out[0]))[16] == 2031);
	mxDestroyArray(out[0]);
	c = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(c != a && c != b);
	CHECK(block_is_ramp(c, 16, 32));
	d = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(d == a);                          /* three blocks in rotation */
	CHECK(block_is_ramp(d, 16, 48));
	CHECK(mt_u64_of("block_get_buffer_stream", 1, mt_u64(d)) == bsv);

	/* st.cancel(); st.destroy(); st.destroy(): the blocks went with the stream */
	mt_ok("stream_cancel", 1, mt_u64(st));
	mt_ok("stream_destroy", 1, mt_u64(st));
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(a)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_is_started", 1, mt_u64(bsv)));
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(st)));

	/* the buffer is free again: iio.Stream(buf, mask, 8) has four blocks of 32 bytes, and the
	 * same buffer then opens as a plain buffer stream */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(buf), mt_dbl(4), mt_dbl(8), mt_u64(mask));
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(a)) == 32);
	CHECK(block_is_ramp(a, 8, 0));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));        /* delete(mask): the stream has its copy */
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(block_is_ramp(a, 8, 8));
	mt_ok("stream_destroy", 1, mt_u64(st));
}

/* tStream.tx: transmit through the block views of an iio_stream */
static void stream_tx(uint64_t ctx)
{
	static const int16_t f1[] = { 11, 12, 13, 14 }, f2[] = { 21, 22, 23, 24 }, f3[] = { 31, 32, 33, 34 };
	uint64_t tx, buf, mask, st, blk;

	tx = find_dev(ctx, "emu-tx");
	buf = buf0(tx);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(tx, "voltage0_i", true)), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(find_chn(tx, "voltage0_q", true)), mt_u64(mask));
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(buf), mt_dbl(1), mt_dbl(2), mt_u64(mask));
	blk = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(blk)) == 8);
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), ciq(2, f1), write_opts(true)) == 2);
	blk = mt_u64_of("stream_get_next_block", 1, mt_u64(st));    /* sends the block */
	CHECK(file_is("emu-tx_buf0.bin", 4, f1));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), ciq(2, f2), write_opts(true)) == 2);
	blk = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(file_is("emu-tx_buf0.bin", 4, f2));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), ciq(2, f3), write_opts(true)) == 2);
	mt_ok("stream_destroy", 1, mt_u64(st));                 /* the block filled last is never sent */
	CHECK(file_is("emu-tx_buf0.bin", 4, f2));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
}

/* tBlock and tBuffer: a buffer stream of group buffer, its blocks in group block */
static void buffer_blocks(uint64_t ctx)
{
	static const int16_t zero_q[] = { 7, 0, -8, 0, 9, 0, -10, 0 };
	static const int16_t f[] = { 1, 5, 2, 6, 3, 7, 4, 8 }, real4[] = { 7, -8, 9, -10 }, host[] = { 7, 8, 9, 10 };
	static const int16_t only_i[] = { 7, 0, 8, 0, 9, 0, 10, 0 };
	static const uint8_t zeros[16];
	mxArray *out[1] = { NULL }, *a;
	uint64_t rx, tx, buf, mask, bs, blk, v0i, v0q, other;

	/* openRx / rxBlock of tBlock.m */
	rx = find_dev(ctx, "emu-rx");
	v0i = find_chn(rx, "voltage0_i", false);
	v0q = find_chn(rx, "voltage0_q", false);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(v0q), mt_u64(mask));
	buf = buf0(rx);
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));        /* the helper's local mask goes */
	CHECK(bs_sample_size(bs, buf) == 4);
	CHECK(bs_mask_bits(bs) == 0x3);
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(1)));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));     /* not enqueued */
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));     /* not started */
	CHECK(mt_raises("iio:EPERM", "block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false)));
	CHECK(mt_raises("iio:EPERM", "block_read", 2, mt_u64(blk), read_opts("double", false)));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(mt_raises("iio:EPERM", "block_dequeue", 2, mt_u64(blk), mt_bool(false)));     /* ours again */
	CHECK(block_is_ramp(blk, 8, 0));
	CHECK(mt_u64_of("block_get_buffer_stream", 1, mt_u64(blk)) == bs);
	/* blk.bytes(28): from the offset to the end */
	CHECK(mt_call(1, out, "block_get_bytes", 3, mt_u64(blk), mt_dbl(28), mt_empty()) == 0);
	CHECK(mxGetN(out[0]) == 4 && ((uint16_t *)mxGetData(out[0]))[0] == fixture_ramp_word_i(7));
	mxDestroyArray(out[0]);
	/* v0i.read(blk): int16 -2048 .. -2041 */
	CHECK(mt_call(1, out, "channel_read", 3, mt_u64(v0i), mt_u64(blk), mt_bool(false)) == 0);
	CHECK(mxGetN(out[0]) == 16 && ((int16_t *)mxGetData(out[0]))[0] == -2048 && ((int16_t *)mxGetData(out[0]))[7] == -2041);
	mxDestroyArray(out[0]);
	/* destroyTwice: the buffer stream makes another block */
	mt_ok("block_destroy", 1, mt_u64(blk));
	mt_ok("block_destroy", 1, mt_u64(blk));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(blk)));
	other = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(other)) == 32);
	/* closeInvalidatesBlocks; the buffer opens again (tBuffer.openClose) */
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("buffer_close", 1, mt_u64(bs));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(other)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_is_started", 1, mt_u64(bs)));
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage2", false)), mt_u64(mask));
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	CHECK(bs != 0 && bs_sample_size(bs, buf) == 2 && bs_mask_bits(bs) == 0x10);
	CHECK(!mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)));
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));

	/* openTx of tBlock.m; writeRealAsComplex */
	tx = find_dev(ctx, "emu-tx");
	v0i = find_chn(tx, "voltage0_i", true);
	v0q = find_chn(tx, "voltage0_q", true);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(v0q), mt_u64(mask));
	buf = buf0(tx);
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(4 * bs_sample_size(bs, buf)));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), ciq(4, f), write_opts(true)) == 4);
	a = mxCreateNumericMatrix(4, 1, mxINT16_CLASS, mxREAL);
	memcpy(mxGetData(a), real4, sizeof(real4));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), a, write_opts(true)) == 4);  /* Complex = true, real data */
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(16), mt_bool(false));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(file_is("emu-tx_buf0.bin", 8, zero_q));
	/* the second round of the test, without a new start */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), ciq(4, f), write_opts(true)) == 4);
	a = mxCreateNumericMatrix(4, 1, mxINT16_CLASS, mxREAL);
	memcpy(mxGetData(a), real4, sizeof(real4));
	/* complex(x, int16(0)) with ~isreal true in M but a real array at the MEX: complex = true */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), a, write_opts(true)) == 4);
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(16), mt_bool(false));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(file_is("emu-tx_buf0.bin", 8, zero_q));
	/* channelWrite: set_bytes(zeros), v0i.write(blk, int16([7 8 9 10])) */
	CHECK(mt_dbl_of("block_set_bytes", 3, mt_u64(blk), mt_bytes(zeros, 16), mt_dbl(0)) == 16);
	CHECK(mt_dbl_of("channel_write", 4, mt_u64(v0i), mt_u64(blk), mt_bytes(host, sizeof(host)), mt_bool(false)) == 8);
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(16), mt_bool(false));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(file_is("emu-tx_buf0.bin", 8, only_i));
	mt_ok("buffer_close", 1, mt_u64(bs));
}

/* m-by-n double, real or complex, from the values in storage order (re, im interleaved) */
static mxArray *dmat(size_t m, size_t n, bool cplx, const double *v)
{
	mxArray *a = mxCreateDoubleMatrix(m, n, cplx ? mxCOMPLEX : mxREAL);

	memcpy(mxGetData(a), v, m * n * (cplx ? 2 : 1) * sizeof(*v));
	return a;
}

/* block_read(blk, opts) is an m-by-n array of that class and complexity with these bytes */
static bool read_is(uint64_t blk, mxArray *o, mxClassID cls, bool cplx, size_t m, size_t n,
		const void *want, size_t bytes)
{
	mxArray *out[1] = { NULL };
	bool ok;

	if (mt_call(1, out, "block_read", 2, mt_u64(blk), o) || !out[0])
		return false;
	ok = mxGetClassID(out[0]) == cls && mxIsComplex(out[0]) == cplx && mxGetM(out[0]) == m &&
		mxGetN(out[0]) == n && !memcmp(mxGetData(out[0]), want, bytes);
	mxDestroyArray(out[0]);
	return ok;
}

/* tTxStream.gainAndOffset, tBlock.writeGainOffset, tBlock.argumentsAreNotConverted (the part
 * that reaches the gateway) and the empty result of tBlock.channelRead: on transmit the device
 * gets (data - offset) / gain */
static void gain_and_offset(uint64_t ctx)
{
	static const double g24[2] = { 2, 4 }, o1020[2] = { 10, -20 };
	static const double real42[8] = { 100, 200, 300, 10, 100, 200, 300, -20 };  /* [100 100; 200 200; 300 300; 10 -20] */
	static const double cplx41[8] = { 100, 50, 10, 20, 10, 10, 12, 8 };
	static const double real22[4] = { 100, 200, 100, 200 };                    /* [100 100; 200 200] */
	static const double cplx21[4] = { 100, 50, 10, 20 };
	static const int16_t sent_real[8] = { 45, 30, 95, 55, 145, 80, 0, 0 };
	static const int16_t sent_cplx[8] = { 45, 20, 0, 5, 0, 0, 1, -1 };
	static const int16_t blk_real[4] = { 45, 95, 30, 55 };                     /* int16([45 30; 95 55]) */
	static const int16_t blk_cplx[4] = { 45, 0, 20, 5 };                       /* int16([45 20; 0 5]) */
	mxArray *out[1] = { NULL }, *two;
	uint64_t tx, rx, cs, buf, mask, bs, blk, v0i, v0q, v2;

	/* t = iio.TxStream(tx, [...], SamplesPerFrame = 4, NumBlocks = 1, Gain = [2 4], Offset = [10 -20]) */
	tx = find_dev(ctx, "emu-tx");
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(tx), iq0(), opts(11, "output", mt_bool(true),
		"buffer", mt_dbl(-1), "samples", mt_dbl(4), "nb_blocks", mt_dbl(1), "cyclic", mt_bool(false),
		"complex", mt_bool(false), "scale", mt_str("none"), "raw", mt_bool(false),
		"gain", dmat(1, 2, false, g24), "offset", dmat(1, 2, false, o1020), "timeout_ms", mt_dbl(-1)));
	CHECK(cs != 0);
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), dmat(4, 2, false, real42)));
	CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
	CHECK(file_is("emu-tx_buf0.bin", 8, sent_real));
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	/* Complex = true, Gain = 2, Offset = 10 */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(tx), iq0(), opts(11, "output", mt_bool(true),
		"buffer", mt_dbl(-1), "samples", mt_dbl(4), "nb_blocks", mt_dbl(1), "cyclic", mt_bool(false),
		"complex", mt_bool(true), "scale", mt_str("none"), "raw", mt_bool(false),
		"gain", mt_dbl(2), "offset", mt_dbl(10), "timeout_ms", mt_dbl(-1)));
	CHECK(cs != 0);
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), dmat(4, 1, true, cplx41)));
	CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
	CHECK(file_is("emu-tx_buf0.bin", 8, sent_cplx));
	mt_ok("conv_stream_close", 1, mt_u64(cs));

	/* openTx of tBlock.m; blk.write([100 100; 200 200], Gain = [2 4], Offset = [10 -20]) */
	v0i = find_chn(tx, "voltage0_i", true);
	v0q = find_chn(tx, "voltage0_q", true);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(v0q), mt_u64(mask));
	buf = buf0(tx);
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(8));
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), dmat(2, 2, false, real22),
		opts(6, "channels", no_ids(), "complex", mt_bool(false), "scale", mt_str("none"),
		"raw", mt_bool(false), "gain", dmat(1, 2, false, g24), "offset", dmat(1, 2, false, o1020))) == 2);
	CHECK(read_is(blk, read_opts("int16", false), mxINT16_CLASS, false, 2, 2, blk_real, sizeof(blk_real)));
	CHECK(read_is(blk, opts(8, "channels", no_ids(), "class", mt_str("double"), "complex", mt_bool(false),
		"scale", mt_str("none"), "raw", mt_bool(false), "gain", dmat(1, 2, false, g24),
		"offset", dmat(1, 2, false, o1020), "samples", none()),
		mxDOUBLE_CLASS, false, 2, 2, real22, sizeof(real22)));
	/* blk.write([100 + 50i; 10 + 20i], Gain = 2, Offset = 10): Complex is ~isreal(data) */
	CHECK(mt_dbl_of("block_write", 3, mt_u64(blk), dmat(2, 1, true, cplx21),
		opts(6, "channels", no_ids(), "complex", mt_bool(true), "scale", mt_str("none"),
		"raw", mt_bool(false), "gain", mt_dbl(2), "offset", mt_dbl(10))) == 2);
	CHECK(read_is(blk, read_opts("int16", false), mxINT16_CLASS, false, 2, 2, blk_cplx, sizeof(blk_cplx)));
	CHECK(read_is(blk, opts(8, "channels", no_ids(), "class", mt_str("double"), "complex", mt_bool(true),
		"scale", mt_str("none"), "raw", mt_bool(false), "gain", mt_dbl(2), "offset", mt_dbl(10),
		"samples", none()), mxDOUBLE_CLASS, true, 2, 1, cplx21, sizeof(cplx21)));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(8), mt_bool(false));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(file_is("emu-tx_buf0.bin", 4, sent_cplx));
	mt_ok("buffer_close", 1, mt_u64(bs));

	/* rxBlock of tBlock.m; a channel twice; v2.read(blk) of a channel that is not enabled */
	rx = find_dev(ctx, "emu-rx");
	v0i = find_chn(rx, "voltage0_i", false);
	v0q = find_chn(rx, "voltage0_q", false);
	v2 = find_chn(rx, "voltage2", false);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(v0q), mt_u64(mask));
	buf = buf0(rx);
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	two = mxCreateNumericMatrix(1, 2, mxUINT64_CLASS, mxREAL);
	((uint64_t *)mxGetData(two))[0] = ((uint64_t *)mxGetData(two))[1] = v0i;
	CHECK(mt_raises("iio:EINVAL", "block_read", 2, mt_u64(blk), opts(8, "channels", two,
		"class", mt_str("double"), "complex", mt_bool(false), "scale", mt_str("none"),
		"raw", mt_bool(false), "gain", none(), "offset", none(), "samples", none())));
	two = mxCreateNumericMatrix(1, 2, mxUINT64_CLASS, mxREAL);
	((uint64_t *)mxGetData(two))[0] = ((uint64_t *)mxGetData(two))[1] = v0i;
	{
		mxArray *d = mxCreateNumericMatrix(1, 2, mxINT16_CLASS, mxREAL);

		((int16_t *)mxGetData(d))[0] = 1;
		((int16_t *)mxGetData(d))[1] = 2;
		CHECK(mt_raises("iio:EINVAL", "block_write", 3, mt_u64(blk), d, opts(6, "channels", two,
			"complex", mt_bool(false), "scale", mt_str("none"), "raw", mt_bool(false),
			"gain", none(), "offset", none())));
	}
	CHECK(block_is_ramp(blk, 8, 0));
	for (int raw = 0; raw < 2; raw++) {
		CHECK(mt_call(1, out, "channel_read", 3, mt_u64(v2), mt_u64(blk), mt_bool(raw)) == 0);
		CHECK(out[0] && mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetNumberOfElements(out[0]) == 0);
		mxDestroyArray(out[0]);
	}
	mt_ok("buffer_close", 1, mt_u64(bs));
}

/* tChannel.maskRoundTrip, tDeviceLowLevel on emu, the handle counts of tRxStream.closeTwiceAndDelete */
static void masks_and_lowlevel(uint64_t ctx)
{
	uint64_t rx, tx, phy, mask, txmask, ch[5], cs;
	size_t n;

	rx = find_dev(ctx, "emu-rx");
	tx = find_dev(ctx, "emu-tx");
	phy = find_dev(ctx, "emu-phy");
	for (size_t k = 0; k < 5; k++)
		ch[k] = id_at("device_get_channels", rx, k);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	CHECK(mask_bits(mask) == 0 && mask_sample_size(rx, mask) == 0);
	mask_assign(mask, rx, 2, ch[0], ch[1]);
	CHECK(mask_bits(mask) == 0x03 && mask_sample_size(rx, mask) == 4);
	mt_ok("channel_disable", 2, mt_u64(ch[0]), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(ch[4]), mt_u64(mask));
	CHECK(mask_bits(mask) == 0x12 && mask_sample_size(rx, mask) == 4);
	mask_assign(mask, rx, 1, ch[0]);
	CHECK(mask_bits(mask) == 0x01 && mask_sample_size(rx, mask) == 2);
	mask_assign(mask, rx, 0);
	CHECK(mask_bits(mask) == 0);
	mask_assign(mask, rx, 5, ch[0], ch[1], ch[2], ch[3], ch[4]);
	CHECK(mask_sample_size(rx, mask) == 10);
	/* a mask of another device; a channel that is not a scan element */
	txmask = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	CHECK(mask_sample_size(tx, txmask) == 0);
	CHECK(mt_raises("iio:EINVAL", "device_get_sample_size", 2, mt_u64(rx), mt_u64(txmask)));
	CHECK(mt_raises("iio:EINVAL", "channel_is_enabled", 2, mt_u64(find_chn(tx, "voltage0_i", true)), mt_u64(mask)));
	CHECK(!mt_bool_of("channel_is_enabled", 2, mt_u64(find_chn(tx, "altvoltage0", true)), mt_u64(txmask)));
	CHECK(mt_raises("iio:EINVAL", "channel_enable", 2, mt_u64(find_chn(tx, "altvoltage0", true)), mt_u64(txmask)));
	mt_ok("channels_mask_destroy", 1, mt_u64(txmask));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));

	/* registers and triggers on emu, as tDeviceLowLevel asserts them */
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(16)));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(4294967295.0), mt_dbl(4294967295.0));
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(16)));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(51966));
	CHECK(mt_raises("iio:ENOENT", "device_reg_read", 2, mt_u64(phy), mt_dbl(17)));
	CHECK(mt_raises("iio:ENOSYS", "device_get_trigger", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:ENOSYS", "device_set_trigger", 2, mt_u64(rx), mt_u64(find_dev(ctx, "trigger0"))));
	CHECK(mt_raises("iio:ENOSYS", "device_set_trigger", 2, mt_u64(rx), mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "device_set_trigger", 2, mt_u64(rx), mt_u64(phy)));
	CHECK(mt_raises("iio:ENOSYS", "device_create_event_stream", 1, mt_u64(phy)));

	/* a fast-path stream leaves its buffer registered: n is taken with the stream open */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(8, 2, "double", false, -1));
	n = gw_handle_count();
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	CHECK(gw_handle_count() == n - 1);
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	CHECK(gw_handle_count() == n - 1);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(8, 2, "double", false, -1));
	CHECK(gw_handle_count() == n);
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	CHECK(gw_handle_count() == n - 1);
}

/* tBuffer.contextDeleteWithOpenStream and tStream.contextGoesFirst: the context first */
static void context_first(void)
{
	uint64_t ctx, rx, v0i, buf, mask, bs, blk, st, sblk;

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	rx = find_dev(ctx, "emu-rx");
	v0i = find_chn(rx, "voltage0_i", false);
	buf = buf0(rx);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(v0i), mt_u64(mask));
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	mt_ok("buffer_stream_cancel", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("block_destroy", 1, mt_u64(blk));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(blk)));

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	rx = find_dev(ctx, "emu-rx");
	buf = buf0(rx);
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage0_i", false)), mt_u64(mask));
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(buf), mt_dbl(2), mt_dbl(16), mt_u64(mask));
	sblk = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(mt_dbl_of("block_size", 1, mt_u64(sblk)) == 32);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(st)));
	CHECK(mt_raises("iio:EBADF", "block_size", 1, mt_u64(sblk)));
	mt_ok("stream_destroy", 1, mt_u64(st));
	mt_ok("block_destroy", 1, mt_u64(sblk));
}

/* tLifetime and tStrings: what the gateway answers to the calls of those two test classes */
static void lifetime_and_strings(void)
{
	uint64_t ctx, phy, rx, tx, text, mask, bs, blk, cs;
	mxArray *a, *cell;
	size_t n;

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	phy = find_dev(ctx, "emu-phy");
	rx = find_dev(ctx, "emu-rx");
	tx = find_dev(ctx, "emu-tx");
	text = mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("text"));

	/* tStrings.emptyAndNonAscii: "" and char(181) name no device (iio:device4 has no name) */
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("")) == 0);
	a = mt_str("u");
	((mxChar *)mxGetData(a))[0] = 181;
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), a) == 0);
	a = mt_str("uV");
	((mxChar *)mxGetData(a))[0] = 181;
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), a) == 0);
	CHECK(mt_u64_of("device_find_channel", 3, mt_u64(phy), mt_str(""), mt_bool(false)) == 0);

	/* tStrings.embeddedNul: refused, never cut at the NUL */
	a = mt_str("emu-phy0x");
	((mxChar *)mxGetData(a))[7] = 0;
	CHECK(mt_raises("iio:EINVAL", "context_find_device", 2, mt_u64(ctx), a));
	a = mt_str("calib_mode0");
	((mxChar *)mxGetData(a))[10] = 0;
	CHECK(mt_raises("iio:EINVAL", "device_find_attr", 2, mt_u64(phy), a));
	a = mt_str("a0b");
	((mxChar *)mxGetData(a))[1] = 0;
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(text), a));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "hello world"));
	cell = mxCreateCellMatrix(1, 2);
	a = mt_str("voltage0_i0");
	((mxChar *)mxGetData(a))[10] = 0;
	mxSetCell(cell, 0, a);
	mxSetCell(cell, 1, mt_str("voltage0_q"));
	n = gw_handle_count();
	CHECK(mt_raises("iio:EINVAL", "conv_stream_open", 3, mt_u64(rx), cell, rx_opts(8, 2, "double", false, -1)));
	CHECK(gw_handle_count() == n);

	/* tStrings.charAndStringAgree: the text an attribute was given is the text it has, the
	 * empty text included; two contexts of one file have the same XML */
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(text), mt_str("abc")) == 4);
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "abc"));
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(text), mt_str("x y")) == 4);
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "x y"));
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(text), mt_str("")) == 1);
	CHECK(mt_str_of("attr_read_raw", 1, mt_u64(text)) && !strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), ""));
	{
		uint64_t other = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
		char *xml = strdup(mt_str_of("context_get_xml", 1, mt_u64(ctx)));

		CHECK(xml && strlen(xml) > 9000 && !strcmp(xml, mt_str_of("context_get_xml", 1, mt_u64(other))));
		free(xml);
		mt_ok("context_destroy", 1, mt_u64(other));
	}

	/* tStrings.emptyAndNonAscii: char([181 86]) is written as UTF-8 and read back as the same
	 * two characters; the bytes carry the NUL that ends every attribute value */
	a = mt_str("uV");
	((mxChar *)mxGetData(a))[0] = 181;
	CHECK(mt_dbl_of("attr_write_string", 2, mt_u64(text), a) == 4);
	{
		static const uint8_t utf8[4] = { 0xC2, 0xB5, 'V', 0 };
		mxArray *out[1] = { NULL };

		CHECK(mt_call(1, out, "attr_read_raw", 1, mt_u64(text)) == 0);
		CHECK(mxIsChar(out[0]) && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 2);
		CHECK(((const mxChar *)mxGetData(out[0]))[0] == 181 && ((const mxChar *)mxGetData(out[0]))[1] == 'V');
		mxDestroyArray(out[0]);
		CHECK(mt_call(1, out, "attr_read_raw", 2, mt_u64(text), mt_bool(true)) == 0);
		CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 4);
		CHECK(mxGetNumberOfElements(out[0]) == 4 && !memcmp(mxGetData(out[0]), utf8, 4));
		mxDestroyArray(out[0]);
	}
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("")) == 0);
	CHECK(mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("")) == 0);
	/* bytes with a zero go through attr_write_raw */
	CHECK(mt_dbl_of("attr_write_raw", 2, mt_u64(text), mt_bytes("a\0b", 3)) == 3);

	/* tLifetime.constructorFailureLeavesNoHandle: an unknown channel, then a failure of the
	 * open itself (Gain = 0), which comes after the buffer was chosen */
	cell = mxCreateCellMatrix(1, 1);
	mxSetCell(cell, 0, mt_str("nope"));
	n = gw_handle_count();
	CHECK(mt_raises("iio:ENOENT", "conv_stream_open", 3, mt_u64(rx), cell, rx_opts(16, 2, "double", false, -1)));
	CHECK(gw_handle_count() == n);          /* tStrings.missingString: no buffer was chosen yet */
	for (int i = 0; i < 50; i++) {
		cell = mxCreateCellMatrix(1, 1);
		mxSetCell(cell, 0, mt_str("nope"));
		CHECK(mt_raises("iio:ENOENT", "conv_stream_open", 3, mt_u64(rx), cell, rx_opts(16, 2, "double", false, -1)));
	}
	CHECK(gw_handle_count() == n);
	for (int i = 0; i < 51; i++) {
		CHECK(mt_raises("iio:EINVAL", "conv_stream_open", 3, mt_u64(rx), iq0(), opts(11,
			"output", mt_bool(false), "buffer", mt_dbl(-1), "samples", mt_dbl(16), "nb_blocks", mt_dbl(2),
			"class", mt_str("double"), "complex", mt_bool(false), "scale", mt_str("none"),
			"raw", mt_bool(false), "gain", mt_dbl(0), "offset", none(), "timeout_ms", mt_dbl(-1))));
		if (i == 0)
			n = gw_handle_count();
	}
	CHECK(gw_handle_count() == n);

	/* tLifetime.childAfterContextDelete and scopeExitAnyOrder: the context goes first, the
	 * objects of the test method are deleted afterwards */
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(tx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(tx, "voltage0_i", true)), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(find_chn(tx, "voltage0_q", true)), mt_u64(mask));
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf0(tx)), mt_u64(mask));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(16));
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts(16, 2, "int16", true, -1));
	CHECK(mask && bs && blk && cs);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(mt_raises("iio:EBADF", "attr_read_raw", 1, mt_u64(text)));
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
	mt_ok("block_destroy", 1, mt_u64(blk));
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* ------------------------------------------------------------------ wave 2b: tToolboxFlow, tExamples, tTiming */

/* cellstr(channels) of iio.RxStream / iio.TxStream: a 1-by-n cell of channel ids */
static mxArray *id_cell(int n, ...)
{
	mxArray *a = mxCreateCellMatrix(1, (size_t)n);
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		mxSetCell(a, (size_t)i, mt_str(va_arg(ap, const char *)));
	va_end(ap);
	return a;
}

/* the struct iio.RxStream builds, with the options MiniRx and tTiming set */
static mxArray *rx_opts_as(double samples, const char *cls, bool cplx, const char *scale, bool raw)
{
	return opts(11, "output", mt_bool(false), "buffer", mt_dbl(-1), "samples", mt_dbl(samples),
		"nb_blocks", mt_dbl(2), "class", mt_str(cls), "complex", mt_bool(cplx),
		"scale", mt_str(scale), "raw", mt_bool(raw), "gain", none(), "offset", none(),
		"timeout_ms", mt_dbl(-1));
}

/* the struct iio.TxStream builds for a complex stream */
static mxArray *tx_opts_as(double samples, const char *scale, bool cyclic)
{
	return opts(11, "output", mt_bool(true), "buffer", mt_dbl(-1), "samples", mt_dbl(samples),
		"nb_blocks", mt_dbl(2), "cyclic", mt_bool(cyclic), "complex", mt_bool(true),
		"scale", mt_str(scale), "raw", mt_bool(false), "gain", none(), "offset", none(),
		"timeout_ms", mt_dbl(-1));
}

/* [data, valid] = rx.read(): valid, and n-by-1 complex int16 whose row r is ramp sample k0 + r,
 * as the storage words (raw) or converted */
static bool frame_is_ramp(uint64_t cs, size_t n, unsigned int k0, bool raw)
{
	mxArray *out[2] = { NULL, NULL };
	bool ok = false;

	if (mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0 && out[0] && out[1]) {
		const int16_t *p = mxGetData(out[0]);

		ok = mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxINT16_CLASS &&
			mxIsComplex(out[0]) && mxGetM(out[0]) == n && mxGetN(out[0]) == 1;
		for (size_t r = 0; ok && r < n; r++) {
			unsigned int k = k0 + (unsigned int)r;

			if (raw)
				ok = p[2 * r] == (int16_t)fixture_ramp_word_i(k) && p[2 * r + 1] == (int16_t)fixture_ramp_word_q(k);
			else
				ok = p[2 * r] == fixture_ramp_i(k) && p[2 * r + 1] == fixture_ramp_q(k);
		}
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	return ok;
}

/* emu-tx_buf0.bin is n I/Q pairs with these values */
static bool tx_file_is(size_t n, const int16_t *iq)
{
	static int16_t got[1024];

	return n <= 512 && file_i16("emu-tx_buf0.bin", got, 1024) == (int)(2 * n) &&
		!memcmp(got, iq, 2 * n * sizeof(*iq));
}

/* MiniRx.setup up to the stream: context, timeout, devices, three attribute writes each read
 * back. Returns the context; *rx is emu-rx. */
static uint64_t mini_rx_setup(uint64_t *rx)
{
	uint64_t ctx, phy, a;

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(1000));
	*rx = find_dev(ctx, "emu-rx");
	phy = find_dev(ctx, "emu-phy");
	CHECK(ctx && *rx && phy);
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(phy, "voltage0", false)), mt_str("rf_bandwidth"));
	mt_ok("attr_write_longlong", 2, mt_u64(a), mt_i64(20000000));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(a)) == 20000000);
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(phy, "voltage0", false)), mt_str("gain_control_mode"));
	mt_ok("attr_write_string", 2, mt_u64(a), mt_str("manual"));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(a)), "manual"));
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(phy, "altvoltage0", true)), mt_str("frequency"));
	mt_ok("attr_write_longlong", 2, mt_u64(a), mt_i64(2450000000LL));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(a)) == 2450000000LL);
	return ctx;
}

/* MiniTx.setup: context, timeout, device, the tone frequency read back, the stream */
static uint64_t mini_tx_setup(double samples, bool cyclic, uint64_t *cs)
{
	uint64_t ctx, tx, a;

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(1000));
	tx = find_dev(ctx, "emu-tx");
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(tx, "altvoltage0", true)), mt_str("frequency"));
	mt_ok("attr_write_longlong", 2, mt_u64(a), mt_i64(2000000));
	CHECK(mt_i64_of("attr_read_longlong", 1, mt_u64(a)) == 2000000);
	*cs = mt_u64_of("conv_stream_open", 3, mt_u64(tx), iq0(), tx_opts_as(samples, "none", cyclic));
	CHECK(ctx && tx && *cs);
	return ctx;
}

/* MiniRx.release / MiniTx.release, and the count tToolboxFlow asserts after it */
static void mini_release(uint64_t ctx, uint64_t cs)
{
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* tToolboxFlow with MiniRx and MiniTx */
static void toolbox_flow(void)
{
	static int16_t A[512], B[512], zero_q[512];
	mxArray *out[2] = { NULL, NULL };
	uint64_t ctx, rx, phy, a, cs;

	/* controlPath: what the attributes refuse, the debug attribute, the register, no such device */
	ctx = mini_rx_setup(&rx);
	phy = find_dev(ctx, "emu-phy");
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(phy, "voltage0", false)), mt_str("rf_bandwidth"));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(a)), "20000000"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_longlong", 2, mt_u64(a), mt_dbl(100)));     /* a.write(100) */
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(a)), "20000000"));
	a = mt_u64_of("channel_find_attr", 2, mt_u64(find_chn(phy, "voltage0", false)), mt_str("gain_control_mode"));
	CHECK(mt_raises("iio:EINVAL", "attr_write_string", 2, mt_u64(a), mt_str("bogus")));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(a)), "manual"));
	a = mt_u64_of("device_find_debug_attr", 2, mt_u64(phy), mt_str("loopback"));
	mt_ok("attr_write_longlong", 2, mt_u64(a), mt_dbl(1));                               /* a.write(1) */
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(a)), "1"));
	mt_ok("device_reg_write", 3, mt_u64(phy), mt_dbl(16), mt_dbl(51966));
	CHECK(mt_call(1, out, "device_reg_read", 2, mt_u64(phy), mt_dbl(16)) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT32_CLASS && mxGetNumberOfElements(out[0]) == 1 &&
		*(uint32_t *)mxGetData(out[0]) == 51966);
	mxDestroyArray(out[0]);
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("hmc425a")) == 0);

	/* rxParityRaw: six frames of 1000 storage words, across the end of the file */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(1000, "int16", true, "none", true));
	for (unsigned int f = 0; f < 6; f++)
		CHECK(frame_is_ramp(cs, 1000, f * 1000, true));
	mini_release(ctx, cs);

	/* rxConverted, rxSurvivesWrap (frame 4 is samples 4000 .. 4999); the first three frames
	 * are also what the example rx_capture returns: its sample 2999 is 951 - 952i */
	ctx = mini_rx_setup(&rx);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(1000, "int16", true, "none", false));
	for (unsigned int f = 0; f < 5; f++)
		CHECK(frame_is_ramp(cs, 1000, f * 1000, false));
	CHECK(fixture_ramp_i(2999) == 951 && fixture_ramp_q(2999) == -952);
	mini_release(ctx, cs);
	/* rxReleaseAndReconfigure: a new frame size starts at sample 0 again; release twice */
	ctx = mini_rx_setup(&rx);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(256, "int16", true, "none", false));
	CHECK(frame_is_ramp(cs, 256, 0, false));
	mini_release(ctx, cs);
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("context_destroy", 1, mt_u64(ctx));

	/* rxPairingFollowsIdOrder: Q then I */
	ctx = mini_rx_setup(&rx);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), id_cell(2, "voltage0_q", "voltage0_i"),
		rx_opts_as(1000, "int16", true, "none", false));
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[1]) && mxIsComplex(out[0]) && mxGetM(out[0]) == 1000 && mxGetN(out[0]) == 1);
	for (unsigned int r = 0; r < 1000; r++) {
		const int16_t *p = mxGetData(out[0]);

		CHECK(p[2 * r] == fixture_ramp_q(r) && p[2 * r + 1] == fixture_ramp_i(r));
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mini_release(ctx, cs);

	/* rxTwoComplexColumnsDouble: four ids, full scale: column c of row s is ramp sample 2s + c */
	ctx = mini_rx_setup(&rx);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx),
		id_cell(4, "voltage0_i", "voltage0_q", "voltage1_i", "voltage1_q"),
		rx_opts_as(1000, "double", true, "fullscale", false));
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxIsComplex(out[0]) &&
		mxGetM(out[0]) == 1000 && mxGetN(out[0]) == 2);
	{
		const double *p = mxGetData(out[0]);

		CHECK(p[0] == -1 && p[1] == 0.99951171875 && p[2000] == -0.99951171875 && p[2001] == 0.9990234375);
		for (unsigned int s = 0; s < 1000; s++)
			for (unsigned int c = 0; c < 2; c++)
				CHECK(p[2 * (1000 * c + s)] == fixture_ramp_i(2 * s + c) / 2048.0 &&
					p[2 * (1000 * c + s) + 1] == fixture_ramp_q(2 * s + c) / 2048.0);
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mini_release(ctx, cs);

	/* rxRealIioScale: voltage2 alone, single, the scale and offset of the channel */
	ctx = mini_rx_setup(&rx);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), id_cell(1, "voltage2"),
		rx_opts_as(1000, "single", false, "iio", false));
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	CHECK(mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxSINGLE_CLASS && !mxIsComplex(out[0]) &&
		mxGetM(out[0]) == 1000 && mxGetN(out[0]) == 1);
	{
		static const float six[6] = { 103, 102.75f, 359, 102.75f, 615, 102.75f };

		CHECK(!memcmp(mxGetData(out[0]), six, sizeof(six)));
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mini_release(ctx, cs);

	/* txComplexInt16: A(k) = (100 + k) - (100 + k)i, 256 rows; step, flush, the file */
	for (int k = 0; k < 256; k++) {
		A[2 * k] = (int16_t)(100 + k);
		A[2 * k + 1] = (int16_t)-(100 + k);
		B[2 * k] = (int16_t)(1100 + k);         /* A + 1000: the real part only */
		B[2 * k + 1] = (int16_t)-(100 + k);
		zero_q[2 * k] = (int16_t)(k + 1);
	}
	ctx = mini_tx_setup(256, false, &cs);
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(256, A)));
	CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
	CHECK(tx_file_is(256, A));
	/* txWrongShape: two columns for one, a real 256-by-2 array, 257 rows; then a valid frame */
	{
		mxArray *m = mxCreateNumericMatrix(256, 2, mxINT16_CLASS, mxCOMPLEX);

		CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), m));
		m = mxCreateNumericMatrix(256, 2, mxINT16_CLASS, mxREAL);
		CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), m));
		m = mxCreateNumericMatrix(257, 1, mxINT16_CLASS, mxCOMPLEX);
		CHECK(mt_raises("iio:EINVAL", "conv_stream_write", 2, mt_u64(cs), m));
	}
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(256, B)));
	CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
	CHECK(tx_file_is(256, B));
	/* txRealInputToComplexStream: real double 1 .. 256 is I, and Q is 0 */
	{
		mxArray *m = mxCreateDoubleMatrix(256, 1, mxREAL);

		for (int k = 0; k < 256; k++)
			((double *)mxGetData(m))[k] = k + 1;
		CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), m));
	}
	CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
	CHECK(tx_file_is(256, zero_q));
	/* txLastFrameOnRelease: two frames, no flush: the close sends them, the file is the second */
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(256, A)));
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(256, B)));
	mini_release(ctx, cs);
	CHECK(tx_file_is(256, B));

	/* txDoubleRoundsAndSaturates: halves to even, saturation, NaN is 0 */
	{
		static const double in[9] = { 0.4, 0.5, 1.5, 2.5, -0.5, -1.5, 40000, -40000, NAN };
		static const int16_t want[18] = { 0, 0, 0, 0, 2, 0, 2, 0, 0, 0, -2, 0, 32767, 0, -32768, 0, 0, 0 };
		mxArray *m = mxCreateDoubleMatrix(9, 1, mxCOMPLEX);

		for (int k = 0; k < 9; k++)
			((double *)mxGetData(m))[2 * k] = in[k];
		ctx = mini_tx_setup(9, false, &cs);
		CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), m));
		CHECK(mt_bool_of("conv_stream_flush", 1, mt_u64(cs)));
		CHECK(tx_file_is(9, want));
		mini_release(ctx, cs);
	}

	/* txCyclicSecondWrite */
	ctx = mini_tx_setup(256, true, &cs);
	CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), ciq(256, A)));
	CHECK(mt_raises("iio:EBUSY", "conv_stream_write", 2, mt_u64(cs), ciq(256, A)));
	mini_release(ctx, cs);

	/* setupFailureReleases: no such device ends before anything is opened; a stream that
	 * does not open leaves nothing once the context is gone */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	mt_ok("context_set_timeout", 2, mt_u64(ctx), mt_dbl(1000));
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("nope")) == 0);
	rx = find_dev(ctx, "emu-rx");
	CHECK(mt_raises("iio:ENOENT", "conv_stream_open", 3, mt_u64(rx), id_cell(2, "voltage0_i", "nope"),
		rx_opts_as(1000, "int16", true, "none", false)));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* The five functions of matlab/examples as tExamples calls them (rx_capture: the converted
 * frames of toolbox_flow) */
static void examples(void)
{
	static const char *const names[14] = { "calib_mode", "calib_mode_available", "dbl", "empty", "ensm_mode",
		"ensm_mode_available", "flag", "frac", "frac_available", "gain", "gain_available", "hex",
		"novalue", "text" };
	mxArray *out[2] = { NULL, NULL };
	uint64_t ctx, phy, rx, tx, text, mask, bs, blk, cs, s;
	const char *v;

	/* attributes: the list in libiio's order, one value each; novalue cannot be read */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	phy = find_dev(ctx, "emu-phy");
	CHECK(id_at("device_get_attrs", phy, 14) == 0);
	for (size_t k = 0; k < 14; k++) {
		uint64_t a = id_at("device_get_attrs", phy, k);

		CHECK(a != 0 && !strcmp(mt_str_of("attr_get_name", 1, mt_u64(a)), names[k]));
		if (k == 12) {
			CHECK(mt_raises("iio:ENOENT", "attr_read_raw", 1, mt_u64(a)));
			continue;
		}
		v = mt_str_of("attr_read_raw", 1, mt_u64(a));
		CHECK(v != NULL);
		if (v && k == 3)
			CHECK(!strcmp(v, ""));
		if (v && k == 4)
			CHECK(!strcmp(v, "fdd"));
		if (v && k == 13)
			CHECK(!strcmp(v, "hello world"));
	}
	/* Set = "text" with text, a number and a logical: Attr.write chooses the command */
	text = mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("text"));
	mt_ok("attr_write_string", 2, mt_u64(text), mt_str("abc"));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "abc"));
	mt_ok("attr_write_longlong", 2, mt_u64(text), mt_dbl(2400000000.0));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "2400000000"));
	mt_ok("attr_write_bool", 2, mt_u64(text), mt_bool(true));
	CHECK(!strcmp(mt_str_of("attr_read_raw", 1, mt_u64(text)), "1"));
	CHECK(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("nope")) == 0);
	mt_ok("context_destroy", 1, mt_u64(ctx));

	/* low_level_blocks: mask, buffer stream, a block of 1000 samples, its bytes and its
	 * samples; the example releases in order, then its locals are deleted once more */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	rx = find_dev(ctx, "emu-rx");
	mask = mt_u64_of("create_channels_mask", 1, mt_u64(rx));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage0_i", false)), mt_u64(mask));
	mt_ok("channel_enable", 2, mt_u64(find_chn(rx, "voltage0_q", false)), mt_u64(mask));
	CHECK(find_chn(rx, "nope", false) == 0);        /* lowLevelBlocksUnknownChannel: M raises */
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf0(rx)), mt_u64(mask));
	CHECK(bs_sample_size(bs, buf0(rx)) == 4);
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(4000));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(0), mt_bool(false));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	CHECK(mt_call(1, out, "block_get_bytes", 3, mt_u64(blk), mt_dbl(0), mt_empty()) == 0);
	CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetNumberOfElements(out[0]) == 4000);
	{
		static const uint8_t first[4] = { 0, 8, 255, 7 };
		const uint16_t *w = mxGetData(out[0]);

		CHECK(!memcmp(mxGetData(out[0]), first, 4));
		for (unsigned int k = 0; k < 1000; k++)
			CHECK(w[2 * k] == fixture_ramp_word_i(k) && w[2 * k + 1] == fixture_ramp_word_q(k));
	}
	mxDestroyArray(out[0]);
	CHECK(block_is_ramp(blk, 1000, 0));
	mt_ok("block_destroy", 1, mt_u64(blk));
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	mt_ok("channels_mask_destroy", 1, mt_u64(mask));
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("block_destroy", 1, mt_u64(blk));

	/* tx_cyclic: a cyclic full-scale stream takes one complex double frame of 1024 rows */
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	tx = find_dev(ctx, "emu-tx");
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(tx), iq0(), tx_opts_as(1024, "fullscale", true));
	{
		mxArray *m = mxCreateDoubleMatrix(1024, 1, mxCOMPLEX);

		((double *)mxGetData(m))[0] = 0.5;
		CHECK(mt_bool_of("conv_stream_write", 2, mt_u64(cs), m));
	}
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	/* rxCaptureUnknownDevice, attributesUnknownNames: nothing is found, M raises */
	CHECK(mt_u64_of("context_find_device", 2, mt_u64(ctx), mt_str("nope")) == 0);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);

	/* list_contexts: LogLevel = "error" is log_level 2. The default, "local": 0 or 1 result,
	 * by the machine, and its uri is "local:". "usb,ip": the pinned libiio has no usb backend
	 * and no network scan, so there is never a result. "emu" cannot scan. */
	s = mt_u64_of("scan", 2, opts(1, "log_level", mt_dbl(2)), mt_str("local"));
	CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
	CHECK(mxIsCell(out[0]) && mxIsCell(out[1]) && mxGetNumberOfElements(out[0]) <= 1 &&
		mxGetNumberOfElements(out[1]) == mxGetNumberOfElements(out[0]));
	if (mxGetNumberOfElements(out[0]) == 1) {
		char *uri = mxArrayToString(mxGetCell(out[0], 0));

		CHECK(uri && !strcmp(uri, "local:"));
		mxFree(uri);
	}
	/* scan_contexts("") and scan_contexts(): M sends [] for both, C NULL, libiio's default
	 * list, which is "local" in the pinned build: the same contexts as the scan above. (''
	 * would be a list of no backend; test_cmd_toplevel has that.) */
	{
		size_t n_local = mxGetNumberOfElements(out[0]);
		mxArray *def[2];
		uint64_t s2;

		s2 = mt_u64_of("scan", 2, opts(1, "log_level", mt_dbl(1)), mt_empty());
		CHECK(mt_call(2, def, "scan_get_results", 1, mt_u64(s2)) == 0);
		CHECK(mxIsCell(def[0]) && mxGetNumberOfElements(def[0]) == n_local &&
			mxGetNumberOfElements(def[1]) == n_local);
		if (n_local == 1) {
			char *uri = mxArrayToString(mxGetCell(def[0], 0));

			CHECK(uri && !strcmp(uri, "local:"));
			mxFree(uri);
		}
		mxDestroyArray(def[0]);
		mxDestroyArray(def[1]);
		mt_ok("scan_destroy", 1, mt_u64(s2));
	}
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mt_ok("scan_destroy", 1, mt_u64(s));
	s = mt_u64_of("scan", 2, opts(1, "log_level", mt_dbl(2)), mt_str("usb,ip"));
	CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
	CHECK(mxGetNumberOfElements(out[0]) == 0 && mxGetNumberOfElements(out[1]) == 0);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mt_ok("scan_destroy", 1, mt_u64(s));
	s = mt_u64_of("scan", 2, opts(1, "log_level", mt_dbl(2)), mt_str("emu"));
	CHECK(mt_call(2, out, "scan_get_results", 1, mt_u64(s)) == 0);
	CHECK(mxGetNumberOfElements(out[0]) == 0 && mxGetNumberOfElements(out[1]) == 0);
	mxDestroyArray(out[0]);
	mxDestroyArray(out[1]);
	mt_ok("scan_destroy", 1, mt_u64(s));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* tTiming: a data file of exactly one frame of 262144 samples without junk bits, and the three
 * streams the test opens on it. Every frame is the file. This replaces the ramp: it runs last. */
static void timing_frames(void)
{
	enum { N = 262144 };
	int16_t *iq = malloc(2 * N * sizeof(*iq));
	mxArray *out[2] = { NULL, NULL };
	uint64_t ctx, rx, cs;

	if (!iq) {
		CHECK(iq != NULL);
		return;
	}
	for (unsigned int k = 0; k < N; k++) {
		iq[2 * k] = fixture_ramp_i(k);
		iq[2 * k + 1] = fixture_ramp_q(k);
	}
	CHECK(fixture_write_file(fx.dir, "emu-rx_buf0.bin", iq, 2 * N * sizeof(*iq)) == 0);
	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	rx = find_dev(ctx, "emu-rx");

	/* (a) int16, complex: the file as it is, frame after frame */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(N, "int16", true, "none", false));
	for (int f = 0; f < 3; f++) {
		CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
		CHECK(mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxINT16_CLASS && mxIsComplex(out[0]) &&
			mxGetM(out[0]) == N && mxGetN(out[0]) == 1 && !memcmp(mxGetData(out[0]), iq, 2 * N * sizeof(*iq)));
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	mt_ok("conv_stream_close", 1, mt_u64(cs));

	/* (b) int16, real, Raw: N-by-2, column 1 is I and column 2 is Q, equal to (a) without junk bits */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(N, "int16", false, "none", true));
	for (int f = 0; f < 2; f++) {
		bool same = true;

		CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
		CHECK(mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxINT16_CLASS && !mxIsComplex(out[0]) &&
			mxGetM(out[0]) == N && mxGetN(out[0]) == 2);
		for (unsigned int k = 0; same && k < N; k++)
			same = ((const int16_t *)mxGetData(out[0]))[k] == iq[2 * k] &&
				((const int16_t *)mxGetData(out[0]))[N + k] == iq[2 * k + 1];
		CHECK(same);
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	mt_ok("conv_stream_close", 1, mt_u64(cs));

	/* (c) double, complex, full scale: exactly double(x) / 2048 */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), iq0(), rx_opts_as(N, "double", true, "fullscale", false));
	for (int f = 0; f < 2; f++) {
		const double *p;
		bool same = true;

		CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
		CHECK(mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxIsComplex(out[0]) &&
			mxGetM(out[0]) == N && mxGetN(out[0]) == 1);
		p = mxGetData(out[0]);
		CHECK(p[0] == -1 && p[1] == 0.99951171875 && p[2] == -0.99951171875 && p[3] == 0.9990234375);
		for (unsigned int k = 0; same && k < 2 * N; k++)
			same = p[k] == iq[k] / 2048.0;
		CHECK(same);
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	mt_ok("conv_stream_close", 1, mt_u64(cs));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	free(iq);
}

int main(int argc, char **argv)
{
	uint64_t ctx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	fake_adopt();
	fake_coupled();
	fake_shutdown();
	fake_tx_events_trigger();

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx != 0);
	stream_views(ctx);
	stream_tx(ctx);
	buffer_blocks(ctx);
	gain_and_offset(ctx);
	masks_and_lowlevel(ctx);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);
	context_first();
	lifetime_and_strings();
	toolbox_flow();
	examples();
	timing_frames();
	CHECK(gw_handle_count() == 0);

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
