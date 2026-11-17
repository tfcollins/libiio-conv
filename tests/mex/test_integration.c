/* SPDX-License-Identifier: MIT */
/* The integration gate: every command group linked into one program, as in the MEX file, and
 * the failure modes that no single group can show: stale ids given to every registered
 * command, arguments nobody planned for, allocation failures, leaks over many contexts,
 * "clear mex" with a stream running, and a process with a decimal comma. The call sequences
 * of the M classes are in test_m_sequences.c. The same program runs as the test
 * mex_bundled_libs_integration with libiio bound to MATLAB's bundled libxml2 and libzstd. */
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

/* 1-by-n cell of char rows */
static mxArray *names(int n, ...)
{
	mxArray *a = mxCreateCellMatrix(1, (mwSize)n);
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		mxSetCell(a, (mwIndex)i, mt_str(va_arg(ap, const char *)));
	va_end(ap);
	return a;
}

/* the options of a receive stream of `samples` rows: int16, or single with the IIO scale */
static mxArray *rx_opts(double samples, bool cplx, bool iio_scale)
{
	return opts(6, "output", mt_bool(false), "samples", mt_dbl(samples), "nb_blocks", mt_dbl(2),
		"class", mt_str(iio_scale ? "single" : "int16"), "complex", mt_bool(cplx),
		"scale", mt_str(iio_scale ? "iio" : "none"));
}

static mxArray *tx_opts(double samples)
{
	return opts(4, "output", mt_bool(true), "samples", mt_dbl(samples), "nb_blocks", mt_dbl(1),
		"timeout_ms", mt_dbl(0));
}

static uint64_t emu_context(void)
{
	return mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
}

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

/* a mask of dev with these channels enabled */
static uint64_t mask_of(uint64_t dev, bool output, int n, ...)
{
	uint64_t mask = mt_u64_of("create_channels_mask", 1, mt_u64(dev));
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		mt_ok("channel_enable", 2, mt_u64(find_chn(dev, va_arg(ap, const char *), output)), mt_u64(mask));
	va_end(ap);
	return mask;
}

/* [data, valid] = conv_stream_read(cs) is a valid complex int16 n-by-1 frame whose row r is ramp
 * sample k0 + r */
static bool frame_is_ramp(uint64_t cs, size_t n, int k0)
{
	mxArray *out[2] = { NULL, NULL };
	bool ok = false;

	if (mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0) {
		const int16_t *p = mxGetData(out[0]);

		ok = mxIsLogicalScalarTrue(out[1]) && mxGetClassID(out[0]) == mxINT16_CLASS &&
			mxIsComplex(out[0]) && mxGetM(out[0]) == n && mxGetN(out[0]) == 1;
		for (size_t r = 0; ok && r < n; r++)
			ok = p[2 * r] == k0 + (int)r - 2048 && p[2 * r + 1] == 2047 - k0 - (int)r;
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	return ok;
}

/* ------------------------------------------------------------------ the registry */

static bool starts(const char *s, const char *prefix)
{
	return !strncmp(s, prefix, strlen(prefix));
}

static bool ends(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);

	return n >= m && !strcmp(s + n - m, suffix);
}

static bool one_of(const char *name, const char *const *list, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (!strcmp(name, list[i]))
			return true;
	return false;
}

/* a destructor command: any uint64 scalar, a stale id is a silent no-op */
static bool is_destructor(const char *name)
{
	return ends(name, "_destroy") || !strcmp(name, "buffer_close") || !strcmp(name, "conv_stream_close");
}

/* mexFunction(nlhs, out, {name, args[0 .. nargs-1]}): the arguments are destroyed, and so are
 * the outputs of a call that returned. Returns mock_call()'s result. */
