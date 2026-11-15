# libiio-conv C Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A C library that converts whole IIO blocks to and from host arrays (type change, I/Q pairing, layout, scaling) with runtime-selected SIMD kernels.

**Architecture:** A libiio-free core turns per-channel descriptors plus a spec into a plan (one kernel + affine constants per channel) and executes it on raw pointers. A thin adapter builds descriptors from libiio v1 channels and masks and maps blocks to raw pointers. Kernels are written once in a template header and instantiated per instruction set in separate translation units; the scalar instance is the correctness oracle.

**Tech Stack:** C11, CMake ≥ 3.20 + Ninja, GCC 13, CTest, libiio v1 (pinned upstream commit), AVX-512/AVX2/SSE4.1/NEON intrinsics.

**Spec:** `docs/superpowers/specs/2026-11-15-libiio-conv-design.md` (Part 1 and "Pinned details"). Part 2 (MATLAB bindings) gets its own plan after this one lands.

## Global Constraints

- libiio v1 = upstream `analogdevicesinc/libiio` commit `cee4310e47198ba2d20e55b482061e3ab30d35d2`, built into `deps/install` inside the repo. Never link the system libiio (v0.26) and never install outside the repo.
- Core files (`src/plan.c`, `src/dispatch.c`, `src/kernels/*`) must not include `<iio/iio.h>`.
- Kernel translation units compile with `-ffp-contract=off`; every backend must equal scalar exactly (`memcmp`) for all outputs.
- Read: `out = raw * a + b`. Write: `raw = (in - b) * inv_a`, `inv_a = 1.0 / a`. `a = gain * user_gain`, `b = offset * gain * user_gain + user_offset`, computed in `double` at plan time, then cast to the destination precision.
- Scale gains: `NONE` gain 1, offset 0; `IIO` gain `scale` if `with_scale` else 1, offset = format `offset`; `FULLSCALE` gain `1 / 2^(bits-1)` signed, `1 / 2^bits` unsigned, offset 0.
- Write from float: nearest, ties to even; NaN → 0; always saturate to the `bits` range. Integer narrowing (either direction): saturate if `spec.saturate`, else wrap.
- Unsupported → `-ENOTSUP`: length not in {8,16,32,64}, `repeat > 1`, `is_float`. Scaled (`a != 1 || b != 0`) with integer host type → `-EINVAL`. `complex_iq` with odd channel count → `-EINVAL`.
- No alignment requirement on caller buffers. All sanitizer runs (`-fsanitize=address,undefined`) must be clean.
- Errors are negative errno return values; no global state except the cached backend choice.
- Commits: `git commit -s`, local only (no remote yet).

## Review Focus

- A caller passes channels in a different order than their scan index (e.g. Q before I): output column k must be channel k as passed, not scan order.
- A block whose byte length is not a multiple of the sample step (short read): convert only whole samples and report that count.
- `dst` capacity smaller than the block: convert only what fits, never write past `dst_bytes`.
- Sample counts of 0 and 1 through 2× the widest lane count: tails must be handled with no over-read past `src + n*step` (matters when the buffer ends at a page boundary).
- A mask with extra enabled channels that the caller did not ask for (wider step, gaps between requested channels): offsets and step must still be right.

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt`, `cmake/IsaFlags.cmake` | targets, per-file ISA flags, options `IIO_CONV_WITH_LIBIIO`, `IIO_CONV_SANITIZE` |
| `deps/build-libiio.sh` | fetch + build pinned libiio v1 into `deps/install` |
| `include/iio/iio-conv.h` | whole public API |
| `src/internal.h` | `struct chan_plan`, `struct iio_conv`, kernel table type |
| `src/plan.c` | validate descriptors/spec, compute constants, choose kernels, execute raw read/write |
| `src/dispatch.c` | CPU detection, `IIO_CONV_FORCE`, backend name |
| `src/kernels/kernel_tmpl.h` | generic strided kernels (all types), compiled per backend |
| `src/kernels/{scalar,sse41,avx2,avx512,neon}.c` | backend instance + its intrinsic fast paths, exports one `const struct conv_backend` |
| `src/iio_adapter.c` | libiio-facing `iio_conv_create/read/write` |
| `tests/test_util.h` | `CHECK`, RNG, guard-page allocator |
| `tests/test_*.c` | one CTest executable each |
| `bench/bench_conv.c` | throughput vs per-sample `iio_channel_convert` |

## Public API (produced by Tasks 1–3, used by all later tasks)

```c
enum iio_conv_type { IIO_CONV_I8, IIO_CONV_U8, IIO_CONV_I16, IIO_CONV_U16, IIO_CONV_I32,
                     IIO_CONV_U32, IIO_CONV_I64, IIO_CONV_U64, IIO_CONV_F32, IIO_CONV_F64 };
