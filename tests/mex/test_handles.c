/* SPDX-License-Identifier: MIT */
/* The handle table (matlab/mex/handles.h): ids with generations, ownership, de-duplication
 * of borrowed pointers, the order of a teardown, and what an op may do while it runs. */
#include "handles.h"
#include "test_util.h"

#include <errno.h>

/* The objects are text: an op appends "<tag><text> " to the log. */
static char A[] = "A", B[] = "B", C[] = "C", D[] = "D", E[] = "E", F[] = "F";
static char Q[] = "Q", Z[] = "Z", AUX[] = "AUX", K1[] = "K1", K2[] = "K2", K3[] = "K3";
static char R1[] = "R1", R2[] = "R2", R3[] = "R3", S[] = "S", P[] = "P";

static char oplog[256];

static void log_add(char tag, const void *text)
{
	size_t n = strlen(oplog);

	snprintf(oplog + n, sizeof(oplog) - n, "%c%s ", tag, (const char *)text);
}

static void *seen_aux;

static void op_pre(void *ptr, void *aux)
{
	(void)aux;
	log_add('p', ptr);
}

static void op_destroy(void *ptr, void *aux)
{
	seen_aux = aux;
	log_add('d', ptr);
}

static void cache_free(void *cache)
{
	log_add('k', cache);
}

static const struct gw_ops ops = { op_pre, op_destroy };
static const struct gw_ops ops_destroy_only = { NULL, op_destroy };

struct tree {
	gw_id a, b, c, d, e, f;
};

/* A (context) > B (buffer stream) > D, E (blocks);  A > C (device, borrowed);  A > F (mask) */
static void build_tree(struct tree *t)
{
	size_t before = gw_handle_count();

	t->a = gw_handle_add(GW_T_CONTEXT, A, 0, true, &ops, NULL);
	t->b = gw_handle_add(GW_T_BUFFER_STREAM, B, t->a, true, &ops, NULL);
	t->c = 0;
	CHECK(gw_handle_borrow(GW_T_DEVICE, C, t->a, &t->c) == 0);
	t->d = gw_handle_add(GW_T_BLOCK, D, t->b, true, &ops_destroy_only, NULL);
	t->e = gw_handle_add(GW_T_BLOCK, E, t->b, true, &ops_destroy_only, NULL);
	t->f = gw_handle_add(GW_T_MASK, F, t->a, true, &ops_destroy_only, NULL);
	CHECK(t->a && t->b && t->c && t->d && t->e && t->f);
	CHECK(gw_handle_count() == before + 6);
}

