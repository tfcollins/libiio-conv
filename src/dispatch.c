/* SPDX-License-Identifier: MIT */
/* Choice of the kernel backend for the running CPU. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool cpu_has(const char *name)
{
#if defined(__x86_64__) || defined(__i386__)
	if (!strcmp(name, "avx512"))
		return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
			__builtin_cpu_supports("avx512dq") && __builtin_cpu_supports("avx512vl");
	if (!strcmp(name, "avx2"))
		return __builtin_cpu_supports("avx2");
	if (!strcmp(name, "sse41"))
		return __builtin_cpu_supports("sse4.1");
#elif defined(__aarch64__)
	if (!strcmp(name, "neon"))
		return true; /* Advanced SIMD is mandatory on AArch64 */
#endif
	return !strcmp(name, "scalar");
}

/* Best first */
static const struct conv_backend *const backends[] = {
#ifdef IIO_CONV_HAVE_AVX512
	&conv_backend_avx512,
#endif
#ifdef IIO_CONV_HAVE_AVX2
	&conv_backend_avx2,
#endif
#ifdef IIO_CONV_HAVE_SSE41
	&conv_backend_sse41,
#endif
#ifdef IIO_CONV_HAVE_NEON
	&conv_backend_neon,
#endif
	&conv_backend_scalar,
};

#define NB_BACKENDS (sizeof(backends) / sizeof(backends[0]))

const struct conv_backend *conv_backend_by_name(const char *name)
{
	unsigned int i;

	if (!name)
		return NULL;
	for (i = 0; i < NB_BACKENDS; i++)
		if (!strcmp(backends[i]->name, name) && cpu_has(name))
			return backends[i];
	return NULL;
}

const struct conv_backend *conv_dispatch(void)
{
	static const struct conv_backend *chosen;
	const struct conv_backend *be = __atomic_load_n(&chosen, __ATOMIC_ACQUIRE);
	const char *force;
	unsigned int i;

	if (be)
		return be;

	force = getenv("IIO_CONV_FORCE");
	if (force && *force) {
		/* an unknown or unsupported name selects the scalar backend */
		be = conv_backend_by_name(force);
		if (!be)
			be = &conv_backend_scalar;
	} else {
		for (i = 0; i < NB_BACKENDS && !be; i++)
			if (cpu_has(backends[i]->name))
				be = backends[i];
	}

	__atomic_store_n(&chosen, be, __ATOMIC_RELEASE);
	return be;
}

const char *iio_conv_get_backend_name(void)
{
	return conv_dispatch()->name;
}
