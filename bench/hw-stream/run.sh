#!/bin/bash
# SPDX-License-Identifier: MIT
# Runs every case of the hardware stream timing and writes results.txt.
# usage: run.sh <uri> [<results file> [<passes>]]      e.g. run.sh ip:<board>
# The results file is results.txt next to this script when none is given.
# Every case is run <passes> times (2 when not given), one pass after the other.
# rxbench must be next to this script; see docs/benchmarks.md for the build.
set -u
[ $# -ge 1 ] && [ $# -le 3 ] || { echo "usage: $0 <uri> [<results file> [<passes>]]" >&2; exit 2; }
U=$1; R=$(realpath -m "${2:-$(dirname "$0")/results.txt}")
cd "$(dirname "$0")"; : > "$R"
for pass in $(seq 1 "${3:-2}"); do for spf in 1024 16384 262144 1048576; do
 for m in "conv $U $spf 50 5000" "conv $U $spf 50 -1" "plain $U $spf 50"; do
  echo "PASS $pass: $m" >> "$R"; timeout 200 ./rxbench $m >> "$R" 2>&1; echo "rc=$?" >> "$R"
 done; done; done
