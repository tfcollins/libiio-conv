# Benchmarks

Measured with `bench/bench_conv` (Release build, single thread, best of 9 runs,
2^20 samples per run, 12-bit signed samples in 16-bit little-endian storage,
COLUMNS layout).

- CPU: Intel(R) Xeon(R) W-2295 CPU @ 3.00GHz
- Compiler: gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- libiio: pinned v1 commit, RelWithDebInfo build
- Date: 2026-11-15

"libiio + C loop" is the same job done with libiio alone:
`iio_channel_convert()` once per sample (what `iio_channel_read()` does
internally) into an int16 temporary, then a plain C loop for the type change,
I/Q pairing and full-scale normalisation. Every row's output was compared
byte for byte against that baseline.

"MS/s per channel" is millions of samples per second for each channel while
all channels are being converted. "int16 real" has one column per channel, so
each channel is read with a stride; the complex rows convert adjacent I/Q
channels in one pass.

| channels | host array | backend | MS/s per channel | vs libiio |
|---|---|---|---|---|
| 2 | int16 real | libiio + C loop | 40 | 1.0x |
| 2 | int16 real | scalar | 705 | 17.6x |
| 2 | int16 real | sse41 | 1433 | 35.7x |
| 2 | int16 real | avx2 | 1252 | 31.2x |
| 2 | int16 real | avx512 | 1252 | 31.2x |
| 2 | int16 complex | libiio + C loop | 40 | 1.0x |
| 2 | int16 complex | scalar | 722 | 18.2x |
| 2 | int16 complex | sse41 | 3385 | 85.2x |
| 2 | int16 complex | avx2 | 3393 | 85.4x |
| 2 | int16 complex | avx512 | 3461 | 87.1x |
| 2 | single complex | libiio + C loop | 39 | 1.0x |
| 2 | single complex | scalar | 435 | 11.0x |
| 2 | single complex | sse41 | 2185 | 55.3x |
| 2 | single complex | avx2 | 2007 | 50.8x |
| 2 | single complex | avx512 | 2126 | 53.8x |
| 2 | double complex | libiio + C loop | 38 | 1.0x |
| 2 | double complex | scalar | 433 | 11.3x |
| 2 | double complex | sse41 | 1209 | 31.4x |
| 2 | double complex | avx2 | 1224 | 31.8x |
| 2 | double complex | avx512 | 1172 | 30.5x |
| 2 | Tx double complex | libiio + C loop | 47 | 1.0x |
| 2 | Tx double complex | scalar | 213 | 4.6x |
| 2 | Tx double complex | sse41 | 706 | 15.2x |
| 2 | Tx double complex | avx2 | 771 | 16.6x |
| 2 | Tx double complex | avx512 | 1036 | 22.2x |
| 4 | int16 real | libiio + C loop | 20 | 1.0x |
| 4 | int16 real | scalar | 360 | 18.0x |
| 4 | int16 real | sse41 | 599 | 30.1x |
| 4 | int16 real | avx2 | 568 | 28.5x |
| 4 | int16 real | avx512 | 567 | 28.4x |
| 4 | int16 complex | libiio + C loop | 20 | 1.0x |
| 4 | int16 complex | scalar | 349 | 17.7x |
| 4 | int16 complex | sse41 | 1125 | 57.0x |
| 4 | int16 complex | avx2 | 1104 | 55.9x |
| 4 | int16 complex | avx512 | 1120 | 56.7x |
| 4 | single complex | libiio + C loop | 19 | 1.0x |
| 4 | single complex | scalar | 201 | 10.4x |
| 4 | single complex | sse41 | 692 | 35.8x |
| 4 | single complex | avx2 | 764 | 39.5x |
| 4 | single complex | avx512 | 765 | 39.5x |
| 4 | double complex | libiio + C loop | 18 | 1.0x |
| 4 | double complex | scalar | 191 | 10.4x |
| 4 | double complex | sse41 | 434 | 23.7x |
| 4 | double complex | avx2 | 440 | 24.0x |
| 4 | double complex | avx512 | 449 | 24.5x |
| 4 | Tx double complex | libiio + C loop | 23 | 1.0x |
| 4 | Tx double complex | scalar | 100 | 4.3x |
| 4 | Tx double complex | sse41 | 297 | 12.7x |
| 4 | Tx double complex | avx2 | 307 | 13.2x |
| 4 | Tx double complex | avx512 | 363 | 15.6x |
| 8 | int16 real | libiio + C loop | 10 | 1.0x |
| 8 | int16 real | scalar | 136 | 13.7x |
| 8 | int16 real | sse41 | 165 | 16.8x |
| 8 | int16 real | avx2 | 168 | 17.0x |
| 8 | int16 real | avx512 | 169 | 17.1x |
| 8 | int16 complex | libiio + C loop | 10 | 1.0x |
| 8 | int16 complex | scalar | 122 | 12.5x |
| 8 | int16 complex | sse41 | 289 | 29.6x |
| 8 | int16 complex | avx2 | 297 | 30.5x |
| 8 | int16 complex | avx512 | 298 | 30.6x |
| 8 | single complex | libiio + C loop | 10 | 1.0x |
| 8 | single complex | scalar | 94 | 9.9x |
| 8 | single complex | sse41 | 230 | 24.1x |
| 8 | single complex | avx2 | 233 | 24.4x |
| 8 | single complex | avx512 | 234 | 24.5x |
| 8 | double complex | libiio + C loop | 9 | 1.0x |
| 8 | double complex | scalar | 72 | 7.9x |
| 8 | double complex | sse41 | 147 | 16.1x |
| 8 | double complex | avx2 | 151 | 16.5x |
| 8 | double complex | avx512 | 149 | 16.4x |
| 8 | Tx double complex | libiio + C loop | 12 | 1.0x |
| 8 | Tx double complex | scalar | 49 | 4.2x |
| 8 | Tx double complex | sse41 | 139 | 12.0x |
| 8 | Tx double complex | avx2 | 141 | 12.1x |
| 8 | Tx double complex | avx512 | 152 | 13.1x |

## Reading the numbers

- The gain over libiio's per-sample conversion is 8x to 18x even for the
  scalar backend, because the format is resolved once per block instead of
  once per sample.
- SIMD adds another 2x to 5x on top for the paired I/Q cases.
- Strided single-channel reads ("int16 real") gain least from SIMD: samples
  are picked up one at a time before being converted as a vector.
- AVX-512 is not consistently faster than AVX2 here; at 4 and 8 channels the
  conversion is limited by memory traffic rather than arithmetic. For strided
  single channels the AVX-512 backend deliberately uses the AVX2 kernels,
  which measured about 25% faster for that case.
- These are in-memory conversion rates on one machine, not end-to-end
  streaming rates.

## Caveats

- One size only (2^20 samples): source and destination are far larger than
  the caches, so every row is limited by memory traffic. Blocks that fit in
  L2 will show different ratios between backends.
- The baseline pays for a call into libiio per sample (a RelWithDebInfo, -O2
  build) and a second pass over a temporary; most of the scalar backend's
  lead over it is that call overhead, not arithmetic.
- Runs are not pinned to a core and always execute in the same order.
