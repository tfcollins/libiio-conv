#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""CI: check of the JUnit file that the MATLAB test run writes (test-results/matlab.xml).

usage: check_junit.py JUNIT.XML --min-tests N --max-seconds S --max-skipped N

A green test step says only that no test failed. This check also refuses a run that
passed by not running. Violations
  1. fewer testcase elements than --min-tests (a test file that was not found, a suite
     that stopped early)
  2. a testcase with a failure or an error element
  3. more testcase elements with a skipped element than --max-skipped (MATLAB writes one
     for a filtered test, that is a failed assumption)
  4. a testcase whose time is --max-seconds or more, or is not a finite number; a slow
     test is a bug, usually a stall of the emu backend. Exempt: classname "tTiming",
     which measures on purpose

Output: one "check_junit: <message>" line per violation, and last "check_junit: <n>
tests, <k> violation(s)". Exit 0 without violations, else 1; exit 2 when the file is
missing or cannot be parsed, or on a usage error (all three limits are required).
"""
import argparse
import math
import sys
import xml.etree.ElementTree as ET

EXEMPT_FROM_TIME = ("tTiming",)


def check(root, min_tests, max_seconds, max_skipped):
    """Return (number of tests, list of violation messages)."""
    out = []
    cases = list(root.iter("testcase"))
    skipped = 0
    for c in cases:
        cls = c.get("classname", "")
        name = "%s.%s" % (cls, c.get("name", "?"))
        for kind in ("failure", "error"):
            if c.find(kind) is not None:
                out.append("%s: %s" % (name, kind))
        if c.find("skipped") is not None:
            skipped += 1
        if cls in EXEMPT_FROM_TIME:
            continue
        text = c.get("time")
        try:
            t = float(text)
        except (TypeError, ValueError):
            t = math.nan
        if not math.isfinite(t):
            out.append("%s: time %r is not a number" % (name, text))
        elif t >= max_seconds:
            out.append("%s: took %g s, the limit is %g s" % (name, t, max_seconds))
    if skipped > max_skipped:
        out.append("%d skipped, more than the %d allowed" % (skipped, max_skipped))
    if len(cases) < min_tests:
        out.append("%d tests, fewer than the minimum %d" % (len(cases), min_tests))
    return len(cases), out


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check a JUnit result file of the MATLAB tests.")
    ap.add_argument("junit", metavar="JUNIT.XML")
    ap.add_argument("--min-tests", type=int, required=True)
    ap.add_argument("--max-seconds", type=float, required=True)
    ap.add_argument("--max-skipped", type=int, required=True)
    args = ap.parse_args(argv)   # exits 2 on a usage error

    try:
        root = ET.parse(args.junit).getroot()
    except (OSError, ET.ParseError) as e:
        print("check_junit: cannot read %s: %s" % (args.junit, e))
        return 2

    n, violations = check(root, args.min_tests, args.max_seconds, args.max_skipped)
    for v in violations:
        print("check_junit: " + v)
    print("check_junit: %d tests, %d violation(s)" % (n, len(violations)))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
