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
#   IIO_CONV_LAB_REDACT    host and place names to take out of the outputs,
#                          space separated. Required: names that do not end in
#                          .local are only known from this list. The
#                          coordinator host and the place of the session are
#                          added to it.
#   IIO_CONV_HW_PASSES     times every case is run (5 when not set)
#
# <out-dir> gets results.txt, info.txt and session.log, redacted. Nothing
# unredacted is printed or left behind (public job logs and artifacts): when
# the redaction fails, the outputs are deleted.
set -uo pipefail

usage="usage: hw-run.sh <part> <carrier> <out-dir> [check_results.py options]"
part=${1:?$usage}
carrier=${2:?$usage}
out=${3:?$usage}
shift 3
here=$(cd "$(dirname "$0")" && pwd)
passes=${IIO_CONV_HW_PASSES:-5}
[ -n "${LG_COORDINATOR:-}" ] || { echo "hw-run: LG_COORDINATOR is not set" >&2; exit 2; }
[ -n "${IIO_CONV_LAB_REDACT:-}" ] || { echo "hw-run: IIO_CONV_LAB_REDACT is not set" >&2; exit 2; }
case "$passes" in '' | *[!0-9]* | 0) echo "hw-run: IIO_CONV_HW_PASSES must be a positive number" >&2; exit 2 ;; esac
[ -x "$here/rxbench" ] || { echo "hw-run: $here/rxbench is not built" >&2; exit 2; }
mkdir -p "$out" && out=$(cd "$out" && pwd) && [ -n "$out" ] ||
	{ echo "hw-run: cannot use the output folder" >&2; exit 2; }
rm -f "$out"/results.txt "$out"/info.txt "$out"/session.log "$out"/*.raw.*

adi-lg request --part "$part" --carrier "$carrier" --mode uri --wait 1800 --power-down \
	--run "printf '%s' \"\$LG_PLACE\" > '$out/place.raw.txt';
	       '$here/rxbench' info \"\$IIO_URI\" > '$out/info.raw.txt' 2>&1;
	       '$here/run.sh' \"\$IIO_URI\" '$out/results.raw.txt' $passes" \
	> "$out/session.raw.log" 2>&1
rc=$?

coordinator=${LG_COORDINATOR%:*}
place=$(cat "$out/place.raw.txt" 2>/dev/null)
export IIO_CONV_LAB_REDACT="$IIO_CONV_LAB_REDACT $coordinator $place"
if ! python3 "$here/redact.py" "$out"; then
	rm -f "$out"/*.txt "$out"/*.log
	echo "hw-run: redaction failed, outputs deleted" >&2
	exit 1
fi
rm -f "$out"/*.raw.* "$out"/place.txt

# The log is board and tool output: the runner must not take a line of it for
# a workflow command
token="hw-run-$$-$RANDOM$RANDOM"
[ "${GITHUB_ACTIONS:-}" = true ] && echo "::stop-commands::$token"
for f in session.log info.txt; do
	echo "----- $f (redacted) -----"
	cat "$out/$f" 2>/dev/null
done
[ "${GITHUB_ACTIONS:-}" = true ] && echo "::$token::"
echo "hw-run: $part/$carrier: board session exit $rc"
[ -f "$out/results.txt" ] || { echo "hw-run: no results" >&2; exit 1; }
python3 "$here/check_results.py" "$out/results.txt" --passes "$passes" "$@" || exit 1
exit "$rc"
