/* SPDX-License-Identifier: MIT */
/* Command group "stream": the iio_stream helper. The stream is an owned child of its buffer;
 * its buffer stream and its blocks appear as borrowed children ("views") of the stream, and the
 * flag BLK_ENQUEUED says which of the blocks the caller may touch: the one returned last. */
#include "mex_test.h"
#include "dp_test.h"
MT_GROUPS(&gw_group_stream);

#include <math.h>
#include <time.h>

/* the table "Group stream" of the Command reference */
static const struct mt_arity arities[] = {
	{ "buffer_create_stream", 4, 4, 1 },
	{ "stream_get_next_block", 1, 1, 1 },
	{ "stream_cancel", 1, 1, 0 },
	{ "stream_destroy", 1, 1, 0 },
};

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static unsigned int count_of(const char *log, char letter)
{
	unsigned int n = 0;

	for (; *log; log++)
		n += *log == letter;
	return n;
}

/* bytes of a block view, 0 when the id is not a block */
static size_t block_bytes(uint64_t blk)
{
	const struct iio_block *b = gw_handle_ptr(blk, GW_T_BLOCK);

	return b ? (size_t)((const char *)iio_block_end(b) - (const char *)iio_block_start(b)) : 0;
}

static uint16_t block_word0(uint64_t blk)
{
	const struct iio_block *b = gw_handle_ptr(blk, GW_T_BLOCK);
	const unsigned char *p = b ? iio_block_start(b) : NULL;

	return p ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
}

/* copies four int16 into a block of eight bytes */
static void block_fill(uint64_t blk, const int16_t v[4])
{
	const struct iio_block *b = gw_handle_ptr(blk, GW_T_BLOCK);

	CHECK(b != NULL && block_bytes(blk) == 8);
	if (b && block_bytes(blk) == 8)
		memcpy(iio_block_start(b), v, 8);
}

/* emu-tx_buf0.bin is exactly these four int16 */
static bool tx_file_is(const struct mt_fixture *fx, const int16_t want[4])
{
	int16_t got[8];
	long n = fixture_read_file(fx->dir, "emu-tx_buf0.bin", got, sizeof(got));

	return n == 8 && !memcmp(got, want, 8);
}

