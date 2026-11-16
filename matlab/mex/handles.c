/* SPDX-License-Identifier: MIT */
/* Handle table of the iio_mex gateway (handles.h).
 *
 * One growable array of entries. An id is the slot number plus one in its low half and the
 * generation of the slot in its high half; the generation changes whenever the slot is
 * freed, so an id of a destroyed entry never matches again. The children of an entry, and
 * the roots, form a list with the newest first, which is the order of a teardown. A hash
 * over (type, pointer) answers gw_handle_find() without a scan.
 *
 * An op may call back into this file (look entries up, add entries, destroy entries), and
 * an add may move the array. Therefore no pointer into the array is held across a call of
 * an op or of a cache free function: entries are named by slot number and fetched again.
 *
 * No locking: the gateway runs on MATLAB's interpreter thread. */
#include "handles.h"

#include <errno.h>
#include <stdlib.h>

#define NIL UINT32_MAX          /* "no slot" in every link below */
#define MIN_SLOTS 64u           /* a power of two, as every capacity is */

enum state {
	ST_FREE = 0,
	ST_LIVE,
	ST_PRE,         /* its pre_destroy op is running: still found and usable */
	ST_DYING,       /* its children are being destroyed, or its destroy op is running */
};

struct entry {
	void *ptr;
	void *aux;
	void *cache;
	void (*cache_free)(void *cache);
	struct gw_ops ops;
	gw_id parent;
	uint32_t gen;
	uint32_t flags;
	uint32_t first_child;   /* newest child */
	uint32_t next, prev;    /* older and newer sibling (or root); `next` also links free slots */
	uint32_t hash_next;     /* next entry of the same hash bucket */
	uint8_t type;
	uint8_t state;
	bool owned;
	bool linked;            /* in the list of its parent, or of the roots */
};

static struct entry *tab;
static uint32_t *buckets;       /* `cap` list heads */
static uint32_t cap;            /* slots allocated */
static uint32_t used;           /* slots that were in use at least once */
static uint32_t free_head = NIL;
static uint32_t roots = NIL;    /* newest root */
static uint32_t base_gen = 1;
static size_t count;

static gw_id id_of(uint32_t i)
{
	return ((gw_id)tab[i].gen << 32) | ((gw_id)i + 1);
}

/* Slot of an id that names an entry, whatever its state; NIL for a zero or stale id. */
static uint32_t slot_of(gw_id id)
{
	uint32_t low = (uint32_t)id;

	if (low == 0 || low > used)
		return NIL;
	if (tab[low - 1].state == ST_FREE || tab[low - 1].gen != (uint32_t)(id >> 32))
		return NIL;
	return low - 1;
}

static uint32_t bucket_of(enum gw_type type, const void *ptr)
{
	uint64_t h = ((uint64_t)(uintptr_t)ptr + (uint64_t)type) * 0x9E3779B97F4A7C15ull;

	return (uint32_t)(h >> 32) & (cap - 1);
}

static void hash_insert(uint32_t i)
{
	uint32_t b = bucket_of((enum gw_type)tab[i].type, tab[i].ptr);

	tab[i].hash_next = buckets[b];
	buckets[b] = i;
}

static void hash_remove(uint32_t i)
{
	uint32_t *link = &buckets[bucket_of((enum gw_type)tab[i].type, tab[i].ptr)];

	while (*link != NIL && *link != i)
		link = &tab[*link].hash_next;
	if (*link == i)
		*link = tab[i].hash_next;
}

/* Head of the list that entry i belongs to: its parent's children, or the roots. */
static uint32_t *list_head(uint32_t i)
{
	uint32_t p = slot_of(tab[i].parent);

	return p == NIL ? &roots : &tab[p].first_child;
}

static void list_insert(uint32_t i)
{
	uint32_t *head = list_head(i);

	tab[i].prev = NIL;
	tab[i].next = *head;
	if (*head != NIL)
		tab[*head].prev = i;
	*head = i;
	tab[i].linked = true;
}

static void list_remove(uint32_t i)
{
	if (!tab[i].linked)
		return;
	if (tab[i].prev != NIL)
		tab[tab[i].prev].next = tab[i].next;
	else
		*list_head(i) = tab[i].next;
	if (tab[i].next != NIL)
		tab[tab[i].next].prev = tab[i].prev;
	tab[i].linked = false;
}

