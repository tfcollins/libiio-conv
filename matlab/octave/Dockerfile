# Test environment for the MATLAB/Octave bindings: GNU Octave plus the pinned
# libiio v1. Needs no MATLAB licence.
#
# Build from the repository root:
#   docker build -t libiio-conv-octave -f matlab/octave/Dockerfile deps
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
	octave octave-dev build-essential cmake ninja-build git ca-certificates \
	pkg-config libxml2-dev libzstd-dev bison flex \
	&& rm -rf /var/lib/apt/lists/*

# Same pinned libiio v1 as deps/build-libiio.sh installs locally
COPY build-libiio.sh /opt/libiio-conv-deps/build-libiio.sh
RUN /opt/libiio-conv-deps/build-libiio.sh \
	&& rm -rf /opt/libiio-conv-deps/src
ENV LIBIIO_ROOT=/opt/libiio-conv-deps/install
