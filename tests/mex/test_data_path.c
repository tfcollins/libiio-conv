/* SPDX-License-Identifier: MIT */
/* The data-path foundations: class, scale and option parsing of data_path.h, the registration
 * of masks, buffer streams and blocks, and the object builders of dp_test.h (emu: and fake). */
#include "mex_test.h"
#include "dp_test.h"

#include <stdarg.h>

const struct gw_group *const gw_groups[] = { &gw_group_mex, NULL };

/* Scalar struct with n fields: pairs of (const char *name, mxArray *value). */
static mxArray *mkstruct(int n, ...)
{
	const char *names[8];
	mxArray *values[8];
	mxArray *s;
	va_list ap;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		names[i] = va_arg(ap, const char *);
		values[i] = va_arg(ap, mxArray *);
	}
	va_end(ap);
	s = mxCreateStructMatrix(1, 1, n, names);
	for (int i = 0; i < n; i++)
		mxSetFieldByNumber(s, 0, i, values[i]);
	return s;
}

/* m-by-n real double with these values in storage order. */
static mxArray *dmat(size_t m, size_t n, ...)
{
	mxArray *a = mxCreateDoubleMatrix(m, n, mxREAL);
	double *p = mxGetData(a);
	va_list ap;

	va_start(ap, n);
	for (size_t i = 0; i < m * n; i++)
		p[i] = va_arg(ap, double);
	va_end(ap);
	return a;
}

static void test_names(void)
{
	static const struct { const char *name; enum iio_conv_type type; mxClassID cls; } t[10] = {
		{ "double", IIO_CONV_F64, mxDOUBLE_CLASS }, { "single", IIO_CONV_F32, mxSINGLE_CLASS },
		{ "int8", IIO_CONV_I8, mxINT8_CLASS },      { "uint8", IIO_CONV_U8, mxUINT8_CLASS },
		{ "int16", IIO_CONV_I16, mxINT16_CLASS },   { "uint16", IIO_CONV_U16, mxUINT16_CLASS },
		{ "int32", IIO_CONV_I32, mxINT32_CLASS },   { "uint32", IIO_CONV_U32, mxUINT32_CLASS },
		{ "int64", IIO_CONV_I64, mxINT64_CLASS },   { "uint64", IIO_CONV_U64, mxUINT64_CLASS },
	};
	static const char *const bad[] = { "int12", "", "Double", "logical", "char", "doubles", " double" };
	enum iio_conv_scale scale;
	enum iio_conv_type type;
	mxClassID cls;

	for (unsigned int i = 0; i < 10; i++) {
		type = (enum iio_conv_type)99;
		cls = mxUNKNOWN_CLASS;
		CHECK(dp_class_from_name(t[i].name, &type, &cls) == 0);
		CHECK(type == t[i].type && cls == t[i].cls);
		CHECK(dp_class_of_type(type) == cls);
		type = (enum iio_conv_type)99;
		CHECK(dp_class_from_id(t[i].cls, &type) == 0);
		CHECK(type == t[i].type);
	}
	CHECK(dp_class_from_name("int16", NULL, NULL) == 0);
	for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(dp_class_from_name(bad[i], &type, &cls) == -EINVAL);
	CHECK(dp_class_from_id(mxCHAR_CLASS, &type) == -EINVAL);
	CHECK(dp_class_from_id(mxLOGICAL_CLASS, &type) == -EINVAL);
	CHECK(dp_class_from_id(mxCELL_CLASS, &type) == -EINVAL);
	CHECK(dp_class_from_id(mxSTRUCT_CLASS, &type) == -EINVAL);
	CHECK(dp_class_from_id(mxUNKNOWN_CLASS, &type) == -EINVAL);
	CHECK(dp_class_of_type(IIO_CONV_I16) == mxINT16_CLASS);
	CHECK(dp_class_of_type((enum iio_conv_type)99) == mxUNKNOWN_CLASS);

	scale = (enum iio_conv_scale)99;
	CHECK(dp_scale_from_name("none", &scale) == 0 && scale == IIO_CONV_SCALE_NONE);
	CHECK(dp_scale_from_name("iio", &scale) == 0 && scale == IIO_CONV_SCALE_IIO);
	CHECK(dp_scale_from_name("fullscale", &scale) == 0 && scale == IIO_CONV_SCALE_FULLSCALE);
	CHECK(dp_scale_from_name("full", &scale) == -EINVAL);
	CHECK(dp_scale_from_name("", &scale) == -EINVAL);
	CHECK(dp_scale_from_name("IIO", &scale) == -EINVAL);
}

