# libiio-conv: SIMD buffer conversion for libiio v1 + MATLAB v1 bindings

## Context

Getting samples between an IIO buffer and user code is slow today, worst of all in MATLAB:

- libiio converts **one sample at a time** in scalar byte-wise C: `iio_channel_convert` /
  `iio_channel_read` (`channel.c` in libiio) — memcpy, byte-loop shift, sign-extend,
  per sample, through a function pointer. It never produces floats, complex, or scaled values.
- The MATLAB toolboxes finish the job in M-code: `TransceiverToolbox/+adi/+common/Rx.m:80-86`
  builds complex doubles column by column, `Rx.m:105-107` does a `calllib` **per sample**, and
  `Tx.m:47-50` interleaves I/Q with strided assignments. They sit on MathWorks'
  `matlabshared.libiio` (libiio v0), which already copies each buffer into a MATLAB array.

Goal: a new project with two parts:

1. **libiio-conv** — a C library layered on libiio v1 that converts whole blocks at once with
   SIMD, including de/interleaving, complex output, type change and scaling.
2. **MATLAB bindings for libiio v1** — a MEX gateway plus M classes covering the full v1 API,
   whose data path uses libiio-conv to write straight into MATLAB arrays (and read straight out
   of them on Tx), so MATLAB does no conversion and no extra copy.

Decisions made with the user: layer on libiio v1 (takes `iio_block` / `iio_channel`); new MATLAB
bindings for v1; MEX gateway + M classes; full v1 API; all four scaling modes.

My calls (not yet confirmed, change freely): C11 + CMake; runtime CPU dispatch; one repo for both
parts; library first, bindings second; porting `adi.common.Rx/Tx` in the toolboxes is a
follow-on project, not part of this one.

## Approach

### Part 1 — C library `libiio-conv`

**Public API** (`include/iio/iio-conv.h`), plan/execute style so per-block cost is only the kernel:

- `iio_conv_spec`: host type (`i8/u8/i16/u16/i32/u32/i64/u64/f32/f64`), `complex` flag (pairs
  two channels I,Q into one interleaved-complex column), layout (`columns` = one contiguous
  column per channel, MATLAB/Fortran order; or `interleaved` rows, C/NumPy order), scale mode,
  optional per-channel user gain/offset, saturate-on-write flag.
- `iio_conv_create(stream_or_mask, channels[], n, spec)` → `struct iio_conv *`. Resolves each
  channel's `iio_data_format` (length, bits, shift, signed, is_be, repeat, scale, offset),
  byte offset in the sample and sample step (`iio_device_get_sample_size`), then picks one
  kernel per channel. Errors returned libiio-style (negative errno / `iio_err`).
- `iio_conv_read(conv, block, dst, dst_len)` — block → host array.
- `iio_conv_write(conv, block, src, n_samples)` — host array → block (inverse: scale, round,
  saturate, mask, shift, byte-swap).
- `iio_conv_destroy`, `iio_conv_get_backend_name()` (reports `avx512`/`avx2`/`neon`/`scalar`),
  env override `IIO_CONV_FORCE=scalar|avx2|...` for testing.
- Raw-pointer variants `iio_conv_read_raw(conv, src, n, dst)` / `write_raw` used internally and
  by tests/benchmarks without hardware.

**Scaling modes** (all four selected), folded into a single `out = (raw + offset) * gain`:

| mode | gain / offset |
|---|---|
| `NONE` | cast only (integers stay integers) |
| `IIO` | channel `scale` / `offset` from `iio_data_format` (physical units) |
| `FULLSCALE` | `1 / 2^(bits-1)` (signed) or `1 / 2^bits` (unsigned); output in [-1, 1) |
| `USER` | caller's per-channel gain/offset, composable with IIO or FULLSCALE |

Float targets only for scaled modes; Tx inverse rounds to nearest and saturates to the
channel's `bits`.

**Kernels** (`src/kernels/`): written once as a generic template over lane width, instantiated
per ISA in separate translation units compiled with the matching flags:

- x86-64: AVX-512 (F/BW/DQ), AVX2, SSE4.1 baseline; AArch64: NEON (Zynq MPSoC/RFSoC targets);
  portable scalar reference that defines correct behaviour.
