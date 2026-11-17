/* SPDX-License-Identifier: MIT */
/* The facts about the MATLAB array API that the gateway relies on. Built twice from this one
 * source: against tests/mxmock (mx_conformance_mock) and against MATLAB's own matrix.h and
 * libmx (mx_conformance_real), so the mock cannot drift from the real runtime. Uses no mex*
 * function and nothing specific to the mock. */
#include "matrix.h"
#include "test_util.h"

static void numeric_classes(void)
{
	static const struct { mxClassID cls; size_t size; const char *name; } k[] = {
		{ mxDOUBLE_CLASS, 8, "double" }, { mxSINGLE_CLASS, 4, "single" },
		{ mxINT8_CLASS, 1, "int8" },     { mxUINT8_CLASS, 1, "uint8" },
		{ mxINT16_CLASS, 2, "int16" },   { mxUINT16_CLASS, 2, "uint16" },
		{ mxINT32_CLASS, 4, "int32" },   { mxUINT32_CLASS, 4, "uint32" },
		{ mxINT64_CLASS, 8, "int64" },   { mxUINT64_CLASS, 8, "uint64" },
	};

	for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		mxArray *r = mxCreateUninitNumericMatrix(4, 2, k[i].cls, mxREAL);
		mxArray *c = mxCreateUninitNumericMatrix(4, 2, k[i].cls, mxCOMPLEX);

		CHECK(mxGetClassID(r) == k[i].cls);
		CHECK(strcmp(mxGetClassName(r), k[i].name) == 0);
		CHECK(mxIsClass(r, k[i].name));
		CHECK(mxIsNumeric(r));
		CHECK(!mxIsComplex(r));
		CHECK(!mxIsSparse(r));
		CHECK(!mxIsEmpty(r));
		CHECK(!mxIsScalar(r));
		CHECK(mxGetElementSize(r) == k[i].size);
		CHECK(mxGetM(r) == 4);
		CHECK(mxGetN(r) == 2);
		CHECK(mxGetNumberOfElements(r) == 8);
		CHECK(mxGetNumberOfDimensions(r) == 2);
		CHECK(mxGetData(r) != NULL);

		CHECK(mxGetClassID(c) == k[i].cls);
		CHECK(mxIsNumeric(c));
		CHECK(mxIsComplex(c));
		CHECK(mxGetElementSize(c) == 2 * k[i].size);
		CHECK(mxGetM(c) == 4);
		CHECK(mxGetN(c) == 2);
		CHECK(mxGetNumberOfElements(c) == 8);
		CHECK(mxGetData(c) != NULL);

		/* Every byte of the data belongs to the array. */
		memset(mxGetData(r), 0x11, 8 * k[i].size);
		memset(mxGetData(c), 0x22, 16 * k[i].size);

		mxDestroyArray(r);
		mxDestroyArray(c);
	}
}