static int call_args(const char *name, int nlhs, int nargs, mxArray **args)
{
	const mxArray *prhs[1 + 8];
	mxArray *out[4] = { NULL, NULL, NULL, NULL };
	int ret;

	prhs[0] = mxCreateString(name);
	for (int i = 0; i < nargs; i++)
		prhs[1 + i] = args[i];
	ret = mock_call(nlhs, out, 1 + nargs, prhs);
	if (ret == 0)
		for (int k = 0; k < (nlhs > 1 ? nlhs : 1); k++)
			if (out[k])
				mxDestroyArray(out[k]);
	for (int i = 0; i <= nargs; i++)
		mxDestroyArray((mxArray *)prhs[i]);
	return ret;
}

/* ------------------------------------------------------------------ staleIdsEverywhere */

struct stale {
	const char *what;
	uint64_t id;
};

static void stale_ids_everywhere(void)
{
	static const char *const skip[] = { "strerror", "has_backend", "get_builtin_backends", "scan",
		"create_context", "event_get_type", "event_get_direction" };
	struct stale ids[16];
	size_t n = 0, calls = 0;
	uint64_t ctx, rx, tx, multi, buf, mask, bs, blk, txmask, st, sblk, scan;

	/* a scan is a root of its own: made stale by its destructor */
	scan = mt_u64_of("scan", 2, mt_empty(), mt_str("emu"));
	CHECK(scan != 0);
	mt_ok("scan_destroy", 1, mt_u64(scan));
	ids[n++] = (struct stale){ "scan", scan };

	ctx = emu_context();
	ids[n++] = (struct stale){ "context", ctx };
	rx = find_dev(ctx, "emu-rx");
	ids[n++] = (struct stale){ "device", rx };
	ids[n++] = (struct stale){ "channel", find_chn(rx, "voltage0_i", false) };
	ids[n++] = (struct stale){ "attr", mt_u64_of("context_find_attr", 2, mt_u64(ctx), mt_str("hw_model")) };
	buf = id_at("device_get_buffers", rx, 0);
	ids[n++] = (struct stale){ "buffer", buf };
	mask = mask_of(rx, false, 2, "voltage0_i", "voltage0_q");
	ids[n++] = (struct stale){ "mask", mask };
	bs = mt_u64_of("buffer_open", 2, mt_u64(buf), mt_u64(mask));
	ids[n++] = (struct stale){ "buffer stream", bs };
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(64));
	mt_ok("block_enqueue", 3, mt_u64(blk), mt_dbl(64), mt_bool(false));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("block_dequeue", 2, mt_u64(blk), mt_bool(false)));
	ids[n++] = (struct stale){ "block", blk };
	ids[n++] = (struct stale){ "mask of a buffer stream",
		mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs)) };

	/* an iio_stream with its views, on another device */
	tx = find_dev(ctx, "emu-tx");
	txmask = mask_of(tx, true, 2, "voltage0_i", "voltage0_q");
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(id_at("device_get_buffers", tx, 0)), mt_dbl(2),
		mt_dbl(4), mt_u64(txmask));
	ids[n++] = (struct stale){ "stream", st };
	sblk = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	ids[n++] = (struct stale){ "block of a stream", sblk };
	ids[n++] = (struct stale){ "buffer stream of a stream",
		mt_u64_of("block_get_buffer_stream", 1, mt_u64(sblk)) };

	/* a fast-path stream on a third */
	multi = find_dev(ctx, "emu-multi");
	ids[n++] = (struct stale){ "conv stream",
		mt_u64_of("conv_stream_open", 3, mt_u64(multi), names(2, "voltage0", "voltage1"), tx_opts(4)) };

	for (size_t i = 1; i < n; i++) {
		CHECK(ids[i].id != 0);
		CHECK(gw_handle_type(ids[i].id) != GW_T_NONE);
	}
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());

	for (size_t g = 0; gw_groups[g]; g++) {
		for (size_t k = 0; k < gw_groups[g]->nb_cmds; k++) {
			const struct gw_cmd *cmd = &gw_groups[g]->cmds[k];

			if (one_of(cmd->name, skip, sizeof(skip) / sizeof(skip[0])) ||
			    starts(cmd->name, "mex_") || starts(cmd->name, "test_"))
				continue;
			CHECK(cmd->min_rhs >= 1 && cmd->min_rhs <= 8);
			for (size_t i = 0; i < n; i++) {
				mxArray *args[8];
				const char *id;
				int ret;

				args[0] = mt_u64(ids[i].id);
				for (int a = 1; a < cmd->min_rhs; a++)
					args[a] = mt_empty();
				ret = call_args(cmd->name, 0, cmd->min_rhs, args);
				id = mock_err_id();
				calls++;
				if (is_destructor(cmd->name) ? ret == 0 :
				    ret != 0 && (!strcmp(id, "iio:EBADF") || !strcmp(id, "iio:EINVAL")))
					continue;
				CHECK(!"a stale id");
				fprintf(stderr, "stale ids: %s(<%s>): %s %s\n", cmd->name, ids[i].what,
					ret ? id : "returned", ret ? mock_err_msg() : "");
			}
		}
	}
	CHECK(calls > 110 * n / 2);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());
}