static bool grow(void)
{
	uint32_t new_cap = cap ? cap * 2 : MIN_SLOTS;
	uint32_t *new_buckets;
	struct entry *new_tab;

	if (new_cap <= cap)             /* 2^32 slots: the slot number no longer fits an id */
		return false;
	new_buckets = malloc((size_t)new_cap * sizeof(*new_buckets));
	if (!new_buckets)
		return false;
	new_tab = realloc(tab, (size_t)new_cap * sizeof(*new_tab));
	if (!new_tab) {
		free(new_buckets);
		return false;
	}
	free(buckets);
	tab = new_tab;
	buckets = new_buckets;
	cap = new_cap;
	for (uint32_t b = 0; b < cap; b++)
		buckets[b] = NIL;
	for (uint32_t i = 0; i < used; i++)
		if (tab[i].state == ST_LIVE || tab[i].state == ST_PRE)
			hash_insert(i);
	return true;
}

static void release_cache(uint32_t i)
{
	void *cache = tab[i].cache;
	void (*cache_free)(void *) = tab[i].cache_free;

	tab[i].cache = NULL;
	tab[i].cache_free = NULL;
	if (cache && cache_free)
		cache_free(cache);
}

/* New entry in state ST_LIVE; NIL if memory ran out. The caller has checked the arguments. */
static uint32_t new_entry(enum gw_type type, void *ptr, gw_id parent, bool owned,
		const struct gw_ops *ops, void *aux)
{
	struct entry *e;
	uint32_t i, gen;

	if (free_head != NIL) {
		i = free_head;
		free_head = tab[i].next;
		gen = tab[i].gen;
	} else {
		if (used == cap && !grow())
			return NIL;
		i = used++;
		gen = base_gen;
	}
	e = &tab[i];
	*e = (struct entry){
		.ptr = ptr, .aux = aux, .parent = parent, .gen = gen, .first_child = NIL,
		.next = NIL, .prev = NIL, .hash_next = NIL, .type = (uint8_t)type,
		.state = ST_LIVE, .owned = owned,
	};
	if (ops)
		e->ops = *ops;
	list_insert(i);
	hash_insert(i);
	count++;
	return i;
}

static bool type_ok(enum gw_type type)
{
	return (int)type > GW_T_NONE && (int)type < GW_T_COUNT;
}

/* An entry can get a child until its own children are being destroyed. */
static bool parent_ok(gw_id parent)
{
	uint32_t p = slot_of(parent);

	return p != NIL && tab[p].state != ST_DYING;
}

void gw_handle_reset(uint32_t seed)
{
	free(tab);
	free(buckets);
	tab = NULL;
	buckets = NULL;
	cap = 0;
	used = 0;
	free_head = NIL;
	roots = NIL;
	count = 0;
	base_gen = seed ? seed : 1;
}

gw_id gw_handle_add(enum gw_type type, void *ptr, gw_id parent, bool owned,
		const struct gw_ops *ops, void *aux)
{
	uint32_t i;

	if (!ptr || !type_ok(type) || (parent != 0 && !parent_ok(parent)))
		return 0;
	i = new_entry(type, ptr, parent, owned, ops, aux);
	return i == NIL ? 0 : id_of(i);
}

int gw_handle_borrow(enum gw_type type, const void *ptr, gw_id parent, gw_id *id)
{
	uint32_t i;

	*id = 0;
	if (!ptr)
		return 0;
	*id = gw_handle_find(type, ptr);
	if (*id)
		return 0;
	if (!type_ok(type) || !parent_ok(parent))
		return -EBADF;
	/* The table never writes through ptr; an entry stores it without the qualifier. */
	i = new_entry(type, (void *)(uintptr_t)ptr, parent, false, NULL, NULL);
	if (i == NIL)
		return -ENOMEM;
	*id = id_of(i);
	return 0;
}