static void test_tree(void)
{
	struct tree t;
	gw_id x, y, chan;

	gw_handle_reset(7);
	CHECK(gw_handle_count() == 0);
	build_tree(&t);
	CHECK(gw_handle_count() == 6);
	CHECK(t.a >> 32 == 7);
	CHECK((uint32_t)t.a == 1);
	CHECK(gw_handle_root(t.e) == t.a);
	CHECK(gw_handle_parent(t.e) == t.b);
	CHECK(gw_handle_root(t.a) == t.a);
	CHECK(gw_handle_parent(t.a) == 0);
	CHECK(gw_handle_root(t.c) == t.a);
	CHECK(gw_handle_owned(t.b));
	CHECK(!gw_handle_owned(t.c));
	CHECK(gw_handle_find(GW_T_BLOCK, E) == t.e);
	CHECK(gw_handle_find(GW_T_MASK, E) == 0);
	CHECK(gw_handle_find(GW_T_BLOCK, Z) == 0);
	CHECK(gw_handle_find(GW_T_BLOCK, NULL) == 0);
	CHECK(gw_handle_type(t.a) == GW_T_CONTEXT);
	CHECK(gw_handle_type(t.c) == GW_T_DEVICE);
	CHECK(gw_handle_ptr(t.a, GW_T_CONTEXT) == A);
	CHECK(gw_handle_aux(t.a) == NULL);
	CHECK(gw_handle_aux(t.c) == NULL);

	/* de-duplication of borrowed pointers: one id per (type, pointer) */
	x = 99;
	CHECK(gw_handle_borrow(GW_T_DEVICE, C, t.a, &x) == 0);
	CHECK(x == t.c);
	CHECK(gw_handle_count() == 6);
	/* also when asked for through another parent, and for a pointer that was added */
	x = 99;
	CHECK(gw_handle_borrow(GW_T_DEVICE, C, t.b, &x) == 0 && x == t.c);
	x = 99;
	CHECK(gw_handle_borrow(GW_T_BLOCK, E, t.a, &x) == 0 && x == t.e);
	CHECK(gw_handle_owned(t.e));
	chan = 0;
	CHECK(gw_handle_borrow(GW_T_CHANNEL, C, t.a, &chan) == 0);
	CHECK(chan != 0 && chan != t.c);
	CHECK(gw_handle_count() == 7);
	CHECK(!gw_handle_owned(chan));
	CHECK(gw_handle_parent(chan) == t.a);
	CHECK(gw_handle_ptr(t.c, GW_T_CHANNEL) == NULL);
	CHECK(gw_handle_ptr(t.c, (enum gw_type)(GW_T_DEVICE + 256)) == NULL);
	CHECK(gw_handle_ptr(t.c, GW_T_DEVICE) == C);
	CHECK(gw_handle_ptr(chan, GW_T_CHANNEL) == C);
	x = 99;
	CHECK(gw_handle_borrow(GW_T_DEVICE, NULL, t.a, &x) == 0);
	CHECK(x == 0);
	x = 99;
	CHECK(gw_handle_borrow(GW_T_DEVICE, Z, 12345, &x) == -EBADF);
	CHECK(x == 0);
	CHECK(gw_handle_borrow(GW_T_DEVICE, Z, 0, &x) == -EBADF);   /* a borrowed entry is never a root */
	x = 99;
	CHECK(gw_handle_borrow(GW_T_COUNT, Z, t.a, &x) == -EBADF && x == 0);   /* no such type */
	CHECK(gw_handle_count() == 7);
	CHECK(gw_handle_find(GW_T_DEVICE, Z) == 0);

	/* ids that never existed */
	CHECK(gw_handle_ptr(0, GW_T_CONTEXT) == NULL);
	CHECK(gw_handle_ptr(12345, GW_T_CONTEXT) == NULL);
	CHECK(gw_handle_ptr(((gw_id)8 << 32) | 1, GW_T_CONTEXT) == NULL);   /* slot of A, wrong generation */
	CHECK(gw_handle_ptr(((gw_id)7 << 32) | 0, GW_T_CONTEXT) == NULL);   /* slot 0 does not exist */
	CHECK(gw_handle_ptr(UINT64_MAX, GW_T_CONTEXT) == NULL);
	CHECK(gw_handle_type(0) == GW_T_NONE);
	CHECK(gw_handle_type(UINT64_MAX) == GW_T_NONE);

	/* teardown: pre_destroy, children newest first, destroy */
	oplog[0] = '\0';
	gw_handle_destroy(t.a);
	CHECK(!strcmp(oplog, "pA dF pB dE dD dB dA "));
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_ptr(t.c, GW_T_DEVICE) == NULL);
	CHECK(gw_handle_ptr(chan, GW_T_CHANNEL) == NULL);
	CHECK(gw_handle_find(GW_T_DEVICE, C) == 0);
	CHECK(gw_handle_find(GW_T_CONTEXT, A) == 0);

	/* stale ids */
	x = gw_handle_add(GW_T_CONTEXT, A, 0, true, &ops, NULL);
	CHECK(x != 0);
	CHECK((uint32_t)x == (uint32_t)t.a);
	CHECK(x != t.a);
	CHECK(x >> 32 > 7);
	CHECK(gw_handle_type(t.a) == GW_T_NONE);
	CHECK(gw_handle_ptr(t.a, GW_T_CONTEXT) == NULL);
	CHECK(gw_handle_parent(t.e) == 0);
	CHECK(gw_handle_root(t.e) == 0);
	CHECK(gw_handle_root(t.a) == 0);
	CHECK(!gw_handle_owned(t.a));
	CHECK(gw_handle_aux(t.a) == NULL);
	oplog[0] = '\0';
	gw_handle_destroy(t.a);
	gw_handle_destroy(t.b);
	gw_handle_destroy(0);
	gw_handle_destroy(12345);
	CHECK(oplog[0] == '\0');
	CHECK(gw_handle_count() == 1);
	CHECK(gw_handle_ptr(x, GW_T_CONTEXT) == A);
	CHECK(gw_handle_add(GW_T_BLOCK, Q, t.a, true, &ops, NULL) == 0);   /* stale parent */
	CHECK(gw_handle_add(GW_T_CONTEXT, NULL, 0, true, &ops, NULL) == 0);
	CHECK(gw_handle_add(GW_T_NONE, Q, 0, true, &ops, NULL) == 0);
	CHECK(gw_handle_add(GW_T_COUNT, Q, 0, true, &ops, NULL) == 0);
	CHECK(gw_handle_add((enum gw_type)-1, Q, 0, true, &ops, NULL) == 0);
	y = 99;
	CHECK(gw_handle_borrow(GW_T_DEVICE, Z, t.a, &y) == -EBADF);
	CHECK(gw_handle_count() == 1);
	CHECK(oplog[0] == '\0');

	/* ops may be NULL, and so may each op */
	y = gw_handle_add(GW_T_SCAN, Q, 0, true, NULL, NULL);
	CHECK(y != 0 && gw_handle_owned(y));
	gw_handle_destroy(y);
	CHECK(gw_handle_type(y) == GW_T_NONE);
	gw_handle_destroy(x);
	CHECK(!strcmp(oplog, "pA dA "));
	CHECK(gw_handle_count() == 0);
}

