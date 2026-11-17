/* SPDX-License-Identifier: MIT */
/* Handle table of the iio_mex gateway. No MATLAB and no libiio dependency. */
#ifndef IIO_MEX_HANDLES_H
#define IIO_MEX_HANDLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0 = none. Low 32 bits: slot + 1. High 32 bits: generation of that slot. */
typedef uint64_t gw_id;

enum gw_type {
	GW_T_NONE = 0,
	GW_T_CONTEXT,       /* struct iio_context *, owned */
	GW_T_SCAN,          /* struct iio_scan *, owned */
	GW_T_DEVICE,        /* struct iio_device *, borrowed */
	GW_T_CHANNEL,       /* struct iio_channel *, borrowed */
	GW_T_ATTR,          /* const struct iio_attr *, borrowed */
	GW_T_BUFFER,        /* struct iio_buffer *, borrowed */
	GW_T_MASK,          /* struct iio_channels_mask *, owned (borrowed: a stream's own mask) */
	GW_T_BUFFER_STREAM, /* struct iio_buffer_stream *, owned */
	GW_T_BLOCK,         /* struct iio_block *, owned (borrowed: a block of an iio_stream) */
	GW_T_STREAM,        /* struct iio_stream *, owned */
	GW_T_EVENT_STREAM,  /* struct iio_event_stream *, owned */
	GW_T_CONV_STREAM,   /* the libiio-conv stream object, owned */
	GW_T_COUNT
};

struct gw_ops {
	/* Called first, before any child is destroyed (for example: cancel). May be NULL. The entry
	 * is still live while it runs: gw_handle_find(), gw_handle_ptr() and gw_handle_flags() work on it. */
	void (*pre_destroy)(void *ptr, void *aux);
	/* Called last, after every child is gone. Releases ptr (owned entries) and aux. May be NULL. */
	void (*destroy)(void *ptr, void *aux);
};

/* Forgets everything WITHOUT calling any op and sets the generation every slot starts
 * from (0 is replaced by 1). The gateway calls it once per load with a clock-derived seed,
 * so ids of an earlier load of the MEX file are stale; tests call it with a fixed seed. */
void gw_handle_reset(uint32_t seed);

/* New entry; parent 0 makes a root. aux is gateway-side state of the entry (may be NULL).
 * Returns 0 if ptr is NULL, type is invalid, parent is stale, or memory ran out. */
gw_id gw_handle_add(enum gw_type type, void *ptr, gw_id parent, bool owned,
		const struct gw_ops *ops, void *aux);

/* *id = the live entry (type, ptr) if there is one, whatever parent is given (another one,
 * zero or a stale id), else a new borrowed child of parent (no ops, no aux). ptr == NULL gives
 * *id = 0 and returns 0. Returns 0, -EBADF (a new entry is needed and parent is zero or
 * stale) or -ENOMEM. */
int gw_handle_borrow(enum gw_type type, const void *ptr, gw_id parent, gw_id *id);

/* pre_destroy(entry); destroy every child, newest first, recursively; release the cache slot;
 * destroy(entry); free the slot (its generation increases). Zero, stale and
 * already-being-destroyed ids are ignored. */
void gw_handle_destroy(gw_id id);
void gw_handle_destroy_all(void);                     /* every root, newest first */

void *gw_handle_ptr(gw_id id, enum gw_type type);     /* NULL if zero, stale, being destroyed, or another type */
enum gw_type gw_handle_type(gw_id id);                /* GW_T_NONE if zero or stale */
gw_id gw_handle_find(enum gw_type type, const void *ptr);  /* 0 if none */
gw_id gw_handle_parent(gw_id id);                     /* 0 for a root or a stale id */
gw_id gw_handle_root(gw_id id);                       /* topmost ancestor (id itself for a root); 0 if stale */
bool gw_handle_owned(gw_id id);                       /* false if stale */
void *gw_handle_aux(gw_id id);                        /* NULL if stale */
uint32_t gw_handle_flags(gw_id id);                   /* 0 for a new entry and for a stale id */
void gw_handle_set_flags(gw_id id, uint32_t flags);   /* ignored for a stale id */
/* Cache slot: a second attachment, for state that another source file keeps on an entry
 * (cmd_block.c: the converter cache of a buffer stream). Allowed on borrowed entries too.
 * Setting it first releases the previous cache with its cache_free; cache_free may be NULL.
 * The slot is already empty while cache_free runs. cache_free may attach a cache to the entry
 * (released in turn before a cache being set is stored, and at the latest when the entry is
 * destroyed) and may destroy the entry (a cache being set is then not stored: stale id). */
void *gw_handle_cache(gw_id id);                      /* NULL if none or stale */
void gw_handle_set_cache(gw_id id, void *cache, void (*cache_free)(void *cache));   /* ignored for a stale id */
size_t gw_handle_count(void);                         /* live entries */
const char *gw_type_name(enum gw_type type);          /* "context", "scan", "device", "channel", "attr",
                                                         "buffer", "channels mask", "buffer stream", "block",
                                                         "stream", "event stream", "conv stream"; else "none" */

#endif
