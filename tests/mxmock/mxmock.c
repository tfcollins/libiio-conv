/* SPDX-License-Identifier: MIT */
/* Mock MATLAB runtime: the mx* and mex* functions of tests/mxmock/matrix.h and mex.h, and the
 * test-side control functions of mxmock.h.
 *
 * It is stricter than MATLAB on purpose. Misuse that MATLAB punishes with a crash, with
 * memory corruption or not at all is an abort() with a one-line message here: a NULL,
 * destroyed or foreign array, an output that is an input, an array in two containers, a
 * double free. Every data area is its own heap block of exactly the array's size, so that
 * AddressSanitizer sees a read or write past the end.
 *
 * Where the mock had to choose (tests/mex/mx_conformance.c holds what was checked against the
 * real libmx of R2025b):
 * - mxArrayToString converts to UTF-8 like mxArrayToUTF8String; the real one converts to the
 *   locale's character set.
 * - mxGetLogicals of an array that is not logical aborts; the real one returns the data.
 * - mxGetScalar of an empty array, a cell, a struct or an object aborts; the real one returns
 *   an undefined value.
 * - mxGetField and mxGetCell with an element index beyond the end abort.
 * - mock_string_object() has class id mxOPAQUE_CLASS; only its class name is certain.
 * - mock_sparse_double() has room for one non-zero element (nzmax 1), as an all-zero sparse
 *   matrix has in MATLAB: mxGetData is 8 bytes whatever the size.
 * - mock_nd_array() with d2 == 1 gives a matrix (2 dimensions): MATLAB drops trailing
 *   singleton dimensions.
 * - mock_fail_alloc() counts only allocations made while mock_call() runs, so a test can
 *   build its inputs after arming it.
 * - mexUnlock without a lock is ignored, as in MATLAB.
 * - mock_call() fills the stack below it with 0xA5 before it calls mexFunction, so that an
 *   uninitialised local of the gateway reads the same, and not as zero, in every call (on
 *   the real stack only; see scribble_stack()).
 * Not thread safe: only the thread that runs mock_call() may use the runtime. */
#include "mex.h"
#include "mxmock.h"
#undef printf

#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC_LIVE   0x6d784c56u
#define MAGIC_DEAD   0x6d784445u
#define MAGIC_POISON 0x6d78504fu

struct mxArray_tag {
	uint32_t magic;
	mxClassID cls;
	const char *class_name;
	bool is_complex;
	bool is_sparse;
	bool returned;          /* in plhs of the call that is ending */
	unsigned int input;     /* times it is in prhs of the running call */
	size_t ndim;
	size_t dims[3];
	size_t elsize;          /* bytes per element; both parts of a complex one */
	size_t bytes;           /* size of data */
	void *data;             /* own heap block of exactly bytes, NULL when bytes == 0;
	                           cell: mxArray *[numel]; struct: mxArray *[numel * nfields] */
	int nfields;
	char **fields;
	mxArray *owner;         /* the cell or struct this array is a member of */
	unsigned long serial;   /* the mock_call that created it; 0 = created outside a call */
	mxArray *prev, *next;   /* list of live arrays, newest first; or list of dead headers */
};

struct block {
	void *ptr;
	unsigned long serial;
};

enum { EXIT_ERROR = 1, EXIT_NOMEM = 2 };

static struct mxArray_tag poison = { .magic = MAGIC_POISON };

static mxArray *live_list;      /* every live array, members included */
static mxArray *dead_list;      /* headers of destroyed arrays: kept, so that later use is
                                   reported and stays valid memory */
static size_t live_top;         /* live arrays that are not members */
static struct block *blocks;    /* live mxMalloc blocks */
static size_t n_blocks, cap_blocks;
static size_t autofreed;

static bool in_call;
static unsigned long call_serial;
static jmp_buf call_env;
static int exit_kind;
static unsigned int fail_countdown;
static bool alloc_failed;

static char err_id[256];
static char err_msg[4096];
static char warn_id[256];
static unsigned int warn_count;
static char *out_buf;
static size_t out_len;

static unsigned int lock_count;
static mex_exit_fn exit_fn;

/* ------------------------------------------------------------------ helpers */

static void die(const char *fmt, ...) __attribute__((noreturn, format(__printf__, 1, 2)));