static void test_aux(void)
{
	gw_id id;

	gw_handle_reset(7);
	id = gw_handle_add(GW_T_CONV_STREAM, A, 0, true, &ops, AUX);
	CHECK(gw_handle_aux(id) == AUX);
	seen_aux = NULL;
	gw_handle_destroy(id);
	CHECK(seen_aux == AUX);
	CHECK(gw_handle_aux(id) == NULL);
}

static void test_flags_and_cache(void)
{
	struct tree t;

	gw_handle_reset(7);
	build_tree(&t);

	CHECK(gw_handle_flags(t.b) == 0);
	gw_handle_set_flags(t.b, 3);
	CHECK(gw_handle_flags(t.b) == 3);
	CHECK(gw_handle_flags(t.d) == 0);
	gw_handle_set_flags(t.c, 0x80000001u);          /* a borrowed entry has flags too */
	CHECK(gw_handle_flags(t.c) == 0x80000001u);
	CHECK(gw_handle_flags(12345) == 0);
	gw_handle_set_flags(12345, 3);
	gw_handle_set_flags(0, 3);
	CHECK(gw_handle_flags(12345) == 0 && gw_handle_flags(0) == 0);

	oplog[0] = '\0';
	CHECK(gw_handle_cache(t.b) == NULL);
	gw_handle_set_cache(t.b, K1, cache_free);
	CHECK(gw_handle_cache(t.b) == K1);
	CHECK(oplog[0] == '\0');
	gw_handle_set_cache(t.b, K2, cache_free);
	CHECK(gw_handle_cache(t.b) == K2);
	CHECK(!strcmp(oplog, "kK1 "));
	gw_handle_set_cache(t.c, K3, NULL);             /* borrowed entry; nothing to call */
	CHECK(gw_handle_cache(t.c) == K3);
	CHECK(gw_handle_cache(12345) == NULL);
	gw_handle_set_cache(12345, K1, cache_free);     /* stale id: ignored, nothing called */
	CHECK(gw_handle_cache(12345) == NULL);
	CHECK(!strcmp(oplog, "kK1 "));
	CHECK(gw_handle_ptr(t.b, GW_T_BUFFER_STREAM) == B);   /* the cache is not the pointer */
	CHECK(gw_handle_aux(t.b) == NULL);

	oplog[0] = '\0';
	gw_handle_destroy(t.a);
	CHECK(!strcmp(oplog, "pA dF pB dE dD kK2 dB dA "));
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_cache(t.b) == NULL);
	CHECK(gw_handle_flags(t.b) == 0);

	/* the slot of B starts clean when it is used again */
	build_tree(&t);
	CHECK(gw_handle_flags(t.b) == 0 && gw_handle_cache(t.b) == NULL);
	CHECK(gw_handle_flags(t.c) == 0 && gw_handle_cache(t.c) == NULL);
	/* a cache set to NULL releases the previous one */
	gw_handle_set_cache(t.d, K1, cache_free);
	oplog[0] = '\0';
	gw_handle_set_cache(t.d, NULL, NULL);
	CHECK(!strcmp(oplog, "kK1 "));
	CHECK(gw_handle_cache(t.d) == NULL);
	oplog[0] = '\0';
	gw_handle_destroy(t.a);
	CHECK(!strcmp(oplog, "pA dF pB dE dD dB dA "));
}

