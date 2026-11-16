/* SPDX-License-Identifier: MIT */
/* The iio_mex gateway: mexFunction, dispatch, the single error exit, the lock, the trace,
 * parked memory and the "mex" command group.
 *
 * mexErrMsgIdAndTxt leaves mexFunction non-locally, and so does every mx allocation that
 * fails. The rules that follow from it:
 * - commands never raise; they return 0 or a negative errno, and mexFunction raises once, as
 *   its last action, holding nothing;
 * - whatever must survive a call that ended in the middle (handles, parked pointers, the
 *   trace file) is in statics, and is looked after at the start of the next call and in the
 *   at-exit function. */
#include "mx_util.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ what commands share */

int gw_fail(struct gw_call *c, int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(c->err_msg, sizeof(c->err_msg), fmt, ap);
	va_end(ap);
	return err;
}

int gw_stub(struct gw_call *c)
{
	return gw_fail(c, -ENOSYS, "not implemented");
}

/* Memory that libiio returned from malloc and that the running command still reads. */
#define MAX_PARKED 4

static struct {
	void *p;
	size_t n;
	void (*free_fn)(void *p, size_t n);
} parked[MAX_PARKED];
static unsigned int nb_parked;

int gw_park(void *p, size_t n, void (*free_fn)(void *p, size_t n))
{
	if (nb_parked == MAX_PARKED) {
		free_fn(p, n);
		return -ENOMEM;
	}
	parked[nb_parked].p = p;
	parked[nb_parked].n = n;
	parked[nb_parked].free_fn = free_fn;
	nb_parked++;
	return 0;
}

static void release_parked(void)
{
	while (nb_parked) {
		nb_parked--;
		parked[nb_parked].free_fn(parked[nb_parked].p, parked[nb_parked].n);
	}
}

/* ------------------------------------------------------------------ the "mex" group */

static int mex_shutdown(struct gw_call *c)
{
	(void)c;
	gw_handle_destroy_all();
	return 0;
}

static int mex_handle_count(struct gw_call *c)
{
	return gw_ret_double(c, 0, (double)gw_handle_count());
}

static const struct gw_cmd cmds[] = {
	GW_XCMD("mex_shutdown", mex_shutdown, 0, 0, 0),
	GW_XCMD("mex_handle_count", mex_handle_count, 0, 0, 1),
};

const struct gw_group gw_group_mex = GW_GROUP("mex", cmds);

/* ------------------------------------------------------------------ load, exit, trace */

/* libiio's exit handler calls xmlCleanupParser(); inside MATLAB that is MATLAB's own
 * libxml2, which libxml2 documents as unsafe while the host still uses the library.
 * Keep libiio loaded until the process ends, so "clear mex" can never trigger it. */
__attribute__((constructor)) static void pin_libiio(void)
{
	(void)dlopen("libiio.so.1", RTLD_NOW | RTLD_NODELETE);
}

static bool initialised;        /* false again once the at-exit function has run */
static FILE *trace;             /* the file IIO_MEX_TRACE named at the first call, or NULL */

/* Runs when MATLAB clears the MEX file (possible only while it is not locked, that is while
 * no handle exists) and when MATLAB exits (whatever the lock): streams are closed before
 * their contexts, every libiio thread ends here. It must not call any mx or mex function. */
static void at_exit(void)
{
	gw_handle_destroy_all();
	release_parked();
	gw_handle_reset(0);         /* frees the table; the next first call sets the real seed */
	if (trace) {
		fclose(trace);
		trace = NULL;
	}
	initialised = false;
}

static void first_call(void)
{
	const char *path = getenv("IIO_MEX_TRACE");
	struct timespec ts = { 0, 0 };

	/* Ids of an earlier load of the MEX file, or of the time before MATLAB's exit handler
	 * ran, must be stale: the generations start at a number that differs each time. */
	clock_gettime(CLOCK_REALTIME, &ts);
	gw_handle_reset((uint32_t)ts.tv_nsec ^ (uint32_t)getpid());
	mexAtExit(at_exit);
	if (path && *path)
		trace = fopen(path, "a");
	initialised = true;
}

static void trace_line(const char *mark, const char *cmd, const char *outcome)
{
	if (!trace)
		return;
	if (outcome)
		fprintf(trace, "%s %s %s\n", mark, cmd, outcome);
	else
		fprintf(trace, "%s %s\n", mark, cmd);
	fflush(trace);
}

/* ------------------------------------------------------------------ dispatch */

#define MAX_CMD_NAME 64

enum { NAME_NONE, NAME_UNUSABLE, NAME_OK };

/* The command name: prhs[0] if it is a char row of printable ASCII that fits (NAME_OK).
 * Nothing is allocated. Other text cannot be a registered name (NAME_UNUSABLE: `name` is a
 * readable, shortened form of it for the message and the trace); anything else is not a
 * name at all (NAME_NONE: `name` is "iio_mex"). */
