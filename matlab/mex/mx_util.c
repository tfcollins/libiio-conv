/* SPDX-License-Identifier: MIT */
/* Argument and return helpers of the iio_mex gateway (mx_util.h).
 *
 * The argument helpers never allocate, except gw_arg_str(): its copy is the caller's to
 * mxFree(). A result helper stores its array in plhs at once, so that the dispatcher (or
 * MATLAB, when an allocation ends the call) releases it whatever happens next. */
#include "mx_util.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ errno names */

const char *gw_errno_name(int err)
{
	/* INT_MIN has no positive counterpart, and no name. */
	switch (err < 0 && err != INT_MIN ? -err : err) {
#define N(e) case e: return #e
	N(EPERM); N(ENOENT); N(ESRCH); N(EINTR); N(EIO); N(ENXIO); N(E2BIG); N(EBADF);
	N(EAGAIN);          /* also EWOULDBLOCK */
	N(ENOMEM); N(EACCES); N(EFAULT); N(EBUSY); N(EEXIST); N(ENODEV); N(ENOTDIR); N(EISDIR);
	N(EINVAL); N(EMFILE); N(EFBIG); N(ENOSPC); N(EROFS); N(EPIPE); N(EDOM); N(ERANGE);
	N(ENAMETOOLONG); N(ENOSYS); N(ENODATA); N(ETIME); N(EPROTO); N(EBADMSG); N(EOVERFLOW);
	N(EILSEQ); N(EMSGSIZE); N(EPROTOTYPE);
	N(ENOTSUP);         /* 95, also EOPNOTSUPP */
	N(EADDRINUSE); N(EAFNOSUPPORT); N(ENETDOWN); N(ENETUNREACH); N(ECONNABORTED);
	N(ECONNRESET); N(ENOBUFS); N(EISCONN); N(ENOTCONN); N(ESHUTDOWN); N(ETIMEDOUT);
	N(ECONNREFUSED); N(EHOSTDOWN); N(EHOSTUNREACH); N(EALREADY); N(EINPROGRESS);
	N(EREMOTEIO); N(ECANCELED);
#undef N
	default:
		return NULL;
	}
}

/* ------------------------------------------------------------------ argument errors */

/* "a 1x2 complex uint64": what arrived, for an error text. */
static void describe(const mxArray *a, char *dst, size_t len)
{
	char size[48];

	if (!a) {
		snprintf(dst, len, "nothing");
		return;
	}
	if (mxGetNumberOfDimensions(a) > 2)
		snprintf(size, sizeof(size), "%zu-D", (size_t)mxGetNumberOfDimensions(a));
	else
		snprintf(size, sizeof(size), "%zux%zu", mxGetM(a), mxGetN(a));
	snprintf(dst, len, "a %s %s%s%s", size, mxIsSparse(a) ? "sparse " : "",
		mxIsComplex(a) ? "complex " : "", mxGetClassName(a));
}

static const mxArray *arg(const struct gw_call *c, int k)
{
	return k >= 0 && k < c->nrhs ? c->prhs[k] : NULL;
}

/* -EINVAL with the text "argument <k+1>: expected <what>, got <class and size>". */
static int bad_arg(struct gw_call *c, int k, const char *what)
{
	char got[128];

	describe(arg(c, k), got, sizeof(got));
	return gw_fail(c, -EINVAL, "argument %d: expected %s, got %s", k + 1, what, got);
}

static bool is_empty_double(const mxArray *a)
{
	return mxGetClassID(a) == mxDOUBLE_CLASS && !mxIsSparse(a) &&
		mxGetNumberOfDimensions(a) == 2 && mxGetM(a) == 0 && mxGetN(a) == 0;
}

/* A full, real scalar of one of the ten numeric classes (not logical, not char). */
static bool is_real_numeric_scalar(const mxArray *a)
{
	return a && mxIsNumeric(a) && !mxIsSparse(a) && !mxIsComplex(a) && mxIsScalar(a) &&
		mxGetData(a) != NULL;
}

