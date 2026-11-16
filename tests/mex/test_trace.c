/* SPDX-License-Identifier: MIT */
/* The dispatcher trace: with IIO_MEX_TRACE naming a file, every command is written to it
 * before it runs and again with its outcome. */
#include "mex_test.h"

static int t_ok(struct gw_call *c)
{
	return gw_ret_double(c, 0, 42);
}

static const struct gw_cmd local_cmds[] = {
	GW_XCMD("t_ok", t_ok, 0, 0, 1),
};

static const struct gw_group gw_group_local = GW_GROUP("local", local_cmds);

MT_GROUPS(&gw_group_local);

static bool file_is(const char *path, const char *want)
{
	static char got[4096];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f)
		return false;
	n = fread(got, 1, sizeof(got) - 1, f);
	fclose(f);
	got[n] = '\0';
	if (strcmp(got, want))
		fprintf(stderr, "trace file holds:\n%s", got);
	return !strcmp(got, want);
}

int main(void)
{
	static const char six[] =
		"> mex_handle_count\n"
		"< mex_handle_count ok\n"
		"> t_ok\n"
		"< t_ok ok\n"
		"> no_such\n"
		"< no_such iio:ENOSYS\n";
	static char more[1024];
	const char *tmp = getenv("TMPDIR");
	char dir[PATH_MAX], path[PATH_MAX + 16];
	mxArray *out[1] = { NULL };

	snprintf(dir, sizeof(dir), "%s/iio-mex-trace-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (!mkdtemp(dir))
		return 2;
	snprintf(path, sizeof(path), "%s/trace.log", dir);
	if (setenv("IIO_MEX_TRACE", path, 1))
		return 2;

	CHECK(mt_dbl_of("mex_handle_count", 0) == 0);
	CHECK(file_is(path, "> mex_handle_count\n< mex_handle_count ok\n"));   /* flushed line by line */
	CHECK(mt_dbl_of("t_ok", 0) == 42);
	CHECK(mt_raises("iio:ENOSYS", "no_such", 0));
	CHECK(file_is(path, six));

	/* a command that fails on its counts, and one that ends by running out of memory */
	CHECK(mt_raises("iio:EINVAL", "t_ok", 1, mt_dbl(1)));
	mock_fail_alloc(1);
	CHECK(mt_call(1, out, "t_ok", 0) == 1);
	CHECK(mock_alloc_failed());
	snprintf(more, sizeof(more), "%s> t_ok\n< t_ok iio:EINVAL\n> t_ok\n", six);
	CHECK(file_is(path, more));

	/* The variable is read when the gateway handles its first call of a load: a change in
	 * between is not seen, and the file is closed at exit. */
	CHECK(unsetenv("IIO_MEX_TRACE") == 0);
	CHECK(mt_dbl_of("t_ok", 0) == 42);
	snprintf(more, sizeof(more), "%s> t_ok\n< t_ok iio:EINVAL\n> t_ok\n> t_ok\n< t_ok ok\n", six);
	CHECK(file_is(path, more));
	mock_exit_matlab();
	CHECK(mt_dbl_of("t_ok", 0) == 42);     /* a new start without the variable: no file access */
	CHECK(file_is(path, more));

	CHECK(unlink(path) == 0);
	CHECK(mt_dbl_of("t_ok", 0) == 42);
	CHECK(access(path, F_OK) != 0);
	CHECK(rmdir(dir) == 0);
	MT_MAIN_RETURN();
}