void gw_handle_destroy(gw_id id)
{
	uint32_t i = slot_of(id), child;
	struct gw_ops ops;
	void *ptr, *aux;

	if (i == NIL || tab[i].state != ST_LIVE)
		return;
	ops = tab[i].ops;
	ptr = tab[i].ptr;
	aux = tab[i].aux;

	/* Out of its parent's list first: whoever destroys the parent from inside an op of
	 * this entry does not meet the entry again. */
	tab[i].state = ST_PRE;
	list_remove(i);
	if (ops.pre_destroy)
		ops.pre_destroy(ptr, aux);

	tab[i].state = ST_DYING;
	hash_remove(i);
	/* Every listed child is live, so each round removes one. */
	while ((child = tab[i].first_child) != NIL)
		gw_handle_destroy(id_of(child));

	release_cache(i);
	if (ops.destroy)
		ops.destroy(ptr, aux);
	release_cache(i);       /* a cache that the destroy op attached */

	tab[i].state = ST_FREE;
	if (++tab[i].gen == 0)
		tab[i].gen = 1;
	tab[i].next = free_head;
	free_head = i;
	count--;
}

void gw_handle_destroy_all(void)
{
	while (roots != NIL)
		gw_handle_destroy(id_of(roots));
}

void *gw_handle_ptr(gw_id id, enum gw_type type)
{
	uint32_t i = slot_of(id);

	if (i == NIL || tab[i].state == ST_DYING || (enum gw_type)tab[i].type != type)
		return NULL;
	return tab[i].ptr;
}

enum gw_type gw_handle_type(gw_id id)
{
	uint32_t i = slot_of(id);

	return i == NIL ? GW_T_NONE : (enum gw_type)tab[i].type;
}

gw_id gw_handle_find(enum gw_type type, const void *ptr)
{
	if (!ptr || !type_ok(type) || !cap)
		return 0;
	for (uint32_t i = buckets[bucket_of(type, ptr)]; i != NIL; i = tab[i].hash_next)
		if (tab[i].ptr == ptr && (enum gw_type)tab[i].type == type)
			return id_of(i);
	return 0;
}

gw_id gw_handle_parent(gw_id id)
{
	uint32_t i = slot_of(id);

	if (i == NIL || slot_of(tab[i].parent) == NIL)
		return 0;
	return tab[i].parent;
}

gw_id gw_handle_root(gw_id id)
{
	uint32_t i = slot_of(id);

	while (i != NIL && tab[i].parent != 0) {
		id = tab[i].parent;
		i = slot_of(id);
	}
	return i == NIL ? 0 : id;
}

bool gw_handle_owned(gw_id id)
{
	uint32_t i = slot_of(id);

	return i != NIL && tab[i].owned;
}

void *gw_handle_aux(gw_id id)
{
	uint32_t i = slot_of(id);

	return i == NIL ? NULL : tab[i].aux;
}

uint32_t gw_handle_flags(gw_id id)
{
	uint32_t i = slot_of(id);

	return i == NIL ? 0 : tab[i].flags;
}

void gw_handle_set_flags(gw_id id, uint32_t flags)
{
	uint32_t i = slot_of(id);

	if (i != NIL)
		tab[i].flags = flags;
}

void *gw_handle_cache(gw_id id)
{
	uint32_t i = slot_of(id);

	return i == NIL ? NULL : tab[i].cache;
}

void gw_handle_set_cache(gw_id id, void *cache, void (*cache_free)(void *cache))
{
	uint32_t i = slot_of(id);

	if (i == NIL)
		return;
	release_cache(i);
	i = slot_of(id);        /* the free function may have destroyed the entry */
	if (i == NIL)
		return;
	tab[i].cache = cache;
	tab[i].cache_free = cache_free;
}

size_t gw_handle_count(void)
{
	return count;
}

const char *gw_type_name(enum gw_type type)
{
	static const char *const names[GW_T_COUNT] = {
		[GW_T_CONTEXT] = "context",
		[GW_T_SCAN] = "scan",
		[GW_T_DEVICE] = "device",
		[GW_T_CHANNEL] = "channel",
		[GW_T_ATTR] = "attr",
		[GW_T_BUFFER] = "buffer",
		[GW_T_MASK] = "channels mask",
		[GW_T_BUFFER_STREAM] = "buffer stream",
		[GW_T_BLOCK] = "block",
		[GW_T_STREAM] = "stream",
		[GW_T_EVENT_STREAM] = "event stream",
		[GW_T_CONV_STREAM] = "conv stream",
	};

	return type_ok(type) ? names[type] : "none";
}
