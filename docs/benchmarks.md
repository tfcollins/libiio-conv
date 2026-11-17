# Benchmarks

Measured with `bench/bench_conv` (Release build, single thread, best of 9
runs, 12-bit signed samples in 16-bit little-endian storage, COLUMNS layout).
Date: 2026-11-15.

"MS/s per channel" is millions of samples per second for each channel while
all channels are being converted. "int16 real" has one column per channel, so
each channel is read with a stride; the complex rows convert adjacent I/Q
channels in one pass. "Tx" is the write direction.

"libiio + C loop" is the same job done with libiio alone:
`iio_channel_convert()` once per sample (what `iio_channel_read()` does
internally) into an int16 temporary, then a plain C loop for the type change,
I/Q pairing and full-scale normalisation. Every row's output was compared
byte for byte against that baseline (or against the scalar backend where
there is no baseline).

## x86-64

- CPU: Intel(R) Xeon(R) W-2295 CPU @ 3.00GHz
- Compiler: gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- libiio: pinned v1 commit, RelWithDebInfo build

### Blocks of 4096 samples (in cache)

`bench_conv -n 12 tests/resources/conv_test.xml`

| channels | host array | backend | MS/s per channel | vs libiio | vs scalar |
|---|---|---|---|---|---|
| 2 | int16 real | libiio + C loop | 40 | 1.0x | - |
| 2 | int16 real | scalar | 687 | 17.1x | 1.0x |
| 2 | int16 real | sse41 | 1247 | 31.0x | 1.8x |
| 2 | int16 real | avx2 | 1438 | 35.7x | 2.1x |
| 2 | int16 real | avx512 | 1446 | 35.9x | 2.1x |
| 2 | int16 complex | libiio + C loop | 40 | 1.0x | - |
| 2 | int16 complex | scalar | 716 | 18.0x | 1.0x |
| 2 | int16 complex | sse41 | 2817 | 70.9x | 3.9x |
| 2 | int16 complex | avx2 | 4276 | 107.6x | 6.0x |
| 2 | int16 complex | avx512 | 6637 | 167.0x | 9.3x |
| 2 | single complex | libiio + C loop | 39 | 1.0x | - |
| 2 | single complex | scalar | 469 | 12.0x | 1.0x |
| 2 | single complex | sse41 | 2113 | 53.9x | 4.5x |
| 2 | single complex | avx2 | 3106 | 79.3x | 6.6x |
| 2 | single complex | avx512 | 4466 | 114.0x | 9.5x |
| 2 | double complex | libiio + C loop | 39 | 1.0x | - |
| 2 | double complex | scalar | 495 | 12.6x | 1.0x |
| 2 | double complex | sse41 | 1315 | 33.5x | 2.7x |
| 2 | double complex | avx2 | 1815 | 46.3x | 3.7x |
| 2 | double complex | avx512 | 2691 | 68.6x | 5.4x |
| 2 | Tx double complex | libiio + C loop | 45 | 1.0x | - |
| 2 | Tx double complex | scalar | 212 | 4.7x | 1.0x |
| 2 | Tx double complex | sse41 | 767 | 16.9x | 3.6x |
| 2 | Tx double complex | avx2 | 1033 | 22.8x | 4.9x |
| 2 | Tx double complex | avx512 | 1704 | 37.6x | 8.0x |
| 4 | int16 real | libiio + C loop | 20 | 1.0x | - |
| 4 | int16 real | scalar | 353 | 17.8x | 1.0x |
| 4 | int16 real | sse41 | 714 | 36.0x | 2.0x |
| 4 | int16 real | avx2 | 715 | 36.0x | 2.0x |
| 4 | int16 real | avx512 | 712 | 35.9x | 2.0x |
| 4 | int16 complex | libiio + C loop | 20 | 1.0x | - |
| 4 | int16 complex | scalar | 350 | 17.8x | 1.0x |
| 4 | int16 complex | sse41 | 1399 | 71.0x | 4.0x |
| 4 | int16 complex | avx2 | 1666 | 84.5x | 4.8x |
| 4 | int16 complex | avx512 | 1865 | 94.6x | 5.3x |
| 4 | single complex | libiio + C loop | 20 | 1.0x | - |
| 4 | single complex | scalar | 212 | 10.8x | 1.0x |
| 4 | single complex | sse41 | 832 | 42.4x | 3.9x |
| 4 | single complex | avx2 | 995 | 50.7x | 4.7x |
| 4 | single complex | avx512 | 1200 | 61.1x | 5.7x |
| 4 | double complex | libiio + C loop | 20 | 1.0x | - |
| 4 | double complex | scalar | 248 | 12.7x | 1.0x |
| 4 | double complex | sse41 | 543 | 27.8x | 2.2x |
| 4 | double complex | avx2 | 659 | 33.8x | 2.7x |
| 4 | double complex | avx512 | 888 | 45.5x | 3.6x |
| 4 | Tx double complex | libiio + C loop | 22 | 1.0x | - |
| 4 | Tx double complex | scalar | 105 | 4.7x | 1.0x |
| 4 | Tx double complex | sse41 | 344 | 15.4x | 3.3x |
| 4 | Tx double complex | avx2 | 437 | 19.5x | 4.2x |
| 4 | Tx double complex | avx512 | 601 | 26.9x | 5.7x |
| 8 | int16 real | libiio + C loop | 10 | 1.0x | - |
| 8 | int16 real | scalar | 159 | 16.0x | 1.0x |
| 8 | int16 real | sse41 | 352 | 35.5x | 2.2x |
| 8 | int16 real | avx2 | 353 | 35.5x | 2.2x |
| 8 | int16 real | avx512 | 353 | 35.5x | 2.2x |
| 8 | int16 complex | libiio + C loop | 10 | 1.0x | - |
| 8 | int16 complex | scalar | 162 | 16.5x | 1.0x |
| 8 | int16 complex | sse41 | 701 | 71.5x | 4.3x |
| 8 | int16 complex | avx2 | 797 | 81.2x | 4.9x |
| 8 | int16 complex | avx512 | 897 | 91.5x | 5.5x |
| 8 | single complex | libiio + C loop | 10 | 1.0x | - |
| 8 | single complex | scalar | 117 | 12.0x | 1.0x |
| 8 | single complex | sse41 | 424 | 43.3x | 3.6x |
| 8 | single complex | avx2 | 523 | 53.4x | 4.5x |
| 8 | single complex | avx512 | 602 | 61.5x | 5.1x |
| 8 | double complex | libiio + C loop | 10 | 1.0x | - |
| 8 | double complex | scalar | 124 | 12.6x | 1.0x |
| 8 | double complex | sse41 | 258 | 26.3x | 2.1x |
| 8 | double complex | avx2 | 362 | 37.0x | 2.9x |
| 8 | double complex | avx512 | 449 | 45.8x | 3.6x |
| 8 | Tx double complex | libiio + C loop | 11 | 1.0x | - |
| 8 | Tx double complex | scalar | 53 | 4.7x | 1.0x |
| 8 | Tx double complex | sse41 | 176 | 15.7x | 3.3x |
| 8 | Tx double complex | avx2 | 193 | 17.1x | 3.6x |
| 8 | Tx double complex | avx512 | 266 | 23.6x | 5.0x |

