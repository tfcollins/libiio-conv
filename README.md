# libiio-conv

[![CI](https://github.com/tfcollins/libiio-conv/actions/workflows/ci.yml/badge.svg)](https://github.com/tfcollins/libiio-conv/actions/workflows/ci.yml)

Fast conversion between IIO buffer samples and host arrays (type change, I/Q
pairing, layout, scaling) for libiio v1, with SIMD kernels chosen at run time.

Measured gain over converting sample by sample with libiio is 13x to 167x for
12-bit-in-16 data on x86-64, depending on the case and block size; see
`docs/benchmarks.md`. The design is in
`docs/superpowers/specs/`.

Status: the C library is complete. All backends are tested: SSE4.1, AVX2 and
AVX-512 on x86-64, and NEON on a Cortex-A72 (static cross build, see below).
MATLAB bindings are the next part of the project.

## Build

    deps/build-libiio.sh          # pinned libiio v1 into deps/install
    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

## Backends

The kernel backend is picked once at run time: `avx512`, `avx2`, `sse41` on
x86-64, `neon` on AArch64, `scalar` everywhere. `IIO_CONV_FORCE=<name>` in the
environment overrides the choice (an unknown or unsupported name selects
`scalar`). Every backend is tested to produce exactly the bytes `scalar` does.

NEON is built by cross compiling and tested on an ARM board. A static build
needs nothing on the board:

    cmake -S . -B build-aarch64 -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
          -DIIO_CONV_WITH_LIBIIO=OFF -DCMAKE_EXE_LINKER_FLAGS=-static
    cmake --build build-aarch64

Copy `build-aarch64/tests/test_*` to the board and run them; `test_backends`
is the one that compares NEON against the scalar reference.

## Use

Receive two I/Q channels as one column of complex single, scaled to [-1, 1):

```c
#include <iio/iio.h>
#include <iio/iio-conv.h>

const struct iio_channel *chans[2] = { rx_i, rx_q };
struct iio_conv_spec spec = {
	.type = IIO_CONV_F32,
	.layout = IIO_CONV_COLUMNS,
	.scale = IIO_CONV_SCALE_FULLSCALE,
	.complex_iq = true,
};
struct iio_conv *conv;

/* mask: the channels mask the buffer was opened with */
int ret = iio_conv_create(mask, chans, 2, &spec, &conv);

float *iq = malloc(nb_samples * iio_conv_get_host_step(conv));
iio_block_dequeue(block, false);
ssize_t n = iio_conv_read(conv, block, iq, nb_samples);
/* iq[2*k] is the real part and iq[2*k + 1] the imaginary part of sample k */
```

Transmit is the mirror image:

```c
ssize_t n = iio_conv_write(tx_conv, block, iq, nb_samples);
iio_block_enqueue(block, n * iio_conv_get_step(tx_conv), false);
```

`iio_conv_create_raw()`, `iio_conv_read_raw()` and `iio_conv_write_raw()` do
the same on plain memory with hand-written channel descriptors and need no
libiio objects.

## Host array

| `type` | I8, U8, I16, U16, I32, U32, I64, U64, F32, F64 |
|---|---|
| `layout` | `COLUMNS`: one contiguous column per channel (MATLAB order). `INTERLEAVED`: sample by sample (C / NumPy order). |
| `complex_iq` | channels 2k and 2k+1 become the real and imaginary part of column k |

## Scaling

Host value = (raw + offset) * gain, then the optional per-channel
`user_gain` and `user_offset`.

| `scale` | gain | offset |
|---|---|---|
| `IIO_CONV_SCALE_NONE` | 1 | 0 |
| `IIO_CONV_SCALE_IIO` | the channel's IIO scale | the channel's IIO offset |
| `IIO_CONV_SCALE_FULLSCALE` | 1/2^(bits-1) signed, 1/2^bits unsigned | 0 |

Any scaling needs a float host type. Writing from floats rounds to nearest
(ties to even), turns NaN into 0 and always saturates to the channel's range.
Integer conversions wrap unless `saturate` is set.

## Limits

- Storage sizes of 8, 16, 32 and 64 bits. Other sizes, `repeat > 1` and float
  storage return `-ENOTSUP`.
- SIMD paths cover 8- and 16-bit storage to int16 / single / double on every
  backend, and 32-bit storage to int32 / single / double on AVX2 and AVX-512,
  for the `COLUMNS` layout (or one column). Everything else is converted by
  the portable kernels, still a whole block at a time.
- `iio_conv_read()` converts up to the block's capacity: a block does not
  record how many bytes a dequeue filled.
- `SCALE_IIO` uses the scale and offset libiio read when the converter was
  created.
- `iio_conv_read()` and `iio_conv_write()` trust that the block comes from a
  buffer opened with the mask given to `iio_conv_create()`.
- Tests assume a little-endian host.

## Benchmark

    cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DIIO_CONV_BUILD_BENCH=ON
    cmake --build build-rel
    build-rel/bench/bench_conv tests/resources/conv_test.xml          # 2^20-sample blocks
    build-rel/bench/bench_conv -n 12 tests/resources/conv_test.xml    # 4096-sample blocks

Without the XML file, or in a build without libiio (such as the static ARM
build), the backends are compared with the scalar backend only.

## Licence

MIT, see `LICENSE`. libiio itself is a separate project with its own licence
terms.