static void numeric_values(void)
{
	mxArray *a = mxCreateDoubleMatrix(0, 0, mxREAL);

	CHECK(mxIsEmpty(a));
	CHECK(mxGetClassID(a) == mxDOUBLE_CLASS);
	CHECK(mxGetM(a) == 0);
	CHECK(mxGetN(a) == 0);
	CHECK(mxGetNumberOfElements(a) == 0);
	CHECK(mxGetData(a) == NULL);
	CHECK(!mxIsScalar(a));
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(1, 0, mxUINT64_CLASS, mxREAL);
	CHECK(mxIsEmpty(a));
	CHECK(mxGetM(a) == 1);
	CHECK(mxGetN(a) == 0);
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
	CHECK(mxIsScalar(a));
	CHECK(*(uint64_t *)mxGetData(a) == 0);          /* mxCreateNumericMatrix zeroes */
	*(uint64_t *)mxGetData(a) = 0xFFFFFFFFFFFFFFFFull;
	CHECK(*(uint64_t *)mxGetData(a) == 0xFFFFFFFFFFFFFFFFull);
	CHECK(strcmp(mxGetClassName(a), "uint64") == 0);
	CHECK(mxIsClass(a, "uint64"));
	CHECK(!mxIsClass(a, "int64"));
	mxArray *d = mxDuplicateArray(a);
	CHECK(d != a);
	CHECK(mxGetClassID(d) == mxUINT64_CLASS);
	CHECK(mxGetData(d) != mxGetData(a));
	CHECK(*(uint64_t *)mxGetData(d) == 0xFFFFFFFFFFFFFFFFull);
	mxDestroyArray(d);
	mxDestroyArray(a);

	a = mxCreateDoubleScalar(3.5);
	CHECK(mxGetClassID(a) == mxDOUBLE_CLASS);
	CHECK(mxIsScalar(a));
	CHECK(mxIsNumeric(a));
	CHECK(!mxIsChar(a));
	CHECK(mxGetScalar(a) == 3.5);
	CHECK(*(double *)mxGetData(a) == 3.5);
	CHECK(mxArrayToUTF8String(a) == NULL);
	CHECK(mxArrayToString(a) == NULL);
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(1, 1, mxINT16_CLASS, mxREAL);
	*(int16_t *)mxGetData(a) = -7;
	CHECK(mxGetScalar(a) == -7.0);
	mxDestroyArray(a);

	/* mxGetScalar goes through double: 64-bit integers lose their low bits there. */
	a = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);
	*(int64_t *)mxGetData(a) = -9007199254740993ll;
	CHECK(mxGetScalar(a) == -9007199254740992.0);
	mxDestroyArray(a);

	/* A complex scalar is a scalar; mxGetScalar gives its real part. */
	a = mxCreateNumericMatrix(1, 1, mxSINGLE_CLASS, mxCOMPLEX);
	((float *)mxGetData(a))[0] = 1.5f;
	((float *)mxGetData(a))[1] = 9.0f;
	CHECK(mxIsScalar(a));
	CHECK(mxGetScalar(a) == 1.5);
	mxDestroyArray(a);

	a = mxCreateNumericMatrix(1, 0, mxUINT8_CLASS, mxREAL);
	CHECK(mxGetData(a) == NULL);
	d = mxDuplicateArray(a);
	CHECK(mxGetM(d) == 1 && mxGetN(d) == 0);
	mxDestroyArray(d);
	mxDestroyArray(a);

	a = mxCreateDoubleMatrix(2, 3, mxCOMPLEX);
	CHECK(mxIsComplex(a));
	CHECK(mxGetElementSize(a) == 16);
	for (int i = 0; i < 12; i++)
		CHECK(((double *)mxGetData(a))[i] == 0.0);
	mxDestroyArray(a);
}

static void logicals(void)
{
	mxArray *a = mxCreateLogicalScalar(true);

	CHECK(mxIsLogical(a));
	CHECK(!mxIsNumeric(a));
	CHECK(mxGetClassID(a) == mxLOGICAL_CLASS);
	CHECK(strcmp(mxGetClassName(a), "logical") == 0);
	CHECK(mxGetElementSize(a) == 1);
	CHECK(mxIsScalar(a));
	CHECK(mxIsLogicalScalarTrue(a));
	CHECK(mxGetLogicals(a) != NULL && mxGetLogicals(a)[0]);
	CHECK((void *)mxGetLogicals(a) == mxGetData(a));
	CHECK(mxGetScalar(a) == 1.0);
	mxDestroyArray(a);

	a = mxCreateLogicalScalar(false);
	CHECK(!mxIsLogicalScalarTrue(a));
	mxDestroyArray(a);

	a = mxCreateLogicalMatrix(1, 3);
	CHECK(mxIsLogical(a));
	CHECK(mxGetNumberOfElements(a) == 3);
	CHECK(!mxGetLogicals(a)[0] && !mxGetLogicals(a)[1] && !mxGetLogicals(a)[2]);
	mxGetLogicals(a)[0] = true;
	CHECK(!mxIsLogicalScalarTrue(a));   /* true, but not a scalar */
	mxDestroyArray(a);

	a = mxCreateDoubleScalar(1.0);
	CHECK(!mxIsLogical(a));
	CHECK(!mxIsLogicalScalarTrue(a));   /* a scalar, but not logical */
	mxDestroyArray(a);
}

