/* SPDX-License-Identifier: MIT */
/*
 * The test fixture: a private copy of tests/resources/iio_fixture.xml in a
 * fresh temporary directory, where the emu backend finds and leaves its data
 * files, and the ramp that is the only receive data.
 *
 * The ramp, emu-rx_buf0.bin, is 8192 little-endian 16-bit words. For
 * k = 0..4095: I(k) = k - 2048, Q(k) = 2047 - k,
 *   word 2k     = (I & 0x0fff) | ((k & 0xf) << 12)
 *   word 2k + 1 = (Q & 0x0fff) | (((k >> 4) & 0xf) << 12)
 * The four upper bits are junk on purpose, so that converted and raw results
 * differ. emu delivers the bytes of the file as they are, whatever the mask.
 */
#ifndef FIXTURE_UTIL_H
#define FIXTURE_UTIL_H

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIXTURE_RAMP_LEN 4096 /* I/Q pairs in the ramp; k is taken modulo this */

static inline int16_t fixture_ramp_i(unsigned int k)
{
	return (int16_t)((int)(k % FIXTURE_RAMP_LEN) - 2048);
}

static inline int16_t fixture_ramp_q(unsigned int k)
{
	return (int16_t)(2047 - (int)(k % FIXTURE_RAMP_LEN));
}

static inline uint16_t fixture_ramp_word_i(unsigned int k)
{
	k %= FIXTURE_RAMP_LEN;
	return (uint16_t)(((unsigned int)fixture_ramp_i(k) & 0x0fff) | ((k & 0xf) << 12));
}

static inline uint16_t fixture_ramp_word_q(unsigned int k)
{
	k %= FIXTURE_RAMP_LEN;
	return (uint16_t)(((unsigned int)fixture_ramp_q(k) & 0x0fff) | (((k >> 4) & 0xf) << 12));
}

static inline int fixture_path(const char *dir, const char *name, char *path)
{
	int n = snprintf(path, PATH_MAX, "%s/%s", dir, name);

	return n < 0 || n >= PATH_MAX ? -ENAMETOOLONG : 0;
}

/* Bytes read (at most max), or a negative errno */
static inline long fixture_read_file(const char *dir, const char *name, void *dst, size_t max)
{
	char path[PATH_MAX];
	FILE *f;
	long n;
	int ret = fixture_path(dir, name, path);

	if (ret)
		return ret;
	f = fopen(path, "rb");
	if (!f)
		return -errno;
	n = (long)fread(dst, 1, max, f);
	if (ferror(f))
		n = -EIO;
	fclose(f);
	return n;
}

static inline int fixture_write_file(const char *dir, const char *name, const void *src,
		size_t len)
{
	char path[PATH_MAX];
	FILE *f;
	int ret = fixture_path(dir, name, path);

	if (ret)
		return ret;
	f = fopen(path, "wb");
	if (!f)
		return -errno;
	if (fwrite(src, 1, len, f) != len)
		ret = -EIO;
	if (fclose(f) && !ret)
		ret = -EIO;
	return ret;
}

/* Removes every regular file in dir, then dir */
static inline void fixture_cleanup(const char *dir)
{
	char path[PATH_MAX];
	struct dirent *e;
	struct stat st;
	DIR *d = opendir(dir);

	while (d && (e = readdir(d))) {
		if (!fixture_path(dir, e->d_name, path) && !stat(path, &st) && S_ISREG(st.st_mode))
			unlink(path);
	}
	if (d)
		closedir(d);
	rmdir(dir);
}

/* "<backend>:<dir>/iio_fixture.xml" */
static inline void fixture_uri(const char *dir, const char *backend, char *uri, size_t len)
{
	snprintf(uri, len, "%s:%s/iio_fixture.xml", backend, dir);
}

/* mkdtemp() under $TMPDIR (default /tmp), copy xml_src there as
 * iio_fixture.xml and write emu-rx_buf0.bin (the ramp). dir needs PATH_MAX
 * bytes. Returns 0 or a negative errno; on failure nothing is left behind. */
static inline int fixture_setup(const char *xml_src, char *dir)
{
	static unsigned char ramp[4 * FIXTURE_RAMP_LEN];
	const char *tmp = getenv("TMPDIR");
	unsigned char *xml = NULL;
	unsigned int k;
	long len = 0;
	FILE *f;
	int ret;

	if (!tmp || !*tmp)
		tmp = "/tmp";
	ret = snprintf(dir, PATH_MAX, "%s/iio-conv-fixture-XXXXXX", tmp);
	if (ret < 0 || ret >= PATH_MAX)
		return -ENAMETOOLONG;

	f = fopen(xml_src, "rb");
	if (!f)
		return -errno;
	if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
		ret = -EIO;
	else if (!(xml = malloc((size_t)len + 1)))
		ret = -ENOMEM;
	else
		ret = fread(xml, 1, (size_t)len, f) == (size_t)len ? 0 : -EIO;
	fclose(f);
	if (ret) {
		free(xml);
		return ret;
	}

	if (!mkdtemp(dir)) {
		ret = -errno;
		free(xml);
		return ret;
	}

	for (k = 0; k < FIXTURE_RAMP_LEN; k++) {
		ramp[4 * k] = (unsigned char)fixture_ramp_word_i(k);
		ramp[4 * k + 1] = (unsigned char)(fixture_ramp_word_i(k) >> 8);
		ramp[4 * k + 2] = (unsigned char)fixture_ramp_word_q(k);
		ramp[4 * k + 3] = (unsigned char)(fixture_ramp_word_q(k) >> 8);
	}

	ret = fixture_write_file(dir, "iio_fixture.xml", xml, (size_t)len);
	if (!ret)
		ret = fixture_write_file(dir, "emu-rx_buf0.bin", ramp, sizeof(ramp));
	free(xml);
	if (ret)
		fixture_cleanup(dir);
	return ret;
}

#endif /* FIXTURE_UTIL_H */