static void test_emu_rx(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, rxb, txb, m, mtx, none, st, st2, id[5], bsv, other;
	const struct iio_block *blk;
	size_t count;
	double t0;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	tx = dp_dev(ctx, "emu-tx");
	rxb = dp_buf(rx, 0);
	txb = dp_buf(tx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	none = dp_mask(rx, false, 0);

	/* arguments */
	count = gw_handle_count();
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(0), mt_dbl(16), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(0), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(mtx)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(txb), mt_dbl(3), mt_dbl(16), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(2.5), mt_dbl(16), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(-1), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(NAN), mt_dbl(16), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(INFINITY), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(1e300), mt_u64(m)));
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(4e18), mt_u64(m)));   /* times the sample size: beyond size_t */
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_str("3"), mt_dbl(16), mt_u64(m)));
	/* no channel enabled: a sample has no bytes */
	CHECK(mt_raises("iio:EINVAL", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(none)));
	CHECK(mt_raises("iio:EBADF", "buffer_create_stream", 4, mt_u64(rx), mt_dbl(3), mt_dbl(16), mt_u64(m)));
	CHECK(mt_raises("iio:EBADF", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "buffer_create_stream", 4, mt_u64(0), mt_dbl(3), mt_dbl(16), mt_u64(m)));
	CHECK(gw_handle_count() == count);

	/* a stream is an owned child of its buffer */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(m));
	CHECK(st != 0);
	CHECK(gw_handle_type(st) == GW_T_STREAM && gw_handle_owned(st) && gw_handle_parent(st) == rxb);
	CHECK(gw_handle_count() == count + 1);
	CHECK(dp_device_of(st) == gw_handle_ptr(rx, GW_T_DEVICE));
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(0)));
	CHECK(mt_raises("iio:EBADF", "stream_cancel", 1, mt_u64(rxb)));
	CHECK(mt_raises("iio:EBADF", "stream_destroy", 1, mt_u64(rxb)));     /* live, another type */
	CHECK(mt_raises("iio:EINVAL", "stream_destroy", 1, mt_dbl(1)));
	mt_ok("stream_destroy", 1, mt_u64(0));
	CHECK(gw_handle_type(st) == GW_T_STREAM);

	/* five blocks out of three: a, b, c, a, b */
	for (unsigned int n = 0; n < 5; n++) {
		id[n] = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
		CHECK(id[n] != 0);
		CHECK(gw_handle_type(id[n]) == GW_T_BLOCK);
		CHECK(!gw_handle_owned(id[n]) && gw_handle_parent(id[n]) == st);
		CHECK(block_bytes(id[n]) == 64);
		CHECK(block_word0(id[n]) == fixture_ramp_word_i(16 * n));
		CHECK(!(gw_handle_flags(id[n]) & BLK_ENQUEUED));
		for (unsigned int k = 0; k < n; k++)
			if (id[k] != id[n])
				CHECK(gw_handle_flags(id[k]) & BLK_ENQUEUED);
		if (n == 1) {
			/* after call 2: a is back in the queue, b is the caller's */
			CHECK(gw_handle_flags(id[0]) & BLK_ENQUEUED);
			CHECK(!(gw_handle_flags(id[1]) & BLK_ENQUEUED));
		}
		/* the stream, its buffer stream, and one entry per block seen so far */
		CHECK(gw_handle_count() == count + 2 + (n < 3 ? n + 1 : 3));
	}
	CHECK(id[0] != id[1] && id[0] != id[2] && id[1] != id[2]);
	CHECK(id[3] == id[0] && id[4] == id[1]);

	/* the stream's buffer stream is a view too */
	blk = gw_handle_ptr(id[0], GW_T_BLOCK);
	CHECK(blk != NULL);
	bsv = blk ? gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(blk)) : 0;
	CHECK(bsv != 0);
	CHECK(gw_handle_type(bsv) == GW_T_BUFFER_STREAM && !gw_handle_owned(bsv) && gw_handle_parent(bsv) == st);
	/* a destroy command of the right type leaves a stream alone only if it IS a stream */
	CHECK(mt_raises("iio:EBADF", "stream_destroy", 1, mt_u64(id[0])));
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(id[0])));

	/* cancel, destroy: the views go with the stream */
	mt_ok("stream_cancel", 1, mt_u64(st));
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);
	CHECK(gw_handle_type(st) == GW_T_NONE);
	CHECK(gw_handle_type(id[0]) == GW_T_NONE && gw_handle_type(id[1]) == GW_T_NONE &&
	      gw_handle_type(id[2]) == GW_T_NONE && gw_handle_type(bsv) == GW_T_NONE);
	CHECK(gw_handle_count() == count);
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(mt_raises("iio:EBADF", "stream_get_next_block", 1, mt_u64(st)));
	CHECK(mt_raises("iio:EBADF", "stream_cancel", 1, mt_u64(st)));

	/* the mask was copied: the stream works after its mask is gone, and every start rewinds */
	other = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_i64(1), mt_u64(8), mt_u64(other));
	CHECK(st != 0);
	gw_handle_destroy(other);
	for (unsigned int n = 0; n < 3; n++) {
		id[n] = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
		CHECK(n == 0 || id[n] == id[0]);
		CHECK(block_bytes(id[n]) == 32);
		CHECK(block_word0(id[n]) == fixture_ramp_word_i(8 * n));
		CHECK(!(gw_handle_flags(id[n]) & BLK_ENQUEUED));
	}
	mt_ok("stream_destroy", 1, mt_u64(st));

	/* a stream that never ran */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(4), mt_dbl(16), mt_u64(m));
	CHECK(st != 0);
	mt_ok("stream_cancel", 1, mt_u64(st));
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);

	/* the context goes while two streams are open, one of them running */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(3), mt_dbl(16), mt_u64(m));
	CHECK(st != 0);
	id[0] = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	id[1] = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(id[0] != 0 && id[1] != 0 && id[0] != id[1]);
	st2 = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(2), mt_dbl(2), mt_u64(mtx));
	CHECK(st2 != 0 && gw_handle_parent(st2) == txb);
	t0 = now_s();
	gw_handle_destroy(ctx);
	CHECK(now_s() - t0 < 1.0);
	CHECK(gw_handle_type(st) == GW_T_NONE && gw_handle_type(st2) == GW_T_NONE &&
	      gw_handle_type(id[0]) == GW_T_NONE && gw_handle_type(id[1]) == GW_T_NONE);
	CHECK(gw_handle_count() == 0);
	mt_ok("stream_destroy", 1, mt_u64(st));
	mt_ok("mex_handle_count", 0);
}