enum iio_conv_layout { IIO_CONV_COLUMNS, IIO_CONV_INTERLEAVED };
enum iio_conv_scale { IIO_CONV_SCALE_NONE, IIO_CONV_SCALE_IIO, IIO_CONV_SCALE_FULLSCALE };

struct iio_conv_spec {
	enum iio_conv_type type;
	enum iio_conv_layout layout;
	enum iio_conv_scale scale;
	bool complex_iq;            /* channels 2k, 2k+1 become re, im of column k */
	bool saturate;
	const double *user_gain;    /* per channel, NULL = all 1.0 */
	const double *user_offset;  /* per channel, NULL = all 0.0 */
};

struct iio_conv_chan {          /* one requested channel, storage side */
	unsigned int length, bits, shift;   /* bits */
	bool is_signed, is_be, with_scale;
	double scale, offset;
	size_t byte_offset;                 /* within one sample of `step` bytes */
};

struct iio_conv;

int iio_conv_create_raw(const struct iio_conv_chan *chans, unsigned int nb_chans,
			size_t step, const struct iio_conv_spec *spec, struct iio_conv **conv);
void iio_conv_destroy(struct iio_conv *conv);
size_t iio_conv_get_step(const struct iio_conv *conv);         /* storage bytes / sample */
size_t iio_conv_get_host_step(const struct iio_conv *conv);    /* host bytes / sample, all columns */
void iio_conv_read_raw(const struct iio_conv *conv, const void *src, size_t nb_samples, void *dst);
void iio_conv_write_raw(const struct iio_conv *conv, void *dst, size_t nb_samples, const void *src);
const char *iio_conv_get_backend_name(void);  /* "avx512" | "avx2" | "sse41" | "neon" | "scalar" */

/* libiio adapter (only when built with IIO_CONV_WITH_LIBIIO) */
int iio_conv_create(const struct iio_channels_mask *mask, const struct iio_channel *const *chans,
		    unsigned int nb_chans, const struct iio_conv_spec *spec, struct iio_conv **conv);
ssize_t iio_conv_read(const struct iio_conv *conv, const struct iio_block *block,
		      void *dst, size_t dst_bytes);     /* returns samples converted */
ssize_t iio_conv_write(const struct iio_conv *conv, struct iio_block *block,
		       const void *src, size_t nb_samples); /* returns samples written */