- Dispatch chosen once at `iio_conv_create` via `__builtin_cpu_supports` / `getauxval`.
- Fast paths in priority order: (1) **16-bit storage → int16 / single / double, real and
  complex** with step 2/4/8/16 bytes (AD9361, ADRV9009, AD9081 class) — the case that matters;
  (2) 8-bit and 32-bit storage; (3) byte-swapped (big-endian) variants via shuffle; (4) anything
  else (odd lengths, `repeat > 1`, 64-bit, float storage) falls back to scalar, still batched.
- Complex with adjacent I/Q channels and no shift is a deinterleave + widen + scale in one pass;
  with `NONE` scaling and matching type it degenerates to a strided copy (or plain `memcpy` when
  the block layout already equals the output layout).
- Non-temporal stores for large outputs; no alignment requirement on caller buffers.

### Part 2 — MATLAB bindings for libiio v1

- **One MEX gateway** `iio_mex` (C, R2018a interleaved-complex API, `-R2018a`): string command
  dispatch, a handle table mapping `uint64` ids → libiio pointers with type tags and
  parent/child tracking, `mexAtExit` + `mexLock` cleanup, libiio errors turned into
  `iio:<errno-name>` MATLAB exceptions.
- **M classes** in package `+iio/` (handle classes, destructors release handles), mirroring the
  full public `iio.h` surface (133 functions): `Context`, `Scan`, `Device`, `Channel`, `Attr`,
  `ChannelsMask`, `Buffer`, `BufferStream`, `Block`, `Stream`, `EventStream`/`Event`, plus
  trigger, hwmon, debug-attr, register read/write, library/backend queries. Naming follows the
  Python/C# bindings in libiio's `bindings/` for consistency and later upstreaming to
  `libiio/bindings/matlab`.
- **Zero-copy data path**: `Buffer.read(...)` / `Stream.read(...)` take `'OutputDataType'`
  (`double|single|int16|...`), `'Complex'`, `'Scale'` (`none|iio|fullscale` + optional gain).
  The MEX creates the output with `mxCreateUninitNumericMatrix(N, M, class, mxCOMPLEX|mxREAL)`
  and passes its data pointer as `dst` to `iio_conv_read` with `columns` layout — MATLAB's
  native N-by-M (interleaved-complex) array is filled directly from the block. `write(data)`
  passes `mxGetData(prhs)` straight to `iio_conv_write`. Converter objects are cached per
  buffer keyed on the option set.
- **Code generation**: a `coder.ExternalDependency` class so `coder.ceval` calls `iio_conv_*`
  and libiio directly in generated code (the toolboxes need codegen); same semantics as MEX.
- Build with CMake `find_package(Matlab)` / `matlab_add_mex`.

### Repo layout

```
CMakeLists.txt            cmake/ (ISA flags, FindLibiio>=1.0, Matlab)
include/iio/iio-conv.h
src/        conv.c plan.c dispatch.c kernels/{scalar,sse41,avx2,avx512,neon}.c kernel_tmpl.h
tests/      unit (no hardware) + libiio-backed
bench/      throughput benchmarks vs iio_channel_read
matlab/     mex/iio_mex.c  +iio/*.m  tests/  examples/
docs/       design spec, API notes
```

Dependency: libiio **v1** — the system has only v0.26 (`/usr/lib`, `/usr/local/lib`). Build v1
from upstream `main` into a project-local prefix (`deps/` script or CMake `FetchContent`), not
over the system install and not from the local `feature/daqiri-backend` checkout.

## Milestones

1. **Scaffold** — `git init`, CMake, libiio v1 dependency build, CI skeleton, design spec in
   `docs/` (written from this plan; reviewed before code).
2. **Scalar reference + tests** — plan/execute API, all formats and scale modes, read and
   write, exhaustive unit tests. This is the correctness oracle.
3. **SIMD kernels** — AVX2 first, then AVX-512, SSE4.1, NEON; each must match scalar
   bit-for-bit (integers) / to 1 ULP-or-exact (floats, same operation order).
4. **Benchmarks** — vs `iio_channel_read` + scalar post-processing; record results in `docs/`.
5. **MEX gateway + core classes** — Context/Device/Channel/Attr, then Buffer/Block/Stream with
   the zero-copy read/write.
