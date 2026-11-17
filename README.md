# libiio-conv

[![CI](https://github.com/tfcollins/libiio-conv/actions/workflows/ci.yml/badge.svg)](https://github.com/tfcollins/libiio-conv/actions/workflows/ci.yml)

Fast conversion between IIO buffer samples and host arrays (type change, I/Q
pairing, layout, scaling) for libiio v1, with SIMD kernels chosen at run time.

Measured gain over converting sample by sample with libiio is 13x to 167x
with the SIMD backends (4x to 18x with the scalar backend) for 12-bit-in-16
data on x86-64, depending on the case and block size; see
`docs/benchmarks.md`. The design is in
`docs/superpowers/specs/`.

Status: the C library is complete. All backends are tested: SSE4.1, AVX2 and
AVX-512 on x86-64, and NEON on a Cortex-A72 (static cross build, see below).
The MATLAB bindings for libiio v1 in `matlab/` are complete for Linux x86-64
and tested in MATLAB R2025b and R2023b on libiio's emulation backend; they
have not been run on hardware yet.

## Build

    deps/build-libiio.sh          # pinned libiio v1 into deps/install
    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

Needs GCC, CMake 3.20 or later and Ninja. `deps/build-libiio.sh` needs git
and the packages `libxml2-dev libzstd-dev bison flex`.

The default build also builds the tests of the MATLAB gateway (label `mex`;
they need no MATLAB). These use Python 3 (at configure and build time), the
ASan/UBSan runtime of the compiler (`libasan`, `libubsan`: separate packages
on some distributions) and one installed locale with a decimal comma
(`sudo locale-gen de_DE.UTF-8`). What is missing is left out with a message:
without Python the gateway tests are not built, without the runtime they are
built unsanitized, without the locale their numeric-locale checks do not
run. `-DIIO_CONV_STRICT_TESTS=ON` (CI uses it) makes each of these an error,
and compiles the gateway with `-Werror`. `-DIIO_CONV_BUILD_MEX_TESTS=OFF`
builds the C library and its own tests only.

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

## Stream layer

`iio-conv-stream.h` puts the buffer stream, its blocks and the converter
behind one object, so that a frame is one call. Receive complex single and
transmit complex double, both scaled to [-1, 1):

```c
#include <iio/iio-conv-stream.h>

const struct iio_channel *rx_chans[2] = { rx_i, rx_q }, *tx_chans[2] = { tx_i, tx_q };
struct iio_conv_spec spec = { .type = IIO_CONV_F32, .complex_iq = true,
	.scale = IIO_CONV_SCALE_FULLSCALE };
struct iio_conv_stream *rx = NULL, *tx = NULL;

/* frames of 16384 samples, 4 blocks in rotation, not cyclic */
int ret = iio_conv_stream_open(rx_buf, rx_chans, 2, 16384, 4, false, &spec, &rx);
if (ret < 0)
	return ret;                           /* rx is still NULL */
spec.type = IIO_CONV_F64;
ret = iio_conv_stream_open(tx_buf, tx_chans, 2, 16384, 4, false, &spec, &tx);

ret = iio_conv_stream_read(rx, iq);           /* 1: a frame, 0: none in time, < 0: errno */
ret = iio_conv_stream_write(tx, wave, 16384); /* 1: queued, 0: no free block in time */

iio_conv_stream_close(&tx);                   /* lets the frames in flight go out; NULL is fine */
iio_conv_stream_close(&rx);
```

- The direction is that of the buffer. Only the channels given are enabled,
  and host column k is channel k as passed (or the pair 2k, 2k+1 with
  `complex_iq`), whatever their order in the device. Sizes and positions
  follow the mask the buffer really has, so hardware that enables more
  channels than asked for is handled; on transmit such channels are sent as
  zero.
- A receive stream captures from the moment it is opened, into all its
  blocks. `iio_conv_stream_read()` returns `IIO_CONV_STREAM_FRAME` (1) with a
  whole frame or `IIO_CONV_STREAM_NO_FRAME` (0) with the array untouched;
  "no data in time" is not an error and the call can be repeated.
  `iio_conv_stream_flush()` drops the frames already captured, so that the
  next read waits for new data.
- `iio_conv_stream_write()` converts and queues the frame before it returns;
  the first write starts the buffer. When every block is in flight it first
  waits for the oldest one, and returns 0 with nothing queued if that takes
  too long. A frame may be shorter than the frame length.
  `iio_conv_stream_write_typed()` takes an array of another element type, or
  a real array for I/Q channels (the Q channels are then sent as zero).
  `iio_conv_stream_flush()` waits, under the same policy, until every queued
  frame has been sent. Closing the stream does that first, so a frame written
  just before the close still goes out.
- With a backend that has no block operations (a local device without the
  high-speed interface, an old IIOD), libiio drops a transmit frame whose
  transfer timed out. Write and flush then return `-EIO`, once per lost
  frame (a flush: once for all it found); the stream goes on, and the frame
  of the write that reported it has to be written again.
- With `cyclic` set at open, the first frame written repeats until the stream
  is closed. One block is used, and further writes return `-EBUSY`.
- `iio_conv_stream_set_timeout()` picks how long a call may wait for a block:

  | value | wait |
  |---|---|
  | `IIO_CONV_STREAM_TIMEOUT_CONTEXT` (-1, default) | one blocking dequeue, bounded by the timeout of the context |
  | `IIO_CONV_STREAM_TIMEOUT_NONBLOCK` (0) | never: one look |
  | a time in ms | polls every millisecond until the time has passed |

  The timeout of the context, which is shared by everything on that context,
  is never changed.

The tests run the stream layer on libiio's emulation backend (`emu:`), on a
scripted backend (`tests/fake_backend.h`) and on a backend without block
operations (`tests/test_stream_generic.c`). `emu:` delivers the bytes of
`<device>_buf<n>.bin` beside the XML file as receive data, from the start
again at its end, and replaces that file with every transmit block it gets
back; this shows conversion, column order, the rotation of the transmit
blocks, partial frames and that the last frame goes out before a close. It
does not show timing or the rotation of the receive blocks: a block is always
ready and is filled when it is taken back, whichever block it is; `cyclic`
and non-blocking dequeues are ignored and cancelling does nothing. Blocks
that are not ready, the three wait policies (also for the wait of a close),
which block is waited for, cyclic transmit, failures and the order of the
backend calls are tested on the scripted backend. The third backend makes
libiio use its own blocks and worker thread, which fills and sends blocks
strictly in the order they were queued and forgets a block whose transfer
failed: frames must arrive whole and in order, and a lost transmit frame
must be reported. The stream layer has not been run on hardware yet.

## MATLAB bindings

`matlab/` holds bindings for libiio v1: one MEX file and the package `iio`,
whose handle classes reach the public functions of `iio.h`. `iio.RxStream`
and `iio.TxStream` sit on the stream layer above and move one frame per call
straight into and out of a MATLAB array.

    deps/build-libiio.sh
    cmake -S . -B build -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR=<MATLAB root>
    cmake --build build

```matlab
addpath('<repo>/matlab')
ctx = iio.Context("ip:192.168.2.1");
dev = ctx.find_device("cf-ad9361-lpc");
rx = iio.RxStream(dev, ["voltage0" "voltage1"], SamplesPerFrame = 16384, ...
    OutputDataType = "single", Complex = true, Scale = "fullscale");
[iq, valid] = rx.read();      % 16384-by-1 complex single, -1 to 1
if ~valid
    disp('no frame in time')  % not an error: iq is all zero, read again
end
rx.close();
```

`matlab/README.md` has the object model, every option, the class reference
and the mapping from libiio v0. The same text is built into HTML pages with
Sphinx, see [Documentation](#documentation). Limitations: Linux x86-64 and MATLAB R2023b
or later only, no code generation yet, a blocking call cannot be interrupted
with Ctrl-C, and nothing has been run on hardware.

## Host array

| `type` | I8, U8, I16, U16, I32, U32, I64, U64, F32, F64 |
|---|---|
| `layout` | `COLUMNS`: one contiguous column per channel (MATLAB order). `INTERLEAVED`: sample by sample (C / NumPy order). |
| `complex_iq` | channels 2k and 2k+1 become the real and imaginary part of column k |
| `raw` | take the storage word as it is: the channel's valid-bit count and shift are ignored, only byte order and signedness apply |

## Scaling

Host value = (raw + offset) * gain, then the optional per-channel
`user_gain` and `user_offset`.

| `scale` | gain | offset |
|---|---|---|
| `IIO_CONV_SCALE_NONE` | 1 | 0 |
| `IIO_CONV_SCALE_IIO` | the channel's IIO scale | the channel's IIO offset |
| `IIO_CONV_SCALE_FULLSCALE` | 1/2^(bits-1) signed, 1/2^bits unsigned | 0 |

With `raw` the whole storage word is the sample, so `bits` in the table is
the storage size, and `IIO_CONV_SCALE_IIO`, which describes the valid bits
only, is refused with `-EINVAL`.

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

## Documentation

`docs/` is a Sphinx project (MyST Markdown). The MATLAB page includes
`matlab/README.md`, so that file stays the single source.

    python -m pip install -r docs/requirements.txt
    python -m sphinx -W -b html docs docs/_build/html

Open `docs/_build/html/index.html`. CI builds the pages in the
[Docs workflow](https://github.com/tfcollins/libiio-conv/actions/workflows/docs.yml);
they are not deployed anywhere.

## Licence

MIT, see `LICENSE`. libiio itself is a separate project with its own licence
terms.