static void die(const char *fmt, ...)
{
	va_list ap;

	fputs("mxmock: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
	abort();
}

static void *host_alloc(size_t n, bool zero)
{
	void *p = zero ? calloc(n ? n : 1, 1) : malloc(n);

	if (!p && !n)
		p = malloc(1);
	if (!p)
		die("the test process itself is out of memory (%zu bytes)", n);
	return p;
}

/* One mx allocation is about to happen: this is where an injected failure ends the call. */
static void alloc_tick(void)
{
	if (!in_call || !fail_countdown || --fail_countdown)
		return;
	exit_kind = EXIT_NOMEM;
	longjmp(call_env, 1);
}

static mxArray *chk(const mxArray *pa, const char *fn)
{
	if (!pa)
		die("%s: NULL array", fn);
	if (pa->magic == MAGIC_POISON)
		die("%s: a plhs entry that was never set", fn);
	if (pa->magic == MAGIC_DEAD)
		die("%s: array was destroyed", fn);
	if (pa->magic != MAGIC_LIVE)
		die("%s: not an mxArray", fn);
	return (mxArray *)pa;
}

static bool is_input(const mxArray *a)
{
	for (; a; a = a->owner)
		if (a->input)
			return true;
	return false;
}

static bool is_container(const mxArray *a)
{
	return a->cls == mxCELL_CLASS || a->cls == mxSTRUCT_CLASS;
}

static size_t numel(const mxArray *a)
{
	return a->dims[0] * a->dims[1] * a->dims[2];
}

static size_t n_members(const mxArray *a)
{
	return a->bytes / sizeof(mxArray *);
}

static const char *class_name(mxClassID cls)
{
	switch (cls) {
	case mxCELL_CLASS:    return "cell";
	case mxSTRUCT_CLASS:  return "struct";
	case mxLOGICAL_CLASS: return "logical";
	case mxCHAR_CLASS:    return "char";
	case mxDOUBLE_CLASS:  return "double";
	case mxSINGLE_CLASS:  return "single";
	case mxINT8_CLASS:    return "int8";
	case mxUINT8_CLASS:   return "uint8";
	case mxINT16_CLASS:   return "int16";
	case mxUINT16_CLASS:  return "uint16";
	case mxINT32_CLASS:   return "int32";
	case mxUINT32_CLASS:  return "uint32";
	case mxINT64_CLASS:   return "int64";
	case mxUINT64_CLASS:  return "uint64";
	default:              return "unknown";
	}
}

/* Bytes of one real element; 0 for a class that is not numeric. */
static size_t numeric_size(mxClassID cls)
{
	switch (cls) {
	case mxDOUBLE_CLASS: case mxINT64_CLASS: case mxUINT64_CLASS: return 8;
	case mxSINGLE_CLASS: case mxINT32_CLASS: case mxUINT32_CLASS: return 4;
	case mxINT16_CLASS:  case mxUINT16_CLASS:                     return 2;
	case mxINT8_CLASS:   case mxUINT8_CLASS:                      return 1;
	default:                                                      return 0;
	}
}

/* A live, top-level array of count elements of elsize bytes; data filled with `fill`. */
static mxArray *new_array(const char *fn, mxClassID cls, size_t d0, size_t d1, size_t d2,
			  size_t elsize, size_t count, int fill)
{
	mxArray *a = host_alloc(sizeof(*a), true);
	size_t bytes;

	if (__builtin_mul_overflow(count, elsize, &bytes))
		die("%s: array size overflows", fn);
	a->magic = MAGIC_LIVE;
	a->cls = cls;
	a->class_name = class_name(cls);
	a->ndim = d2 == 1 ? 2 : 3;
	a->dims[0] = d0;
	a->dims[1] = d1;
	a->dims[2] = d2;
	a->elsize = elsize;
	a->bytes = bytes;
	if (bytes) {
		a->data = host_alloc(bytes, false);
		memset(a->data, fill, bytes);
	}
	a->serial = in_call ? call_serial : 0;
	a->next = live_list;
	if (live_list)
		live_list->prev = a;
	live_list = a;
	live_top++;
	return a;
}

static size_t mul_dims(const char *fn, size_t a, size_t b)
{
	size_t n;

	if (__builtin_mul_overflow(a, b, &n))
		die("%s: dimensions overflow", fn);
	return n;
}

static mxArray *new_matrix(const char *fn, mxClassID cls, size_t m, size_t n, size_t elsize, int fill)
{
	return new_array(fn, cls, m, n, 1, elsize, mul_dims(fn, m, n), fill);
}

static mxArray *new_numeric(const char *fn, size_t m, size_t n, mxClassID cls, mxComplexity flag, int fill)
{
	size_t size = numeric_size(cls);
	mxArray *a;

	if (!size)
		die("%s: class id %d is not numeric", fn, (int)cls);
	if (flag != mxREAL && flag != mxCOMPLEX)
		die("%s: bad complexity flag %d", fn, (int)flag);
	a = new_matrix(fn, cls, m, n, flag == mxCOMPLEX ? 2 * size : size, fill);
	a->is_complex = flag == mxCOMPLEX;
	return a;
}

/* Destroys a and, first, its members. The header stays, on the dead list. */
static void destroy(mxArray *a)
{
	if (is_container(a)) {
		mxArray **m = a->data;

		for (size_t i = 0; i < n_members(a); i++)
			if (m[i])
				destroy(m[i]);
	}
	for (int i = 0; i < a->nfields; i++)
		free(a->fields[i]);
	free(a->fields);
	free(a->data);
	a->fields = NULL;
	a->data = NULL;
	if (!a->owner)
		live_top--;

	if (a->prev)
		a->prev->next = a->next;
	else
		live_list = a->next;
	if (a->next)
		a->next->prev = a->prev;
	a->magic = MAGIC_DEAD;
	a->prev = NULL;
	a->next = dead_list;
	dead_list = a;
}

/* Makes v (NULL allowed) member number slot of container c. */
static void set_member(const char *fn, mxArray *c, size_t slot, mxArray *v)
{
	mxArray **m = c->data;

	if (is_input(c))
		die("%s: the container is an input (prhs) of the running call", fn);
	if (v) {
		chk(v, fn);
		if (v->owner)
			die("%s: the value is already a member of a cell or struct", fn);
		if (is_input(v))
			die("%s: the value is an input (prhs) of the running call", fn);
		for (const mxArray *x = c; x; x = x->owner)
			if (x == v)
				die("%s: an array cannot contain itself", fn);
	}
	if (m[slot]) {          /* MATLAB does not free the old value: it is on its own again */
		m[slot]->owner = NULL;
		live_top++;
	}
	m[slot] = v;
	if (v) {
		v->owner = c;
		live_top--;
	}
}

static void *block_new(size_t n, bool zero)
{
	void *p = host_alloc(n, zero);

	if (n_blocks == cap_blocks) {
		cap_blocks = cap_blocks ? 2 * cap_blocks : 64;
		blocks = realloc(blocks, cap_blocks * sizeof(*blocks));
		if (!blocks)
			die("the test process itself is out of memory");
	}
	blocks[n_blocks].ptr = p;
	blocks[n_blocks].serial = in_call ? call_serial : 0;
	n_blocks++;
	return p;
}

/* Releases what the ending call created and did not return. */
static void sweep(bool count)
{
	for (;;) {
		mxArray *a = live_list;

		/* The arrays of the running call are the newest ones. */
		while (a && a->serial == call_serial && (a->owner || a->returned))
			a = a->next;
		if (!a || a->serial != call_serial)
			break;
		destroy(a);
		if (count)
			autofreed++;
	}
	for (size_t i = n_blocks; i-- > 0;) {
		if (blocks[i].serial != call_serial)
			continue;
		free(blocks[i].ptr);
		blocks[i] = blocks[--n_blocks];
		if (count)
			autofreed++;
	}
}

/* MATLAB's rule for message identifiers: at least two components separated by colons, each a
 * letter followed by letters, digits or underscores. */
static bool valid_identifier(const char *id)
{
	int components = 0;

	if (!id)
		return false;
	for (;;) {
		if (!isalpha((unsigned char)*id))
			return false;
		while (isalnum((unsigned char)*id) || *id == '_')
			id++;
		components++;
		if (!*id)
			return components >= 2;
		if (*id++ != ':')
			return false;
	}
}

/* Decodes one code point of well-formed UTF-8 (no overlong forms, no surrogates, at most
 * U+10FFFF). Anything else is U+FFFD for one byte, which is what mxCreateString does. */
static size_t utf8_next(const unsigned char *s, size_t len, uint32_t *cp)
{
	unsigned char lo = 0x80, hi = 0xBF;
	size_t need;

	*cp = 0xFFFD;
	if (s[0] < 0x80) {
		*cp = s[0];
		return 1;
	}
	if (s[0] >= 0xC2 && s[0] <= 0xDF) {
		need = 1;
		*cp = s[0] & 0x1F;
	} else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
		need = 2;
		*cp = s[0] & 0x0F;
		if (s[0] == 0xE0)
			lo = 0xA0;
		if (s[0] == 0xED)
			hi = 0x9F;
	} else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
		need = 3;
		*cp = s[0] & 0x07;
		if (s[0] == 0xF0)
			lo = 0x90;
		if (s[0] == 0xF4)
			hi = 0x8F;
	} else {
		return 1;
	}
	if (len < need + 1 || s[1] < lo || s[1] > hi) {
		*cp = 0xFFFD;
		return 1;
	}
	for (size_t i = 1; i <= need; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		*cp = (*cp << 6) | (s[i] & 0x3F);
	}
	return need + 1;
}