/* What an entry sees of itself from inside its ops. */
static gw_id probe_id, probe_child;
static gw_id pre_found, add_in_pre, add_in_destroy;
static uint32_t pre_flags;
static void *pre_ptr, *destroy_ptr;
static enum gw_type destroy_type;
static int probe_pre_runs, probe_destroy_runs;
static int borrow_in_destroy;

static void probe_pre(void *ptr, void *aux)
{
	(void)aux;
	probe_pre_runs++;
	pre_found = gw_handle_find(GW_T_BUFFER_STREAM, ptr);
	pre_flags = gw_handle_flags(probe_id);
	pre_ptr = gw_handle_ptr(probe_id, GW_T_BUFFER_STREAM);
	gw_handle_destroy(probe_id);                    /* already being destroyed: ignored */
	add_in_pre = gw_handle_add(GW_T_BLOCK, Q, probe_id, true, &ops_destroy_only, NULL);
}

static void probe_destroy(void *ptr, void *aux)
{
	gw_id id = 99;

	(void)ptr;
	(void)aux;
	probe_destroy_runs++;
	destroy_ptr = gw_handle_ptr(probe_id, GW_T_BUFFER_STREAM);
	destroy_type = gw_handle_type(gw_handle_find(GW_T_BUFFER_STREAM, P));
	/* nothing can be attached to an entry whose children are already gone */
	add_in_destroy = gw_handle_add(GW_T_BLOCK, Z, probe_id, true, &ops_destroy_only, NULL);
	borrow_in_destroy = gw_handle_borrow(GW_T_DEVICE, Z, probe_id, &id);
	/* re-entrancy: its own id and its child, which is already destroyed */
	gw_handle_destroy(probe_id);
	gw_handle_destroy(probe_child);
}

static const struct gw_ops ops_probe = { probe_pre, probe_destroy };

static void test_ops_see_the_entry(void)
{
	gw_handle_reset(7);
	probe_id = gw_handle_add(GW_T_BUFFER_STREAM, P, 0, true, &ops_probe, NULL);
	probe_child = gw_handle_add(GW_T_BLOCK, D, probe_id, true, &ops, NULL);
	gw_handle_set_flags(probe_id, 3);
	oplog[0] = '\0';
	destroy_ptr = P;
	add_in_destroy = 99;
	gw_handle_destroy(probe_id);
	CHECK(probe_pre_runs == 1 && probe_destroy_runs == 1);
	CHECK(pre_found == probe_id);
	CHECK(pre_flags == 3);
	CHECK(pre_ptr == P);
	CHECK(destroy_ptr == NULL);
	CHECK(destroy_type == GW_T_NONE);               /* and it is not found any more */
	CHECK(add_in_pre != 0);                         /* a child added in pre_destroy goes with the rest */
	CHECK(add_in_destroy == 0);
	CHECK(borrow_in_destroy == -EBADF);
	CHECK(!strcmp(oplog, "dQ pD dD "));             /* each op of the children exactly once */
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_type(probe_id) == GW_T_NONE);
}

/* A child whose pre_destroy destroys its own parent: nothing runs twice, nothing hangs. */
static gw_id up_parent;
static int up_runs[4];

static void up_child_pre(void *ptr, void *aux)
{
	(void)ptr;
	(void)aux;
	up_runs[0]++;
	gw_handle_destroy(up_parent);
}

static void up_child_destroy(void *ptr, void *aux)
{
	(void)ptr;
	(void)aux;
	up_runs[1]++;
}

static void up_parent_pre(void *ptr, void *aux)
{
	(void)ptr;
	(void)aux;
	up_runs[2]++;
}

static void up_parent_destroy(void *ptr, void *aux)
{
	(void)ptr;
	(void)aux;
	up_runs[3]++;
}

static const struct gw_ops ops_up_child = { up_child_pre, up_child_destroy };
static const struct gw_ops ops_up_parent = { up_parent_pre, up_parent_destroy };