static void test_emu_tx(const struct mt_fixture *fx)
{
	static const int16_t first[4] = { 11, 12, 13, 14 }, second[4] = { 21, 22, 23, 24 },
		third[4] = { 31, 32, 33, 34 };
	uint64_t ctx, tx, txb, mtx, st, a, b, x;
	double t0;

	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	tx = dp_dev(ctx, "emu-tx");
	txb = dp_buf(tx, 0);
	mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");

	/* one block of two samples: the block that next() returned is sent by the following next() */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(1), mt_dbl(2), mt_u64(mtx));
	CHECK(st != 0 && gw_handle_owned(st) && gw_handle_parent(st) == txb);
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(a != 0 && !gw_handle_owned(a) && gw_handle_parent(a) == st);
	CHECK(!(gw_handle_flags(a) & BLK_ENQUEUED));
	block_fill(a, first);
	CHECK(mt_u64_of("stream_get_next_block", 1, mt_u64(st)) == a);
	CHECK(!(gw_handle_flags(a) & BLK_ENQUEUED));
	CHECK(tx_file_is(fx, first));
	block_fill(a, second);
	CHECK(mt_u64_of("stream_get_next_block", 1, mt_u64(st)) == a);
	CHECK(tx_file_is(fx, second));
	/* the block filled last is never sent */
	block_fill(a, third);
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);
	CHECK(tx_file_is(fx, second));
	CHECK(gw_handle_type(a) == GW_T_NONE);

	/* two blocks: the caller always holds the one that is not queued */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(2), mt_dbl(2), mt_u64(mtx));
	CHECK(st != 0);
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	block_fill(a, first);
	b = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(b != 0 && b != a);
	CHECK((gw_handle_flags(a) & BLK_ENQUEUED) && !(gw_handle_flags(b) & BLK_ENQUEUED));
	block_fill(b, second);
	x = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(x == a);
	CHECK(!(gw_handle_flags(a) & BLK_ENQUEUED) && (gw_handle_flags(b) & BLK_ENQUEUED));
	CHECK(tx_file_is(fx, first));
	/* destroy on an output buffer sends what is queued (the context timeout is finite) */
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);
	CHECK(tx_file_is(fx, second));

	/* destroyed after a single next(): nothing was queued, nothing stalls */
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(2), mt_dbl(2), mt_u64(mtx));
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(st != 0 && a != 0);
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);

	gw_handle_destroy(ctx);
	mt_ok("mex_handle_count", 0);
}