6. **Remaining v1 API** — scan, events, triggers, hwmon, debug/register access.
7. **Codegen support + examples + docs**; hardware validation.

Tests are written first per milestone (TDD); detailed task breakdown via the writing-plans
workflow once this is approved.

## Verification

- **Unit (no hardware)**: randomized property tests over `iio_data_format` (length 8–64, bits,
  shift, signed, endianness, repeat, step, channel offset) comparing every SIMD backend against
  scalar, and scalar against libiio's own `iio_channel_convert` / `_inverse` for the raw-integer
  path. Round-trip `write` → `read`. Edge cases: lengths not a multiple of the lane width,
  unaligned pointers, saturation limits, zero-length. Run under ASan/UBSan; force each backend
  with `IIO_CONV_FORCE`. NEON via `qemu-aarch64` or an ARM board.
- **libiio-backed**: run against libiio's emulated/XML or local test context where available,
  else `iio-emu`, to cover `iio_conv_create` from real channel metadata and block enqueue/dequeue.
- **Benchmarks**: MS/s for 2/4/8-channel 16-bit complex → int16 / single / double, each backend
  vs. baseline; target a clear multiple over `iio_channel_read` and near memory-bandwidth for
  the int16 copy case.
- **MATLAB**: `matlab.unittest` suite run with R2025b (`matlab -batch`) — API tests against the
  emulated context; data tests asserting class/size/complexity and values equal to the C
  reference; timing comparison against the existing `adi.common.Rx` M-code path; leak check by
  create/destroy loops; `codegen` smoke test of a small Rx function.
- **Hardware**: one Rx capture and one Tx→Rx loopback on a lab board (Pluto or an FMCOMMS /
  ADRV9009 rig) comparing MATLAB output
  against pyadi-iio for the same capture.

## Out of scope

- Changing libiio itself or upstreaming (layout is kept upstream-friendly).
- Porting TransceiverToolbox / HighSpeedConverterToolbox to the new bindings (follow-on).
- Python/pyadi-iio integration, GPU paths, Windows/macOS packaging (Linux x86-64 + AArch64 first).

## Pinned details (added after approval, from checking upstream libiio)

- libiio v1 is pinned to upstream `analogdevicesinc/libiio` commit
  `cee4310e47198ba2d20e55b482061e3ab30d35d2` (main, 2026-09-23): 134 public functions,
  `iio_buffer_open` / `iio_buffer_stream_create_block`, `emu:` backend behind
  `WITH_EMU_BACKEND`. `iio_data_format` there has `offset` and `is_float`.
- The conversion core takes plain channel descriptors and has no libiio dependency; a thin
  adapter builds descriptors from `iio_channel` + `iio_channels_mask`. `iio_conv_create`
  takes the mask and channels (the device comes from the first channel).
- One affine form everywhere: read `out = raw * a + b`, write `raw = (in - b) * (1/a)`, with
  `a = gain * user_gain` and `b = offset * gain * user_gain + user_offset`, evaluated in the
  destination precision without FMA contraction, so every SIMD backend equals scalar exactly.
- Write from float: round to nearest (ties to even), NaN becomes 0, always saturate to the
  channel's `bits` range. Integer narrowing saturates when `saturate` is set, else wraps.
- Not supported in the first version (returns `-ENOTSUP`): storage lengths other than
  8/16/32/64 bits, `repeat > 1`, float storage (`is_float`), scaled modes with an integer
  host type (`-EINVAL`).
- This machine has `aarch64-linux-gnu-gcc` but no `qemu-aarch64`: NEON is compile-checked here
  and its tests run on an ARM board.
- Two implementation plans: the C library first (`docs/superpowers/plans/`), the MATLAB
  bindings second, written once the C API has settled.

## Changes after the C library landed (2026-11-15)

- The code is public at `github.com/tfcollins/libiio-conv` under the MIT licence, the same
  licence as libiio v1's own sources.
- MATLAB is tested in GitHub Actions on hosted runners (`matlab-actions/setup-matlab`), which
  provide MATLAB to public repositories without a licence. No local MATLAB is assumed.
  The C library is tested there too, on x86-64 and on AArch64 runners (NEON).
- NEON has been verified on a Cortex-A72; see `docs/benchmarks.md`.
- `iio_conv_read` takes the host array's row count, not a byte count (review fix).