static void strings(void)
{
	static const char utf8[] = "voltage0 \xC2\xB5V \xE2\x82\xAC";   /* micro sign, euro sign */
	mxArray *a = mxCreateString("");
	char *s;

	CHECK(mxIsChar(a));
	CHECK(mxGetClassID(a) == mxCHAR_CLASS);
	CHECK(strcmp(mxGetClassName(a), "char") == 0);
	CHECK(mxGetM(a) == 0);
	CHECK(mxGetN(a) == 0);
	CHECK(mxIsEmpty(a));
	s = mxArrayToUTF8String(a);
	CHECK(s != NULL && strcmp(s, "") == 0);
	mxFree(s);
	mxDestroyArray(a);

	a = mxCreateString(utf8);
	CHECK(mxIsChar(a));
	CHECK(!mxIsNumeric(a));
	CHECK(mxGetM(a) == 1);
	CHECK(mxGetN(a) == 13);
	CHECK(mxGetElementSize(a) == 2);
	CHECK(mxGetNumberOfDimensions(a) == 2);
	CHECK(((mxChar *)mxGetData(a))[0] == 'v');
	CHECK(((mxChar *)mxGetData(a))[9] == 0x00B5);
	CHECK(((mxChar *)mxGetData(a))[12] == 0x20AC);
	s = mxArrayToUTF8String(a);
	CHECK(s != NULL && strcmp(s, utf8) == 0);
	mxFree(s);
	mxDestroyArray(a);

	a = mxCreateString("abc");
	CHECK(mxGetM(a) == 1);
	CHECK(mxGetN(a) == 3);
	s = mxArrayToString(a);
	CHECK(s != NULL && strcmp(s, "abc") == 0);
	mxFree(s);
	mxArray *d = mxDuplicateArray(a);
	CHECK(mxIsChar(d));
	s = mxArrayToUTF8String(d);
	CHECK(s != NULL && strcmp(s, "abc") == 0);
	mxFree(s);
	mxDestroyArray(d);
	mxDestroyArray(a);

	/* Bytes that are not UTF-8 (here Latin-1 e acute) become U+FFFD, one per bad byte. */
	a = mxCreateString("caf\xE9!");
	CHECK(mxGetNumberOfElements(a) == 5);
	CHECK(((mxChar *)mxGetData(a))[3] == 0xFFFD);
	CHECK(((mxChar *)mxGetData(a))[4] == '!');
	mxDestroyArray(a);

	/* A sequence cut short gives one U+FFFD per byte, not one for the sequence. */
	a = mxCreateString("a\xE2\x82x");
	CHECK(mxGetNumberOfElements(a) == 4);
	CHECK(((mxChar *)mxGetData(a))[1] == 0xFFFD);
	CHECK(((mxChar *)mxGetData(a))[2] == 0xFFFD);
	CHECK(((mxChar *)mxGetData(a))[3] == 'x');
	mxDestroyArray(a);

	/* Outside the BMP: a surrogate pair, two elements, and back to the same four bytes. */
	a = mxCreateString("\xF0\x9F\x98\x80z");
	CHECK(mxGetNumberOfElements(a) == 3);
	CHECK(((mxChar *)mxGetData(a))[0] == 0xD83D);
	CHECK(((mxChar *)mxGetData(a))[1] == 0xDE00);
	s = mxArrayToUTF8String(a);
	CHECK(s != NULL && strcmp(s, "\xF0\x9F\x98\x80z") == 0);
	mxFree(s);
	mxDestroyArray(a);

	/* The conversion to C text stops at the first NUL element; half a surrogate pair
	 * becomes U+FFFD. */
	a = mxCreateString("abcd");
	((mxChar *)mxGetData(a))[1] = 0;
	s = mxArrayToUTF8String(a);
	CHECK(s != NULL && strcmp(s, "a") == 0);
	mxFree(s);
	((mxChar *)mxGetData(a))[1] = 0xD800;
	s = mxArrayToUTF8String(a);
	CHECK(s != NULL && strcmp(s, "a\xEF\xBF\xBD" "cd") == 0);
	mxFree(s);
	mxDestroyArray(a);

	a = mxCreateString("A");
	CHECK(mxIsScalar(a));
	CHECK(mxGetScalar(a) == 65.0);
	mxDestroyArray(a);
}

