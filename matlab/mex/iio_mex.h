/* SPDX-License-Identifier: MIT */
/* Shared declarations of the iio_mex gateway: command signature, registry, error ids. */
#ifndef IIO_MEX_H
#define IIO_MEX_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mex.h"
#include <iio/iio.h>

#include "handles.h"

#if !MX_HAS_INTERLEAVED_COMPLEX
#error "build with the R2018a (interleaved complex) MEX API"
#endif

/* One mexFunction invocation as a command sees it. */
struct gw_call {
	const char *cmd;        /* command name */
	int nlhs;               /* outputs requested; plhs[k] may be set for k < max(nlhs, 1) */
	mxArray **plhs;         /* all NULL on entry */
	int nrhs;               /* arguments after the command name */
	const mxArray **prhs;   /* prhs[0] is the first argument after the command name */
	char err_msg[256];      /* "": the text is iio_strerror() of the return value */
};

/* A command returns 0 or a negative errno. It never raises: it frees what it allocated and
 * returns, with every libiio object it created either registered in the handle table or destroyed. */
typedef int (*gw_fn)(struct gw_call *c);

struct gw_cmd {
	const char *name;
	gw_fn fn;
	int min_rhs, max_rhs;   /* allowed number of arguments after the command name */
	int max_lhs;            /* allowed number of outputs */
};

struct gw_group {
	const char *name;
	const struct gw_cmd *cmds;
	size_t nb_cmds;
};

/* One registration per source line: matlab/tools/check_api.py reads the names from the C files.
 * GW_CMD: the command of a row of matlab/api-map.tsv. GW_XCMD: an extra command (no row). */
#define GW_CMD(name, fn, min_rhs, max_rhs, max_lhs) { name, fn, min_rhs, max_rhs, max_lhs }
#define GW_XCMD(name, fn, min_rhs, max_rhs, max_lhs) { name, fn, min_rhs, max_rhs, max_lhs }
#define GW_GROUP(name, cmds) { name, cmds, sizeof(cmds) / sizeof((cmds)[0]) }

/* NULL-terminated. registry.c lists every group for the MEX file; a test lists what it links. */
extern const struct gw_group *const gw_groups[];

extern const struct gw_group gw_group_mex,   /* iio_mex.c */
	gw_group_toplevel, gw_group_context, gw_group_attr, gw_group_device, gw_group_channel,
	gw_group_buffer, gw_group_block, gw_group_stream, gw_group_event, gw_group_lowlevel,
	gw_group_conv, gw_group_test;            /* cmd_<name>.c; gw_group_test only with IIO_MEX_TEST_HOOKS */

/* Table entry of a command whose task has not landed yet: gw_fail(c, -ENOSYS, "not implemented"). */
int gw_stub(struct gw_call *c);

/* Records the message in c and returns err (negative). The dispatcher raises "iio:<ERRNO NAME>". */
int gw_fail(struct gw_call *c, int err, const char *fmt, ...) __attribute__((format(__printf__, 3, 4)));

#endif
