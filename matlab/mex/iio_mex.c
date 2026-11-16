/* SPDX-License-Identifier: MIT */
/* The iio_mex gateway: mexFunction, dispatch, the single error exit, the "mex" command group.
 * Skeleton of Task 4b: every call raises iio:ENOSYS. Task 6 writes the dispatcher. */
#include "mx_util.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>

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

static const struct gw_cmd cmds[] = {
	GW_XCMD("mex_shutdown", gw_stub, 0, 0, 0),
	GW_XCMD("mex_handle_count", gw_stub, 0, 0, 1),
};

const struct gw_group gw_group_mex = GW_GROUP("mex", cmds);

/* libiio's exit handler calls xmlCleanupParser(); inside MATLAB that is MATLAB's own
 * libxml2, which libxml2 documents as unsafe while the host still uses the library.
 * Keep libiio loaded until the process ends, so "clear mex" can never trigger it. */
__attribute__((constructor)) static void pin_libiio(void)
{
	(void)dlopen("libiio.so.1", RTLD_NOW | RTLD_NODELETE);
}

/* Skeleton only: nothing else here calls libiio yet, and the MEX file is linked with
 * --as-needed (FindMatlab), which would drop libiio.so.1 from its dependencies; the pin above
 * would then find no loaded libiio. Task 6 removes this with the dispatcher (iio_strerror). */
__attribute__((used)) static void (*const skeleton_uses_libiio)(int, char *, size_t) = iio_strerror;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	(void)nlhs;
	(void)plhs;
	(void)nrhs;
	(void)prhs;
	mexErrMsgIdAndTxt("iio:ENOSYS", "not implemented");
}