static void test_fake(const struct mt_fixture *fx)
{
	uint64_t ctx, rx, tx, rxb, txb, m, mtx, st, a;
	unsigned int before;
	size_t n;
	double t0;

	/* a next() that fails: nothing is registered, and no block may be touched */
	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0);
	fake.rx_frames = 1;
	rx = dp_dev(ctx, "emu-rx");
	rxb = dp_buf(rx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(1), mt_dbl(4), mt_u64(m));
	CHECK(st != 0);
	CHECK(!strcmp(fake.log, "OB"));
	a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
	CHECK(a != 0 && !(gw_handle_flags(a) & BLK_ENQUEUED));
	CHECK(!strcmp(fake.log, "OBESD"));
	n = gw_handle_count();
	CHECK(mt_raises("iio:ETIMEDOUT", "stream_get_next_block", 1, mt_u64(st)));
	CHECK(gw_handle_count() == n);
	CHECK(gw_handle_flags(a) & BLK_ENQUEUED);
	CHECK(gw_handle_type(a) == GW_T_BLOCK);
	t0 = now_s();
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(now_s() - t0 < 1.0);
	/* an input stream is cancelled first, stopped before it is closed */
	CHECK(!strcmp(fake.log, "OBESDEdXdFsXC"));
	if (strcmp(fake.log, "OBESDEdXdFsXC"))
		fprintf(stderr, "fake log (rx): %s\n", fake.log);
	gw_handle_destroy(ctx);

	/* the first next() fails: no view exists yet */
	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0);
	fake.rx_frames = 0;
	rx = dp_dev(ctx, "emu-rx");
	rxb = dp_buf(rx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(2), mt_dbl(4), mt_u64(m));
	n = gw_handle_count();
	CHECK(mt_raises("iio:ETIMEDOUT", "stream_get_next_block", 1, mt_u64(st)));
	CHECK(gw_handle_count() == n);
	mt_ok("stream_destroy", 1, mt_u64(st));
	CHECK(gw_handle_count() == n - 1);
	gw_handle_destroy(ctx);

	/* destroy never blocks for ever: a queued output block is awaited when the context
	 * timeout is finite, and cancelled first when it is infinite */
	for (int infinite = 0; infinite < 2; infinite++) {
		ctx = dp_open_fake(fx->xml);
		CHECK(ctx != 0);
		fake.tx_frames = 0;
		if (infinite)
			CHECK(iio_context_set_timeout(gw_handle_ptr(ctx, GW_T_CONTEXT), -1) == 0);
		tx = dp_dev(ctx, "emu-tx");
		txb = dp_buf(tx, 0);
		mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
		st = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(1), mt_dbl(2), mt_u64(mtx));
		CHECK(st != 0);
		a = mt_u64_of("stream_get_next_block", 1, mt_u64(st));
		CHECK(a != 0 && !(gw_handle_flags(a) & BLK_ENQUEUED));
		CHECK(mt_raises("iio:ETIMEDOUT", "stream_get_next_block", 1, mt_u64(st)));
		CHECK(gw_handle_flags(a) & BLK_ENQUEUED);
		CHECK(!strcmp(fake.log, "OBESd"));
		fake_clear_log();
		t0 = now_s();
		mt_ok("stream_destroy", 1, mt_u64(st));
		CHECK(now_s() - t0 < 1.0);
		CHECK(fake.log[0] == (infinite ? 'X' : 'd'));
		CHECK(!strcmp(fake.log, infinite ? "XdFsXC" : "dFsXC"));
		if (strcmp(fake.log, infinite ? "XdFsXC" : "dFsXC"))
			fprintf(stderr, "fake log (tx, %s): %s\n", infinite ? "infinite" : "finite", fake.log);
		gw_handle_destroy(ctx);
	}

	/* the same choice when the context is destroyed with the stream open */
	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0);
	CHECK(iio_context_set_timeout(gw_handle_ptr(ctx, GW_T_CONTEXT), -1) == 0);
	tx = dp_dev(ctx, "emu-tx");
	txb = dp_buf(tx, 0);
	mtx = dp_mask(tx, true, 2, "voltage0_i", "voltage0_q");
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(txb), mt_dbl(1), mt_dbl(2), mt_u64(mtx));
	CHECK(mt_u64_of("stream_get_next_block", 1, mt_u64(st)) != 0);
	fake_clear_log();
	gw_handle_destroy(ctx);
	CHECK(fake.log[0] == 'X');

	/* out of memory: a stream that MATLAB never heard of does not exist, and a failed attempt
	 * neither opens a buffer nor takes a block */
	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0);
	rx = dp_dev(ctx, "emu-rx");
	rxb = dp_buf(rx, 0);
	m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	before = count_of(fake.log, 'O');
	n = gw_handle_count();
	CHECK(mt_oom_sweep(1, "buffer_create_stream", 4, mt_u64(rxb), mt_dbl(2), mt_dbl(4), mt_u64(m)) >= 1);
	CHECK(count_of(fake.log, 'O') == before + 1);
	CHECK(gw_handle_count() == n + 1);
	st = mt_u64_of("buffer_create_stream", 4, mt_u64(rxb), mt_dbl(2), mt_dbl(4), mt_u64(m));
	CHECK(st != 0);
	before = count_of(fake.log, 'D');
	CHECK(mt_oom_sweep(1, "stream_get_next_block", 1, mt_u64(st)) >= 1);
	CHECK(count_of(fake.log, 'D') == before + 1);
	gw_handle_destroy(ctx);
	mt_ok("mex_handle_count", 0);
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	mt_check_arities(&gw_group_stream, arities, sizeof(arities) / sizeof(arities[0]));

	test_emu_rx(&fx);
	test_emu_tx(&fx);
	test_fake(&fx);

	mt_ok("mex_shutdown", 0);
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