static void test_per_channel(void)
{
	double *v = (double *)&v;
	mxArray *a;

	a = mt_empty();
	CHECK(dp_per_channel(a, 4, true, &v) == 0 && v == NULL);
	mxDestroyArray(a);
	v = (double *)&v;
	CHECK(dp_per_channel(NULL, 4, true, &v) == 0 && v == NULL);

	a = mt_dbl(2);
	CHECK(dp_per_channel(a, 4, true, &v) == 0);
	CHECK(v && v[0] == 2 && v[1] == 2 && v[2] == 2 && v[3] == 2);
	mxFree(v);
	CHECK(dp_per_channel(a, 1, false, &v) == 0);
	CHECK(v && v[0] == 2);
	mxFree(v);
	CHECK(dp_per_channel(a, 3, true, &v) == -EINVAL && v == NULL);   /* half a pair */
	CHECK(dp_per_channel(a, 0, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);

	a = dmat(1, 2, 2.0, 3.0);
	CHECK(dp_per_channel(a, 4, true, &v) == 0);
	CHECK(v && v[0] == 2 && v[1] == 2 && v[2] == 3 && v[3] == 3);
	mxFree(v);
	CHECK(dp_per_channel(a, 2, false, &v) == 0);
	CHECK(v && v[0] == 2 && v[1] == 3);
	mxFree(v);
	CHECK(dp_per_channel(a, 2, true, &v) == -EINVAL && v == NULL);   /* 2 values, 1 column */
	CHECK(dp_per_channel(a, 4, false, &v) == -EINVAL && v == NULL);  /* 2 values, 4 columns */
	mxDestroyArray(a);

	a = dmat(2, 1, 2.0, 3.0);                                       /* a column is a vector too */
	CHECK(dp_per_channel(a, 2, false, &v) == 0);
	CHECK(v && v[0] == 2 && v[1] == 3);
	mxFree(v);
	mxDestroyArray(a);

	a = dmat(1, 3, 1.0, 2.0, 3.0);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	CHECK(dp_per_channel(a, 4, true, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);

	a = dmat(2, 2, 1.0, 2.0, 3.0, 4.0);                             /* a matrix */
	CHECK(dp_per_channel(a, 4, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mxCreateDoubleMatrix(1, 0, mxREAL);                         /* empty, but not [] */
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mxCreateNumericMatrix(1, 1, mxINT16_CLASS, mxREAL);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mxCreateDoubleMatrix(1, 1, mxCOMPLEX);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mock_sparse_double(1, 1);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mock_nd_array(mxDOUBLE_CLASS, mxREAL, 1, 1, 2);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mt_str("2");
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
	a = mt_bool(true);
	CHECK(dp_per_channel(a, 2, false, &v) == -EINVAL && v == NULL);
	mxDestroyArray(a);
}

static void test_check_matrix(void)
{
	enum iio_conv_type type = (enum iio_conv_type)99;
	bool cplx = false;
	mxArray *a;

	a = mxCreateNumericMatrix(4, 2, mxINT16_CLASS, mxCOMPLEX);
	CHECK(dp_check_matrix(a, &type, &cplx) == 0 && type == IIO_CONV_I16 && cplx);
	mxDestroyArray(a);
	a = mxCreateDoubleMatrix(3, 1, mxREAL);
	CHECK(dp_check_matrix(a, &type, &cplx) == 0 && type == IIO_CONV_F64 && !cplx);
	mxDestroyArray(a);
	a = mxCreateNumericMatrix(0, 2, mxSINGLE_CLASS, mxREAL);
	CHECK(dp_check_matrix(a, &type, &cplx) == 0 && type == IIO_CONV_F32 && !cplx);
	mxDestroyArray(a);
	a = mt_empty();
	CHECK(dp_check_matrix(a, &type, &cplx) == 0 && type == IIO_CONV_F64 && !cplx);
	mxDestroyArray(a);
	a = mt_u64(1);
	CHECK(dp_check_matrix(a, &type, &cplx) == 0 && type == IIO_CONV_U64 && !cplx);
	mxDestroyArray(a);

	CHECK(dp_check_matrix(NULL, &type, &cplx) == -EINVAL);
	a = mt_str("abc");
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mxCreateLogicalMatrix(2, 2);
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mxCreateCellMatrix(1, 1);
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mkstruct(0);
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mock_sparse_double(4, 2);
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mock_string_object();
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
	a = mock_nd_array(mxINT16_CLASS, mxREAL, 4, 1, 2);     /* mxGetM 4, mxGetN 2: only the count tells */
	CHECK(mxGetM(a) == 4 && mxGetN(a) == 2);
	CHECK(dp_check_matrix(a, &type, &cplx) == -EINVAL);
	mxDestroyArray(a);
}

static void test_options(void)
{
	static const char *const allowed[] = { "class", "complex", "scale", "raw", "gain", "offset" };
	struct iio_conv_spec spec;
	double *gain, *offset, num;
	const char *two[2] = { "class", "complex" };
	char bad[32], text[8];
	mxClassID cls;
	mxArray *o;
	bool b;

	/* a full set */
	o = mkstruct(4, "class", mt_str("single"), "complex", mt_bool(true),
		"scale", mt_str("fullscale"), "gain", mt_dbl(2));
	strcpy(bad, "?");
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == 0 && !strcmp(bad, ""));
	memset(&spec, 0xff, sizeof(spec));
	cls = mxUNKNOWN_CLASS;
	CHECK(dp_opt_spec(o, &spec, &cls) == 0);
	CHECK(spec.type == IIO_CONV_F32 && spec.layout == IIO_CONV_COLUMNS);
	CHECK(spec.scale == IIO_CONV_SCALE_FULLSCALE && spec.complex_iq && spec.saturate && !spec.raw);
	CHECK(spec.user_gain == NULL && spec.user_offset == NULL);
	CHECK(cls == mxSINGLE_CLASS);
	gain = offset = (double *)&spec;
	CHECK(dp_opt_gains(o, 2, &spec, &gain, &offset) == 0);
	CHECK(gain && spec.user_gain == gain && gain[0] == 2 && gain[1] == 2);
	CHECK(offset == NULL && spec.user_offset == NULL);
	mxFree(gain);
	CHECK(dp_opt_gains(o, 3, &spec, &gain, &offset) == -EINVAL);   /* complex, 3 channels */
	CHECK(gain == NULL && offset == NULL && spec.user_gain == NULL && spec.user_offset == NULL);
	CHECK(dp_opt_spec(o, &spec, NULL) == 0 && spec.type == IIO_CONV_F32);
	mxDestroyArray(o);

	/* no options: [] and an absent argument */
	o = mt_empty();
	strcpy(bad, "?");
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == 0 && !strcmp(bad, ""));
	CHECK(dp_opt_check(NULL, allowed, 6, bad, sizeof(bad)) == 0);
	CHECK(dp_opt_check(o, NULL, 0, NULL, 0) == 0);
	memset(&spec, 0xff, sizeof(spec));
	cls = mxUNKNOWN_CLASS;
	CHECK(dp_opt_spec(o, &spec, &cls) == 0);
	CHECK(spec.type == IIO_CONV_F64 && spec.layout == IIO_CONV_COLUMNS);
	CHECK(spec.scale == IIO_CONV_SCALE_NONE && !spec.complex_iq && spec.saturate && !spec.raw);
	CHECK(spec.user_gain == NULL && spec.user_offset == NULL && cls == mxDOUBLE_CLASS);
	gain = offset = (double *)&spec;
	CHECK(dp_opt_gains(o, 2, &spec, &gain, &offset) == 0);
	CHECK(gain == NULL && offset == NULL && spec.user_gain == NULL && spec.user_offset == NULL);
	CHECK(dp_opt_number(o, "samples", 7.5, &num) == 0 && num == 7.5);
	CHECK(dp_opt_bool(o, "cyclic", true, &b) == 0 && b);
	CHECK(dp_opt_string(o, "scale", "none", text, sizeof(text)) == 0 && !strcmp(text, "none"));
	mxDestroyArray(o);
	memset(&spec, 0xff, sizeof(spec));
	CHECK(dp_opt_spec(NULL, &spec, NULL) == 0 && spec.type == IIO_CONV_F64 && spec.saturate);
	CHECK(dp_opt_number(NULL, "samples", -1, &num) == 0 && num == -1);

	/* gain and offset per column; raw */
	o = mkstruct(4, "class", mt_str("int16"), "raw", mt_bool(true),
		"gain", dmat(1, 2, 2.0, 3.0), "offset", dmat(2, 1, -1.0, 0.5));
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == 0);
	CHECK(dp_opt_spec(o, &spec, &cls) == 0);
	CHECK(spec.type == IIO_CONV_I16 && cls == mxINT16_CLASS && spec.raw && !spec.complex_iq);
	CHECK(dp_opt_gains(o, 2, &spec, &gain, &offset) == 0);
	CHECK(gain && gain[0] == 2 && gain[1] == 3 && spec.user_gain == gain);
	CHECK(offset && offset[0] == -1 && offset[1] == 0.5 && spec.user_offset == offset);
	mxFree(gain);
	mxFree(offset);
	spec.complex_iq = true;                       /* as block_write decides it after dp_opt_spec */
	CHECK(dp_opt_gains(o, 4, &spec, &gain, &offset) == 0);
	CHECK(gain && gain[0] == 2 && gain[1] == 2 && gain[2] == 3 && gain[3] == 3);
	CHECK(offset && offset[0] == -1 && offset[1] == -1 && offset[2] == 0.5 && offset[3] == 0.5);
	mxFree(gain);
	mxFree(offset);
	spec.complex_iq = false;
	CHECK(dp_opt_gains(o, 3, &spec, &gain, &offset) == -EINVAL);   /* 2 values, 3 columns */
	CHECK(gain == NULL && offset == NULL && spec.user_gain == NULL && spec.user_offset == NULL);
	mxDestroyArray(o);
	o = mkstruct(2, "gain", mt_dbl(2), "offset", dmat(1, 3, 1.0, 2.0, 3.0));   /* the offset is wrong */
	CHECK(dp_opt_gains(o, 2, &spec, &gain, &offset) == -EINVAL);
	CHECK(gain == NULL && offset == NULL && spec.user_gain == NULL && spec.user_offset == NULL);
	mxDestroyArray(o);

	/* an unknown field */
	o = mkstruct(2, "class", mt_str("int16"), "colour", mt_dbl(1));
	strcpy(bad, "?");
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	CHECK(!strcmp(bad, "colour"));
	strcpy(bad, "?");
	CHECK(dp_opt_check(o, allowed, 6, bad, 4) == -EINVAL && !strcmp(bad, "col"));
	CHECK(dp_opt_check(o, allowed, 6, NULL, 0) == -EINVAL);
	CHECK(dp_opt_check(o, two, 2, bad, sizeof(bad)) == -EINVAL && !strcmp(bad, "colour"));
	mxDestroyArray(o);
	o = mkstruct(2, "class", mt_str("int16"), "complex", mt_bool(false));
	CHECK(dp_opt_check(o, two, 2, bad, sizeof(bad)) == 0);
	CHECK(dp_opt_check(o, two, 1, bad, sizeof(bad)) == -EINVAL && !strcmp(bad, "complex"));
	mxDestroyArray(o);

	/* options that are neither [] nor a scalar struct */
	o = mxCreateStructMatrix(1, 2, 2, two);
	strcpy(bad, "?");
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL && !strcmp(bad, ""));
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	CHECK(dp_opt_gains(o, 2, &spec, &gain, &offset) == -EINVAL && gain == NULL && offset == NULL);
	CHECK(dp_opt_number(o, "samples", 1, &num) == -EINVAL);
	CHECK(dp_opt_bool(o, "complex", false, &b) == -EINVAL);
	CHECK(dp_opt_string(o, "class", "double", text, sizeof(text)) == -EINVAL);
	mxDestroyArray(o);
	o = mxCreateStructMatrix(0, 0, 2, two);
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	mxDestroyArray(o);
	o = mt_dbl(3);
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	CHECK(dp_opt_number(o, "samples", 1, &num) == -EINVAL);
	mxDestroyArray(o);
	o = mxCreateDoubleMatrix(1, 0, mxREAL);
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	mxDestroyArray(o);
	o = mxCreateCellMatrix(1, 1);
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	mxDestroyArray(o);
	o = mt_str("class");
	CHECK(dp_opt_check(o, allowed, 6, bad, sizeof(bad)) == -EINVAL);
	mxDestroyArray(o);

	/* field values: numbers */
	o = mkstruct(8, "a", mt_dbl(1024), "b", mt_i64(-3), "c", mt_bool(true), "d", mt_str("4"),
		"e", dmat(1, 2, 1.0, 2.0), "f", mxCreateDoubleMatrix(1, 1, mxCOMPLEX), "g", mt_empty(),
		"h", mock_sparse_double(1, 1));
	CHECK(dp_opt_number(o, "a", 0, &num) == 0 && num == 1024);
	CHECK(dp_opt_number(o, "b", 0, &num) == 0 && num == -3);
	CHECK(dp_opt_number(o, "c", 0, &num) == -EINVAL);
	CHECK(dp_opt_number(o, "d", 0, &num) == -EINVAL);
	CHECK(dp_opt_number(o, "e", 0, &num) == -EINVAL);
	CHECK(dp_opt_number(o, "f", 0, &num) == -EINVAL);
	CHECK(dp_opt_number(o, "g", 5, &num) == 0 && num == 5);        /* a field that is [] is absent */
	CHECK(dp_opt_number(o, "h", 0, &num) == -EINVAL);
	CHECK(dp_opt_number(o, "z", 9, &num) == 0 && num == 9);
	/* booleans */
	CHECK(dp_opt_bool(o, "c", false, &b) == 0 && b);
	CHECK(dp_opt_bool(o, "a", false, &b) == -EINVAL);
	CHECK(dp_opt_bool(o, "d", false, &b) == -EINVAL);
	CHECK(dp_opt_bool(o, "g", true, &b) == 0 && b);
	CHECK(dp_opt_bool(o, "z", false, &b) == 0 && !b);
	/* text */
	CHECK(dp_opt_string(o, "d", "x", text, sizeof(text)) == 0 && !strcmp(text, "4"));
	CHECK(dp_opt_string(o, "a", "x", text, sizeof(text)) == -EINVAL);
	CHECK(dp_opt_string(o, "c", "x", text, sizeof(text)) == -EINVAL);
	CHECK(dp_opt_string(o, "g", "dflt", text, sizeof(text)) == 0 && !strcmp(text, "dflt"));
	CHECK(dp_opt_string(o, "z", "dflt", text, sizeof(text)) == 0 && !strcmp(text, "dflt"));
	CHECK(dp_opt_string(o, "z", "12345678", text, sizeof(text)) == -EINVAL);   /* does not fit */
	mxDestroyArray(o);
	o = mkstruct(6, "a", mt_str("1234567"), "b", mt_str("12345678"), "c", mt_str(""),
		"d", mock_char_array(2, 1, 1, "ab"), "e", mock_string_object(), "f", mxCreateLogicalMatrix(1, 2));
	CHECK(dp_opt_string(o, "a", "x", text, sizeof(text)) == 0 && !strcmp(text, "1234567"));
	CHECK(dp_opt_string(o, "b", "x", text, sizeof(text)) == -EINVAL);
	CHECK(dp_opt_string(o, "c", "x", text, sizeof(text)) == 0 && !strcmp(text, ""));
	CHECK(dp_opt_string(o, "d", "x", text, sizeof(text)) == -EINVAL);          /* a column */
	CHECK(dp_opt_string(o, "e", "x", text, sizeof(text)) == -EINVAL);          /* a string object */
	CHECK(dp_opt_bool(o, "f", false, &b) == -EINVAL);                          /* not a scalar */
	mxDestroyArray(o);

	/* dp_opt_spec passes the errors of its fields on */
	o = mkstruct(1, "class", mt_str("int12"));
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	mxDestroyArray(o);
	o = mkstruct(1, "class", mock_string_object());
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	mxDestroyArray(o);
	o = mkstruct(1, "scale", mt_str("full"));
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	mxDestroyArray(o);
	o = mkstruct(1, "complex", mt_dbl(1));
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	mxDestroyArray(o);
	o = mkstruct(1, "raw", mt_str("yes"));
	CHECK(dp_opt_spec(o, &spec, &cls) == -EINVAL);
	mxDestroyArray(o);
	o = mkstruct(2, "scale", mt_str("iio"), "class", mt_str("uint64"));
	CHECK(dp_opt_spec(o, &spec, &cls) == 0);
	CHECK(spec.scale == IIO_CONV_SCALE_IIO && spec.type == IIO_CONV_U64 && cls == mxUINT64_CLASS);
	mxDestroyArray(o);
}

