/* SPDX-License-Identifier: MIT */
/*
 * A libiio backend for tests only. The structure of the context comes from an
 * XML file; what buffers, blocks, events and triggers do comes from the table
 * below, so that a test sees every backend call, in order, and decides when a
 * block is ready. Nothing waits: "not ready" is reported at once.
 *
 * Include this header in exactly one translation unit per program.
 *
 *   op             log   behaviour
 *   open_buffer    O     enables fake.couple in the mask it is given
 *   create_block   B     malloc, filled with 0xEE
 *   enqueue_block  E, e  e when cyclic. -EPERM (no log) if already queued.
 *                        Output: copies the block to tx_data, sets tx_bytes
 *                        and tx_cyclic
 *   enable_buffer  S, s  S enable, s disable
 *   dequeue_block  D, d  D done, d not ready. -EPERM (no log) if not queued.
 *                        Not ready while fake.busy > 0 (counted down) or once
 *                        the direction's count has reached rx_frames /
 *                        tx_frames: -EBUSY if nonblock, else -ETIMEDOUT.
 *                        Done, input: block filled with little-endian 16-bit
 *                        words (256 * rx_count + i) & 0x7ff, i = word index
 *   cancel_buffer  X
 *   free_block     F
 *   close_buffer   C
 *   open_ev              only for device iio:device2, else -ENODEV
 *   read_ev              two events, then -EBUSY if nonblock, else -ETIMEDOUT
 *   get_trigger          fake.trigger, or -ENODEV when it is NULL
 *   set_trigger          stores it
 *
 * iio_buffer_close() itself calls the cancel op, so closing a buffer stream
 * always logs "XC".
 */
#ifndef FAKE_BACKEND_H
#define FAKE_BACKEND_H

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_state {
	char log[256];        /* one letter per backend call, in call order (above) */
	int rx_frames;        /* input dequeues that deliver data before "not ready"; default 3 */
	int tx_frames;        /* output dequeues that succeed before "not ready"; default 2 */
	int busy;             /* the next `busy` dequeue calls report "not ready"; default 0 */
	const char *couple;   /* id of one more channel the backend enables in open_buffer; default NULL */
	unsigned int rx_count, tx_count;
	/* most recent output enqueue; tx_data holds at most its first 256 bytes */
	unsigned char tx_data[256];
	size_t tx_bytes;
	bool tx_cyclic;
	const struct iio_device *trigger;
};

static struct fake_state fake = { .rx_frames = 3, .tx_frames = 2 };

struct iio_buffer_pdata {
	bool output;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	unsigned char *data;
	size_t size;
	bool queued;
};

struct iio_event_stream_pdata {
	unsigned int nb_read;
};

static void fake_log(char c)
{
	size_t len = strlen(fake.log);

	if (len + 1 < sizeof(fake.log))
		fake.log[len] = c;
}

static struct iio_buffer_pdata *fake_open_buffer(const struct iio_device *dev, unsigned int idx,
		struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = calloc(1, sizeof(*pdata));
	const struct iio_channel *chn;

	if (!pdata)
		return iio_ptr(-ENOMEM);
	pdata->output = iio_buffer_is_output(iio_device_get_buffer(dev, idx));
	if (fake.couple) {
		/* as hardware with coupled channels does */
		chn = iio_device_find_channel(dev, fake.couple, pdata->output);
		if (chn)
			iio_channel_enable(chn, mask);
	}
	fake_log('O');
	return pdata;
}

static void fake_close_buffer(struct iio_buffer_pdata *pdata)
{
	fake_log('C');
	free(pdata);
}

static int fake_enable_buffer(struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable,
		bool cyclic)
{
	(void)pdata; (void)nb_samples; (void)cyclic;
	fake_log(enable ? 'S' : 's');
	return 0;
}

static void fake_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	(void)pdata;
	fake_log('X');
}

static struct iio_block_pdata *fake_create_block(struct iio_buffer_pdata *pdata, size_t size,
		void **data)
{
	struct iio_block_pdata *block = calloc(1, sizeof(*block));

	if (!block)
		return iio_ptr(-ENOMEM);
	block->data = malloc(size);
	if (!block->data) {
		free(block);
		return iio_ptr(-ENOMEM);
	}
	memset(block->data, 0xEE, size);
	block->buf = pdata;
	block->size = size;
	*data = block->data;
	fake_log('B');
	return block;
}

static void fake_free_block(struct iio_block_pdata *block)
{
	fake_log('F');
	free(block->data);
	free(block);
}

