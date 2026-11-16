/* SPDX-License-Identifier: MIT */
/* Command group "buffer": what a buffer is (device, direction, attributes, scan elements) and
 * the life of a buffer stream: open, the read-only view of its mask, blocks, start / stop /
 * cancel, close. The order in which a close releases things is read from the fake backend's log. */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_buffer);

/* the table "Group buffer" of the Command reference */
static const struct mt_arity arities[] = {
	{ "buffer_get_device", 1, 1, 1 },
	{ "buffer_is_output", 1, 1, 1 },
	{ "buffer_get_attrs", 1, 1, 1 },
	{ "buffer_find_attr", 2, 2, 1 },
	{ "buffer_get_scan_elements", 1, 1, 1 },
	{ "buffer_open", 2, 2, 1 },
	{ "buffer_close", 1, 1, 0 },
	{ "buffer_stream_start", 1, 1, 0 },
	{ "buffer_stream_stop", 1, 1, 0 },
	{ "buffer_stream_cancel", 1, 1, 0 },
	{ "buffer_stream_get_channels_mask", 1, 1, 1 },
	{ "buffer_stream_create_block", 2, 2, 1 },
	{ "buffer_stream_is_started", 1, 1, 1 },
};

#define MAX_IDS 16

/* A list command: a 1-by-n uint64 copied to ids (n at most MAX_IDS), each element a borrowed
 * child of ctx with that type. Returns n, or -1 after a failure. */