static void test_emu(const struct mt_fixture *fx)
{
	gw_id ctx, rx, tx, chn, buf, mask, bs, blk, blk2, txmask;
	const struct iio_device *dev;
	struct iio_buffer_stream *bsp;
	size_t n;

	CHECK(dp_device_of(0) == NULL);
	ctx = mt_open_context(fx->uri);
	CHECK(ctx != 0);
	CHECK(mock_is_locked());

	rx = dp_dev(ctx, "emu-rx");
	CHECK(rx != 0 && rx != ctx);
	CHECK(dp_dev(ctx, "emu-rx") == rx);
	CHECK(gw_handle_type(rx) == GW_T_DEVICE && !gw_handle_owned(rx) && gw_handle_parent(rx) == ctx);
	dev = gw_handle_ptr(rx, GW_T_DEVICE);
	CHECK(dev != NULL && !strcmp(iio_device_get_name(dev), "emu-rx"));
	CHECK(dp_device_of(rx) == dev);
	CHECK(dp_device_of(ctx) == NULL);
	tx = dp_dev(ctx, "emu-tx");
	CHECK(tx != 0 && tx != rx && dp_device_of(tx) != dev);

	chn = dp_chn(rx, "voltage0_i", false);
	CHECK(chn != 0 && dp_chn(rx, "voltage0_i", false) == chn);
	CHECK(gw_handle_type(chn) == GW_T_CHANNEL && !gw_handle_owned(chn) && gw_handle_parent(chn) == ctx);
	CHECK(dp_device_of(chn) == dev);
	CHECK(dp_chn(rx, "voltage0_q", false) != chn);
	CHECK(dp_device_of(dp_chn(tx, "voltage0_i", true)) == gw_handle_ptr(tx, GW_T_DEVICE));

	buf = dp_buf(rx, 0);
	CHECK(buf != 0 && dp_buf(rx, 0) == buf);
	CHECK(gw_handle_type(buf) == GW_T_BUFFER && !gw_handle_owned(buf) && gw_handle_parent(buf) == ctx);
	CHECK(dp_device_of(buf) == dev);

	mask = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	CHECK(mask != 0);
	CHECK(gw_handle_type(mask) == GW_T_MASK && gw_handle_owned(mask) && gw_handle_parent(mask) == rx);
	CHECK(dp_device_of(mask) == dev);
	CHECK(iio_device_get_sample_size(dev, gw_handle_ptr(mask, GW_T_MASK)) == 4);
	txmask = dp_mask(tx, true, 1, "voltage1_q");
	CHECK(txmask != 0 && txmask != mask && gw_handle_parent(txmask) == tx);
	CHECK(dp_device_of(txmask) == gw_handle_ptr(tx, GW_T_DEVICE));

	bs = dp_bufstream(buf, mask);
	CHECK(bs != 0);
	CHECK(gw_handle_type(bs) == GW_T_BUFFER_STREAM && gw_handle_owned(bs) && gw_handle_parent(bs) == buf);
	CHECK(gw_handle_flags(bs) == 0);
	CHECK(dp_device_of(bs) == dev);

	blk = dp_block(bs, 32);
	CHECK(blk != 0);
	CHECK(gw_handle_type(blk) == GW_T_BLOCK && gw_handle_owned(blk) && gw_handle_parent(blk) == bs);
	CHECK(gw_handle_root(blk) == ctx);
	CHECK(gw_handle_flags(blk) == 0);
	CHECK(dp_device_of(blk) == dev);
	CHECK(iio_block_end(gw_handle_ptr(blk, GW_T_BLOCK)) ==
		(void *)((char *)iio_block_start(gw_handle_ptr(blk, GW_T_BLOCK)) + 32));
	blk2 = dp_block(bs, 16);
	CHECK(blk2 != 0 && blk2 != blk);

	/* closing the buffer stream takes its blocks along, and nothing else */
	n = gw_handle_count();
	gw_handle_destroy(bs);
	CHECK(gw_handle_count() == n - 3);
	CHECK(gw_handle_type(bs) == GW_T_NONE && gw_handle_type(blk) == GW_T_NONE);
	CHECK(gw_handle_type(blk2) == GW_T_NONE);
	CHECK(gw_handle_ptr(blk, GW_T_BLOCK) == NULL && dp_device_of(blk) == NULL && dp_device_of(bs) == NULL);
	CHECK(gw_handle_type(mask) == GW_T_MASK && gw_handle_type(buf) == GW_T_BUFFER);

	/* a stale parent takes nothing: the caller keeps the object */
	CHECK(dp_add_block((struct iio_block *)&n, bs) == 0);
	CHECK(dp_add_bufstream((struct iio_buffer_stream *)&n, blk) == 0);
	CHECK(dp_add_mask((struct iio_channels_mask *)&n, bs) == 0);
	CHECK(dp_add_mask(NULL, rx) == 0);
	CHECK(gw_handle_count() == n - 3);

	/* a mask goes when it is destroyed, the others with their context */
	gw_handle_destroy(txmask);
	CHECK(gw_handle_type(txmask) == GW_T_NONE && gw_handle_type(tx) == GW_T_DEVICE);

	/* the context is destroyed with a started buffer stream and a block left open */
	bs = dp_bufstream(buf, mask);
	blk = dp_block(bs, 32);
	CHECK(bs != 0 && blk != 0);
	bsp = gw_handle_ptr(bs, GW_T_BUFFER_STREAM);
	CHECK(bsp != NULL && iio_buffer_stream_start(bsp) == 0);
	gw_handle_set_flags(bs, BS_STARTED);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
	CHECK(gw_handle_type(blk) == GW_T_NONE && gw_handle_type(bs) == GW_T_NONE);
	CHECK(gw_handle_type(mask) == GW_T_NONE && gw_handle_type(rx) == GW_T_NONE);
	CHECK(dp_device_of(rx) == NULL && dp_device_of(chn) == NULL && dp_device_of(mask) == NULL);
	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(!mock_is_locked());

	/* the xml: backend gives the same objects; its buffers cannot be opened */
	ctx = mt_open_context(fx->xml_uri);
	rx = dp_dev(ctx, "emu-rx");
	CHECK(ctx != 0 && rx != 0);
	CHECK(dp_buf(rx, 0) != 0);
	CHECK(dp_mask(rx, false, 1, "voltage2") != 0);
	gw_handle_destroy(ctx);
	CHECK(gw_handle_count() == 0);
}

