# libiio-conv

Fast conversion between IIO buffer samples and host arrays (type change, I/Q
pairing, layout, scaling) for libiio v1, with SIMD kernels chosen at run time.

Work in progress. See `docs/superpowers/specs/` for the design.

## Build

    deps/build-libiio.sh          # pinned libiio v1 into deps/install
    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure
