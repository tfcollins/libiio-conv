/* SPDX-License-Identifier: MIT */
/* Command group "test": a context on the fake backend, and what the backend saw (timeout,
 * attribute bytes, call log) and can be told (coupled channel, frames before "not ready").
 * The context is used through the libiio C API: no sibling group's command is needed. */
#include "mex_test.h"
MT_GROUPS(&gw_group_test);

/* the table "Group test" of the Command reference */
static const struct mt_arity arities[] = {
	{ "test_create_fake_context", 1, 1, 1 },
	{ "test_fake_last_timeout", 0, 0, 1 },
	{ "test_fake_last_write", 0, 0, 1 },
	{ "test_fake_set", 2, 2, 0 },
	{ "test_fake_log", 0, 0, 1 },
};

/* the text with its character k (0-based) replaced by a NUL */
static mxArray *nul_str(const char *text, size_t k)
{
	mxArray *a = mt_str(text);

	((mxChar *)mxGetData(a))[k] = 0;
	return a;
}

/* test_fake_last_write is a real 1-by-n uint8 with these bytes */
static bool last_write_is(const unsigned char *want, size_t n)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "test_fake_last_write", 0))
		return false;
	ok = mxGetClassID(out[0]) == mxUINT8_CLASS && !mxIsComplex(out[0]) &&
		mxGetNumberOfDimensions(out[0]) == 2 && mxGetM(out[0]) == 1 && mxGetN(out[0]) == n;
	if (ok && n)
		ok = !memcmp(mxGetData(out[0]), want, n);
	mxDestroyArray(out[0]);
	return ok;
}

/* test_fake_log is '' (0x0 char), not a 1x0 char and not [] */
static bool log_is_0x0_char(void)
{
	mxArray *out[1];
	bool ok;

	if (mt_call(1, out, "test_fake_log", 0))
		return false;
	ok = mxIsChar(out[0]) && mxGetM(out[0]) == 0 && mxGetN(out[0]) == 0;
	mxDestroyArray(out[0]);
	return ok;
}

static bool log_is(const char *want)
{
	const char *s = mt_str_of("test_fake_log", 0);

	return s && !strcmp(s, want);
}

static const struct iio_device *rx_of(uint64_t id)
{
	const struct iio_context *ctx = gw_handle_ptr(id, GW_T_CONTEXT);

	return ctx ? iio_context_find_device(ctx, "emu-rx") : NULL;
}

/* A mask of emu-rx with voltage0_i and voltage0_q enabled; NULL (and a failed CHECK) on error. */
static struct iio_channels_mask *iq_mask(const struct iio_device *dev)
{
	struct iio_channels_mask *mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	const struct iio_channel *i = iio_device_find_channel(dev, "voltage0_i", false);
	const struct iio_channel *q = iio_device_find_channel(dev, "voltage0_q", false);

	CHECK(mask && i && q);
	if (!mask || !i || !q) {
		if (mask)
			iio_channels_mask_destroy(mask);
		return NULL;
	}
	iio_channel_enable(i, mask);
	iio_channel_enable(q, mask);
	return mask;
}

/* Sample size of the mask a buffer stream of emu-rx buffer 0 really has when it is opened
 * with voltage0_i and voltage0_q; negative on error. The stream is closed again. */
static long opened_sample_size(uint64_t id)
{
	const struct iio_device *dev = rx_of(id);
	struct iio_channels_mask *mask = dev ? iq_mask(dev) : NULL;
	struct iio_buffer_stream *bs;
	long size;

	if (!mask)
		return -1;
	bs = iio_buffer_open(iio_device_get_buffer(dev, 0), mask);
	if (iio_err(bs)) {
		iio_channels_mask_destroy(mask);
		return iio_err(bs);
	}
	size = (long)iio_device_get_sample_size(dev, iio_buffer_stream_get_channels_mask(bs));
	iio_buffer_close(bs);
	iio_channels_mask_destroy(mask);
	return size;
}

/* scale 0.25 of emu-rx/voltage2, as libiio holds it for this context */
static bool voltage2_scaled(uint64_t id)
{
	const struct iio_device *dev = rx_of(id);
	const struct iio_channel *ch = dev ? iio_device_find_channel(dev, "voltage2", false) : NULL;
	const struct iio_data_format *f = ch ? iio_channel_get_data_format(ch) : NULL;

	return f && f->with_scale && f->scale == 0.25;
}