static int command_name(int nrhs, const mxArray *prhs[], char name[MAX_CMD_NAME])
{
	const mxChar *text;
	bool plain = true;
	size_t len;

	snprintf(name, MAX_CMD_NAME, "iio_mex");
	if (nrhs < 1 || !prhs[0] || !mxIsChar(prhs[0]) || mxGetNumberOfDimensions(prhs[0]) != 2 ||
	    mxGetM(prhs[0]) != 1)
		return NAME_NONE;
	len = mxGetN(prhs[0]);
	text = (const mxChar *)mxGetData(prhs[0]);
	if (len == 0 || !text)
		return NAME_NONE;
	for (size_t i = 0; i < len; i++) {
		bool ok = text[i] > 0x20 && text[i] < 0x7f;

		if (i < MAX_CMD_NAME - 1)
			name[i] = ok ? (char)text[i] : '?';
		plain = plain && ok;
	}
	name[len < MAX_CMD_NAME ? len : MAX_CMD_NAME - 1] = '\0';
	return plain && len < MAX_CMD_NAME ? NAME_OK : NAME_UNUSABLE;
}

static const struct gw_cmd *find_command(const char *name)
{
	for (size_t g = 0; gw_groups[g]; g++)
		for (size_t i = 0; i < gw_groups[g]->nb_cmds; i++)
			if (!strcmp(gw_groups[g]->cmds[i].name, name))
				return &gw_groups[g]->cmds[i];
	return NULL;
}

static int run(struct gw_call *c, int named)
{
	const struct gw_cmd *cmd = named == NAME_OK ? find_command(c->cmd) : NULL;
	int ret;

	if (named == NAME_NONE)
		return gw_fail(c, -ENOSYS, "the first argument must be a command name (a char row vector)");
	if (!cmd)
		return gw_fail(c, -ENOSYS, "unknown command");
	if (c->nrhs < cmd->min_rhs || c->nrhs > cmd->max_rhs) {
		if (cmd->min_rhs == cmd->max_rhs)
			return gw_fail(c, -EINVAL, "%d argument(s) given, %d expected", c->nrhs, cmd->min_rhs);
		return gw_fail(c, -EINVAL, "%d argument(s) given, %d to %d expected", c->nrhs,
			cmd->min_rhs, cmd->max_rhs);
	}
	if (c->nlhs > cmd->max_lhs)
		return gw_fail(c, -EINVAL, "%d output(s) requested, at most %d available", c->nlhs,
			cmd->max_lhs);

	ret = cmd->fn(c);
	if (ret > 0)                /* not a negative errno: still a failure, never a success */
		ret = -ret;
	if (ret)
		return ret;
	for (int k = 0; k < c->nlhs; k++)
		if (!c->plhs[k])
			return gw_fail(c, -EFAULT, "gateway bug: output %d was not set", k + 1);
	return 0;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	char name[MAX_CMD_NAME];
	char id[32];
	char msg[MAX_CMD_NAME + 2 + sizeof(((struct gw_call *)0)->err_msg)];
	struct gw_call c;
	const char *errname;
	int named, ret;

	/* (1) Nothing is returned yet; nothing of an earlier call is still held. */
	for (int k = 0; k < (nlhs > 1 ? nlhs : 1); k++)
		plhs[k] = NULL;
	release_parked();

	/* (2) */
	if (!initialised)
		first_call();

	/* (3) and (4) */
	named = command_name(nrhs, prhs, name);
	c.cmd = name;
	c.nlhs = nlhs;
	c.plhs = plhs;
	c.nrhs = nrhs > 0 ? nrhs - 1 : 0;
	c.prhs = nrhs > 0 ? prhs + 1 : prhs;
	c.err_msg[0] = '\0';
	trace_line(">", name, NULL);
	ret = run(&c, named);
	release_parked();

	/* (5) Locked exactly while a handle exists: "clear mex" cannot unload open streams. */
	if (gw_handle_count() > 0 && !mexIsLocked())
		mexLock();
	else if (gw_handle_count() == 0 && mexIsLocked())
		mexUnlock();

	if (!ret) {
		trace_line("<", name, "ok");
		return;
	}

	/* (6) The single error exit. Everything it needs is on the stack. */
	for (int k = 0; k < (nlhs > 1 ? nlhs : 1); k++) {
		if (plhs[k])
			mxDestroyArray(plhs[k]);
		plhs[k] = NULL;
	}
	errname = gw_errno_name(ret);
	if (errname)
		snprintf(id, sizeof(id), "iio:%s", errname);
	else
		snprintf(id, sizeof(id), "iio:E%u", (unsigned int)(-(long long)ret));
	if (c.err_msg[0]) {
		snprintf(msg, sizeof(msg), "%s: %s", name, c.err_msg);
	} else {
		char text[128];

		iio_strerror(ret, text, sizeof(text));
		snprintf(msg, sizeof(msg), "%s: %s", name, text);
	}
	trace_line("<", name, id);
	mexErrMsgIdAndTxt(id, "%s", msg);
}