static int list_of(const char *cmd, uint64_t buf, uint64_t ctx, enum gw_type type, uint64_t ids[MAX_IDS])
{
	mxArray *out[1];
	int n = -1;

	if (mt_call(1, out, cmd, 1, mt_u64(buf)))
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

static unsigned int count_of(const char *log, char letter)
{
	unsigned int n = 0;

	for (; *log; log++)
		n += *log == letter;
	return n;
}

/* enqueue and dequeue blk with libiio itself; the first 16-bit word of the data, or -1 */
static long first_word(uint64_t blk)
{
	struct iio_block *b = gw_handle_ptr(blk, GW_T_BLOCK);
	const unsigned char *p;

	if (!b || iio_block_enqueue(b, 0, false) || iio_block_dequeue(b, false))
		return -1;
	p = iio_block_start(b);
	return p[0] | (p[1] << 8);
}

/* identity of a buffer, its attributes and scan elements */
static void test_info(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, multi, rxb, txb, ids[MAX_IDS], again[MAX_IDS];
	const struct iio_buffer *b;
	size_t before;

	ctx = mt_open_context(fx->uri);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	multi = dp_dev(ctx, "emu-multi");
	rxb = dp_buf(rx, 0);
	txb = dp_buf(tx, 0);
	CHECK(ctx && rx && tx && multi && rxb && txb);
	b = gw_handle_ptr(rxb, GW_T_BUFFER);

	CHECK(mt_u64_of("buffer_get_device", 1, mt_u64(rxb)) == rx);
	CHECK(mt_u64_of("buffer_get_device", 1, mt_u64(txb)) == tx);
	CHECK(mt_u64_of("buffer_get_device", 1, mt_u64(dp_buf(multi, 1))) == multi);
	CHECK(mt_bool_of("buffer_is_output", 1, mt_u64(rxb)) == false);
	CHECK(mt_bool_of("buffer_is_output", 1, mt_u64(txb)) == true);
	CHECK(mt_bool_of("buffer_is_output", 1, mt_u64(dp_buf(multi, 0))) == false);
	CHECK(mt_bool_of("buffer_is_output", 1, mt_u64(dp_buf(multi, 1))) == true);

	/* attributes, libiio order; the same ids on every call */
	memset(ids, 0, sizeof(ids));
	if (list_of("buffer_get_attrs", rxb, ctx, GW_T_ATTR, ids) == 3) {
		static const char *const names[3] = { "data_available", "length_align_bytes", "watermark" };

		for (unsigned int k = 0; k < 3; k++) {
			CHECK(gw_handle_ptr(ids[k], GW_T_ATTR) == iio_buffer_get_attr(b, k));
			CHECK(!strcmp(iio_attr_get_name(gw_handle_ptr(ids[k], GW_T_ATTR)), names[k]));
		}
	} else {
		CHECK(!"buffer_get_attrs(rxb) is a list of 3 attributes");
	}
	{       /* what tBuffer.m reads through iio.Attr */
		char val[16] = "";

		CHECK(iio_attr_read_raw(iio_buffer_find_attr(b, "watermark"), val, sizeof(val)) > 0);
		CHECK(!strcmp(val, "1024"));
	}
	before = gw_handle_count();
	CHECK(list_of("buffer_get_attrs", rxb, ctx, GW_T_ATTR, again) == 3);
	CHECK(!memcmp(ids, again, 3 * sizeof(ids[0])));
	CHECK(gw_handle_count() == before);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(rxb), mt_str("watermark")) == ids[2]);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(rxb), mt_str("data_available")) == ids[0]);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(rxb), mt_str("nope")) == 0);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(rxb), mt_str("")) == 0);
	CHECK(list_of("buffer_get_attrs", txb, ctx, GW_T_ATTR, again) == 2);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(txb), mt_str("watermark")) == again[1]);
	CHECK(again[1] != ids[2]);
	CHECK(mt_u64_of("buffer_find_attr", 2, mt_u64(txb), mt_str("length_align_bytes")) == 0);
	{       /* text with an embedded NUL is refused, not cut to "watermark" */
		mxArray *name = mt_str("watermarkXy");

		((mxChar *)mxGetData(name))[9] = 0;
		CHECK(mt_raises("iio:EINVAL", "buffer_find_attr", 2, mt_u64(rxb), name));
	}
	CHECK(mt_raises("iio:EINVAL", "buffer_find_attr", 2, mt_u64(rxb), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "buffer_find_attr", 2, mt_u64(rxb), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "buffer_find_attr", 1, mt_u64(rxb)));

	/* scan elements: the ids of the device's channels */
	memset(ids, 0, sizeof(ids));
	CHECK(list_of("buffer_get_scan_elements", rxb, ctx, GW_T_CHANNEL, ids) == 5);
	CHECK(ids[4] == dp_chn(rx, "voltage2", false));
	CHECK(ids[0] == dp_chn(rx, "voltage0_i", false));
	for (unsigned int k = 0; k < 5; k++)
		CHECK(gw_handle_ptr(ids[k], GW_T_CHANNEL) == iio_buffer_get_scan_element(b, k));
	CHECK(list_of("buffer_get_scan_elements", txb, ctx, GW_T_CHANNEL, ids) == 4);
	CHECK(ids[0] == dp_chn(tx, "voltage0_i", true));

	/* allocation failures: nothing leaks, the ids stay the same */
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "buffer_get_device", 1, mt_u64(rxb)) >= 1);
	CHECK(mt_oom_sweep(1, "buffer_get_attrs", 1, mt_u64(rxb)) >= 1);
	CHECK(mt_oom_sweep(1, "buffer_find_attr", 2, mt_u64(rxb), mt_str("watermark")) >= 1);
	CHECK(mt_oom_sweep(1, "buffer_get_scan_elements", 1, mt_u64(rxb)) >= 1);
	CHECK(gw_handle_count() == before);

	/* a handle of another type, none, not a handle, a stale one */
	CHECK(mt_raises("iio:EBADF", "buffer_get_device", 1, mt_u64(rx)));
	CHECK(mt_raises("iio:EBADF", "buffer_is_output", 1, mt_u64(ctx)));
	CHECK(mt_raises("iio:EBADF", "buffer_get_attrs", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "buffer_find_attr", 2, mt_u64(rx), mt_str("watermark")));
	CHECK(mt_raises("iio:EBADF", "buffer_get_scan_elements", 1, mt_u64(ids[0])));
	CHECK(mt_raises("iio:EINVAL", "buffer_is_output", 1, mt_dbl((double)rxb)));
	gw_handle_destroy(ctx);
	CHECK(mt_raises("iio:EBADF", "buffer_get_device", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_get_attrs", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_get_scan_elements", 1, mt_u64(txb)));
	CHECK(gw_handle_count() == 0);
}