```

Host layout for `nb_samples` = N, C columns (C = `nb_chans`, or `nb_chans/2` when `complex_iq`), element size E (type size, ×2 when complex): `COLUMNS` puts column c at `dst + c*N*E`; `INTERLEAVED` puts sample i, column c at `dst + (i*C + c)*E`. `write_raw` zero-fills nothing: it writes only the requested channels' bytes and leaves other bytes of `dst` untouched; within a channel's storage word, bits outside `bits << shift` are written as 0.

Internal (in `src/internal.h`):

```c
struct chan_plan {
	unsigned int length, bits, shift; bool is_signed, swap, saturate, affine;
	size_t src_off, dst_off, dst_stride;   /* bytes; dst_off for sample 0 */
	double a, b, inv_a; enum iio_conv_type type;
};
typedef void (*conv_rd_fn)(const uint8_t *src, size_t step, uint8_t *dst, size_t n, const struct chan_plan *p);
typedef void (*conv_wr_fn)(uint8_t *dst, size_t step, const uint8_t *src, size_t n, const struct chan_plan *p);
struct conv_backend {
	const char *name;
	conv_rd_fn (*pick_rd)(const struct chan_plan *p);   /* NULL = no fast path, use scalar */
	conv_wr_fn (*pick_wr)(const struct chan_plan *p);
};
extern const struct conv_backend conv_backend_scalar;  /* never returns NULL */
const struct conv_backend *conv_dispatch(void);        /* honours IIO_CONV_FORCE */
```

`src`/`dst` in the kernel typedefs already include the channel's `src_off`/`dst_off`; for `rd`, `dst` advances by `p->dst_stride`, for `wr` the host `src` advances by `p->dst_stride`.

---

### Task 1: Scaffold, pinned libiio v1, test harness

**Files:**
- Create: `CMakeLists.txt`, `cmake/IsaFlags.cmake`, `deps/build-libiio.sh`, `.gitignore`, `README.md`, `include/iio/iio-conv.h`, `src/internal.h`, `src/dispatch.c`, `src/plan.c` (stubs), `src/kernels/scalar.c` (stub), `tests/CMakeLists.txt`, `tests/test_util.h`, `tests/test_smoke.c`

**Interfaces:**
- Produces: the public header above in full; library target `iio-conv` (shared + static alias `iio-conv-static` for tests); `tests/test_util.h` with `CHECK(cond)` (prints file:line, sets failure flag), `TEST_MAIN_RETURN()`, `uint64_t rng_next(uint64_t *state)` (xorshift64*), and `void *guard_alloc(size_t bytes)` / `guard_free` that place the buffer so its last byte is the last byte before a `PROT_NONE` page.

- [ ] **Step 1:** Write `deps/build-libiio.sh`: clone the pinned commit into `deps/src/libiio`, configure with `-DCMAKE_INSTALL_PREFIX=$repo/deps/install -DWITH_EMU_BACKEND=ON -DWITH_EMU_BACKEND_DYNAMIC=OFF -DWITH_XML_BACKEND=ON -DWITH_USB_BACKEND=OFF -DWITH_SERIAL_BACKEND=OFF -DWITH_IIOD=OFF -DWITH_UTILS=OFF -DWITH_EXAMPLES=OFF -DWITH_DOC=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo`, build, install. Idempotent (skip if `deps/install/include/iio/iio.h` exists and the checked-out commit matches). `.gitignore`: `build*/`, `deps/src/`, `deps/install/`.
- [ ] **Step 2:** Run `deps/build-libiio.sh`. Expected: `deps/install/lib*/libiio.so.1*` exists and `grep -c iio_buffer_open deps/install/include/iio/iio.h` ≥ 1.
- [ ] **Step 3:** Write `tests/test_smoke.c`: `CHECK(strcmp(iio_conv_get_backend_name(), "scalar") == 0)` with `IIO_CONV_FORCE=scalar` set via `setenv` before the first call; `CHECK(iio_conv_create_raw(NULL, 0, 0, NULL, &c) == -EINVAL)`.
- [ ] **Step 4:** Write CMake: C11, `-Wall -Wextra -Werror=implicit-function-declaration`, option `IIO_CONV_SANITIZE`, option `IIO_CONV_WITH_LIBIIO` (default ON; finds libiio only under `deps/install` via `find_library`/`find_path` with `NO_DEFAULT_PATH`, sets build RPATH). `cmake/IsaFlags.cmake` provides `iio_conv_add_backend(name flags...)` which adds `src/kernels/<name>.c` with those flags plus `-ffp-contract=off` and defines `IIO_CONV_HAVE_<NAME>`. Stubs so the smoke test links; `iio_conv_create_raw` returns `-EINVAL` on NULL args.
- [ ] **Step 5:** Run `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`. Expected: `test_smoke` passes.
- [ ] **Step 6:** Commit `build: scaffold, pinned libiio v1 dependency, test harness`.

### Task 2: Plan creation and validation

**Files:**
- Modify: `src/plan.c`, `src/internal.h`
- Test: `tests/test_plan.c`

**Interfaces:**
- Consumes: public header, `conv_backend_scalar` (stub pickers may return a no-op kernel for now).
- Produces: `struct iio_conv { unsigned nb_chans; size_t step, host_step; struct chan_plan *chans; conv_rd_fn *rd; conv_wr_fn *wr; }`; working `iio_conv_create_raw`, `iio_conv_destroy`, `iio_conv_get_step`, `iio_conv_get_host_step`; test-only accessor `const struct chan_plan *iio_conv_test_get_plan(const struct iio_conv *, unsigned chan)` declared in `src/internal.h`.

- [ ] **Step 1:** Write `tests/test_plan.c` with these cases (descriptor `D16` = length 16, bits 12, shift 0, signed, LE, with_scale, scale 0.5, offset 3):
  - errors: length 24 → `-ENOTSUP`; `bits == 0` or `bits + shift > length` → `-EINVAL`; `byte_offset + length/8 > step` → `-EINVAL`; `complex_iq` with 3 channels → `-EINVAL`; `SCALE_IIO` with type `I16` → `-EINVAL`; `SCALE_NONE` + `user_gain = {2.0}` with type `I16` → `-EINVAL`; `nb_chans == 0` → `-EINVAL`.
  - constants: `SCALE_IIO`, F64 → `a == 0.5`, `b == 1.5`; `SCALE_FULLSCALE` → `a == 1.0/2048`, `b == 0`; unsigned 12-bit `FULLSCALE` → `a == 1.0/4096`; `SCALE_IIO` + `user_gain 4`, `user_offset 1` → `a == 2.0`, `b == 7.0`; `SCALE_NONE`, I16 → `affine == false`.
  - layout, 4 channels, F32: `COLUMNS` → `host_step == 16`, chan 2 `dst_stride == 4`; `INTERLEAVED` → chan 2 `dst_off == 8`, `dst_stride == 16`; `complex_iq` `COLUMNS` → `host_step == 16`, chan 1 `dst_off == 4`, `dst_stride == 8`, chan 2 column base is set at execute time (see Task 3) so `dst_off == 0`.
  - `swap` is true exactly when `is_be` differs from host endianness and length > 8.
- [ ] **Step 2:** Run `ctest --test-dir build -R test_plan`. Expected: FAIL (cases not implemented).
- [ ] **Step 3:** Implement in `src/plan.c`. `dst_off` holds the within-sample offset (interleaved) or the re/im offset (columns); the column base `c * N * E` is added in the execute functions because it depends on N.
- [ ] **Step 4:** Run the test. Expected: PASS, also under `-DIIO_CONV_SANITIZE=ON` in `build-asan`.
- [ ] **Step 5:** Commit `feat: conversion plan creation and validation`.

### Task 3: Scalar kernels, read and write

**Files:**
- Create: `src/kernels/kernel_tmpl.h`
- Modify: `src/kernels/scalar.c`, `src/plan.c`
- Test: `tests/test_scalar.c`

**Interfaces:**
- Consumes: `struct chan_plan`, `struct iio_conv`.
- Produces: `conv_backend_scalar` with real kernels for every supported (length, host type) pair; working `iio_conv_read_raw` / `iio_conv_write_raw`. `kernel_tmpl.h` defines, when included with `#define KT_NAME(x) x##_<backend>`, static functions `KT_NAME(rd_generic)` and `KT_NAME(wr_generic)` (switch on length and type outside the sample loop so the compiler can vectorise each inner loop) — later backends reuse it for the cases they have no intrinsic path for.