/* What an unload of the MEX file ("MATLAB exit") leaves of an open buffer stream on the fake backend. */
static void test_fake(const struct mt_fixture *fx, bool started)
{
	gw_id ctx, rx, buf, mask, bs, b1, b2;

	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0);
	CHECK(gw_handle_type(ctx) == GW_T_CONTEXT && gw_handle_owned(ctx) && gw_handle_parent(ctx) == 0);
	CHECK(mock_is_locked());
	CHECK(!strcmp(iio_context_get_name(gw_handle_ptr(ctx, GW_T_CONTEXT)), "fake"));
	CHECK(!strcmp(fake.log, ""));
	rx = dp_dev(ctx, "emu-rx");
	buf = dp_buf(rx, 0);
	mask = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q");
	bs = dp_bufstream(buf, mask);
	b1 = dp_block(bs, 32);
	b2 = dp_block(bs, 32);
	CHECK(rx && buf && mask && bs && b1 && b2 && b1 != b2);
	CHECK(!strcmp(fake.log, "OBB"));
	if (started) {
		CHECK(iio_buffer_stream_start(gw_handle_ptr(bs, GW_T_BUFFER_STREAM)) == 0);
		gw_handle_set_flags(bs, BS_STARTED);
	}
	CHECK(mt_dbl_of("mex_handle_count", 0) == 7);

	mock_exit_matlab();
	CHECK(gw_handle_count() == 0);
	CHECK(!mock_is_locked());
	if (started)
		CHECK(!strcmp(fake.log, "OBBSXsFFXC"));    /* cancel, stop, blocks, close */
	else
		CHECK(!strcmp(fake.log, "OBBXFFXC"));      /* cancel, blocks, close */
	if (test_failures)
		fprintf(stderr, "fake log (%s): %s\n", started ? "started" : "not started", fake.log);
}