static int fake_enqueue_block(struct iio_block_pdata *block, size_t bytes_used, bool cyclic)
{
	if (block->queued)
		return -EPERM;
	block->queued = true;
	if (block->buf->output) {
		size_t n = bytes_used < sizeof(fake.tx_data) ? bytes_used : sizeof(fake.tx_data);

		memcpy(fake.tx_data, block->data, n);
		fake.tx_bytes = bytes_used;
		fake.tx_cyclic = cyclic;
	}
	fake_log(cyclic ? 'e' : 'E');
	return 0;
}

static int fake_dequeue_block(struct iio_block_pdata *block, bool nonblock)
{
	bool output = block->buf->output;
	unsigned int *count = output ? &fake.tx_count : &fake.rx_count;
	int frames = output ? fake.tx_frames : fake.rx_frames;
	size_t i;

	if (!block->queued)
		return -EPERM;
	if (fake.busy > 0 || (int)*count >= frames) {
		if (fake.busy > 0)
			fake.busy--;
		fake_log('d');
		return nonblock ? -EBUSY : -ETIMEDOUT;
	}
	if (!output) {
		for (i = 0; 2 * i + 1 < block->size; i++) {
			unsigned int w = (256 * *count + (unsigned int)i) & 0x7ff;

			block->data[2 * i] = (unsigned char)w;
			block->data[2 * i + 1] = (unsigned char)(w >> 8);
		}
	}
	(*count)++;
	block->queued = false;
	fake_log('D');
	return 0;
}

static struct iio_event_stream_pdata *fake_open_ev(const struct iio_device *dev)
{
	struct iio_event_stream_pdata *pdata;

	if (strcmp(iio_device_get_id(dev), "iio:device2"))
		return iio_ptr(-ENODEV);
	pdata = calloc(1, sizeof(*pdata));
	return pdata ? pdata : iio_ptr(-ENOMEM);
}

static void fake_close_ev(struct iio_event_stream_pdata *pdata)
{
	free(pdata);
}

static int fake_read_ev(struct iio_event_stream_pdata *pdata, struct iio_event *out_event,
		bool nonblock)
{
	/* THRESH, RISING, VOLTAGE, channel 0; then MAG, FALLING, ACCEL, modifier X */
	static const uint64_t ids[2] = { UINT64_C(0x0001000000000000), UINT64_C(0x0102010300000000) };

	if (pdata->nb_read >= 2)
		return nonblock ? -EBUSY : -ETIMEDOUT;
	out_event->id = ids[pdata->nb_read];
	out_event->timestamp = 1000 + (int64_t)pdata->nb_read;
	pdata->nb_read++;
	return 0;
}

static const struct iio_device *fake_get_trigger(const struct iio_device *dev)
{
	(void)dev;
	return fake.trigger ? fake.trigger : iio_ptr(-ENODEV);
}

static int fake_set_trigger(const struct iio_device *dev, const struct iio_device *trigger)
{
	(void)dev;
	fake.trigger = trigger;
	return 0;
}

static const struct iio_backend_ops fake_ops = {
	.get_trigger = fake_get_trigger,
	.set_trigger = fake_set_trigger,
	.open_buffer = fake_open_buffer,
	.close_buffer = fake_close_buffer,
	.enable_buffer = fake_enable_buffer,
	.cancel_buffer = fake_cancel_buffer,
	.create_block = fake_create_block,
	.free_block = fake_free_block,
	.enqueue_block = fake_enqueue_block,
	.dequeue_block = fake_dequeue_block,
	.open_ev = fake_open_ev,
	.close_ev = fake_close_ev,
	.read_ev = fake_read_ev,
};

static const struct iio_backend fake_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "fake",
	.uri_prefix = "fake:",
	.ops = &fake_ops,
	.default_timeout_ms = 1000,
};

/* Zero everything, then the defaults */
static inline void fake_reset(void)
{
	memset(&fake, 0, sizeof(fake));
	fake.rx_frames = 3;
	fake.tx_frames = 2;
}

static inline void fake_clear_log(void)
{
	memset(fake.log, 0, sizeof(fake.log));
}

/* A context with the structure of the XML file at xml_path and the behaviour
 * above, or a pointer-encoded error. */
static inline struct iio_context *fake_create_context(const char *xml_path)
{
	static const struct iio_context_params params;
	char uri[PATH_MAX + 8];

	snprintf(uri, sizeof(uri), "xml:%s", xml_path);
	return iio_create_context_from_xml(&params, uri, &fake_backend, "(fake)", NULL, NULL, 0);
}

#endif /* FAKE_BACKEND_H */