static void test_child_destroys_parent(void)
{
	gw_id child, other;

	gw_handle_reset(7);
	up_parent = gw_handle_add(GW_T_CONTEXT, A, 0, true, &ops_up_parent, NULL);
	other = gw_handle_add(GW_T_MASK, F, up_parent, true, &ops, NULL);
	child = gw_handle_add(GW_T_BUFFER_STREAM, B, up_parent, true, &ops_up_child, NULL);
	oplog[0] = '\0';
	gw_handle_destroy(child);
	CHECK(up_runs[0] == 1 && up_runs[1] == 1 && up_runs[2] == 1 && up_runs[3] == 1);
	CHECK(!strcmp(oplog, "pF dF "));
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_type(other) == GW_T_NONE);
	CHECK(gw_handle_type(child) == GW_T_NONE);
	CHECK(gw_handle_type(up_parent) == GW_T_NONE);
}

static void test_destroy_all(void)
{
	gw_id r1, r2, r3, s;

	gw_handle_reset(7);
	gw_handle_destroy_all();                        /* empty table */
	r1 = gw_handle_add(GW_T_CONTEXT, R1, 0, true, &ops_destroy_only, NULL);
	r2 = gw_handle_add(GW_T_CONTEXT, R2, 0, true, &ops_destroy_only, NULL);
	r3 = gw_handle_add(GW_T_SCAN, R3, 0, true, &ops_destroy_only, NULL);
	s = gw_handle_add(GW_T_MASK, S, r2, true, &ops_destroy_only, NULL);
	CHECK(r1 && r2 && r3 && s);
	CHECK(gw_handle_count() == 4);
	oplog[0] = '\0';
	gw_handle_destroy_all();
	CHECK(!strcmp(oplog, "dR3 dS dR2 dR1 "));
	CHECK(gw_handle_count() == 0);
	gw_handle_destroy_all();
	CHECK(!strcmp(oplog, "dR3 dS dR2 dR1 "));

	/* a root destroyed out of order leaves the order of the others alone */
	r1 = gw_handle_add(GW_T_CONTEXT, R1, 0, true, &ops_destroy_only, NULL);
	r2 = gw_handle_add(GW_T_CONTEXT, R2, 0, true, &ops_destroy_only, NULL);
	r3 = gw_handle_add(GW_T_SCAN, R3, 0, true, &ops_destroy_only, NULL);
	gw_handle_destroy(r2);
	r2 = gw_handle_add(GW_T_CONTEXT, R2, 0, true, &ops_destroy_only, NULL);   /* slot of old R2, newest root */
	oplog[0] = '\0';
	gw_handle_destroy_all();
	CHECK(!strcmp(oplog, "dR2 dR3 dR1 "));
	CHECK(gw_handle_count() == 0);
}

#define N_GROW 10000
static char pool[N_GROW];
static gw_id ids[N_GROW];
static int counted;

static void op_count(void *ptr, void *aux)
{
	(void)ptr;
	(void)aux;
	counted++;
}

static const struct gw_ops ops_count = { NULL, op_count };

static void test_growth(void)
{
	int bad = 0;
	gw_id kid = 0;

	gw_handle_reset(7);
	for (int i = 0; i < N_GROW; i++)
		ids[i] = gw_handle_add(GW_T_CONTEXT, &pool[i], 0, true, &ops_count, NULL);
	CHECK(gw_handle_count() == N_GROW);
	for (int i = 0; i < N_GROW; i++) {
		bad += ids[i] == 0 || (uint32_t)ids[i] != (uint32_t)i + 1 || ids[i] >> 32 != 7;
		bad += gw_handle_ptr(ids[i], GW_T_CONTEXT) != &pool[i];
		bad += gw_handle_find(GW_T_CONTEXT, &pool[i]) != ids[i];
		bad += gw_handle_find(GW_T_DEVICE, &pool[i]) != 0;
	}
	CHECK(bad == 0);

	/* every second one goes; the others are untouched and the slots are used again */
	for (int i = 0; i < N_GROW; i += 2)
		gw_handle_destroy(ids[i]);
	CHECK(counted == N_GROW / 2);
	CHECK(gw_handle_count() == N_GROW / 2);
	for (int i = 0; i < N_GROW; i++) {
		void *want = i % 2 ? &pool[i] : NULL;

		bad += gw_handle_ptr(ids[i], GW_T_CONTEXT) != want;
		bad += gw_handle_find(GW_T_CONTEXT, &pool[i]) != (i % 2 ? ids[i] : 0);
	}
	CHECK(bad == 0);
	for (int i = 0; i < N_GROW; i += 2) {
		gw_id old = ids[i];

		bad += gw_handle_borrow(GW_T_DEVICE, &pool[i], ids[1], &ids[i]) != 0;
		bad += ids[i] == old || ids[i] == 0 || (uint32_t)ids[i] > N_GROW;
		bad += gw_handle_ptr(old, GW_T_CONTEXT) != NULL;
	}
	CHECK(bad == 0);
	CHECK(gw_handle_count() == N_GROW);
	for (int i = 0; i < N_GROW; i += 2) {
		bad += gw_handle_ptr(ids[i], GW_T_DEVICE) != &pool[i];
		bad += gw_handle_borrow(GW_T_DEVICE, &pool[i], ids[3], &kid) != 0 || kid != ids[i];
		bad += gw_handle_root(ids[i]) != ids[1];
	}
	CHECK(bad == 0);
	CHECK(gw_handle_count() == N_GROW);

	/* one parent with 5000 children, then everything */
	counted = 0;
	gw_handle_destroy(ids[1]);
	CHECK(counted == 1);
	CHECK(gw_handle_count() == N_GROW / 2 - 1);
	gw_handle_destroy_all();
	CHECK(counted == N_GROW / 2);
	CHECK(gw_handle_count() == 0);
	for (int i = 0; i < N_GROW; i++)
		bad += gw_handle_type(ids[i]) != GW_T_NONE;
	CHECK(bad == 0);
}