/* The value of a real numeric scalar if it is an integer that a long long holds. */
static bool exact_int(const mxArray *a, long long *v)
{
	const void *d = mxGetData(a);
	double x;

	switch (mxGetClassID(a)) {
	case mxINT8_CLASS:   *v = *(const int8_t *)d; return true;
	case mxUINT8_CLASS:  *v = *(const uint8_t *)d; return true;
	case mxINT16_CLASS:  *v = *(const int16_t *)d; return true;
	case mxUINT16_CLASS: *v = *(const uint16_t *)d; return true;
	case mxINT32_CLASS:  *v = *(const int32_t *)d; return true;
	case mxUINT32_CLASS: *v = *(const uint32_t *)d; return true;
	case mxINT64_CLASS:  *v = *(const int64_t *)d; return true;
	case mxUINT64_CLASS:
		if (*(const uint64_t *)d > (uint64_t)LLONG_MAX)
			return false;
		*v = (long long)*(const uint64_t *)d;
		return true;
	case mxDOUBLE_CLASS:
	case mxSINGLE_CLASS:
		x = mxGetClassID(a) == mxDOUBLE_CLASS ? *(const double *)d : (double)*(const float *)d;
		/* -2^63 and 2^63 are exact doubles; NaN fails both comparisons. */
		if (!(x >= -9223372036854775808.0 && x < 9223372036854775808.0) || x != floor(x))
			return false;
		*v = (long long)x;
		return true;
	default:
		return false;
	}
}

/* ------------------------------------------------------------------ arguments */

bool gw_arg_given(const struct gw_call *c, int k)
{
	const mxArray *a = arg(c, k);

	return a && !is_empty_double(a);
}

int gw_arg_id(struct gw_call *c, int k, gw_id *id)
{
	const mxArray *a = arg(c, k);

	if (!a || mxGetClassID(a) != mxUINT64_CLASS || mxIsComplex(a) || mxIsSparse(a) ||
	    !mxIsScalar(a) || !mxGetData(a))
		return bad_arg(c, k, "a handle (a real uint64 scalar)");
	*id = *(const uint64_t *)mxGetData(a);
	return 0;
}

static const char *article(enum gw_type type)
{
	return strchr("aeiou", gw_type_name(type)[0]) ? "an" : "a";
}

static int wrong_type(struct gw_call *c, int k, enum gw_type want, enum gw_type got)
{
	return gw_fail(c, -EBADF, "argument %d: expected %s %s handle, got %s %s", k + 1,
		article(want), gw_type_name(want), article(got), gw_type_name(got));
}

static int handle_arg(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id, bool zero_ok)
{
	void *p = NULL;
	gw_id h;
	int ret = gw_arg_id(c, k, &h);

	if (ret)
		return ret;
	if (h != 0 || !zero_ok) {
		p = gw_handle_ptr(h, type);
		if (!p) {
			enum gw_type got = gw_handle_type(h);

			if (got != GW_T_NONE && got != type)
				return wrong_type(c, k, type, got);
			return gw_fail(c, -EBADF, "argument %d: invalid or destroyed %s handle",
				k + 1, gw_type_name(type));
		}
	}
	if (ptr)
		memcpy(ptr, &p, sizeof(p));     /* the caller's variable has its own pointer type */
	if (id)
		*id = h;
	return 0;
}

int gw_arg_handle(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id)
{
	return handle_arg(c, k, type, ptr, id, false);
}

int gw_arg_handle0(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id)
{
	return handle_arg(c, k, type, ptr, id, true);
}

int gw_arg_destroy(struct gw_call *c, int k, enum gw_type type)
{
	enum gw_type got;
	gw_id id;
	int ret = gw_arg_id(c, k, &id);

	if (ret)
		return ret;
	got = gw_handle_type(id);
	if (got == GW_T_NONE)
		return 0;
	if (got != type)
		return wrong_type(c, k, type, got);
	if (gw_handle_owned(id))
		gw_handle_destroy(id);
	return 0;
}

int gw_arg_str(struct gw_call *c, int k, char **s)
{
	static const char what[] = "text (a char row vector)";
	const mxArray *a = arg(c, k);

	*s = NULL;
	if (!a || !mxIsChar(a) || mxGetNumberOfDimensions(a) > 2 ||
	    (mxGetM(a) != 1 && mxGetNumberOfElements(a) != 0))
		return bad_arg(c, k, what);
	*s = mxArrayToUTF8String(a);
	if (!*s)
		return bad_arg(c, k, what);
	return 0;
}

int gw_arg_str0(struct gw_call *c, int k, char **s)
{
	*s = NULL;
	return gw_arg_given(c, k) ? gw_arg_str(c, k, s) : 0;
}

