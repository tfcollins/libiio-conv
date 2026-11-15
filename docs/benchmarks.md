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
| 2 | int16 real | scalar | 706 | 17.8x |
| 2 | int16 real | sse41 | 1415 | 35.6x |
| 2 | int16 real | avx2 | 1414 | 35.6x |
| 2 | int16 real | avx512 | 1056 | 26.6x |
| 2 | int16 complex | libiio + C loop | 39 | 1.0x |
| 2 | int16 complex | scalar | 708 | 18.0x |
| 2 | int16 complex | sse41 | 2851 | 72.5x |
| 2 | int16 complex | avx2 | 3395 | 86.4x |
| 2 | int16 complex | avx512 | 3475 | 88.4x |
| 2 | single complex | libiio + C loop | 39 | 1.0x |
| 2 | single complex | scalar | 475 | 12.1x |
| 2 | single complex | sse41 | 1976 | 50.5x |
| 2 | single complex | avx2 | 2136 | 54.5x |
| 2 | single complex | avx512 | 2124 | 54.2x |
| 2 | double complex | libiio + C loop | 38 | 1.0x |
| 2 | double complex | scalar | 497 | 12.9x |
| 2 | double complex | sse41 | 1205 | 31.3x |
| 2 | double complex | avx2 | 1251 | 32.5x |
| 2 | double complex | avx512 | 1181 | 30.7x |
| 2 | Tx double complex | libiio + C loop | 46 | 1.0x |
| 2 | Tx double complex | scalar | 185 | 4.0x |
| 2 | Tx double complex | sse41 | 699 | 15.1x |
| 2 | Tx double complex | avx2 | 784 | 16.9x |
| 2 | Tx double complex | avx512 | 1058 | 22.8x |
| 4 | int16 real | libiio + C loop | 20 | 1.0x |
| 4 | int16 real | scalar | 359 | 18.0x |
| 4 | int16 real | sse41 | 595 | 29.8x |
| 4 | int16 real | avx2 | 589 | 29.5x |
| 4 | int16 real | avx512 | 506 | 25.4x |
| 4 | int16 complex | libiio + C loop | 20 | 1.0x |
| 4 | int16 complex | scalar | 358 | 18.1x |
| 4 | int16 complex | sse41 | 1119 | 56.6x |
| 4 | int16 complex | avx2 | 1127 | 57.1x |
| 4 | int16 complex | avx512 | 1123 | 56.9x |
| 4 | single complex | libiio + C loop | 19 | 1.0x |
| 4 | single complex | scalar | 210 | 10.9x |
| 4 | single complex | sse41 | 702 | 36.5x |
| 4 | single complex | avx2 | 763 | 39.6x |
| 4 | single complex | avx512 | 758 | 39.3x |
| 4 | double complex | libiio + C loop | 18 | 1.0x |
| 4 | double complex | scalar | 199 | 10.9x |
| 4 | double complex | sse41 | 427 | 23.3x |
| 4 | double complex | avx2 | 411 | 22.4x |
| 4 | double complex | avx512 | 447 | 24.4x |
| 4 | Tx double complex | libiio + C loop | 23 | 1.0x |
| 4 | Tx double complex | scalar | 99 | 4.3x |
| 4 | Tx double complex | sse41 | 298 | 12.9x |
| 4 | Tx double complex | avx2 | 303 | 13.1x |
| 4 | Tx double complex | avx512 | 358 | 15.5x |
| 8 | int16 real | libiio + C loop | 10 | 1.0x |
| 8 | int16 real | scalar | 135 | 13.6x |
| 8 | int16 real | sse41 | 169 | 17.1x |
| 8 | int16 real | avx2 | 171 | 17.3x |
| 8 | int16 real | avx512 | 165 | 16.7x |
| 8 | int16 complex | libiio + C loop | 10 | 1.0x |
| 8 | int16 complex | scalar | 132 | 13.6x |
| 8 | int16 complex | sse41 | 313 | 32.2x |
| 8 | int16 complex | avx2 | 317 | 32.6x |
| 8 | int16 complex | avx512 | 318 | 32.7x |
| 8 | single complex | libiio + C loop | 10 | 1.0x |
| 8 | single complex | scalar | 99 | 10.4x |
| 8 | single complex | sse41 | 239 | 25.1x |
| 8 | single complex | avx2 | 241 | 25.4x |
| 8 | single complex | avx512 | 238 | 25.1x |
| 8 | double complex | libiio + C loop | 9 | 1.0x |
| 8 | double complex | scalar | 74 | 8.1x |
| 8 | double complex | sse41 | 148 | 16.3x |
| 8 | double complex | avx2 | 144 | 15.9x |
| 8 | double complex | avx512 | 150 | 16.5x |
| 8 | Tx double complex | libiio + C loop | 12 | 1.0x |
| 8 | Tx double complex | scalar | 49 | 4.2x |
| 8 | Tx double complex | sse41 | 137 | 11.9x |
| 8 | Tx double complex | avx2 | 139 | 12.0x |
| 8 | Tx double complex | avx512 | 151 | 13.1x |

## Reading the numbers

- The gain over libiio's per-sample conversion is 8x to 18x even for the
  scalar backend, because the format is resolved once per block instead of
  once per sample.
- SIMD adds another 2x to 5x on top for the paired I/Q cases.
- Strided single-channel reads ("int16 real") gain least from SIMD: samples
  are picked up one at a time before being converted as a vector.
- AVX-512 is not consistently faster than AVX2 here; at 4 and 8 channels the
  conversion is limited by memory traffic rather than arithmetic.
- These are in-memory conversion rates on one machine, not end-to-end
  streaming rates.
