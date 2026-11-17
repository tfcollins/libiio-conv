/* SPDX-License-Identifier: MIT */
/* Argument and return helpers of the iio_mex gateway. */
#ifndef IIO_MEX_MX_UTIL_H
#define IIO_MEX_MX_UTIL_H

#include "iio_mex.h"

#include <locale.h>

/* "EINVAL" for -22 or 22; NULL for 0 and for numbers without a name. 95 is "ENOTSUP", 11 "EAGAIN". */
const char *gw_errno_name(int err);

/* ---- arguments: k is 0-based, counted after the command name. Each returns 0, or the
 * negative errno it passed to gw_fail(): -EINVAL for a wrong class, shape or range, -EBADF for a handle. ---- */
bool gw_arg_given(const struct gw_call *c, int k);   /* k < nrhs and not [] (0x0 double) */
int gw_arg_id(struct gw_call *c, int k, gw_id *id);  /* real uint64 scalar; no liveness check */
/* Live handle of that type. ptr (may be NULL) is the address of a pointer variable; id may be NULL. */
int gw_arg_handle(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id);
/* The same, but id 0 is accepted and gives a NULL pointer. */
int gw_arg_handle0(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id);
/* Destructor argument: zero or stale id: nothing. Live owned id of that type: gw_handle_destroy().
 * Live borrowed id of that type: nothing. Live id of another type: -EBADF. */
int gw_arg_destroy(struct gw_call *c, int k, enum gw_type type);
/* char row vector or empty char -> UTF-8 copy from mxMalloc; the caller mxFree()s it.
 * Text with an embedded NUL is -EINVAL (C would cut it there without notice). */
int gw_arg_str(struct gw_call *c, int k, char **s);
int gw_arg_str0(struct gw_call *c, int k, char **s); /* absent or [] -> *s = NULL */
int gw_arg_bool(struct gw_call *c, int k, bool *v);  /* logical scalar */
/* Real, non-sparse, integer-valued numeric scalar in [lo, hi]. int64/uint64 are read exactly. */
int gw_arg_int(struct gw_call *c, int k, long long lo, long long hi, long long *v);
int gw_arg_index(struct gw_call *c, int k, unsigned int *v);   /* gw_arg_int in [0, UINT_MAX]; no command uses it yet */
int gw_arg_double(struct gw_call *c, int k, double *v);        /* real numeric scalar */
/* Real uint8 vector (or empty uint8): pointer INTO prhs (read only), and the element count.
 * *p is never NULL: for an empty array (mxGetData is NULL) it points at a static byte, *n = 0. */
int gw_arg_bytes(struct gw_call *c, int k, const uint8_t **p, size_t *n);
/* [] -> *params = NULL. Scalar struct with fields out of log_level, stderr_level,
 * timestamp_level, timeout_ms, flags (each a real integer-valued scalar in int range)
 * -> *storage zeroed, filled, *params = storage. An unknown field is -EINVAL. */
int gw_arg_params(struct gw_call *c, int k, struct iio_context_params *storage,
		const struct iio_context_params **params);

/* ---- results: set plhs[k]; do nothing when k > 0 and k >= c->nlhs. Return 0, or -errno. ---- */
int gw_ret_empty(struct gw_call *c, int k);                    /* [] */
int gw_ret_id(struct gw_call *c, int k, gw_id id);             /* uint64 scalar */
/* Overwrites the uint64 scalar that gw_ret_id() put in plhs[k]; never fails; nothing when k > 0 and
 * k >= nlhs. "Result first": gw_ret_id(c, 0, 0), create the object, register it, gw_set_id(c, 0, id). */