/* UTF-16 to UTF-8, up to the first NUL (as the real functions do). With out == NULL only the
 * length is returned. An unpaired surrogate becomes U+FFFD. */
static size_t utf16_to_utf8(const mxChar *s, size_t n, char *out)
{
	size_t len = 0;

	for (size_t i = 0; i < n && s[i]; i++) {
		uint32_t cp = s[i];
		unsigned char b[4];
		size_t nb;

		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			i++;
		} else if (cp >= 0xD800 && cp <= 0xDFFF) {
			cp = 0xFFFD;
		}
		if (cp < 0x80) {
			b[0] = (unsigned char)cp;
			nb = 1;
		} else if (cp < 0x800) {
			b[0] = (unsigned char)(0xC0 | (cp >> 6));
			b[1] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 2;
		} else if (cp < 0x10000) {
			b[0] = (unsigned char)(0xE0 | (cp >> 12));
			b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
			b[2] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 3;
		} else {
			b[0] = (unsigned char)(0xF0 | (cp >> 18));
			b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
			b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
			b[3] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 4;
		}
		if (out)
			memcpy(out + len, b, nb);
		len += nb;
	}
	return len;
}

static char *array_to_utf8(const mxArray *pa, const char *fn)
{
	const mxArray *a = chk(pa, fn);
	size_t len;
	char *s;

	if (a->cls != mxCHAR_CLASS)
		return NULL;
	alloc_tick();
	len = utf16_to_utf8(a->data, numel(a), NULL);
	s = block_new(len + 1, false);
	utf16_to_utf8(a->data, numel(a), s);
	s[len] = '\0';
	return s;
}