/* ------------------------------------------------------------------ fuzzNoCrash */

enum { NB_KINDS = 19 };

struct pool {
	uint64_t dead_ctx, ctx, phy, chn, attr;
};

static mxArray *fuzz_arg(const struct pool *p, unsigned int kind)
{
	static const char *const field[] = { "log_level" };
	static const uint8_t four[4] = { 1, 2, 3, 4 };
	mxArray *a;

	switch (kind) {
	case 0: return mt_str("");
	case 1: return mt_str("emu-rx");
	case 2: return mt_str("voltage0_i");
	case 3: return mt_dbl(0);
	case 4: return mt_dbl(-1);
	case 5: return mt_dbl(1e300);
	case 6: return mt_bool(true);
	case 7: return mt_empty();
	case 8: return mt_u64(0);
	case 9: return mt_u64(p->dead_ctx);
	case 10: return mt_u64(p->ctx);
	case 11: return mt_u64(p->phy);
	case 12: return mt_u64(p->chn);
	case 13: return mt_u64(p->attr);
	case 14:
		a = mxCreateCellMatrix(1, 2);
		mxSetCell(a, 0, mt_u64(p->phy));
		mxSetCell(a, 1, mt_u64(p->chn));
		return a;
	case 15:
		a = mxCreateStructMatrix(1, 1, 1, (const char **)field);
		mxSetFieldByNumber(a, 0, 0, mt_dbl(1));
		return a;
	case 16: return mock_string_object();
	case 17: return mock_sparse_double(2, 2);
	default: return mt_bytes(four, 4);
	}
}

/* the call returned, or raised an identifier of the gateway */
static bool fuzz_call(const char *name, int nlhs, int nargs, mxArray **args, const unsigned int *kinds)
{
	bool ok = call_args(name, nlhs, nargs, args) == 0 || starts(mock_err_id(), "iio:");

	if (!ok) {
		fprintf(stderr, "fuzz: %s, %d output(s), argument kinds", name, nlhs);
		for (int i = 0; kinds && i < nargs; i++)
			fprintf(stderr, " %u", kinds[i]);
		fprintf(stderr, ": %s: %s\n", mock_err_id(), mock_err_msg());
	}
	return ok;
}

