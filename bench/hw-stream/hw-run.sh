#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Have labgrid boot a lab board on its catalog image (adi-lg request --mode
# uri), run run.sh against the exported IIO_URI, release the board and power it
# off, then pass or fail the numbers with check_results.py. Used by
# .github/workflows/hw-test.yml on a lab runner; runnable by hand with the same
# environment.
#
#   bench/hw-stream/hw-run.sh <part> <carrier> <out-dir> [check_results.py options]
#
# Needs bench/hw-stream/rxbench (see docs/benchmarks.md) and adi-lg of
# labgrid-plugins on PATH. Host facts come from the environment, never from
# this repository:
#   LG_COORDINATOR         labgrid coordinator host:port
#   IIO_CONV_LAB_REDACT    words to take out of the outputs (optional)
#   IIO_CONV_HW_PASSES     times every case is run (5 when not set)
#
# <out-dir> gets results.txt, info.txt and session.log, redacted. Nothing
# unredacted is printed (public job logs).
set -uo pipefail

usage="usage: hw-run.sh <part> <carrier> <out-dir> [check_results.py options]"
part=${1:?$usage}
carrier=${2:?$usage}
out=${3:?$usage}
shift 3
here=$(cd "$(dirname "$0")" && pwd)
[ -n "${LG_COORDINATOR:-}" ] || { echo "hw-run: LG_COORDINATOR is not set" >&2; exit 2; }
[ -x "$here/rxbench" ] || { echo "hw-run: $here/rxbench is not built" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out"/results.txt "$out"/info.txt "$out"/session.log "$out"/*.raw.*

adi-lg request --part "$part" --carrier "$carrier" --mode uri --wait 1800 --power-down \
	--run "echo \"### board booted on place \$LG_PLACE\";
	       '$here/rxbench' info \"\$IIO_URI\" > '$out/info.raw.txt' 2>&1;
	       '$here/run.sh' \"\$IIO_URI\" '$out/results.raw.txt' ${IIO_CONV_HW_PASSES:-5}" \
	> "$out/session.raw.log" 2>&1
rc=$?

python3 "$here/redact.py" "$out" && rm -f "$out"/*.raw.*
for f in session.log info.txt; do
	echo "----- $f (redacted) -----"
	cat "$out/$f" 2>/dev/null
done
echo "hw-run: $part/$carrier: board session exit $rc"
[ -f "$out/results.txt" ] || { echo "hw-run: no results" >&2; exit 1; }
python3 "$here/check_results.py" "$out/results.txt" "$@" || exit 1
exit "$rc"