/* ------------------------------------------------------------------ memory */

/* As in MATLAB, a request for no bytes is answered with NULL and is not an allocation. */
void *mxMalloc(size_t n)
{
	if (!n)
		return NULL;
	alloc_tick();
	return block_new(n, false);
}

void *mxCalloc(size_t n, size_t size)
{
	size_t bytes = mul_dims("mxCalloc", n, size);

	if (!bytes)
		return NULL;
	alloc_tick();
	return block_new(bytes, true);
}

void mxFree(void *ptr)
{
	if (!ptr)
		return;
	for (size_t i = n_blocks; i-- > 0;) {
		if (blocks[i].ptr != ptr)
			continue;
		blocks[i] = blocks[--n_blocks];
		free(ptr);
		return;
	}
	die("mxFree: %p is not a live mxMalloc block (freed twice, from malloc, or array data)", ptr);
}

void mxDestroyArray(mxArray *pa)
{
	if (!pa)
		return;
	chk(pa, "mxDestroyArray");
	if (is_input(pa))
		die("mxDestroyArray: the array is an input (prhs) of the running call");
	if (pa->owner)
		die("mxDestroyArray: the array is a member of a cell or struct; destroy the container");
	destroy(pa);
}

static mxArray *duplicate(const mxArray *a)
{
	mxArray *d = new_array("mxDuplicateArray", a->cls, a->dims[0], a->dims[1], a->dims[2], 1, a->bytes, 0);

	d->class_name = a->class_name;
	d->ndim = a->ndim;
	d->elsize = a->elsize;
	d->is_complex = a->is_complex;
	d->is_sparse = a->is_sparse;
	if (a->nfields) {
		d->fields = host_alloc((size_t)a->nfields * sizeof(char *), true);
		for (int i = 0; i < a->nfields; i++) {
			d->fields[i] = strdup(a->fields[i]);
			if (!d->fields[i])
				die("the test process itself is out of memory");
		}
		d->nfields = a->nfields;
	}
	if (is_container(a)) {
		mxArray *const *src = a->data;
		mxArray **dst = d->data;

		for (size_t i = 0; i < n_members(a); i++) {
			if (!src[i])
				continue;
			dst[i] = duplicate(src[i]);
			dst[i]->owner = d;
			live_top--;
		}
	} else if (a->bytes) {
		memcpy(d->data, a->data, a->bytes);
	}
	return d;
}

mxArray *mxDuplicateArray(const mxArray *in)
{
	chk(in, "mxDuplicateArray");
	alloc_tick();
	return duplicate(in);
}

/* ------------------------------------------------------------------ class */

mxClassID mxGetClassID(const mxArray *pa)
{
	return chk(pa, "mxGetClassID")->cls;
}

const char *mxGetClassName(const mxArray *pa)
{
	return chk(pa, "mxGetClassName")->class_name;
}

bool mxIsClass(const mxArray *pa, const char *name)
{
	const mxArray *a = chk(pa, "mxIsClass");

	if (!name)
		die("mxIsClass: NULL class name");
	return strcmp(a->class_name, name) == 0;
}

bool mxIsChar(const mxArray *pa)
{
	return chk(pa, "mxIsChar")->cls == mxCHAR_CLASS;
}

bool mxIsCell(const mxArray *pa)
{
	return chk(pa, "mxIsCell")->cls == mxCELL_CLASS;
}

bool mxIsStruct(const mxArray *pa)
{
	return chk(pa, "mxIsStruct")->cls == mxSTRUCT_CLASS;
}

bool mxIsLogical(const mxArray *pa)
{
	return chk(pa, "mxIsLogical")->cls == mxLOGICAL_CLASS;
}