static void fuzz_no_crash(void)
{
	static const char *const skip[] = { "create_context", "scan", "context_destroy", "mex_shutdown" };
	const struct gw_cmd *cmds[200];
	size_t nb = 0, returned = 0, guided = 0;
	uint64_t state = 0x1105;
	struct pool p;

	for (size_t g = 0; gw_groups[g]; g++)
		for (size_t k = 0; k < gw_groups[g]->nb_cmds; k++) {
			const struct gw_cmd *cmd = &gw_groups[g]->cmds[k];

			if (!one_of(cmd->name, skip, sizeof(skip) / sizeof(skip[0])) &&
			    !starts(cmd->name, "test_") && nb < sizeof(cmds) / sizeof(cmds[0]))
				cmds[nb++] = cmd;
		}
	CHECK(nb == 120);       /* the 129 registered commands less the four above and the five test_ */

	p.dead_ctx = emu_context();
	mt_ok("context_destroy", 1, mt_u64(p.dead_ctx));
	p.ctx = emu_context();
	p.phy = find_dev(p.ctx, "emu-phy");       /* no buffer: nothing here can reach a dequeue */
	p.chn = find_chn(p.phy, "voltage0", false);   /* not a scan element: a data format of length 0 */
	p.attr = mt_u64_of("device_find_attr", 2, mt_u64(p.phy), mt_str("text"));
	CHECK(p.dead_ctx && p.ctx && p.phy && p.chn && p.attr);
	CHECK(id_at("device_get_buffers", p.phy, 0) == 0);

	/* what a random draw would rarely hit: a channel without a storage format and data for it */
	for (size_t i = 0; i < nb; i++) {
		for (int empty = 0; empty < 2 && cmds[i]->min_rhs >= 2; empty++) {
			mxArray *args[8];

			args[0] = mt_u64(p.chn);
			args[1] = empty ? mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL) : fuzz_arg(&p, 18);
			for (int a = 2; a < cmds[i]->min_rhs; a++)
				args[a] = mt_empty();
			CHECK(fuzz_call(cmds[i]->name, 1, cmds[i]->min_rhs, args, NULL));
		}
	}

	/* any number of arguments of any kind: nearly every call ends at the argument count or at
	 * the first argument */
	for (int i = 0; i < 3000; i++) {
		const struct gw_cmd *cmd = cmds[rng_range(&state, nb)];
		int nargs = (int)rng_range(&state, 7);
		int nlhs = (int)rng_range(&state, 4);
		unsigned int kinds[6];
		mxArray *args[6];

		for (int a = 0; a < nargs; a++) {
			kinds[a] = (unsigned int)rng_range(&state, NB_KINDS);
			args[a] = fuzz_arg(&p, kinds[a]);
		}
		CHECK(fuzz_call(cmd->name, nlhs, nargs, args, kinds));
		if (!mock_err_id()[0])
			returned++;
	}
	CHECK(returned >= 1);

	/* and past those two guards: an argument count the command accepts, no more outputs than
	 * it has, and for half of the calls an id (zero, stale or live) in the first place */
	for (int i = 0; i < 3000; i++) {
		const struct gw_cmd *cmd = cmds[rng_range(&state, nb)];
		int nargs = cmd->min_rhs + (int)rng_range(&state, (uint64_t)(cmd->max_rhs - cmd->min_rhs) + 1);
		int nlhs = (int)rng_range(&state, (uint64_t)cmd->max_lhs + 1);
		unsigned int kinds[6];
		mxArray *args[6];

		if (nargs > 6)
			continue;
		for (int a = 0; a < nargs; a++) {
			kinds[a] = (unsigned int)rng_range(&state, NB_KINDS);
			if (a == 0 && rng_range(&state, 2))
				kinds[a] = 8 + (unsigned int)rng_range(&state, 6);
			args[a] = fuzz_arg(&p, kinds[a]);
		}
		CHECK(fuzz_call(cmd->name, nlhs, nargs, args, kinds));
		if (!mock_err_id()[0])
			guided++;
	}
	CHECK(guided >= 200);                   /* the pool reaches the commands, not only their guards */
	CHECK(gw_handle_type(p.ctx) == GW_T_CONTEXT && gw_handle_type(p.dead_ctx) == GW_T_NONE);
	mt_ok("mex_shutdown", 0);               /* the context and every mask the calls made */
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* ------------------------------------------------------------------ oomSweep */

/* the sweep failed at least once for lack of memory and its last attempt returned */
#define SWEPT(sweep) do { CHECK((sweep) >= 1); CHECK(mock_err_id()[0] == '\0'); } while (0)

