#!/bin/sh
# SPDX-License-Identifier: MIT
# G2: the MEX file has the test_* commands (the scripted backend of the tests) exactly when it
# was configured with IIO_CONV_MEX_TEST_HOOKS=ON. A MEX file that is handed out has none.
# usage: check_mex_hooks.sh <mex file> with|without
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <mex file> with|without"; exit 2; }
mex=$1
[ -f "$mex" ] || { echo "FAIL: no file $mex"; exit 1; }
# The command names are string literals of the registry in cmd_test.c.
n=$(grep -a -c 'test_create_fake_context' "$mex" || true)
case $2 in
with)
	[ "$n" -gt 0 ] || { echo "FAIL: no test_* commands in $mex (configure with -DIIO_CONV_MEX_TEST_HOOKS=ON)"; exit 1; }
	echo "ok: $mex has the test_* commands";;
without)
	[ "$n" -eq 0 ] || { echo "FAIL: the test_* commands are in $mex"; exit 1; }
	echo "ok: $mex has no test_* commands";;
*)
	echo "usage: $0 <mex file> with|without"; exit 2;;
esac