bool mxIsNumeric(const mxArray *pa)
{
	return numeric_size(chk(pa, "mxIsNumeric")->cls) != 0;
}

bool mxIsComplex(const mxArray *pa)
{
	return chk(pa, "mxIsComplex")->is_complex;
}

bool mxIsSparse(const mxArray *pa)
{
	return chk(pa, "mxIsSparse")->is_sparse;
}

bool mxIsEmpty(const mxArray *pa)
{
	return numel(chk(pa, "mxIsEmpty")) == 0;
}

bool mxIsScalar(const mxArray *pa)
{
	const mxArray *a = chk(pa, "mxIsScalar");

	return a->dims[0] == 1 && a->dims[1] == 1 && a->dims[2] == 1;
}

bool mxIsLogicalScalarTrue(const mxArray *pa)
{
	const mxArray *a = chk(pa, "mxIsLogicalScalarTrue");

	return a->cls == mxLOGICAL_CLASS && numel(a) == 1 && *(const mxLogical *)a->data;
}

/* ------------------------------------------------------------------ shape and data */

size_t mxGetM(const mxArray *pa)
{
	return chk(pa, "mxGetM")->dims[0];
}

size_t mxGetN(const mxArray *pa)
{
	const mxArray *a = chk(pa, "mxGetN");

	return a->dims[1] * a->dims[2];
}

size_t mxGetNumberOfElements(const mxArray *pa)
{
	return numel(chk(pa, "mxGetNumberOfElements"));
}

mwSize mxGetNumberOfDimensions(const mxArray *pa)
{
	return chk(pa, "mxGetNumberOfDimensions")->ndim;
}

size_t mxGetElementSize(const mxArray *pa)
{
	return chk(pa, "mxGetElementSize")->elsize;
}

void *mxGetData(const mxArray *pa)
{
	if (!pa)
		return NULL;
	return chk(pa, "mxGetData")->data;
}

mxLogical *mxGetLogicals(const mxArray *pa)
{
	const mxArray *a = chk(pa, "mxGetLogicals");

	if (a->cls != mxLOGICAL_CLASS)
		die("mxGetLogicals: the array is %s, not logical", a->class_name);
	return a->data;
}

double mxGetScalar(const mxArray *pa)
{
	const mxArray *a = chk(pa, "mxGetScalar");
	const void *d = a->data;

	if (!d)
		die("mxGetScalar: the array has no element (class %s)", a->class_name);
	switch (a->cls) {
	case mxLOGICAL_CLASS: return *(const mxLogical *)d;
	case mxCHAR_CLASS:    return *(const mxChar *)d;
	case mxDOUBLE_CLASS:  return *(const double *)d;
	case mxSINGLE_CLASS:  return *(const float *)d;
	case mxINT8_CLASS:    return *(const int8_t *)d;
	case mxUINT8_CLASS:   return *(const uint8_t *)d;
	case mxINT16_CLASS:   return *(const int16_t *)d;
	case mxUINT16_CLASS:  return *(const uint16_t *)d;
	case mxINT32_CLASS:   return *(const int32_t *)d;
	case mxUINT32_CLASS:  return *(const uint32_t *)d;
	case mxINT64_CLASS:   return (double)*(const int64_t *)d;
	case mxUINT64_CLASS:  return (double)*(const uint64_t *)d;
	default:
		die("mxGetScalar: the array is a %s", a->class_name);
	}
}

/* ------------------------------------------------------------------ numeric and logical arrays */

mxArray *mxCreateDoubleScalar(double value)
{
	mxArray *a;

	alloc_tick();
	a = new_numeric("mxCreateDoubleScalar", 1, 1, mxDOUBLE_CLASS, mxREAL, 0);
	*(double *)a->data = value;
	return a;
}

mxArray *mxCreateLogicalScalar(bool value)
{
	mxArray *a;

	alloc_tick();
	a = new_matrix("mxCreateLogicalScalar", mxLOGICAL_CLASS, 1, 1, sizeof(mxLogical), 0);
	*(mxLogical *)a->data = value;
	return a;
}

mxArray *mxCreateNumericMatrix(mwSize m, mwSize n, mxClassID classid, mxComplexity flag)
{
	alloc_tick();
	return new_numeric("mxCreateNumericMatrix", m, n, classid, flag, 0);
}

mxArray *mxCreateUninitNumericMatrix(size_t m, size_t n, mxClassID classid, mxComplexity flag)
{
	alloc_tick();
	return new_numeric("mxCreateUninitNumericMatrix", m, n, classid, flag, 0xA5);
}

mxArray *mxCreateDoubleMatrix(mwSize m, mwSize n, mxComplexity flag)
{
	alloc_tick();
	return new_numeric("mxCreateDoubleMatrix", m, n, mxDOUBLE_CLASS, flag, 0);
}