static void test_reset(void)
{
	gw_id a, b, x;

	gw_handle_reset(7);
	a = gw_handle_add(GW_T_CONTEXT, A, 0, true, &ops, NULL);
	b = gw_handle_add(GW_T_BUFFER_STREAM, B, a, true, &ops, NULL);
	gw_handle_set_cache(b, K1, cache_free);
	oplog[0] = '\0';
	gw_handle_reset(9);
	CHECK(oplog[0] == '\0');                        /* no op, no cache free */
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_type(a) == GW_T_NONE && gw_handle_type(b) == GW_T_NONE);
	CHECK(gw_handle_find(GW_T_CONTEXT, A) == 0);
	x = gw_handle_add(GW_T_CONTEXT, A, 0, true, &ops, NULL);
	CHECK(x >> 32 == 9);
	CHECK((uint32_t)x == 1);
	CHECK(gw_handle_ptr(a, GW_T_CONTEXT) == NULL);  /* same slot, generation 7 */
	gw_handle_destroy_all();
	CHECK(!strcmp(oplog, "pA dA "));

	gw_handle_reset(0);
	x = gw_handle_add(GW_T_CONTEXT, A, 0, true, NULL, NULL);
	CHECK(x == (((gw_id)1 << 32) | 1));
	gw_handle_reset(UINT32_MAX);
	x = gw_handle_add(GW_T_CONTEXT, A, 0, true, NULL, NULL);
	CHECK(x == (((gw_id)UINT32_MAX << 32) | 1));
	gw_handle_destroy(x);
	/* the generation after the last one is not 0 */
	x = gw_handle_add(GW_T_CONTEXT, A, 0, true, NULL, NULL);
	CHECK((uint32_t)x == 1 && x >> 32 != 0 && x >> 32 != UINT32_MAX);
	gw_handle_destroy_all();
	CHECK(gw_handle_count() == 0);
}

static void test_type_names(void)
{
	static const char *const want[GW_T_COUNT] = {
		"none", "context", "scan", "device", "channel", "attr", "buffer", "channels mask",
		"buffer stream", "block", "stream", "event stream", "conv stream",
	};

	CHECK(!strcmp(gw_type_name(GW_T_ATTR), "attr"));
	CHECK(!strcmp(gw_type_name(GW_T_COUNT), "none"));
	CHECK(!strcmp(gw_type_name((enum gw_type)-1), "none"));
	CHECK(!strcmp(gw_type_name((enum gw_type)1000), "none"));
	for (int t = 0; t < GW_T_COUNT; t++)
		CHECK(!strcmp(gw_type_name((enum gw_type)t), want[t]));
}

int main(void)
{
	/* before any reset the table is empty and usable */
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_type(1) == GW_T_NONE);
	gw_handle_destroy_all();

	test_tree();
	test_aux();
	test_flags_and_cache();
	test_ops_see_the_entry();
	test_child_destroys_parent();
	test_destroy_all();
	test_growth();
	test_reset();
	test_type_names();
	gw_handle_reset(1);                             /* releases the table's memory */

	TEST_MAIN_RETURN();
}