int gw_arg_bool(struct gw_call *c, int k, bool *v)
{
	const mxArray *a = arg(c, k);

	if (!a || !mxIsLogical(a) || mxIsSparse(a) || !mxIsScalar(a) || !mxGetData(a))
		return bad_arg(c, k, "a logical scalar");
	*v = *mxGetLogicals(a) != 0;
	return 0;
}

int gw_arg_int(struct gw_call *c, int k, long long lo, long long hi, long long *v)
{
	const mxArray *a = arg(c, k);
	char what[96];
	long long x;

	if (!is_real_numeric_scalar(a) || !exact_int(a, &x) || x < lo || x > hi) {
		snprintf(what, sizeof(what), "a real integer scalar in [%lld, %lld]", lo, hi);
		return bad_arg(c, k, what);
	}
	*v = x;
	return 0;
}

int gw_arg_index(struct gw_call *c, int k, unsigned int *v)
{
	long long x;
	int ret = gw_arg_int(c, k, 0, UINT_MAX, &x);

	if (!ret)
		*v = (unsigned int)x;
	return ret;
}

int gw_arg_double(struct gw_call *c, int k, double *v)
{
	const mxArray *a = arg(c, k);

	if (!is_real_numeric_scalar(a))
		return bad_arg(c, k, "a real numeric scalar");
	*v = mxGetScalar(a);
	return 0;
}

int gw_arg_bytes(struct gw_call *c, int k, const uint8_t **p, size_t *n)
{
	static const uint8_t nothing[1];
	const mxArray *a = arg(c, k);
	size_t count;

	if (!a || mxGetClassID(a) != mxUINT8_CLASS || mxIsComplex(a) || mxIsSparse(a) ||
	    mxGetNumberOfDimensions(a) > 2)
		return bad_arg(c, k, "bytes (a real uint8 vector)");
	count = mxGetNumberOfElements(a);
	if (count != 0 && mxGetM(a) != 1 && mxGetN(a) != 1)
		return bad_arg(c, k, "bytes (a real uint8 vector)");
	if (count != 0 && !mxGetData(a))
		return bad_arg(c, k, "bytes (a real uint8 vector)");
	*p = count ? (const uint8_t *)mxGetData(a) : nothing;
	*n = count;
	return 0;
}

int gw_arg_params(struct gw_call *c, int k, struct iio_context_params *storage,
		const struct iio_context_params **params)
{
	static const char what[] = "[] or a scalar struct of context parameters";
	const mxArray *a = arg(c, k);
	int nfields;

	*params = NULL;
	if (!gw_arg_given(c, k))
		return 0;
	if (!mxIsStruct(a) || !mxIsScalar(a))
		return bad_arg(c, k, what);

	memset(storage, 0, sizeof(*storage));
	nfields = mxGetNumberOfFields(a);
	for (int i = 0; i < nfields; i++) {
		const char *name = mxGetFieldNameByNumber(a, i);
		const mxArray *f = name ? mxGetField(a, 0, name) : NULL;
		long long v;

		if (!name)
			return bad_arg(c, k, what);
		if (strcmp(name, "log_level") && strcmp(name, "stderr_level") &&
		    strcmp(name, "timestamp_level") && strcmp(name, "timeout_ms") &&
		    strcmp(name, "flags"))
			return gw_fail(c, -EINVAL, "argument %d: unknown field \"%s\" (the fields are "
				"log_level, stderr_level, timestamp_level, timeout_ms, flags)", k + 1, name);
		if (!is_real_numeric_scalar(f) || !exact_int(f, &v) || v < INT_MIN || v > INT_MAX) {
			char got[128];

			describe(f, got, sizeof(got));
			return gw_fail(c, -EINVAL, "argument %d: field %s: expected a real integer "
				"scalar in [%d, %d], got %s", k + 1, name, INT_MIN, INT_MAX, got);
		}
		if (!strcmp(name, "log_level"))
			storage->log_level = (enum iio_log_level)v;
		else if (!strcmp(name, "stderr_level"))
			storage->stderr_level = (enum iio_log_level)v;
		else if (!strcmp(name, "timestamp_level"))
			storage->timestamp_level = (enum iio_log_level)v;
		else if (!strcmp(name, "timeout_ms"))
			storage->timeout_ms = (int)v;
		else
			storage->flags = (unsigned int)v;
	}
	*params = storage;
	return 0;
}

/* ------------------------------------------------------------------ results */

static bool wanted(const struct gw_call *c, int k)
{
	return k == 0 || (k > 0 && k < c->nlhs);
}