### Blocks of 2^20 samples (larger than the caches)

`bench_conv tests/resources/conv_test.xml`

| channels | host array | backend | MS/s per channel | vs libiio | vs scalar |
|---|---|---|---|---|---|
| 2 | int16 real | libiio + C loop | 40 | 1.0x | - |
| 2 | int16 real | scalar | 722 | 18.1x | 1.0x |
| 2 | int16 real | sse41 | 1445 | 36.2x | 2.0x |
| 2 | int16 real | avx2 | 1446 | 36.2x | 2.0x |
| 2 | int16 real | avx512 | 1441 | 36.1x | 2.0x |
| 2 | int16 complex | libiio + C loop | 39 | 1.0x | - |
| 2 | int16 complex | scalar | 630 | 16.1x | 1.0x |
| 2 | int16 complex | sse41 | 2581 | 65.9x | 4.1x |
| 2 | int16 complex | avx2 | 3272 | 83.5x | 5.2x |
| 2 | int16 complex | avx512 | 3323 | 84.8x | 5.3x |
| 2 | single complex | libiio + C loop | 38 | 1.0x | - |
| 2 | single complex | scalar | 433 | 11.4x | 1.0x |
| 2 | single complex | sse41 | 1965 | 51.8x | 4.5x |
| 2 | single complex | avx2 | 2134 | 56.3x | 4.9x |
| 2 | single complex | avx512 | 2039 | 53.8x | 4.7x |
| 2 | double complex | libiio + C loop | 36 | 1.0x | - |
| 2 | double complex | scalar | 441 | 12.2x | 1.0x |
| 2 | double complex | sse41 | 1103 | 30.5x | 2.5x |
| 2 | double complex | avx2 | 1185 | 32.8x | 2.7x |
| 2 | double complex | avx512 | 1155 | 32.0x | 2.6x |
| 2 | Tx double complex | libiio + C loop | 44 | 1.0x | - |
| 2 | Tx double complex | scalar | 208 | 4.7x | 1.0x |
| 2 | Tx double complex | sse41 | 761 | 17.1x | 3.7x |
| 2 | Tx double complex | avx2 | 848 | 19.1x | 4.1x |
| 2 | Tx double complex | avx512 | 1065 | 24.0x | 5.1x |
| 4 | int16 real | libiio + C loop | 20 | 1.0x | - |
| 4 | int16 real | scalar | 356 | 18.1x | 1.0x |
| 4 | int16 real | sse41 | 599 | 30.4x | 1.7x |
| 4 | int16 real | avx2 | 600 | 30.5x | 1.7x |
| 4 | int16 real | avx512 | 581 | 29.5x | 1.6x |
| 4 | int16 complex | libiio + C loop | 20 | 1.0x | - |
| 4 | int16 complex | scalar | 352 | 18.0x | 1.0x |
| 4 | int16 complex | sse41 | 1118 | 57.2x | 3.2x |
| 4 | int16 complex | avx2 | 1119 | 57.2x | 3.2x |
| 4 | int16 complex | avx512 | 1120 | 57.3x | 3.2x |
| 4 | single complex | libiio + C loop | 19 | 1.0x | - |
| 4 | single complex | scalar | 207 | 10.8x | 1.0x |
| 4 | single complex | sse41 | 698 | 36.4x | 3.4x |
| 4 | single complex | avx2 | 771 | 40.2x | 3.7x |
| 4 | single complex | avx512 | 762 | 39.8x | 3.7x |
| 4 | double complex | libiio + C loop | 18 | 1.0x | - |
| 4 | double complex | scalar | 204 | 11.2x | 1.0x |
| 4 | double complex | sse41 | 431 | 23.6x | 2.1x |
| 4 | double complex | avx2 | 438 | 24.0x | 2.1x |
| 4 | double complex | avx512 | 445 | 24.4x | 2.2x |
| 4 | Tx double complex | libiio + C loop | 22 | 1.0x | - |
| 4 | Tx double complex | scalar | 99 | 4.5x | 1.0x |
| 4 | Tx double complex | sse41 | 295 | 13.2x | 3.0x |
| 4 | Tx double complex | avx2 | 303 | 13.6x | 3.0x |
| 4 | Tx double complex | avx512 | 353 | 15.8x | 3.5x |
| 8 | int16 real | libiio + C loop | 10 | 1.0x | - |
| 8 | int16 real | scalar | 135 | 13.7x | 1.0x |
| 8 | int16 real | sse41 | 169 | 17.2x | 1.3x |
| 8 | int16 real | avx2 | 170 | 17.2x | 1.3x |
| 8 | int16 real | avx512 | 165 | 16.7x | 1.2x |
| 8 | int16 complex | libiio + C loop | 10 | 1.0x | - |
| 8 | int16 complex | scalar | 120 | 12.3x | 1.0x |
| 8 | int16 complex | sse41 | 283 | 29.1x | 2.4x |
| 8 | int16 complex | avx2 | 291 | 30.0x | 2.4x |
| 8 | int16 complex | avx512 | 295 | 30.4x | 2.5x |
| 8 | single complex | libiio + C loop | 9 | 1.0x | - |
| 8 | single complex | scalar | 96 | 10.8x | 1.0x |
| 8 | single complex | sse41 | 239 | 26.9x | 2.5x |
| 8 | single complex | avx2 | 247 | 27.8x | 2.6x |
| 8 | single complex | avx512 | 242 | 27.3x | 2.5x |
| 8 | double complex | libiio + C loop | 9 | 1.0x | - |
| 8 | double complex | scalar | 73 | 8.1x | 1.0x |
| 8 | double complex | sse41 | 145 | 16.0x | 2.0x |
| 8 | double complex | avx2 | 146 | 16.1x | 2.0x |
| 8 | double complex | avx512 | 144 | 15.9x | 2.0x |
| 8 | Tx double complex | libiio + C loop | 11 | 1.0x | - |
| 8 | Tx double complex | scalar | 48 | 4.3x | 1.0x |
| 8 | Tx double complex | sse41 | 140 | 12.7x | 2.9x |
| 8 | Tx double complex | avx2 | 141 | 12.7x | 3.0x |
| 8 | Tx double complex | avx512 | 156 | 14.1x | 3.3x |