void gw_set_id(struct gw_call *c, int k, gw_id id);
int gw_ret_ids(struct gw_call *c, int k, const gw_id *ids, size_t n);     /* 1-by-n uint64 */
/* Id of ptr as a borrowed child of the ROOT of owner (its context); id 0 for a NULL ptr. */
int gw_ret_borrowed(struct gw_call *c, int k, enum gw_type type, const void *ptr, gw_id owner);
int gw_ret_str(struct gw_call *c, int k, const char *s);       /* NULL -> []; else char row */
int gw_ret_strs(struct gw_call *c, int k, char *const *list, size_t n);   /* 1-by-n cell of char rows */
int gw_ret_bool(struct gw_call *c, int k, bool v);
int gw_ret_double(struct gw_call *c, int k, double v);
/* Overwrites the double scalar that gw_ret_double() put in plhs[k]; never fails; nothing when k > 0
 * and k >= nlhs. "Result first" for a command that writes to the device and returns a count:
 * gw_ret_double(c, 0, 0), the write, gw_set_double(c, 0, count). */
void gw_set_double(struct gw_call *c, int k, double v);
int gw_ret_doubles(struct gw_call *c, int k, const double *v, size_t n);  /* 1-by-n double */
int gw_ret_int64(struct gw_call *c, int k, int64_t v);         /* int64 scalar */
int gw_ret_uint64(struct gw_call *c, int k, uint64_t v);       /* uint64 scalar */
int gw_ret_bytes(struct gw_call *c, int k, const void *p, size_t n);      /* 1-by-n uint8 */

/* ---- memory libiio returned from malloc (iio_context_get_xml, iio_attr_get_available) ----
 * Parks p: the dispatcher calls free_fn(p, n) when the call ends, however it ends: right after the
 * command returned or, when an mx allocation ended the call non-locally, at the start of the next
 * call or in the at-exit function. The command never frees p itself. Up to 4 pointers per call.
 * Returns 0, or -ENOMEM after calling free_fn(p, n) at once when the four slots are taken. */
int gw_park(void *p, size_t n, void (*free_fn)(void *p, size_t n));

/* ---- numeric locale ----
 * libiio parses and prints some doubles with the plain C functions (strtof, sscanf "%lf",
 * "%f"), which follow LC_NUMERIC: under a locale with a decimal comma every channel scale of a
 * context made from XML is cut at the point, the XML text is written with commas and a
 * fractional range is not parsed. A command brackets such a libiio call with this pair:
 *     struct gw_c_numeric loc;
 *     ret = gw_c_numeric_enter(&loc);        (0, or -ENOMEM: nothing to leave)
 *     ... the libiio call ...
 *     gw_c_numeric_leave(&loc);
 * Between the two the decimal separator of the calling thread is a point: when it is not one
 * already, the thread's LC_NUMERIC is "C" and every other category is what it was. Leave
 * restores the thread's locale object exactly. No mx* call and no return between
 * them: an mx allocation that fails ends the call non-locally and would leave the thread in
 * the temporary locale. Which calls: "Conventions / Gateway (C)", "Numeric locale". */
struct gw_c_numeric { locale_t c, old; };
int gw_c_numeric_enter(struct gw_c_numeric *s);
void gw_c_numeric_leave(struct gw_c_numeric *s);     /* nothing after a failed enter or a leave */

/* ---- enum names: tables generated from iio.h at build time by matlab/tools/gen_enums.py ---- */
enum gw_enum {
	GW_ENUM_CHAN_TYPE,        /* enum iio_chan_type */
	GW_ENUM_MODIFIER,         /* enum iio_modifier */
	GW_ENUM_EVENT_TYPE,       /* enum iio_event_type */
	GW_ENUM_EVENT_DIRECTION,  /* enum iio_event_direction */
	GW_ENUM_HWMON_CHAN_TYPE,  /* enum hwmon_chan_type */
	GW_ENUM_COUNT
};
struct gw_enum_entry { const char *name; long long value; };
const struct gw_enum_entry *gw_enum_table(enum gw_enum which, size_t *count);  /* header order */
const char *gw_enum_name(enum gw_enum which, long long value);  /* "IIO_VOLTAGE"; NULL if no such value */

#endif
