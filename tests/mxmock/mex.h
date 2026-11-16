/* SPDX-License-Identifier: MIT */
/* Mock of MATLAB's mex.h: exactly the subset of the mex* functions that the iio_mex gateway
 * may use. Like the real header it includes <stdio.h> first and ends by turning printf into
 * mexPrintf; test code that wants the C printf writes "#undef printf" after its includes. */
#ifndef MXMOCK_MEX_H
#define MXMOCK_MEX_H

#include <stdio.h>

#include "matrix.h"

typedef void (*mex_exit_fn)(void);

/* The gateway. mxmock.c holds a weak definition that aborts, so that a program which never
 * calls mock_call() links without one. */
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

/* The format attributes are the mock's own: the real header has none. (Spelled __printf__
 * because of the macro at the end of this file.) */
void mexErrMsgIdAndTxt(const char *identifier, const char *err_msg, ...)
	__attribute__((format(__printf__, 2, 3)));
void mexWarnMsgIdAndTxt(const char *identifier, const char *warn_msg, ...)
	__attribute__((format(__printf__, 2, 3)));
int mexPrintf(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));

void mexLock(void);
void mexUnlock(void);
bool mexIsLocked(void);
int mexAtExit(mex_exit_fn exit_fcn);

#define printf mexPrintf

#endif