static void oom_sweep(void)
{
	uint64_t ctx, rx, tx, phy, multi, rxbuf, mask, bs, cs;
	size_t before;

	CHECK(gw_handle_count() == 0);
	SWEPT(mt_oom_sweep(1, "create_context", 2, mt_empty(), mt_str(fx.uri)));
	CHECK(gw_handle_count() == 1);          /* a failed attempt registers nothing and creates nothing */
	mt_ok("mex_shutdown", 0);
	CHECK(gw_handle_count() == 0);

	ctx = emu_context();
	SWEPT(mt_oom_sweep(1, "context_find_device", 2, mt_u64(ctx), mt_str("emu-phy")));
	SWEPT(mt_oom_sweep(1, "context_get_devices", 1, mt_u64(ctx)));
	SWEPT(mt_oom_sweep(1, "context_get_xml", 1, mt_u64(ctx)));
	phy = find_dev(ctx, "emu-phy");
	rx = find_dev(ctx, "emu-rx");
	tx = find_dev(ctx, "emu-tx");
	multi = find_dev(ctx, "emu-multi");
	SWEPT(mt_oom_sweep(1, "device_find_channel", 3, mt_u64(phy), mt_str("voltage0"), mt_bool(false)));
	SWEPT(mt_oom_sweep(1, "device_find_attr", 2, mt_u64(phy), mt_str("text")));
	SWEPT(mt_oom_sweep(1, "device_get_channels", 1, mt_u64(phy)));
	SWEPT(mt_oom_sweep(1, "attr_read_raw", 1,
		mt_u64(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("text")))));
	SWEPT(mt_oom_sweep(1, "attr_get_available", 1,
		mt_u64(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("calib_mode_available")))));

	before = gw_handle_count();
	SWEPT(mt_oom_sweep(1, "create_channels_mask", 1, mt_u64(rx)));
	CHECK(gw_handle_count() == before + 1);

	/* buffer_open on emu-tx: the stream of the last attempt stays open there, with an id that
	 * nobody has, until the context goes */
	mask = mask_of(tx, true, 2, "voltage0_i", "voltage0_q");
	(void)id_at("device_get_buffers", tx, 0);
	before = gw_handle_count();
	SWEPT(mt_oom_sweep(1, "buffer_open", 2, mt_u64(id_at("device_get_buffers", tx, 0)), mt_u64(mask)));
	CHECK(gw_handle_count() == before + 1);

	rxbuf = id_at("device_get_buffers", rx, 0);
	mask = mask_of(rx, false, 2, "voltage0_i", "voltage0_q");
	bs = mt_u64_of("buffer_open", 2, mt_u64(rxbuf), mt_u64(mask));
	before = gw_handle_count();
	SWEPT(mt_oom_sweep(1, "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(64)));
	CHECK(gw_handle_count() == before + 1);
	mt_ok("buffer_close", 1, mt_u64(bs));

	/* the fast path: an open on emu-multi (its stream stays, as above), reads on emu-rx */
	(void)id_at("device_get_buffers", multi, 1);
	before = gw_handle_count();
	SWEPT(mt_oom_sweep(1, "conv_stream_open", 3, mt_u64(multi), names(2, "voltage0", "voltage1"), tx_opts(4)));
	CHECK(gw_handle_count() == before + 1);
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(rx), names(2, "voltage0_i", "voltage0_q"), rx_opts(16, true, false));
	CHECK(frame_is_ramp(cs, 16, 0));
	SWEPT(mt_oom_sweep(2, "conv_stream_read", 1, mt_u64(cs)));
	CHECK(frame_is_ramp(cs, 16, 32));       /* the sweep took one frame, not one per attempt */

	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);
}

/* ------------------------------------------------------------------ leakLoop */

static void leak_loop(void)
{
	size_t live_first = 0;

	for (int i = 1; i <= 200; i++) {
		uint64_t ctx = emu_context();
		uint64_t rx = find_dev(ctx, "emu-rx");
		uint64_t mask = mask_of(rx, false, 2, "voltage0_i", "voltage0_q");
		uint64_t bs = mt_u64_of("buffer_open", 2, mt_u64(id_at("device_get_buffers", rx, 0)), mt_u64(mask));
		uint64_t blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(64));

		if (!ctx || !rx || !mask || !bs || !blk) {
			CHECK(!"an object of the loop");
			break;
		}
		mt_ok("context_destroy", 1, mt_u64(ctx));
		if (i == 1) {
			CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
			live_first = mock_live();
		}
		if (i == 200) {
			CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
			CHECK(mock_live() == live_first);
		}
	}
	CHECK(!mock_is_locked());
}