- [ ] **Step 1:** Write `tests/test_scalar.c`:
  - an independent reference `ref_read_one(const struct iio_conv_chan *, const struct iio_conv_spec *, double ug, double uo, const uint8_t *sample, void *out)` written bit-by-bit (assemble the storage word from bytes honouring `is_be`, `>> shift`, mask to `bits`, sign-extend arithmetically, then cast / affine in the destination precision) — deliberately not sharing code with the library.
  - fixed vectors: 12-bit signed in 16-bit LE, shift 4, bytes `F0 FF` → I16 `-1`; same → F64 `FULLSCALE` `-1.0/2048`; bytes `00 80` → I16 `-2048`; 16-bit BE `12 34` unsigned → U16 `0x1234`; 8-bit unsigned `FF` → F32 `255.0f`; 32-bit signed LE 24 bits shift 8 `00 FF FF FF` → I32 `-1`; I/Q pair `(1, -2)` complex F32 → dst `{1.0f, -2.0f}`.
  - narrowing read: 16-bit value `300` → I8 gives `127` with `saturate`, `44` without; value `-1` signed → U8 gives `0` with `saturate`, `255` without.
  - property test, 2000 random configurations from a fixed seed: 1–8 channels, each length ∈ {8,16,32,64}, random bits/shift/sign/endianness, random non-overlapping `byte_offset`s in a random `step`, random host type/layout/scale (only valid combinations), `complex_iq` when the count is even, N ∈ 0..70, random bytes; compare every output element against `ref_read_one` with `memcmp`.
  - channel order: two channels passed as (offset 2, offset 0) → column 0 holds the offset-2 channel.
  - write fixed vectors: F64 `FULLSCALE` 12-bit shift 4: `0.5` → bytes `00 40`; `1.0` → `F0 7F` (saturated to 2047); `-2.0` → `00 80`; `NaN` → `00 00`; ties: F32 `NONE` `2.5` → raw 2, `3.5` → raw 4; I32 → 12-bit: `5000` → 2047 with `saturate`, `5000 & 0xFFF` without.
  - write leaves other bytes untouched: fill `dst` with `0xAA`, write one 16-bit channel at offset 2 of step 8, check bytes 0,1,4..7 of each sample are still `0xAA`.
  - round trip: for integer host types wide enough for `bits`, `read(write(x)) == x` for random in-range x over the same random configurations.
  - `guard_alloc` both `src` and `dst` so the last sample ends at the guard page, N ∈ {0, 1, 7, 33}.
