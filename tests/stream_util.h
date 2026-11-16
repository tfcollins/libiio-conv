/* SPDX-License-Identifier: MIT */
/* The emu backend reads and writes sample data in files beside the XML, so
 * stream tests work on a copy of the fixture in a fresh temporary directory. */
#ifndef STREAM_UTIL_H
#define STREAM_UTIL_H

#include <dirent.h>
#include <time.h>
#include "test_util.h"

static char fx_dir[512], fx_xml[640];

static inline void fx_path(char *out, size_t len, const char *name)
{
	snprintf(out, len, "%s/%s", fx_dir, name);
}

static inline void fx_write(const char *name, const void *data, size_t len)
{
	char path[768];
	FILE *f;

	fx_path(path, sizeof(path), name);
	f = fopen(path, "wb");
	if (!f || fwrite(data, 1, len, f) != len)
		abort();
	fclose(f);
}

/* returns the file length, or -1 if there is no such file */
static inline long fx_read(const char *name, void *data, size_t cap)
{
	char path[768];
	FILE *f;
	long n;

	fx_path(path, sizeof(path), name);
	f = fopen(path, "rb");
	if (!f)
		return -1;
	n = (long)fread(data, 1, cap, f);
	fclose(f);
	return n;
}

static inline void fx_remove(const char *name)
{
	char path[768];

	fx_path(path, sizeof(path), name);
	remove(path);
}

static inline void fx_setup(const char *xml_src)
{
	static char buf[1 << 16];
	FILE *f = fopen(xml_src, "rb");
	size_t n;

	snprintf(fx_dir, sizeof(fx_dir), "/tmp/iio-conv-test-XXXXXX");
	if (!f || !mkdtemp(fx_dir))
		abort();
	n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	fx_write("fixture.xml", buf, n);
	fx_path(fx_xml, sizeof(fx_xml), "fixture.xml");
}

static inline void fx_teardown(void)
{
	DIR *d = opendir(fx_dir);
	struct dirent *e;

	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.')
			fx_remove(e->d_name);
	if (d)
		closedir(d);
	rmdir(fx_dir);
}

static inline double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

#endif /* STREAM_UTIL_H */