/* ------------------------------------------------------------------ clearWhileStreaming */

static void clear_while_streaming(void)
{
	uint64_t ctx = emu_context();
	uint64_t cs = mt_u64_of("conv_stream_open", 3, mt_u64(find_dev(ctx, "emu-rx")),
		names(2, "voltage0_i", "voltage0_q"), rx_opts(16, true, false));

	CHECK(cs != 0);
	CHECK(frame_is_ramp(cs, 16, 0));
	mock_clear_mex();                       /* "clear mex": nothing happens while a handle exists */
	CHECK(mock_is_locked());
	CHECK(frame_is_ramp(cs, 16, 16));
	mock_clear_mex();
	CHECK(frame_is_ramp(cs, 16, 32));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(!mock_is_locked());
	CHECK(mt_raises("iio:EBADF", "conv_stream_read", 1, mt_u64(cs)));
	mock_clear_mex();                       /* now it unloads: the at-exit function runs */
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
}

/* ------------------------------------------------------------------ xmlOnBothBackends */

/* numel(context_get_xml(ctx)), and whether the text has this part; 0 after a raise */
static size_t xml_length(uint64_t ctx, const char *part, bool *has)
{
	mxArray *out[1] = { NULL };
	size_t n = 0;

	if (mt_call(1, out, "context_get_xml", 1, mt_u64(ctx)) == 0 && mxIsChar(out[0]) && mxGetM(out[0]) == 1) {
		char *s = mxArrayToUTF8String(out[0]);

		n = mxGetN(out[0]);
		if (has)
			*has = s && strstr(s, part) != NULL;
		mxFree(s);
	}
	if (out[0])
		mxDestroyArray(out[0]);
	return n;
}

static void xml_on_both_backends(void)
{
	const char *const uris[2] = { fx.xml_uri, fx.uri };

	for (int i = 0; i < 2; i++) {
		uint64_t ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(uris[i]));
		bool has = false;

		CHECK(ctx != 0);
		CHECK(xml_length(ctx, "<device id=\"iio:device4\"", &has) > 9000);
		CHECK(has);
		mt_ok("context_destroy", 1, mt_u64(ctx));
	}
	CHECK(gw_handle_count() == 0);
}

/* ------------------------------------------------------------------ commaLocale */

/* What every context shows of emu-rx/voltage2 (le:s14/16>>2, scale 0.25) whatever backend made
 * it: the scale of the data format, the conversion of one sample and the scale in the XML. */
static void voltage2_under_comma(uint64_t ctx)
{
	static const int16_t raw = 4;
	mxArray *out[1] = { NULL };
	uint64_t v2 = find_chn(find_dev(ctx, "emu-rx"), "voltage2", false);
	bool has = false;

	CHECK(v2 != 0);
	CHECK(mt_call(1, out, "channel_get_data_format", 1, mt_u64(v2)) == 0);
	if (out[0]) {
		const mxArray *scale = mxIsStruct(out[0]) ? mxGetField(out[0], 0, "scale") : NULL;
		const mxArray *with = mxIsStruct(out[0]) ? mxGetField(out[0], 0, "with_scale") : NULL;
		const mxArray *shift = mxIsStruct(out[0]) ? mxGetField(out[0], 0, "shift") : NULL;

		CHECK(scale && mxGetClassID(scale) == mxDOUBLE_CLASS && mxGetScalar(scale) == 0.25);
		CHECK(with && mxIsLogicalScalarTrue(with));
		CHECK(shift && mxGetScalar(shift) == 2);
		mxDestroyArray(out[0]);
	}
	/* iio_channel_convert shifts and sign-extends, it does not scale: 4 >> 2 is 1, and the
	 * value the format implies is 1 * 0.25 */
	CHECK(mt_call(1, out, "channel_convert", 2, mt_u64(v2), mt_bytes(&raw, 2)) == 0);
	if (out[0]) {
		CHECK(mxGetClassID(out[0]) == mxUINT8_CLASS && mxGetNumberOfElements(out[0]) == 2);
		CHECK(mxGetNumberOfElements(out[0]) == 2 && *(const int16_t *)mxGetData(out[0]) == 1);
		mxDestroyArray(out[0]);
	}
	CHECK(xml_length(ctx, "scale=\"0.250000\"", &has) > 9000);
	CHECK(has);
	CHECK(xml_length(ctx, "scale=\"0,", &has) > 9000);
	CHECK(!has);
}