- [ ] **Step 2:** Run `ctest --test-dir build -R test_scalar`. Expected: FAIL.
- [ ] **Step 3:** Implement `kernel_tmpl.h` and wire `scalar.c` + the execute functions in `plan.c`. Load/store storage words with `memcpy` + `__builtin_bswap*`. Float→int on write: `rint`/`rintf` under the default rounding mode, clamp in the float domain before the integer cast, NaN test first. 64-bit to/from `double` uses the plain C conversion.
- [ ] **Step 4:** Run the test in `build` and `build-asan`. Expected: PASS, no sanitizer reports.
- [ ] **Step 5:** Commit `feat: scalar reference kernels for read and write`.

### Task 4: libiio adapter

**Files:**
- Create: `src/iio_adapter.c`, `tests/resources/conv_test.xml`
- Test: `tests/test_adapter.c`

**Interfaces:**
- Consumes: `iio_conv_create_raw`, `iio_conv_read_raw`, `iio_conv_write_raw`, `iio_conv_get_step`, `iio_conv_get_host_step`.
- Produces: `iio_conv_create`, `iio_conv_read`, `iio_conv_write` as declared in the public header.

- [ ] **Step 1:** Write `tests/resources/conv_test.xml`: one context, device `conv-rx` with scan-element channels `voltage0_i` (index 0, `le:s12/16>>0`), `voltage0_q` (index 1, `le:s12/16>>0`), `voltage1` (index 2, `be:u24/32>>8`), `temp` (index 3, `le:s8/8>>0`), `voltage2` (index 4, `le:s14/16>>2`, scale 0.25), plus device `conv-tx` with two output channels `le:s16/16>>0`. Follow the DTD/attribute spelling in `deps/src/libiio` (`iio.h` XML docs and `tests`/`xml.c`).
- [ ] **Step 2:** Write `tests/test_adapter.c` using `iio_create_context(NULL, "xml:<path>")` (path passed by CTest as argv[1]):
  - all five channels enabled → `iio_conv_get_step(conv) == (size_t)iio_device_get_sample_size(dev, mask)` (expected 12: 2+2, pad to 4, +4, +1, pad to 10, +2); plan `src_off` for the five channels `== {0, 2, 4, 8, 10}`.
  - mask enables all five, request only `{voltage2, voltage0_i}` → step still 12, `src_off == {10, 0}`.
  - mask enables only `{temp, voltage2}` → step 4, `src_off == {0, 2}`.
  - a requested channel not enabled in the mask → `-EINVAL`; channels from two devices → `-EINVAL`; a non-scan-element channel → `-EINVAL`.
  - `voltage2` with `SCALE_IIO`, F64 → plan `a == 0.25`.
  - block path: create an `emu:` context from the same XML; if `iio_device_create_buffer`/`iio_buffer_open`/`iio_buffer_stream_create_block` is unavailable there, exit 77 for that sub-test binary (`test_adapter_block`, registered with `SKIP_RETURN_CODE 77`). Otherwise: fill the block memory via `iio_block_start` with a known ramp, check `iio_conv_read` equals `iio_conv_read_raw` on the same bytes; with `dst_bytes` for 5 samples returns 5 and leaves byte `5*host_step` of `dst` untouched; `iio_conv_write` with more samples than the block holds returns the block capacity.
