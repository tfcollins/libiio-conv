/* SPDX-License-Identifier: MIT */
/* Mock of MATLAB's matrix.h (R2018a interleaved complex API): exactly the subset of the mx*
 * functions that the iio_mex gateway may use. The prototypes are those of the real header.
 * A gateway source that needs anything else does not compile against the mock; add the
 * function here, to mxmock.c, to tests/mex/mx_conformance.c and to tests/mex/test_mxmock.c.
 * The behaviour is checked against the real libmx by tests/mex/mx_conformance.c. */
#ifndef MXMOCK_MATRIX_H
#define MXMOCK_MATRIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MX_TARGET_API_VER 0x08000000
#define MX_HAS_INTERLEAVED_COMPLEX 1

typedef struct mxArray_tag mxArray;

typedef size_t mwSize;
typedef size_t mwIndex;
typedef ptrdiff_t mwSignedIndex;

typedef uint16_t mxChar;
typedef bool mxLogical;

typedef enum {
	mxUNKNOWN_CLASS = 0,
	mxCELL_CLASS,
	mxSTRUCT_CLASS,
	mxLOGICAL_CLASS,
	mxCHAR_CLASS,
	mxVOID_CLASS,
	mxDOUBLE_CLASS,
	mxSINGLE_CLASS,
	mxINT8_CLASS,
	mxUINT8_CLASS,
	mxINT16_CLASS,
	mxUINT16_CLASS,
	mxINT32_CLASS,
	mxUINT32_CLASS,
	mxINT64_CLASS,
	mxUINT64_CLASS,
	mxFUNCTION_CLASS,
	mxOPAQUE_CLASS,
	mxOBJECT_CLASS
} mxClassID;

typedef enum { mxREAL, mxCOMPLEX } mxComplexity;

typedef double mxDouble;
typedef float mxSingle;
typedef int8_t mxInt8;
typedef uint8_t mxUint8;
typedef int16_t mxInt16;
typedef uint16_t mxUint16;
typedef int32_t mxInt32;
typedef uint32_t mxUint32;
typedef int64_t mxInt64;
typedef uint64_t mxUint64;

typedef struct { mxDouble real, imag; } mxComplexDouble;
typedef struct { mxSingle real, imag; } mxComplexSingle;
typedef struct { mxInt8 real, imag; } mxComplexInt8;
typedef struct { mxUint8 real, imag; } mxComplexUint8;
typedef struct { mxInt16 real, imag; } mxComplexInt16;
typedef struct { mxUint16 real, imag; } mxComplexUint16;
typedef struct { mxInt32 real, imag; } mxComplexInt32;
typedef struct { mxUint32 real, imag; } mxComplexUint32;
typedef struct { mxInt64 real, imag; } mxComplexInt64;
typedef struct { mxUint64 real, imag; } mxComplexUint64;

/* Memory */
void *mxMalloc(size_t n);
void *mxCalloc(size_t n, size_t size);
void mxFree(void *ptr);
void mxDestroyArray(mxArray *pa);
mxArray *mxDuplicateArray(const mxArray *in);

/* Class */
mxClassID mxGetClassID(const mxArray *pa);
const char *mxGetClassName(const mxArray *pa);
bool mxIsClass(const mxArray *pa, const char *name);
bool mxIsChar(const mxArray *pa);
bool mxIsCell(const mxArray *pa);
bool mxIsStruct(const mxArray *pa);
bool mxIsLogical(const mxArray *pa);
bool mxIsNumeric(const mxArray *pa);
bool mxIsComplex(const mxArray *pa);
bool mxIsSparse(const mxArray *pa);
bool mxIsEmpty(const mxArray *pa);
bool mxIsScalar(const mxArray *pa);
bool mxIsLogicalScalarTrue(const mxArray *pa);

/* Shape and data */
size_t mxGetM(const mxArray *pa);
size_t mxGetN(const mxArray *pa);
size_t mxGetNumberOfElements(const mxArray *pa);
mwSize mxGetNumberOfDimensions(const mxArray *pa);
size_t mxGetElementSize(const mxArray *pa);
void *mxGetData(const mxArray *pa);
mxLogical *mxGetLogicals(const mxArray *pa);
double mxGetScalar(const mxArray *pa);

/* Numeric and logical arrays */
mxArray *mxCreateDoubleScalar(double value);
mxArray *mxCreateLogicalScalar(bool value);
mxArray *mxCreateNumericMatrix(mwSize m, mwSize n, mxClassID classid, mxComplexity flag);
mxArray *mxCreateUninitNumericMatrix(size_t m, size_t n, mxClassID classid, mxComplexity flag);
mxArray *mxCreateDoubleMatrix(mwSize m, mwSize n, mxComplexity flag);
mxArray *mxCreateLogicalMatrix(mwSize m, mwSize n);

/* Text */
mxArray *mxCreateString(const char *str);
char *mxArrayToUTF8String(mxArray const *pa);
char *mxArrayToString(const mxArray *pa);

/* Cells and structs */
mxArray *mxCreateCellMatrix(mwSize m, mwSize n);
mxArray *mxGetCell(const mxArray *pa, mwIndex i);
void mxSetCell(mxArray *pa, mwIndex i, mxArray *value);
mxArray *mxCreateStructMatrix(mwSize m, mwSize n, int nfields, const char **fieldnames);
void mxSetFieldByNumber(mxArray *pa, mwIndex i, int fieldnum, mxArray *value);
mxArray *mxGetField(const mxArray *pa, mwIndex i, const char *fieldname);
int mxGetNumberOfFields(const mxArray *pa);
const char *mxGetFieldNameByNumber(const mxArray *pa, int n);

#endif