mxArray *mxCreateLogicalMatrix(mwSize m, mwSize n)
{
	alloc_tick();
	return new_matrix("mxCreateLogicalMatrix", mxLOGICAL_CLASS, m, n, sizeof(mxLogical), 0);
}

/* ------------------------------------------------------------------ text */

mxArray *mxCreateString(const char *str)
{
	const unsigned char *s = (const unsigned char *)str;
	size_t len, units = 0;
	uint32_t cp;
	mxArray *a;
	mxChar *d;

	if (!str)
		die("mxCreateString: NULL string");
	alloc_tick();
	len = strlen(str);
	for (size_t i = 0; i < len; i += utf8_next(s + i, len - i, &cp))
		units += s[i] >= 0xF0 && utf8_next(s + i, len - i, &cp) == 4 ? 2 : 1;
	a = new_matrix("mxCreateString", mxCHAR_CLASS, units ? 1 : 0, units, sizeof(mxChar), 0);
	d = a->data;
	for (size_t i = 0; i < len; i += utf8_next(s + i, len - i, &cp)) {
		utf8_next(s + i, len - i, &cp);
		if (cp >= 0x10000) {
			*d++ = (mxChar)(0xD800 + ((cp - 0x10000) >> 10));
			*d++ = (mxChar)(0xDC00 + ((cp - 0x10000) & 0x3FF));
		} else {
			*d++ = (mxChar)cp;
		}
	}
	return a;
}

char *mxArrayToUTF8String(mxArray const *pa)
{
	return array_to_utf8(pa, "mxArrayToUTF8String");
}

char *mxArrayToString(const mxArray *pa)
{
	return array_to_utf8(pa, "mxArrayToString");
}

/* ------------------------------------------------------------------ cells and structs */

mxArray *mxCreateCellMatrix(mwSize m, mwSize n)
{
	alloc_tick();
	return new_matrix("mxCreateCellMatrix", mxCELL_CLASS, m, n, sizeof(mxArray *), 0);
}

mxArray *mxGetCell(const mxArray *pa, mwIndex i)
{
	const mxArray *a = chk(pa, "mxGetCell");

	if (a->cls != mxCELL_CLASS)
		die("mxGetCell: the array is a %s, not a cell", a->class_name);
	if (i >= numel(a))
		die("mxGetCell: index %zu beyond the end (%zu elements)", (size_t)i, numel(a));
	return ((mxArray *const *)a->data)[i];
}

void mxSetCell(mxArray *pa, mwIndex i, mxArray *value)
{
	mxArray *a = chk(pa, "mxSetCell");

	if (a->cls != mxCELL_CLASS)
		die("mxSetCell: the array is a %s, not a cell", a->class_name);
	if (i >= numel(a))
		die("mxSetCell: index %zu beyond the end (%zu elements)", (size_t)i, numel(a));
	set_member("mxSetCell", a, i, value);
}

mxArray *mxCreateStructMatrix(mwSize m, mwSize n, int nfields, const char **fieldnames)
{
	size_t count = mul_dims("mxCreateStructMatrix", m, n);
	mxArray *a;

	if (nfields < 0 || (nfields > 0 && !fieldnames))
		die("mxCreateStructMatrix: bad field list");
	for (int i = 0; i < nfields; i++) {
		const char *f = fieldnames[i];
		size_t len = f ? strlen(f) : 0;

		if (!len || len > 63 || !isalpha((unsigned char)f[0]))
			die("mxCreateStructMatrix: \"%s\" is not a valid field name", f ? f : "(null)");
		for (size_t k = 1; k < len; k++)
			if (!isalnum((unsigned char)f[k]) && f[k] != '_')
				die("mxCreateStructMatrix: \"%s\" is not a valid field name", f);
		for (int k = 0; k < i; k++)
			if (strcmp(fieldnames[k], f) == 0)
				die("mxCreateStructMatrix: field name \"%s\" twice", f);
	}
	alloc_tick();
	a = new_array("mxCreateStructMatrix", mxSTRUCT_CLASS, m, n, 1, sizeof(mxArray *),
		      mul_dims("mxCreateStructMatrix", count, (size_t)nfields), 0);
	if (nfields) {
		a->fields = host_alloc((size_t)nfields * sizeof(char *), true);
		for (int i = 0; i < nfields; i++) {
			a->fields[i] = strdup(fieldnames[i]);
			if (!a->fields[i])
				die("the test process itself is out of memory");
		}
		a->nfields = nfields;
	}
	return a;
}

void mxSetFieldByNumber(mxArray *pa, mwIndex i, int fieldnum, mxArray *value)
{
	mxArray *a = chk(pa, "mxSetFieldByNumber");

	if (a->cls != mxSTRUCT_CLASS)
		die("mxSetFieldByNumber: the array is a %s, not a struct", a->class_name);
	if (i >= numel(a))
		die("mxSetFieldByNumber: index %zu beyond the end (%zu elements)", (size_t)i, numel(a));
	if (fieldnum < 0 || fieldnum >= a->nfields)
		die("mxSetFieldByNumber: field number %d of %d", fieldnum, a->nfields);
	set_member("mxSetFieldByNumber", a, i * (size_t)a->nfields + (size_t)fieldnum, value);
}