/* Stores a new array in plhs[k]; a result set earlier is released. */
static int put(struct gw_call *c, int k, mxArray *a)
{
	if (!a)
		return gw_fail(c, -ENOMEM, "out of memory for output %d", k + 1);
	if (c->plhs[k])
		mxDestroyArray(c->plhs[k]);
	c->plhs[k] = a;
	return 0;
}

static int put_numeric(struct gw_call *c, int k, mxClassID cls, const void *data, size_t n, size_t size)
{
	mxArray *a;
	int ret;

	if (!wanted(c, k))
		return 0;
	a = mxCreateNumericMatrix(1, n, cls, mxREAL);
	ret = put(c, k, a);
	if (!ret && n && data)
		memcpy(mxGetData(a), data, n * size);
	return ret;
}

int gw_ret_empty(struct gw_call *c, int k)
{
	return wanted(c, k) ? put(c, k, mxCreateDoubleMatrix(0, 0, mxREAL)) : 0;
}

int gw_ret_id(struct gw_call *c, int k, gw_id id)
{
	return put_numeric(c, k, mxUINT64_CLASS, &id, 1, sizeof(id));
}

void gw_set_id(struct gw_call *c, int k, gw_id id)
{
	mxArray *a = wanted(c, k) ? c->plhs[k] : NULL;

	if (a && mxGetClassID(a) == mxUINT64_CLASS && mxIsScalar(a) && mxGetData(a))
		*(uint64_t *)mxGetData(a) = id;
}

int gw_ret_ids(struct gw_call *c, int k, const gw_id *ids, size_t n)
{
	return put_numeric(c, k, mxUINT64_CLASS, ids, n, sizeof(*ids));
}

int gw_ret_borrowed(struct gw_call *c, int k, enum gw_type type, const void *ptr, gw_id owner)
{
	gw_id root, id = 0;
	int ret;

	if (!wanted(c, k))
		return 0;
	if (!ptr)
		return gw_ret_id(c, k, 0);
	root = gw_handle_root(owner);
	if (!root)
		return gw_fail(c, -EBADF, "the owner of the %s is invalid or destroyed", gw_type_name(type));
	/* Result first: the entry is made only when the array that carries its id exists. */
	ret = gw_ret_id(c, k, 0);
	if (ret)
		return ret;
	ret = gw_handle_borrow(type, ptr, root, &id);
	if (ret)
		return gw_fail(c, ret, "cannot register the %s", gw_type_name(type));
	gw_set_id(c, k, id);
	return 0;
}

int gw_ret_str(struct gw_call *c, int k, const char *s)
{
	if (!wanted(c, k))
		return 0;
	return s ? put(c, k, mxCreateString(s)) : gw_ret_empty(c, k);
}

int gw_ret_strs(struct gw_call *c, int k, char *const *list, size_t n)
{
	mxArray *cell;
	int ret;

	if (!wanted(c, k))
		return 0;
	cell = mxCreateCellMatrix(1, n);
	ret = put(c, k, cell);
	for (size_t i = 0; !ret && i < n; i++) {
		mxArray *s = mxCreateString(list[i] ? list[i] : "");

		if (!s)
			return gw_fail(c, -ENOMEM, "out of memory for output %d", k + 1);
		mxSetCell(cell, i, s);
	}
	return ret;
}

int gw_ret_bool(struct gw_call *c, int k, bool v)
{
	return wanted(c, k) ? put(c, k, mxCreateLogicalScalar(v)) : 0;
}

int gw_ret_double(struct gw_call *c, int k, double v)
{
	return wanted(c, k) ? put(c, k, mxCreateDoubleScalar(v)) : 0;
}

int gw_ret_doubles(struct gw_call *c, int k, const double *v, size_t n)
{
	return put_numeric(c, k, mxDOUBLE_CLASS, v, n, sizeof(*v));
}

int gw_ret_int64(struct gw_call *c, int k, int64_t v)
{
	return put_numeric(c, k, mxINT64_CLASS, &v, 1, sizeof(v));
}

int gw_ret_uint64(struct gw_call *c, int k, uint64_t v)
{
	return put_numeric(c, k, mxUINT64_CLASS, &v, 1, sizeof(v));
}

int gw_ret_bytes(struct gw_call *c, int k, const void *p, size_t n)
{
	return put_numeric(c, k, mxUINT8_CLASS, p, n, 1);
}