- [ ] **Step 3:** Run `ctest --test-dir build -R test_adapter`. Expected: FAIL.
- [ ] **Step 4:** Implement `src/iio_adapter.c`. Byte offsets follow libiio's layout rule using public calls only: walk `iio_device_get_channel(dev, i)` in order, skip channels that are not scan elements or not enabled, skip a channel whose index equals the previous enabled one, align the running offset up to the channel's `length/8` before placing it. `iio_conv_read` sample count = `min((end - start) / step, dst_bytes / host_step)`; for `COLUMNS` the host layout uses that count as N.
- [ ] **Step 5:** Run in `build` and `build-asan`. Expected: PASS (block sub-test PASS or SKIP — record which in the commit message).
- [ ] **Step 6:** Commit `feat: libiio v1 adapter (channels mask and block entry points)`.

### Task 5: Dispatch and AVX2 backend

**Files:**
- Create: `src/kernels/avx2.c`, `tests/test_backends.c`
- Modify: `src/dispatch.c`, `src/plan.c`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `kernel_tmpl.h`, `struct conv_backend`.
- Produces: `conv_backend_avx2`; `conv_dispatch()` choosing the best compiled-in backend the CPU supports (`__builtin_cpu_supports`), overridable with `IIO_CONV_FORCE=<name>` (unknown or unsupported name → scalar); plan creation asks the chosen backend's `pick_rd`/`pick_wr` first and falls back to scalar per channel. Test hook `int iio_conv_create_raw_backend(const char *backend, ...same args...)` in `src/internal.h` so one process can build plans for several backends.

- [ ] **Step 1:** Write `tests/test_backends.c`: for each backend name in `{"sse41","avx2","avx512","neon"}` that is compiled in and supported by the CPU (others print `skip <name>`), run the Task 3 random-configuration generator with 5000 configurations weighted 70% toward 16-bit storage, plus an explicit sweep of 16-bit LE/BE × shift {0,2,4} × bits {12,14,16} × signed/unsigned × host {I16,F32,F64} × {real, complex} × both layouts × step {2,4,8,16} × N ∈ 0..130, and the same sweep for write; assert `memcmp` equality with the scalar backend's output for read, and equality of the written storage bytes for write. Guard-page buffers throughout. Also `CHECK` that for the 16-bit signed LE shift-0 → F32 complex `COLUMNS` case `pick_rd` returned non-NULL (the fast path is really in use).
- [ ] **Step 2:** Run. Expected: FAIL to build (`iio_conv_create_raw_backend` undefined).
- [ ] **Step 3:** Implement dispatch, then `avx2.c` (`-mavx2`): intrinsic read paths for 16-bit storage → I16 / F32 / F64 with any shift/bits/sign and optional byte swap (`_mm256_shuffle_epi8`), for source strides 2 (contiguous load) and 4 with contiguous destination (`dst_stride == element size`) or complex destination stride (`2 × element size`); a paired path where the plan detects channel 2k at `src_off` o and 2k+1 at o+2 with step 4 and identical format, filling re/im in one pass. Write paths for F32 / F64 / I16 → 16-bit storage with the same stride cases. Everything else: `pick_*` returns NULL. Tails (N mod lanes) go through the `kernel_tmpl.h` generic instance; never load past `src + n*step`.
- [ ] **Step 4:** Run `test_backends` and the whole suite in `build` and `build-asan`, then again with `IIO_CONV_FORCE=avx2` and `=scalar`. Expected: PASS.
- [ ] **Step 5:** Commit `feat: runtime dispatch and AVX2 kernels for 16-bit storage`.