mxArray *mxGetField(const mxArray *pa, mwIndex i, const char *fieldname)
{
	const mxArray *a = chk(pa, "mxGetField");

	if (!fieldname)
		die("mxGetField: NULL field name");
	if (a->cls != mxSTRUCT_CLASS)
		return NULL;
	if (i >= numel(a))
		die("mxGetField: index %zu beyond the end (%zu elements)", (size_t)i, numel(a));
	for (int f = 0; f < a->nfields; f++)
		if (strcmp(a->fields[f], fieldname) == 0)
			return ((mxArray *const *)a->data)[i * (size_t)a->nfields + (size_t)f];
	return NULL;
}

int mxGetNumberOfFields(const mxArray *pa)
{
	return chk(pa, "mxGetNumberOfFields")->nfields;
}

const char *mxGetFieldNameByNumber(const mxArray *pa, int n)
{
	const mxArray *a = chk(pa, "mxGetFieldNameByNumber");

	return n >= 0 && n < a->nfields ? a->fields[n] : NULL;
}

/* ------------------------------------------------------------------ mex.h */

__attribute__((weak)) void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	(void)nlhs;
	(void)plhs;
	(void)nrhs;
	(void)prhs;
	die("mock_call: this program has no mexFunction");
}

void mexErrMsgIdAndTxt(const char *identifier, const char *err_msg_fmt, ...)
{
	va_list ap;

	if (!in_call)
		die("mexErrMsgIdAndTxt: called outside mock_call (id %s)", identifier ? identifier : "(null)");
	if (!valid_identifier(identifier))
		die("mexErrMsgIdAndTxt: \"%s\" is not a valid message identifier", identifier ? identifier : "(null)");
	if (!err_msg_fmt)
		die("mexErrMsgIdAndTxt: NULL message");
	snprintf(err_id, sizeof(err_id), "%s", identifier);
	va_start(ap, err_msg_fmt);
	vsnprintf(err_msg, sizeof(err_msg), err_msg_fmt, ap);
	va_end(ap);
	exit_kind = EXIT_ERROR;
	longjmp(call_env, 1);
}

void mexWarnMsgIdAndTxt(const char *identifier, const char *warn_msg, ...)
{
	if (!valid_identifier(identifier))
		die("mexWarnMsgIdAndTxt: \"%s\" is not a valid message identifier", identifier ? identifier : "(null)");
	if (!warn_msg)
		die("mexWarnMsgIdAndTxt: NULL message");
	snprintf(warn_id, sizeof(warn_id), "%s", identifier);
	warn_count++;
}

int mexPrintf(const char *fmt, ...)
{
	va_list ap;
	int n;

	if (!fmt)
		die("mexPrintf: NULL format");
	va_start(ap, fmt);
	n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		die("mexPrintf: bad format");
	out_buf = realloc(out_buf, out_len + (size_t)n + 1);
	if (!out_buf)
		die("the test process itself is out of memory");
	va_start(ap, fmt);
	vsnprintf(out_buf + out_len, (size_t)n + 1, fmt, ap);
	va_end(ap);
	out_len += (size_t)n;
	return n;
}

void mexLock(void)
{
	lock_count++;
}

void mexUnlock(void)
{
	if (lock_count)
		lock_count--;
}

bool mexIsLocked(void)
{
	return lock_count != 0;
}

int mexAtExit(mex_exit_fn exit_fcn)
{
	exit_fn = exit_fcn;
	return 0;
}

/* ------------------------------------------------------------------ mxmock.h */

/* MATLAB promises nothing about the stack either. Whatever mexFunction and the functions it
 * calls leave uninitialised reads as 0xA5 in every call, not as what the call before left
 * there. It relies on the next frames reusing this one's memory: true on the real stack, not
 * on AddressSanitizer's fake stack, so a test that wants it under ASan runs with
 * ASAN_OPTIONS=detect_stack_use_after_return=0 (tests/mex/CMakeLists.txt: test_dispatch). */
static __attribute__((noinline)) void scribble_stack(void)
{
	volatile unsigned char junk[32768];

	for (size_t i = 0; i < sizeof(junk); i++)
		junk[i] = 0xA5;
}