static void cells(void)
{
	mxArray *c = mxCreateCellMatrix(1, 2);
	mxArray *v = mxCreateDoubleScalar(4.0);

	CHECK(mxIsCell(c));
	CHECK(!mxIsNumeric(c));
	CHECK(mxGetClassID(c) == mxCELL_CLASS);
	CHECK(strcmp(mxGetClassName(c), "cell") == 0);
	CHECK(mxGetM(c) == 1);
	CHECK(mxGetN(c) == 2);
	CHECK(mxGetNumberOfElements(c) == 2);
	CHECK(mxGetCell(c, 0) == NULL);
	CHECK(mxGetCell(c, 1) == NULL);
	CHECK(mxGetNumberOfFields(c) == 0);            /* not a struct: no fields, no error */
	CHECK(mxGetField(c, 0, "a") == NULL);
	mxSetCell(c, 1, v);
	CHECK(mxGetCell(c, 0) == NULL);
	CHECK(mxGetCell(c, 1) == v);
	CHECK(mxGetScalar(mxGetCell(c, 1)) == 4.0);

	mxArray *d = mxDuplicateArray(c);   /* deep copy */
	CHECK(mxIsCell(d));
	CHECK(mxGetCell(d, 0) == NULL);
	CHECK(mxGetCell(d, 1) != NULL && mxGetCell(d, 1) != v);
	CHECK(mxGetScalar(mxGetCell(d, 1)) == 4.0);
	mxDestroyArray(d);
	mxDestroyArray(c);                  /* destroys v too */

	c = mxCreateCellMatrix(1, 0);
	CHECK(mxIsCell(c));
	CHECK(mxIsEmpty(c));
	CHECK(mxGetM(c) == 1);
	CHECK(mxGetN(c) == 0);
	mxDestroyArray(c);
}

static void structs(void)
{
	const char *names[] = { "a", "b" };
	mxArray *s = mxCreateStructMatrix(1, 1, 2, names);
	mxArray *c = mxCreateDoubleScalar(9.0);

	CHECK(mxIsStruct(s));
	CHECK(!mxIsNumeric(s));
	CHECK(!mxIsCell(s));
	CHECK(mxGetClassID(s) == mxSTRUCT_CLASS);
	CHECK(strcmp(mxGetClassName(s), "struct") == 0);
	CHECK(mxIsScalar(s));
	CHECK(mxGetNumberOfElements(s) == 1);
	CHECK(mxGetNumberOfFields(s) == 2);
	CHECK(strcmp(mxGetFieldNameByNumber(s, 0), "a") == 0);
	CHECK(strcmp(mxGetFieldNameByNumber(s, 1), "b") == 0);
	CHECK(mxGetFieldNameByNumber(s, 2) == NULL);
	CHECK(mxGetFieldNameByNumber(s, -1) == NULL);
	CHECK(mxGetField(s, 0, "a") == NULL);          /* unset */
	CHECK(mxGetField(s, 0, "nope") == NULL);       /* unknown */
	mxSetFieldByNumber(s, 0, 0, c);
	CHECK(mxGetField(s, 0, "a") == c);
	CHECK(mxGetField(s, 0, "b") == NULL);
	CHECK(mxGetField(s, 0, "A") == NULL);          /* field names are case sensitive */

	mxArray *d = mxDuplicateArray(s);
	CHECK(mxIsStruct(d));
	CHECK(mxGetNumberOfFields(d) == 2);
	CHECK(mxGetField(d, 0, "a") != NULL && mxGetField(d, 0, "a") != c);
	CHECK(mxGetScalar(mxGetField(d, 0, "a")) == 9.0);
	mxDestroyArray(d);
	mxDestroyArray(s);                             /* destroys c too */

	s = mxCreateStructMatrix(1, 1, 0, NULL);
	CHECK(mxIsStruct(s));
	CHECK(mxGetNumberOfFields(s) == 0);
	mxDestroyArray(s);
}

static void memory(void)
{
	char *p = mxMalloc(16);
	uint32_t *q = mxCalloc(4, sizeof(*q));

	CHECK(p != NULL);
	memset(p, 0x5a, 16);
	CHECK(q != NULL && q[0] == 0 && q[3] == 0);
	mxFree(p);
	mxFree(q);

	/* A request for no bytes gives NULL, not a block. */
	CHECK(mxMalloc(0) == NULL);
	CHECK(mxCalloc(0, 4) == NULL);
	CHECK(mxCalloc(4, 0) == NULL);

	mxDestroyArray(NULL);
	mxFree(NULL);
	CHECK(mxGetData(NULL) == NULL);
}

int main(void)
{
	numeric_classes();
	numeric_values();
	logicals();
	strings();
	cells();
	structs();
	memory();
	TEST_MAIN_RETURN();
}