### Task 6: AVX-512, SSE4.1, NEON backends and wider storage

**Files:**
- Create: `src/kernels/avx512.c`, `src/kernels/sse41.c`, `src/kernels/neon.c`, `cmake/aarch64-toolchain.cmake`
- Modify: `src/kernels/avx2.c`, `src/dispatch.c`, `CMakeLists.txt`, `tests/test_backends.c`

**Interfaces:**
- Consumes/Produces: `conv_backend_avx512` (`-mavx512f -mavx512bw -mavx512dq -mavx512vl`), `conv_backend_sse41` (`-msse4.1`), `conv_backend_neon` (AArch64 only). Dispatch order: avx512, avx2, sse41 on x86-64; neon on AArch64; scalar last.

- [ ] **Step 1:** Extend `tests/test_backends.c` sweeps with 8-bit storage → {I8, U8, I16, F32} and 32-bit storage → {I32, F32, F64}, read and write, and assert `pick_rd` is non-NULL on avx2 and avx512 for 8-bit unsigned → F32 and 32-bit signed → F64.
- [ ] **Step 2:** Run. Expected: FAIL on the new `pick_rd` assertions.
- [ ] **Step 3:** Implement the three backends with the same 16-bit paths as Task 5, and add 8-bit and 32-bit storage paths to avx2 and avx512 (sse41 and neon: 16-bit paths only). Share the per-width body through a macro header `src/kernels/simd16_tmpl.h` where the intrinsic differences allow; do not duplicate the shift/sign/affine sequence by hand four times.
- [ ] **Step 4:** Run the suite in `build` and `build-asan` with `IIO_CONV_FORCE` set to each of `avx512`, `avx2`, `sse41`, `scalar`. Expected: PASS for each (this machine supports all three).
- [ ] **Step 5:** Cross-compile check: `cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -DIIO_CONV_WITH_LIBIIO=OFF && cmake --build build-aarch64`. Expected: builds with no warnings; tests are not run here (no `qemu-aarch64`). Note in `README.md` that NEON tests run with `ctest` on an ARM board.
- [ ] **Step 6:** Commit `feat: AVX-512, SSE4.1 and NEON backends; 8- and 32-bit storage paths`.

### Task 7: Benchmarks and README

**Files:**
- Create: `bench/bench_conv.c`, `bench/CMakeLists.txt`, `docs/benchmarks.md`
- Modify: `README.md`, `CMakeLists.txt` (option `IIO_CONV_BUILD_BENCH`, default OFF)

**Interfaces:**
- Consumes: public API; `iio_channel_convert` from libiio for the baseline.

- [ ] **Step 1:** Write `bench/bench_conv.c`: for 2, 4 and 8 channels of 12-bit-in-16 signed LE (the `conv_test.xml` style, loaded through an `xml:` context so the baseline can use real `iio_channel` handles), N = 2^20 samples, for host ∈ {I16 real, I16 complex, F32 complex, F64 complex} `COLUMNS`: time (best of 9, `clock_gettime(CLOCK_MONOTONIC)`) (a) baseline = per-sample `iio_channel_convert` into an int16 temp, followed by a plain C loop doing the cast/pairing/scale, (b) `iio_conv_read_raw` for each available backend via `IIO_CONV_FORCE`-equivalent hook. Print a table: case, backend, MS/s per channel, speed-up over baseline. Also the Tx direction for F64 complex → 16-bit.
- [ ] **Step 2:** Build with `-DCMAKE_BUILD_TYPE=Release -DIIO_CONV_BUILD_BENCH=ON` in `build-rel`; run `build-rel/bench/bench_conv tests/resources/conv_test.xml`. Expected: table prints; every SIMD backend row is faster than baseline. If any SIMD row is slower than scalar, fix the kernel before continuing.
- [ ] **Step 3:** Paste the table (with CPU model and compiler version) into `docs/benchmarks.md`; write `README.md`: what it is, build steps (`deps/build-libiio.sh`, cmake, ctest), a 20-line usage example for Rx complex single and Tx, the scaling table, supported formats and what returns `-ENOTSUP`, `IIO_CONV_FORCE`.
- [ ] **Step 4:** Commit `bench: throughput benchmarks and README`.