int mock_call(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	int nout = nlhs > 1 ? nlhs : 1;

	if (in_call)
		die("mock_call: called while a call is running");
	if (nlhs < 0 || nrhs < 0 || !plhs || (nrhs > 0 && !prhs))
		die("mock_call: bad arguments");
	for (int k = 0; k < nrhs; k++) {
		mxArray *a = chk(prhs[k], "mock_call: prhs");

		if (a->owner)
			die("mock_call: prhs[%d] is a member of a cell or struct", k);
		a->input++;
	}
	for (int k = 0; k < nout; k++)
		plhs[k] = &poison;

	call_serial++;
	err_id[0] = '\0';
	err_msg[0] = '\0';
	alloc_failed = false;
	exit_kind = 0;
	scribble_stack();
	in_call = true;
	if (setjmp(call_env) == 0)
		mexFunction(nlhs, plhs, nrhs, prhs);
	in_call = false;

	if (exit_kind == 0) {
		for (int k = 0; k < nout; k++) {
			mxArray *a = plhs[k];

			if (!a || a == &poison) {
				if (k < nlhs)
					die("mock_call: output %d of %d was not set", k, nlhs);
				plhs[k] = NULL;
				continue;
			}
			chk(a, "mock_call: plhs");
			if (is_input(a))
				die("mock_call: plhs[%d] is an input (prhs)", k);
			if (a->owner)
				die("mock_call: plhs[%d] is a member of a cell or struct", k);
			if (a->returned)
				die("mock_call: plhs[%d] is also an earlier output", k);
			if (a->serial != call_serial)
				die("mock_call: plhs[%d] was not created by this call", k);
			a->returned = true;
		}
		sweep(true);
		for (int k = 0; k < nout; k++)
			if (plhs[k])
				plhs[k]->returned = false;
	} else {
		for (int k = 0; k < nout; k++)
			plhs[k] = NULL;
		if (exit_kind == EXIT_NOMEM) {
			alloc_failed = true;
			snprintf(err_id, sizeof(err_id), "MATLAB:nomem");
			snprintf(err_msg, sizeof(err_msg), "Out of memory.");
		}
		sweep(exit_kind == EXIT_ERROR);
	}
	for (int k = 0; k < nrhs; k++)
		((mxArray *)prhs[k])->input--;
	return exit_kind ? 1 : 0;
}

const char *mock_err_id(void)
{
	return err_id;
}

const char *mock_err_msg(void)
{
	return err_msg;
}

unsigned int mock_warn_count(void)
{
	return warn_count;
}

const char *mock_warn_id(void)
{
	return warn_id;
}

const char *mock_output(void)
{
	return out_buf ? out_buf : "";
}

bool mock_is_locked(void)
{
	return lock_count != 0;
}

static void run_exit_fn(const char *fn)
{
	mex_exit_fn f = exit_fn;

	if (in_call)
		die("%s: called while a call is running", fn);
	exit_fn = NULL;
	if (f)
		f();
}

void mock_clear_mex(void)
{
	if (!lock_count)
		run_exit_fn("mock_clear_mex");
}

void mock_exit_matlab(void)
{
	run_exit_fn("mock_exit_matlab");
	lock_count = 0;
}

size_t mock_autofreed(void)
{
	return autofreed;
}

size_t mock_live(void)
{
	return live_top + n_blocks;
}

void mock_fail_alloc(unsigned int nth)
{
	fail_countdown = nth;
}

bool mock_alloc_failed(void)
{
	return alloc_failed;
}

mxArray *mock_string_object(void)
{
	mxArray *a = new_matrix("mock_string_object", mxOPAQUE_CLASS, 1, 1, 0, 0);

	a->class_name = "string";
	return a;
}

mxArray *mock_sparse_double(size_t m, size_t n)
{
	mxArray *a = new_array("mock_sparse_double", mxDOUBLE_CLASS, m, n, 1, sizeof(double), 1, 0);

	mul_dims("mock_sparse_double", m, n);
	a->is_sparse = true;
	return a;
}

mxArray *mock_nd_array(mxClassID cls, mxComplexity c, size_t d0, size_t d1, size_t d2)
{
	size_t size = numeric_size(cls) * (c == mxCOMPLEX ? 2 : 1);
	const char *fn = "mock_nd_array";
	mxArray *a;

	if (!size)
		die("%s: class id %d is not numeric", fn, (int)cls);
	a = new_array(fn, cls, d0, d1, d2, size, mul_dims(fn, mul_dims(fn, d0, d1), d2), 0);
	a->is_complex = c == mxCOMPLEX;
	return a;
}

mxArray *mock_char_array(size_t d0, size_t d1, size_t d2, const char *text)
{
	const char *fn = "mock_char_array";
	size_t count = mul_dims(fn, mul_dims(fn, d0, d1), d2);
	mxArray *a;
	mxChar *d;

	if (!text || strlen(text) != count)
		die("%s: the text must have exactly %zu characters", fn, count);
	a = new_array(fn, mxCHAR_CLASS, d0, d1, d2, sizeof(mxChar), count, 0);
	d = a->data;
	for (size_t i = 0; i < count; i++)
		d[i] = (unsigned char)text[i];
	return a;
}