## AArch64 (NEON)

- CPU: Raspberry Pi 4 Model B (Cortex-A72)
- Compiler: aarch64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, static cross build
- No libiio on the board, so there is no libiio baseline: compare with the
  scalar backend.

### Blocks of 4096 samples

| channels | host array | backend | MS/s per channel | vs libiio | vs scalar |
|---|---|---|---|---|---|
| 2 | int16 real | scalar | 183 | - | 1.0x |
| 2 | int16 real | neon | 384 | - | 2.1x |
| 2 | int16 complex | scalar | 180 | - | 1.0x |
| 2 | int16 complex | neon | 763 | - | 4.2x |
| 2 | single complex | scalar | 122 | - | 1.0x |
| 2 | single complex | neon | 227 | - | 1.9x |
| 2 | double complex | scalar | 107 | - | 1.0x |
| 2 | double complex | neon | 143 | - | 1.3x |
| 2 | Tx double complex | scalar | 81 | - | 1.0x |
| 2 | Tx double complex | neon | 154 | - | 1.9x |
| 4 | int16 real | scalar | 88 | - | 1.0x |
| 4 | int16 real | neon | 190 | - | 2.2x |
| 4 | int16 complex | scalar | 89 | - | 1.0x |
| 4 | int16 complex | neon | 246 | - | 2.7x |
| 4 | single complex | scalar | 61 | - | 1.0x |
| 4 | single complex | neon | 92 | - | 1.5x |
| 4 | double complex | scalar | 54 | - | 1.0x |
| 4 | double complex | neon | 60 | - | 1.1x |
| 4 | Tx double complex | scalar | 41 | - | 1.0x |
| 4 | Tx double complex | neon | 77 | - | 1.9x |
| 8 | int16 real | scalar | 44 | - | 1.0x |
| 8 | int16 real | neon | 86 | - | 2.0x |
| 8 | int16 complex | scalar | 44 | - | 1.0x |
| 8 | int16 complex | neon | 124 | - | 2.8x |
| 8 | single complex | scalar | 30 | - | 1.0x |
| 8 | single complex | neon | 46 | - | 1.5x |
| 8 | double complex | scalar | 27 | - | 1.0x |
| 8 | double complex | neon | 30 | - | 1.1x |
| 8 | Tx double complex | scalar | 20 | - | 1.0x |
| 8 | Tx double complex | neon | 39 | - | 1.9x |