/* the life of a buffer stream on emu */
static void test_stream(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, rxb, txb, m, mtx, empty, bs, v, blk, blk2, q, view, vmask;
	struct iio_buffer_stream *raw;
	const struct iio_channels_mask *vm;
	size_t before;

	ctx = mt_open_context(fx->uri);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	rxb = dp_buf(rx, 0);
	txb = dp_buf(tx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	empty = dp_mask(rx, false, 0);
	q = dp_chn(rx, "voltage0_q", false);
	CHECK(ctx && rx && tx && rxb && txb && m && mtx && empty && q);

	/* open */
	CHECK(mt_raises("iio:EINVAL", "buffer_open", 2, mt_u64(rxb), mt_u64(empty)));
	CHECK(mt_raises("iio:EINVAL", "buffer_open", 2, mt_u64(rxb), mt_u64(mtx)));   /* a mask of emu-tx */
	CHECK(strstr(mock_err_msg(), "buffer_open: ") == mock_err_msg());
	CHECK(mt_raises("iio:EINVAL", "buffer_open", 2, mt_u64(txb), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "buffer_open", 2, mt_u64(rx), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "buffer_open", 2, mt_u64(rxb), mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_open", 2, mt_u64(rxb), mt_u64(0)));
	CHECK(mt_raises("iio:EINVAL", "buffer_open", 1, mt_u64(rxb)));
	before = gw_handle_count();
	bs = mt_u64_of("buffer_open", 2, mt_u64(rxb), mt_u64(m));
	CHECK(bs != 0);
	CHECK(gw_handle_type(bs) == GW_T_BUFFER_STREAM && gw_handle_owned(bs) && gw_handle_parent(bs) == rxb);
	CHECK(gw_handle_count() == before + 1);
	/* the stream has its own copy of the mask: the caller's may go at once */
	gw_handle_destroy(m);
	CHECK(gw_handle_type(m) == GW_T_NONE && gw_handle_type(bs) == GW_T_BUFFER_STREAM);

	/* the mask of the stream: a read-only view, a child of the stream, one id */
	v = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs));
	CHECK(v != 0 && v != m);
	CHECK(mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs)) == v);
	CHECK(gw_handle_type(v) == GW_T_MASK && !gw_handle_owned(v) && gw_handle_parent(v) == bs);
	vm = gw_handle_ptr(v, GW_T_MASK);
	CHECK(vm != NULL && vm == iio_buffer_stream_get_channels_mask(gw_handle_ptr(bs, GW_T_BUFFER_STREAM)));
	CHECK(vm != NULL && iio_channel_is_enabled(gw_handle_ptr(q, GW_T_CHANNEL), vm));
	CHECK(vm != NULL && !iio_channel_is_enabled(gw_handle_ptr(dp_chn(rx, "voltage2", false), GW_T_CHANNEL), vm));
	CHECK(dp_device_of(v) == gw_handle_ptr(rx, GW_T_DEVICE));
	CHECK(vm != NULL && iio_device_get_sample_size(gw_handle_ptr(rx, GW_T_DEVICE), vm) == 4);
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "buffer_stream_get_channels_mask", 1, mt_u64(bs)) >= 1);
	CHECK(gw_handle_count() == before);

	/* blocks; no start without one (libiio prints "Cannot start buffer before creating blocks.") */
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == false);
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_start", 1, mt_u64(bs)));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == false);
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(2)));   /* less than a sample */
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(0)));
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(-32)));
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32.5)));
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 2, mt_u64(bs), mt_str("32")));
	CHECK(mt_raises("iio:EINVAL", "buffer_stream_create_block", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_create_block", 2, mt_u64(rxb), mt_dbl(32)));
	before = gw_handle_count();
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	CHECK(blk != 0);
	CHECK(gw_handle_type(blk) == GW_T_BLOCK && gw_handle_owned(blk) && gw_handle_parent(blk) == bs);
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(gw_handle_count() == before + 1);
	blk2 = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_u64(64));
	CHECK(blk2 != 0 && blk2 != blk && gw_handle_parent(blk2) == bs);

	/* start; a second start does not reach libiio (it would rewind the emu data to word 0) */
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == true);
	CHECK(gw_handle_flags(bs) == BS_STARTED);
	CHECK(first_word(blk) == fixture_ramp_word_i(0));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == true);
	CHECK(first_word(blk) == fixture_ramp_word_i(8));

	/* cancel is not stop */
	mt_ok("buffer_stream_cancel", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == true);
	mt_ok("buffer_stream_stop", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == false);
	CHECK(gw_handle_flags(bs) == 0);
	mt_ok("buffer_stream_stop", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == false);
	/* ... and a start after a stop is a start: the data begins at word 0 again */
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == true);
	CHECK(first_word(blk) == fixture_ramp_word_i(0));

	/* a handle of another type */
	CHECK(mt_raises("iio:EBADF", "buffer_stream_start", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_stop", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_cancel", 1, mt_u64(v)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_is_started", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_get_channels_mask", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_close", 1, mt_u64(blk)));
	CHECK(mt_raises("iio:EBADF", "buffer_close", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EINVAL", "buffer_close", 1, mt_dbl(0)));
	CHECK(gw_handle_type(bs) == GW_T_BUFFER_STREAM && gw_handle_type(blk) == GW_T_BLOCK);

	/* close (started): the blocks and the mask view go with it */
	mt_ok("buffer_close", 1, mt_u64(bs));
	CHECK(gw_handle_type(bs) == GW_T_NONE);
	CHECK(gw_handle_ptr(blk, GW_T_BLOCK) == NULL);
	CHECK(gw_handle_ptr(blk2, GW_T_BLOCK) == NULL);
	CHECK(gw_handle_ptr(v, GW_T_MASK) == NULL);
	CHECK(gw_handle_type(rxb) == GW_T_BUFFER);
	mt_ok("buffer_close", 1, mt_u64(bs));
	mt_ok("buffer_close", 1, mt_u64(0));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_start", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_stop", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_cancel", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_is_started", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_get_channels_mask", 1, mt_u64(bs)));
	CHECK(mt_raises("iio:EBADF", "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32)));

	/* the buffer can be opened again */
	m = dp_mask(rx, false, 1, "voltage2");
	bs = mt_u64_of("buffer_open", 2, mt_u64(rxb), mt_u64(m));
	CHECK(bs != 0 && gw_handle_parent(bs) == rxb);
	mt_ok("buffer_close", 1, mt_u64(bs));

	/* a view: a buffer stream that belongs to something else (an iio_stream). Here: one
	 * opened with libiio and registered as a borrowed entry. */
	raw = iio_buffer_open(gw_handle_ptr(txb, GW_T_BUFFER), gw_handle_ptr(mtx, GW_T_MASK));
	CHECK(!iio_err(raw));
	if (!iio_err(raw)) {
		view = 0;
		CHECK(gw_handle_borrow(GW_T_BUFFER_STREAM, raw, mtx, &view) == 0);
		CHECK(view != 0 && !gw_handle_owned(view));
		CHECK(mt_raises("iio:EPERM", "buffer_stream_start", 1, mt_u64(view)));
		CHECK(mt_raises("iio:EPERM", "buffer_stream_stop", 1, mt_u64(view)));
		CHECK(mt_raises("iio:EPERM", "buffer_stream_create_block", 2, mt_u64(view), mt_dbl(32)));
		CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(view)) == false);
		mt_ok("buffer_stream_cancel", 1, mt_u64(view));
		vmask = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(view));
		CHECK(vmask != 0 && gw_handle_parent(vmask) == view && !gw_handle_owned(vmask));
		mt_ok("buffer_close", 1, mt_u64(view));
		CHECK(gw_handle_ptr(view, GW_T_BUFFER_STREAM) == raw);
		CHECK(gw_handle_type(vmask) == GW_T_MASK);
		gw_handle_destroy(view);       /* forgets the entries; no op of a borrowed entry runs */
		CHECK(gw_handle_type(view) == GW_T_NONE && gw_handle_type(vmask) == GW_T_NONE);
		iio_buffer_close(raw);
	}

	/* a context destroyed with a started stream and blocks */
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	bs = mt_u64_of("buffer_open", 2, mt_u64(rxb), mt_u64(m));
	blk = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	v = mt_u64_of("buffer_stream_get_channels_mask", 1, mt_u64(bs));
	CHECK(bs && blk && v);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EBADF", "buffer_stream_is_started", 1, mt_u64(bs)));
	mt_ok("buffer_close", 1, mt_u64(bs));

	/* the xml: backend cannot open a buffer */
	ctx = mt_open_context(fx->xml_uri);
	rx = dp_dev(ctx, "emu-rx");
	rxb = dp_buf(rx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	before = gw_handle_count();
	CHECK(mt_raises("iio:ENOSYS", "buffer_open", 2, mt_u64(rxb), mt_u64(m)));
	CHECK(gw_handle_count() == before);
	CHECK(mt_bool_of("buffer_is_output", 1, mt_u64(rxb)) == false);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
}

/* One fake context: buffer_open, nb_blocks blocks, a start if asked; then the stream is closed
 * by buffer_close, or the context is destroyed with everything open. The backend calls, in order. */
static void fake_case(const struct mt_fixture *fx, unsigned int nb_blocks, bool start, bool close,
		const char *want)
{
	uint64_t ctx, rx, bs, blk[2] = { 0, 0 };

	ctx = dp_open_fake(fx->xml);
	rx = dp_dev(ctx, "emu-rx");
	bs = mt_u64_of("buffer_open", 2, mt_u64(dp_buf(rx, 0)),
			mt_u64(dp_mask(rx, false, 2, "voltage0_i", "voltage0_q")));
	CHECK(ctx && rx && bs);
	for (unsigned int i = 0; i < nb_blocks && i < 2; i++) {
		blk[i] = mt_u64_of("buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(32));
		CHECK(blk[i] != 0);
	}
	if (start) {
		mt_ok("buffer_stream_start", 1, mt_u64(bs));
		mt_ok("buffer_stream_start", 1, mt_u64(bs));       /* one S in the log */
	} else {
		mt_ok("buffer_stream_stop", 1, mt_u64(bs));        /* no s in the log */
	}
	if (close) {
		mt_ok("buffer_close", 1, mt_u64(bs));
		CHECK(gw_handle_type(bs) == GW_T_NONE && gw_handle_type(blk[0]) == GW_T_NONE);
		CHECK(gw_handle_type(ctx) == GW_T_CONTEXT);
		CHECK(!strcmp(fake.log, want));
	}
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
	CHECK(!strcmp(fake.log, want));
	if (strcmp(fake.log, want))
		fprintf(stderr, "fake log: %s, expected %s\n", fake.log, want);
}

/* stop and start as the backend sees them, and the commands that create an object when memory runs out */
static void test_fake(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, rxb, m, bs;
	unsigned int n;
	size_t before;

	fake_case(fx, 2, true, false, "OBBSXsFFXC");    /* stopped before the blocks go and the buffer closes */
	fake_case(fx, 1, true, true, "OBSXsFXC");
	fake_case(fx, 1, false, true, "OBXFXC");

	ctx = dp_open_fake(fx->xml);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	rxb = dp_buf(rx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	CHECK(ctx && rx && tx && rxb && m);

	/* buffer_open: the attempts that ran out of memory opened nothing */
	CHECK(!strcmp(fake.log, ""));
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "buffer_open", 2, mt_u64(rxb), mt_u64(m)) >= 1);
	CHECK(count_of(fake.log, 'O') == 1 && strlen(fake.log) == 1);
	CHECK(gw_handle_count() == before + 1);         /* the stream of the attempt that succeeded */

	/* buffer_stream_create_block */
	bs = mt_u64_of("buffer_open", 2, mt_u64(dp_buf(tx, 0)),
			mt_u64(dp_mask(tx, true, 2, "voltage0_i", "voltage0_q")));
	CHECK(bs != 0);
	n = count_of(fake.log, 'B');
	before = gw_handle_count();
	CHECK(mt_oom_sweep(1, "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(16)) >= 1);
	CHECK(count_of(fake.log, 'B') == n + 1);
	CHECK(gw_handle_count() == before + 1);
	CHECK(!strcmp(fake.log, "OOB"));

	/* start, stop, start, stop, cancel, as the backend sees them */
	fake_clear_log();
	mt_ok("buffer_stream_stop", 1, mt_u64(bs));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	mt_ok("buffer_stream_stop", 1, mt_u64(bs));
	mt_ok("buffer_stream_stop", 1, mt_u64(bs));
	mt_ok("buffer_stream_start", 1, mt_u64(bs));
	mt_ok("buffer_stream_cancel", 1, mt_u64(bs));
	CHECK(!strcmp(fake.log, "SsSX"));
	CHECK(mt_bool_of("buffer_stream_is_started", 1, mt_u64(bs)) == true);

	/* MATLAB exits with both streams open: the started one is stopped, both are closed */
	fake_clear_log();
	mock_exit_matlab();
	CHECK(gw_handle_count() == 0);
	CHECK(!strcmp(fake.log, "XsFXCXXC"));
	if (test_failures)
		fprintf(stderr, "fake log at exit: %s\n", fake.log);
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_buffer, arities, sizeof(arities) / sizeof(arities[0]));

	test_info(&fx);
	test_stream(&fx);
	test_fake(&fx);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
