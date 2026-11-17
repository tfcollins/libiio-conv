/* SPDX-License-Identifier: MIT */
/* G2: the MEX file loads into a plain process against the real libmx/libmex (run with
 * LD_LIBRARY_PATH on MATLAB's libraries), exports the two entry points with the R2018a API
 * version, and keeps libiio loaded after it is unloaded itself. Plain C, no sanitizer.
 * argv[1] = path of iio_mex.mexa64. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Prints the executable mappings of libxml2, libzstd and libiio (which copies were bound)
 * and says whether libiio.so.1 is mapped at all. */
static bool scan_maps(bool print)
{
	char line[4096];
	bool iio = false;
	FILE *f = fopen("/proc/self/maps", "r");

	if (!f)
		return false;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "/libiio.so.1"))
			iio = true;
		if (print && strstr(line, " r-xp ") && (strstr(line, "/libxml2") ||
				strstr(line, "/libzstd") || strstr(line, "/libiio.so")))
			fputs(line, stdout);
	}
	fclose(f);
	return iio;
}

int main(int argc, char **argv)
{
	void (*version)(unsigned int *built_by_rel, unsigned int *target_api_ver);
	unsigned int built_by_rel = 0, target_api_ver = 0;
	int fail = 0;
	void *h;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <mex file>\n", argv[0]);
		return 2;
	}
	h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
		return 1;
	}
	if (!dlsym(h, "mexFunction")) {
		fprintf(stderr, "FAIL: no mexFunction\n");
		fail = 1;
	}
	*(void **)&version = dlsym(h, "mexfilerequiredapiversion");
	if (!version) {
		fprintf(stderr, "FAIL: no mexfilerequiredapiversion\n");
		fail = 1;
	} else {
		version(&built_by_rel, &target_api_ver);
		printf("built by release 0x%x, target API version 0x%08x\n", built_by_rel, target_api_ver);
		if (target_api_ver != 0x08000000) {
			fprintf(stderr, "FAIL: target API version is not 0x08000000 (R2018a)\n");
			fail = 1;
		}
	}
	if (!scan_maps(true)) {
		fprintf(stderr, "FAIL: libiio.so.1 is not mapped while the MEX file is loaded\n");
		fail = 1;
	}
	if (dlclose(h)) {
		fprintf(stderr, "FAIL: dlclose: %s\n", dlerror());
		fail = 1;
	}
	if (!scan_maps(false)) {
		fprintf(stderr, "FAIL: libiio.so.1 was unloaded with the MEX file (the pin is missing)\n");
		fail = 1;
	}
	if (!fail)
		printf("ok: %s\n", argv[1]);
	return fail;
}