### Blocks of 2^20 samples

| channels | host array | backend | MS/s per channel | vs libiio | vs scalar |
|---|---|---|---|---|---|
| 2 | int16 real | scalar | 180 | - | 1.0x |
| 2 | int16 real | neon | 378 | - | 2.1x |
| 2 | int16 complex | scalar | 144 | - | 1.0x |
| 2 | int16 complex | neon | 677 | - | 4.7x |
| 2 | single complex | scalar | 91 | - | 1.0x |
| 2 | single complex | neon | 222 | - | 2.4x |
| 2 | double complex | scalar | 49 | - | 1.0x |
| 2 | double complex | neon | 139 | - | 2.8x |
| 2 | Tx double complex | scalar | 74 | - | 1.0x |
| 2 | Tx double complex | neon | 154 | - | 2.1x |
| 4 | int16 real | scalar | 88 | - | 1.0x |
| 4 | int16 real | neon | 109 | - | 1.2x |
| 4 | int16 complex | scalar | 51 | - | 1.0x |
| 4 | int16 complex | neon | 202 | - | 4.0x |
| 4 | single complex | scalar | 37 | - | 1.0x |
| 4 | single complex | neon | 91 | - | 2.4x |
| 4 | double complex | scalar | 22 | - | 1.0x |
| 4 | double complex | neon | 58 | - | 2.7x |
| 4 | Tx double complex | scalar | 31 | - | 1.0x |
| 4 | Tx double complex | neon | 62 | - | 2.0x |
| 8 | int16 real | scalar | 29 | - | 1.0x |
| 8 | int16 real | neon | 29 | - | 1.0x |
| 8 | int16 complex | scalar | 21 | - | 1.0x |
| 8 | int16 complex | neon | 55 | - | 2.7x |
| 8 | single complex | scalar | 15 | - | 1.0x |
| 8 | single complex | neon | 44 | - | 2.9x |
| 8 | double complex | scalar | 10 | - | 1.0x |
| 8 | double complex | neon | 29 | - | 3.0x |
| 8 | Tx double complex | scalar | 9 | - | 1.0x |
| 8 | Tx double complex | neon | 18 | - | 2.0x |

