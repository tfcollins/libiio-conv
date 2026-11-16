#!/bin/sh
# G3: no MATLAB Code Analyzer error (level ML2 or ML3) in any .m file.
# usage: check_mlint.sh [file.m ...]      default: every .m file under matlab/
# mlint is taken from $MLINT, else $MATLAB_ROOT/bin/glnxa64/mlint. It needs no licence.
# Exit: 0 clean, 1 errors found, 77 mlint not available.
set -u
here=$(cd "$(dirname "$0")" && pwd)
mlint=${MLINT:-${MATLAB_ROOT:+$MATLAB_ROOT/bin/glnxa64/mlint}}
if [ -z "$mlint" ] || [ ! -x "$mlint" ]; then
	echo "check_mlint: mlint not found (set MATLAB_ROOT or MLINT)"
	exit 77
fi
if [ $# -eq 0 ]; then
	set -- $(find "$here/.." -name '*.m' | sort)
fi
[ $# -gt 0 ] || { echo "check_mlint: no .m files"; exit 1; }
# mlint always exits 0 and writes to stderr. The extra /dev/null argument forces the
# "========== file ==========" header even when only one file is checked.
out=$("$mlint" -id -severity -m2 "$@" /dev/null 2>&1 | grep -v '^========== /dev/null' | grep -v "'/dev/null'")
if printf '%s\n' "$out" | grep -q '^L [0-9]'; then
	printf '%s\n' "$out" | awk '/^==========/ { file = $2; next } /^L [0-9]/ { print file ": " $0 }'
	echo "check_mlint: FAILED"
	exit 1
fi
echo "check_mlint: $# file(s), no errors"
