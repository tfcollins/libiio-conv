/* SPDX-License-Identifier: MIT */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int test_failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		if (test_failures++ < 20) \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
	} } while (0)

#define TEST_MAIN_RETURN() do { \
	if (test_failures) fprintf(stderr, "%d check(s) failed\n", test_failures); \
	else printf("ok\n"); \
	return test_failures ? 1 : 0; } while (0)

/* xorshift64* */
static inline uint64_t rng_next(uint64_t *state)
{
	uint64_t x = *state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static inline uint64_t rng_range(uint64_t *state, uint64_t n) /* 0 .. n-1 */
{
	return rng_next(state) % n;
}

/* Buffer whose last byte is the last byte before an inaccessible page, so
 * any read or write past the end faults. */
static inline void *guard_alloc(size_t bytes)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t body = (bytes + page - 1) / page * page;
	uint8_t *map = mmap(NULL, body + 2 * page, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		abort();
	mprotect(map + page + body, page, PROT_NONE);
	memcpy(map, &body, sizeof(body));
	return map + page + body - bytes;
}

static inline void guard_free(void *ptr, size_t bytes)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t body = (bytes + page - 1) / page * page;
	munmap((uint8_t *)ptr + bytes - body - page, body + 2 * page);
}

#endif /* TEST_UTIL_H */