## MATLAB fast path (emu, GitHub-hosted runner)

`iio.RxStream` against the conversion written in M code, measured by the test
`matlab/tests/tTiming.m` in CI run
[43977125124](https://github.com/tfcollins/libiio-conv/actions/runs/43977125124) on
`ubuntu-24.04` hosted runners (the CPU model of each runner is the `Runner CPU:` line of that
job's summary "MATLAB fast path timing"). One frame is 262144 samples of 2 channels of 16 bit
(one I/Q pair); each value is the median of 30 frames after 5 warm-up frames.

| path | R2025b ms per frame | R2025b MS/s | R2023b ms per frame | R2023b MS/s |
|---|---|---|---|---|
| `fast_int16_complex`: `RxStream` returns complex int16 | 0.090 | 2929.0 | 0.142 | 1839.6 |
| `mcode_int16_complex`: raw int16 from the stream, then transpose, `complex()` of the two rows, transpose | 0.875 | 299.6 | 1.120 | 234.1 |
| `fast_double_fullscale`: `RxStream` returns complex double scaled to full scale | 0.135 | 1949.0 | 0.201 | 1304.2 |
| `mcode_double_fullscale`: the M-code path above, then `double(out) / 2048` | 2.259 | 116.1 | 2.703 | 97.0 |

| ratio (M code / fast path) | R2025b | R2023b |
|---|---|---|
| complex int16 | 9.78 | 7.86 |
| complex double, full scale | 16.79 | 13.45 |

How to read this:

- Every time includes the emu backend reading the 1 MiB frame from its data file, the same
  in all four paths. No MS/s value here is a device throughput, and the ratios understate the
  ratio of the conversions alone.
- `mcode_int16_complex` keeps int16. The receive step of the ADI toolboxes
  (`adi.common.Rx`) also allocates a complex double matrix and assigns into it, so this path
  is a lower bound of the toolbox step, not the toolbox step itself.
- The two releases ran on two different runners, so the columns compare paths within a
  release, not the releases with each other.
- Shared runners are noisy; the test asserts values, never a time.

## Reading the numbers

- The gain over libiio's per-sample conversion is 4x to 18x even for the
  scalar backend, because the format is resolved once per block instead of
  once per sample. Most of that is the cost of one call into libiio per
  sample, not arithmetic.
- SIMD adds 1.2x to 9.5x on top on x86-64 (13x to 167x over libiio in total)
  and 1.0x to 4.7x on the Cortex-A72. The 1.0x case is eight strided int16
  channels in 2^20-sample blocks on the Pi, where memory traffic dominates.
- Block size matters. With blocks that fit in the cache, AVX-512 is clearly
  ahead of AVX2; with 2^20-sample blocks every backend is limited by memory
  traffic and they converge.
- Strided single-channel reads ("int16 real") gain least from SIMD: samples
  are picked up one at a time before being converted as a vector. For that
  case the AVX-512 backend deliberately uses the AVX2 kernels, which measured
  faster.
- On the Cortex-A72 the double-precision paths gain least in cache (two
  doubles per vector).

## Caveats

- These are in-memory conversion rates, not end-to-end streaming rates.
- The libiio baseline pays for a call into libiio per sample (a
  RelWithDebInfo, -O2 build) and a second pass over a temporary.
- Runs are not pinned to a core and always execute in the same order.
- One machine per architecture.
