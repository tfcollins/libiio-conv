# libiio-conv

Fast conversion between IIO buffer samples and host arrays (type change, I/Q
pairing, layout, scaling) for libiio v1, with SIMD kernels chosen at run time.

Work in progress. See `docs/superpowers/specs/` for the design.

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

NEON is only compile-checked on an x86 host:

    cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
          -DIIO_CONV_WITH_LIBIIO=OFF
    cmake --build build-aarch64

Copy `build-aarch64/tests/test_*` to an ARM board and run them there
(`test_backends` is the one that exercises NEON).
