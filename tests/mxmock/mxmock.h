/* SPDX-License-Identifier: MIT */
/* Test-side control of the mock MATLAB runtime. */
#ifndef MXMOCK_H
#define MXMOCK_H

#include "matrix.h"

/* Runs mexFunction once. Returns 0 on a normal return, 1 when mexErrMsgIdAndTxt was raised
 * (then every plhs[k] is NULL). plhs needs max(nlhs, 1) entries and is pre-filled with a
 * non-NULL poison value, as MATLAB promises nothing. Arrays returned in plhs belong to the
 * test afterwards. Arrays the test creates outside mock_call (inputs) belong to the test. */
int mock_call(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

const char *mock_err_id(void);       /* "" if the last mock_call returned normally */
const char *mock_err_msg(void);
unsigned int mock_warn_count(void);  /* mexWarnMsgIdAndTxt calls since process start */
const char *mock_warn_id(void);      /* identifier of the latest one, "" if none */
const char *mock_output(void);       /* everything mexPrintf wrote so far */

bool mock_is_locked(void);
void mock_clear_mex(void);    /* "clear mex": nothing while locked; else runs the at-exit function once and forgets it */
void mock_exit_matlab(void);  /* MATLAB exit: runs the at-exit function even when locked, lock count becomes 0 */

/* "Auto-freed" = mxArrays and mxMalloc blocks the gateway left behind when a call ended (by
 * return or by error) and that MATLAB's automatic cleanup would have released. */
size_t mock_autofreed(void);  /* cumulative count; a disciplined gateway keeps it at 0 */
size_t mock_live(void);       /* mxArrays (not counting cell/struct members) + mxMalloc blocks alive now */

/* The nth mx allocation from now on (mxCreate*, mxMalloc, mxCalloc, mxDuplicateArray,
 * mxArrayToUTF8String; nth >= 1) ends the running mock_call as MATLAB ends a MEX on out of
 * memory: non-local exit, mock_call returns 1, mock_err_id() is "MATLAB:nomem". It fires once;
 * 0 switches it off. What that call had allocated is released and is NOT counted by
 * mock_autofreed(). */
void mock_fail_alloc(unsigned int nth);
bool mock_alloc_failed(void);                       /* did the last mock_call end that way */
mxArray *mock_string_object(void);                  /* stands for a MATLAB string: class name "string",
                                                       mxIsChar false, mxArrayToUTF8String NULL, mxGetData NULL */
mxArray *mock_sparse_double(size_t m, size_t n);    /* mxIsSparse true, class double */
/* Full numeric d0-by-d1-by-d2 array, zeroed. As in MATLAB: mxGetNumberOfDimensions 3, mxGetM d0,
 * mxGetN d1 * d2 (the product of the trailing dimensions), so only the dimension count tells it
 * from a d0-by-(d1*d2) matrix. */
mxArray *mock_nd_array(mxClassID cls, mxComplexity c, size_t d0, size_t d1, size_t d2);
/* char d0-by-d1-by-d2 array (a matrix when d2 == 1) whose elements, in storage order (first
 * dimension fastest), are the characters of text; strlen(text) must be d0 * d1 * d2. For the
 * char arrays mxCreateString cannot make: a column, a matrix, N-D, 0-by-3. */
mxArray *mock_char_array(size_t d0, size_t d1, size_t d2, const char *text);

/* Known mock gaps (behaviour of real MATLAB that no local test can show): add one line per gap here.
 * - mxArrayToString converts to UTF-8; the real one converts to the locale's character set.
 * - mxGetLogicals of an array that is not logical, mxGetScalar of an empty array, a cell, a
 *   struct or an object, and mxGetField / mxGetCell beyond the end abort; the real ones return
 *   the data, an undefined value, or NULL.
 * - mock_string_object() has class id mxOPAQUE_CLASS; of a real string only the class name is certain.
 * - mock_sparse_double() always has room for exactly one non-zero element.
 * - mock_fail_alloc() counts only allocations made while mock_call() runs.
 * - The stack is filled with 0xA5 before mexFunction runs (uninitialised locals), which has an
 *   effect on the real stack only, not on AddressSanitizer's fake stack.
 * (tests/mxmock/mxmock.c, at its top, gives the reason for each choice.) */

#endif