static void comma_locale(void)
{
	static const float want[6] = { 103, 102.75f, 359, 102.75f, 615, 102.75f };
	mxArray *out[2] = { NULL, NULL };
	uint64_t ctx, phy, frac, cs, scan;
	const char *s;

	if (!mt_comma_begin())
		return;

	ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.xml_uri));
	CHECK(ctx != 0);
	voltage2_under_comma(ctx);
	mt_ok("context_destroy", 1, mt_u64(ctx));

	ctx = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(ctx != 0);
	voltage2_under_comma(ctx);
	mt_ok("context_destroy", 1, mt_u64(ctx));

	ctx = emu_context();
	CHECK(ctx != 0);
	voltage2_under_comma(ctx);
	phy = find_dev(ctx, "emu-phy");
	CHECK(mt_call(1, out, "attr_get_range", 1,
		mt_u64(mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("frac_available")))) == 0);
	if (out[0]) {
		const double *v = mxGetData(out[0]);

		CHECK(mxGetClassID(out[0]) == mxDOUBLE_CLASS && mxGetM(out[0]) == 1 && mxGetN(out[0]) == 3);
		CHECK(mxGetNumberOfElements(out[0]) == 3 && v[0] == 0 && v[1] == 0.25 && v[2] == 1);
		mxDestroyArray(out[0]);
	}
	frac = mt_u64_of("device_find_attr", 2, mt_u64(phy), mt_str("frac"));
	mt_ok("attr_write_double", 2, mt_u64(frac), mt_dbl(0.75));
	s = mt_str_of("attr_read_raw", 1, mt_u64(frac));
	CHECK(s && !strcmp(s, "0.75"));
	CHECK(mt_dbl_of("attr_read_double", 1, mt_u64(frac)) == 0.75);

	/* the fast path with the IIO scale: (v - 100) * 0.25 of "Test data" */
	cs = mt_u64_of("conv_stream_open", 3, mt_u64(find_dev(ctx, "emu-rx")), names(1, "voltage2"),
		rx_opts(6, false, true));
	CHECK(cs != 0);
	CHECK(mt_call(2, out, "conv_stream_read", 1, mt_u64(cs)) == 0);
	if (out[0]) {
		CHECK(mxIsLogicalScalarTrue(out[1]));
		CHECK(mxGetClassID(out[0]) == mxSINGLE_CLASS && !mxIsComplex(out[0]) && mxGetM(out[0]) == 6 &&
			mxGetN(out[0]) == 1);
		CHECK(mxGetNumberOfElements(out[0]) == 6 && !memcmp(mxGetData(out[0]), want, sizeof(want)));
		mxDestroyArray(out[0]);
		mxDestroyArray(out[1]);
	}
	mt_ok("conv_stream_close", 1, mt_u64(cs));

	scan = mt_u64_of("scan", 2, mt_empty(), mt_str("emu"));
	CHECK(scan != 0);
	mt_ok("scan_destroy", 1, mt_u64(scan));
	mt_ok("context_destroy", 1, mt_u64(ctx));
	CHECK(gw_handle_count() == 0);

	mt_comma_end();         /* no command left the locale changed */
}

int main(int argc, char **argv)
{
	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	stale_ids_everywhere();
	fuzz_no_crash();
	oom_sweep();
	leak_loop();
	clear_while_streaming();
	xml_on_both_backends();
	comma_locale();

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
