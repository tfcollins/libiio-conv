#!/bin/sh
# G2: the MEX file exports exactly mexFunction and mexfilerequiredapiversion (version node
# MEX), imports only R2018a-API (_800) MATLAB symbols, needs libiio.so.1, and has
# libiio-conv linked in statically.
set -eu
mex=$1
fail=0
n=$(nm -D --defined-only "$mex" | grep -c -E ' T (mexFunction|mexfilerequiredapiversion)@@MEX$' || true)
[ "$n" = 2 ] || { echo "FAIL: expected mexFunction@@MEX and mexfilerequiredapiversion@@MEX, found $n"; fail=1; }
extra=$(nm -D --defined-only "$mex" | awk '$2 == "T" && $3 !~ /^(mexFunction|mexfilerequiredapiversion)@@MEX$/ {print $3}')
[ -z "$extra" ] || { echo "FAIL: unexpected exported functions: $extra"; fail=1; }
old=$(nm -D --undefined-only "$mex" | awk '$2 ~ /^m(x|ex)[A-Z]/ && $2 !~ /_800$/ {print $2}')
[ -z "$old" ] || { echo "FAIL: non-R2018a MATLAB imports: $old"; fail=1; }
readelf -d "$mex" | grep -q 'NEEDED.*libiio\.so\.1' || { echo "FAIL: libiio.so.1 is not a dependency"; fail=1; }
if readelf -d "$mex" | grep -q 'NEEDED.*libiio-conv'; then echo "FAIL: libiio-conv must be linked statically"; fail=1; fi
[ $fail = 0 ] && echo "ok: $mex"
exit $fail
