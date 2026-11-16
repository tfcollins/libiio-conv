/* SPDX-License-Identifier: MIT */
/* Helpers for gateway tests that run mexFunction on the mock MATLAB runtime. */
#ifndef MEX_TEST_H
#define MEX_TEST_H

#include <errno.h>
#include <string.h>

#include "mx_util.h"
#include "mxmock.h"
#undef printf
#include "test_util.h"
#include "fixture_util.h"

/* Command groups of this test, in addition to gw_group_mex. Must start a line: tests/mex/
 * CMakeLists.txt reads it to decide which cmd_<group>.c objects to link. */
#define MT_GROUPS(...) const struct gw_group *const gw_groups[] = { &gw_group_mex, __VA_ARGS__, NULL }

struct mt_fixture {
	char dir[PATH_MAX];          /* made by fixture_setup(): holds iio_fixture.xml and emu-rx_buf0.bin (the ramp) */
	char xml[PATH_MAX + 32];     /* <dir>/iio_fixture.xml */
	char uri[PATH_MAX + 40];     /* "emu:<xml>" */
	char xml_uri[PATH_MAX + 40]; /* "xml:<xml>" */
};
int mt_fixture_setup(struct mt_fixture *fx, const char *src_xml);   /* fixture_setup() and the three paths; 0 or -errno */
void mt_fixture_teardown(struct mt_fixture *fx);                    /* fixture_cleanup() */

mxArray *mt_str(const char *s);
mxArray *mt_u64(uint64_t v);
mxArray *mt_i64(int64_t v);
mxArray *mt_dbl(double v);
mxArray *mt_bool(bool v);
mxArray *mt_bytes(const void *p, size_t n);   /* 1-by-n uint8 */
mxArray *mt_empty(void);                      /* [] */

/* mexFunction(nlhs, out, 1 + nargs, {cmd, args...}); args are mxArray * and are destroyed
 * afterwards. Returns mock_call()'s result. out needs max(nlhs, 1) entries; at most 15 args. */
int mt_call(int nlhs, mxArray *out[], const char *cmd, int nargs, ...);

/* One-output shorthands. On a raise or a result of another class they print the error, count
 * a test failure and return 0 / false / NULL. The result array is destroyed. */
uint64_t mt_u64_of(const char *cmd, int nargs, ...);
int64_t mt_i64_of(const char *cmd, int nargs, ...);
double mt_dbl_of(const char *cmd, int nargs, ...);
bool mt_bool_of(const char *cmd, int nargs, ...);
const char *mt_str_of(const char *cmd, int nargs, ...);   /* static 64 KiB buffer; NULL when the result is [] */
void mt_ok(const char *cmd, int nargs, ...);               /* nlhs = 0; must not raise */
bool mt_raises(const char *id, const char *cmd, int nargs, ...);  /* true iff raised with exactly this identifier */
/* Calls cmd with the 1st, then the 2nd, ... mx allocation failing (mock_fail_alloc), until an
 * attempt ends without an injected failure. Returns the number of failed attempts. Arguments
 * as mt_call, reused for every attempt and destroyed at the end; outputs are destroyed. Objects
 * a failed attempt had already registered stay in the handle table: MT_MAIN_RETURN proves they
 * are released. */
int mt_oom_sweep(int nlhs, const char *cmd, int nargs, ...);

/* One row of a group's "Command reference" table, written into the test from the reference. */
struct mt_arity {
	const char *name;
	int min_rhs, max_rhs;   /* column "Args" */
	int max_lhs;            /* column "Out" */
};
/* The registered table of g is exactly want[0 .. n-1]: the same names in the same order, each
 * with these limits. Every difference is printed and counted as a harness failure (exit status
 * 1). The dispatcher enforces what is registered (test_dispatch); this is what proves that the
 * registered limits are the ones of the reference. Every test_cmd_<group>.c calls it once. */
void mt_check_arities(const struct gw_group *g, const struct mt_arity *want, size_t n);

/* iio_create_context(NULL, uri) registered as an owned GW_T_CONTEXT root whose destroy op is
 * iio_context_destroy; 0 on failure. Lets a group's test run without the context commands. */
gw_id mt_open_context(const char *uri);

/* Ends main(): MATLAB exit, then no live handle, no live mx memory, nothing auto-freed. */
#define MT_MAIN_RETURN() do { \
	mock_exit_matlab(); \
	CHECK(gw_handle_count() == 0); \
	CHECK(mock_live() == 0); \
	CHECK(mock_autofreed() == 0); \
	TEST_MAIN_RETURN(); } while (0)

#endif