/* Destroying one block or one buffer stream by its id, and the state a new fake context starts from. */
static void test_fake_single(const struct mt_fixture *fx)
{
	gw_id ctx, tx, bs, b1, b2;

	fake.rx_frames = 77;
	ctx = dp_open_fake(fx->xml);
	CHECK(ctx != 0 && fake.rx_frames == 3 && !strcmp(fake.log, ""));
	tx = dp_dev(ctx, "emu-tx");
	bs = dp_bufstream(dp_buf(tx, 0), dp_mask(tx, true, 2, "voltage0_i", "voltage0_q"));
	b1 = dp_block(bs, 16);
	b2 = dp_block(bs, 16);
	CHECK(bs && b1 && b2);
	CHECK(dp_device_of(b2) == gw_handle_ptr(tx, GW_T_DEVICE));
	gw_handle_destroy(b1);
	CHECK(!strcmp(fake.log, "OBBF"));
	CHECK(gw_handle_type(b1) == GW_T_NONE && gw_handle_type(b2) == GW_T_BLOCK);
	gw_handle_destroy(bs);
	CHECK(!strcmp(fake.log, "OBBFXFXC"));
	CHECK(gw_handle_type(b2) == GW_T_NONE && gw_handle_type(tx) == GW_T_DEVICE);
	gw_handle_destroy(ctx);
	CHECK(!strcmp(fake.log, "OBBFXFXC"));
	CHECK(gw_handle_count() == 0);
}

int main(int argc, char **argv)
{
	struct mt_fixture fx;

	if (argc < 2 || mt_fixture_setup(&fx, argv[1]))
		return 2;

	test_names();
	test_per_channel();
	test_check_matrix();
	test_options();
	test_emu(&fx);
	test_fake(&fx, false);
	test_fake(&fx, true);
	test_fake_single(&fx);

	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();
}
