#!/bin/bash
# SPDX-License-Identifier: MIT
# Runs every case of the hardware stream timing twice and writes results.txt.
# usage: run.sh <uri>      e.g. run.sh ip:<board>
# rxbench must be next to this script; see docs/benchmarks.md for the build.
set -u
[ $# -eq 1 ] || { echo "usage: $0 <uri>" >&2; exit 2; }
cd "$(dirname "$0")"; U=$1; : > results.txt
for pass in 1 2; do for spf in 1024 16384 262144 1048576; do
 for m in "conv $U $spf 50 5000" "conv $U $spf 50 -1" "plain $U $spf 50"; do
  echo "PASS $pass: $m" >> results.txt; timeout 200 ./rxbench $m >> results.txt 2>&1; echo "rc=$?" >> results.txt
 done; done; done