int main(int argc, char **argv)
{
	static unsigned char payload[47000];
	struct mt_fixture fx;
	struct iio_context *ctx;
	const struct iio_device *phy, *rx;
	const struct iio_attr *text;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *bs;
	struct iio_block *blk;
	char value[64];
	const char *s;
	uint64_t id, id2;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_test, arities, sizeof(arities) / sizeof(arities[0]));

	/* errors of test_create_fake_context: nothing is registered */
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, mt_str("/nonexistent.xml")));
	CHECK(gw_handle_count() == 0);
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, mt_dbl(3)));
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, nul_str(fx.xml, strlen(fx.xml) - 1)));
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());

	/* the context: an owned root on the fake backend, with the structure of the fixture */
	id = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(id != 0);
	CHECK(gw_handle_type(id) == GW_T_CONTEXT);
	CHECK(gw_handle_owned(id) && gw_handle_parent(id) == 0 && gw_handle_root(id) == id);
	CHECK(gw_handle_count() == 1);
	CHECK(mock_is_locked());
	ctx = gw_handle_ptr(id, GW_T_CONTEXT);
	CHECK(ctx != NULL);
	if (!ctx)
		return 1;
	CHECK(!strcmp(iio_context_get_name(ctx), "fake"));
	CHECK(!strcmp(iio_context_get_description(ctx), "Emulated fixture & test context (fake)"));
	CHECK(iio_context_get_devices_count(ctx) == 8);
	CHECK(voltage2_scaled(id));
	phy = iio_context_find_device(ctx, "emu-phy");
	rx = iio_context_find_device(ctx, "emu-rx");
	CHECK(phy && rx);
	if (!phy || !rx)
		return 1;

	/* the timeout reaches the backend */
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -12345);
	CHECK(iio_context_set_timeout(ctx, 250) == 0);
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == 250);
	CHECK(iio_context_set_timeout(ctx, -1) == 0);        /* what iio.Context.set_timeout(Inf) sends */
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -1);

	/* attributes: a read is answered by the fake; a write is kept by its byte count */
	text = iio_device_find_attr(phy, "text");
	CHECK(text != NULL);
	if (!text)
		return 1;
	CHECK(iio_attr_read_raw(text, value, sizeof(value)) == 10);
	CHECK(!strcmp(value, "fake:text"));
	CHECK(last_write_is(NULL, 0));
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (unsigned char)(i % 256);
	CHECK(iio_attr_write_raw(text, payload, sizeof(payload)) == (ssize_t)sizeof(payload));
	CHECK(last_write_is(payload, sizeof(payload)));
	CHECK(mt_oom_sweep(1, "test_fake_last_write", 0) >= 1);
	CHECK(last_write_is(payload, sizeof(payload)));
	CHECK(iio_attr_write_raw(text, "ab", 2) == 2);        /* the last write only */
	CHECK(last_write_is((const unsigned char *)"ab", 2));

	/* the log: one letter per backend call */
	CHECK(log_is_0x0_char());
	mask = iq_mask(rx);
	if (!mask)
		return 1;
	bs = iio_buffer_open(iio_device_get_buffer(rx, 0), mask);
	CHECK(iio_err(bs) == 0);
	if (iio_err(bs))
		return 1;
	CHECK(log_is("O"));
	CHECK(iio_device_get_sample_size(rx, iio_buffer_stream_get_channels_mask(bs)) == 4);
	iio_buffer_close(bs);
	iio_channels_mask_destroy(mask);
	CHECK(log_is("OXC"));
	CHECK(mt_oom_sweep(1, "test_fake_log", 0) >= 1);

	/* a coupled channel: the stream's mask is wider than the one it was opened with */
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str("voltage1_i"));
	CHECK(opened_sample_size(id) == 6);
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str("nope"));   /* no such channel: nothing more */
	CHECK(opened_sample_size(id) == 4);
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str("voltage1_i"));
	CHECK(opened_sample_size(id) == 6);
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str(""));
	CHECK(opened_sample_size(id) == 4);
	CHECK(log_is("OXCOXCOXCOXCOXC"));

	/* rx_frames: one dequeue delivers, the next is "not ready" */
	mt_ok("test_fake_set", 2, mt_str("rx_frames"), mt_dbl(1));
	mask = iq_mask(rx);
	if (!mask)
		return 1;
	bs = iio_buffer_open(iio_device_get_buffer(rx, 0), mask);
	CHECK(iio_err(bs) == 0);
	if (iio_err(bs))
		return 1;
	blk = iio_buffer_stream_create_block(bs, 16);
	CHECK(iio_err(blk) == 0);
	if (iio_err(blk))
		return 1;
	CHECK(iio_block_enqueue(blk, 0, false) == 0);
	CHECK(iio_buffer_stream_start(bs) == 0);
	CHECK(iio_block_dequeue(blk, true) == 0);
	CHECK(iio_block_enqueue(blk, 0, false) == 0);
	CHECK(iio_block_dequeue(blk, true) == -EBUSY);
	/* busy: the next dequeue is "not ready" whatever the frame count allows */
	mt_ok("test_fake_set", 2, mt_str("rx_frames"), mt_i64(3));
	mt_ok("test_fake_set", 2, mt_str("busy"), mt_dbl(1));
	CHECK(iio_block_dequeue(blk, true) == -EBUSY);
	CHECK(iio_block_dequeue(blk, true) == 0);
	mt_ok("test_fake_set", 2, mt_str("tx_frames"), mt_dbl(0));
	CHECK(log_is("OXCOXCOXCOXCOXCOBESDEddD"));
	iio_buffer_stream_cancel(bs);
	iio_buffer_stream_stop(bs);
	iio_block_destroy(blk);
	iio_buffer_close(bs);
	iio_channels_mask_destroy(mask);
	CHECK(log_is("OXCOXCOXCOXCOXCOBESDEddDXsFXC"));

	/* arguments of test_fake_set */
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("bogus"), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("busy"), mt_str("x")));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("rx_frames"), mt_dbl(1.5)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("rx_frames"), mt_dbl(-1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("tx_frames"), mt_dbl(2147483648.0)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"), mock_string_object()));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"), mt_empty()));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_dbl(1), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str(""), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"),
		mt_str("a-channel-id-that-is-longer-than-any-channel-id-of-a-fixture-can-be-0123456789")));
	/* the limit itself: 63 characters fit the 64-byte buffer, 64 do not */
	mt_ok("test_fake_set", 2, mt_str("couple"),
		mt_str("c23456789012345678901234567890123456789012345678901234567890123"));
	CHECK(opened_sample_size(id) == 4);      /* no such channel: nothing is coupled */
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"),
		mt_str("c234567890123456789012345678901234567890123456789012345678901234")));
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str(""));
	/* text with an embedded NUL is refused, not cut */
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, nul_str("busy?x", 4), mt_dbl(1)));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, nul_str("couple?", 6), mt_str("voltage1_i")));
	CHECK(mt_raises("iio:EINVAL", "test_fake_set", 2, mt_str("couple"), nul_str("voltage1_i?x", 10)));
	CHECK(opened_sample_size(id) == 4);      /* none of them changed the coupling */

	/* one fake context at a time: the fake's state is one set of statics */
	CHECK(mt_raises("iio:EBUSY", "test_create_fake_context", 1, mt_str(fx.xml)));
	CHECK(gw_handle_count() == 1);
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -1);   /* and nothing was reset */
	CHECK(last_write_is((const unsigned char *)"ab", 2));
	gw_handle_destroy(id);
	CHECK(gw_handle_count() == 0);
	s = mt_str_of("test_fake_log", 0);       /* the log of the destroyed context */
	CHECK(s && strlen(s) > 0 && s[strlen(s) - 1] == 'C');
	mt_ok("test_fake_set", 2, mt_str("couple"), mt_str("voltage1_i"));   /* forgotten by the next creation */
	id2 = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(id2 != 0 && id2 != id);
	CHECK(gw_handle_type(id) == GW_T_NONE);
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == -12345);
	CHECK(last_write_is(NULL, 0));
	CHECK(log_is_0x0_char());
	CHECK(opened_sample_size(id2) == 4);
	mt_ok("test_fake_set", 2, mt_str("rx_frames"), mt_dbl(0));
	id = id2;

	/* a creation that fails while a context is alive leaves that context and its state alone */
	CHECK(iio_context_set_timeout(gw_handle_ptr(id, GW_T_CONTEXT), 77) == 0);
	CHECK(mt_raises("iio:EBUSY", "test_create_fake_context", 1, mt_str("/nonexistent.xml")));
	CHECK(mt_dbl_of("test_fake_last_timeout", 0) == 77);
	CHECK(gw_handle_type(id) == GW_T_CONTEXT);
	/* a creation that fails with no context alive makes none, and the next one works */
	gw_handle_destroy(id);
	CHECK(mt_raises("iio:EINVAL", "test_create_fake_context", 1, mt_str("/nonexistent.xml")));
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());

	/* numeric locale: a process with a decimal comma. libiio reads the channel scales of the
	 * XML with strtof: without the gateway's "C" guard the scale 0.25 is 0, with no error. */
	if (mt_comma_begin()) {
		id = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
		CHECK(id != 0);
		CHECK(voltage2_scaled(id));
		gw_handle_destroy(id);
		mt_comma_end();
	}
	CHECK(gw_handle_count() == 0);

	/* an allocation failure while the context is created leaves no context behind, registered
	 * or not, and no "alive" mark: the attempt that gets its memory succeeds */
	CHECK(mt_oom_sweep(1, "test_create_fake_context", 1, mt_str(fx.xml)) >= 1);
	CHECK(gw_handle_count() == 1);
	CHECK(mt_raises("iio:EBUSY", "test_create_fake_context", 1, mt_str(fx.xml)));
	/* MATLAB's shutdown path destroys it and clears the mark */
	mt_ok("mex_shutdown", 0);
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());
	id = mt_u64_of("test_create_fake_context", 1, mt_str(fx.xml));
	CHECK(id != 0 && gw_handle_type(id) == GW_T_CONTEXT);
	/* left alive on purpose: MT_MAIN_RETURN proves that MATLAB's exit releases it */

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
