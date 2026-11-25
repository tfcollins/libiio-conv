#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pass or fail a results.txt written by run.sh.

    check_results.py <results.txt> [--margin PCT] [--summary FILE]

The stream layer (rxbench conv) is compared with the plain libiio block loop
(rxbench plain) run in the same session, so the limit does not depend on the
link or on the board. For every frame size:

- every case ran to the end (rc=0), gave a RESULT line and no invalid frame;
- the last frame of every case is live data (at least half of its samples
  differ from the sample before);
- the best steady rate of the stream layer, with each of its timeout
  settings, is at most --margin percent (default 10) behind the best steady
  rate of the plain loop. The best of the passes is compared because the link
  is shared: other traffic only ever makes a pass slower;
- the median time to close the stream is at most twice the median close of
  the plain loop plus 10 ms.

Prints a Markdown table (also appended to --summary) and the violations.
Exit status 0 when there is none, 1 otherwise.
"""

import argparse
import re
import statistics
import sys

FIELD = re.compile(r"(\w+)=(\S+)")


def parse(text):
    """Return (results, violations). A result is a dict of one RESULT line."""
    results, bad, case, got = [], [], None, False
    for line in text.splitlines():
        if line.startswith("PASS "):
            if case is not None and not got:
                bad.append(f"no result for '{case}'")
            case, got = line.split(": ", 1)[-1], False
        elif line.startswith("RESULT "):
            r = dict(FIELD.findall(line))
            r["mode"] = line.split()[1]
            r["case"] = case or line
            results.append(r)
            got = True
        elif line.startswith("rc="):
            if line != "rc=0":
                bad.append(f"'{case}' ended with {line}")
            if not got:
                bad.append(f"no result for '{case}'")
            case, got = None, True
    if case is not None and not got:
        bad.append(f"no result for '{case}'")
    return results, bad


def check(results, margin):
    bad, rows = [], []
    for r in results:
        if int(r["invalid"]) or int(r["first_err"]):
            bad.append(f"'{r['case']}': invalid={r['invalid']} first_err={r['first_err']}")
        changed, total = map(int, r["changes"].split("/"))
        if 2 * changed < total:
            bad.append(f"'{r['case']}': changes={r['changes']}, the last frame is not live data")
    for spf in sorted({int(r["spf"]) for r in results}):
        of = lambda mode: [r for r in results if int(r["spf"]) == spf and r["mode"] == mode]
        conv, plain = of("conv"), of("plain")
        if not conv or not plain:
            bad.append(f"spf={spf}: no result for {'conv' if not conv else 'plain'}")
            continue
        rate = lambda rs: max(float(r["rate_steady_sps"]) for r in rs)
        close = lambda rs: statistics.median(float(r["close_ms"]) for r in rs)
        # the slower of the timeout settings of the stream layer
        variants = {r.get("timeout_ms") for r in conv}
        worst = min(rate([r for r in conv if r.get("timeout_ms") == v]) for v in variants)
        diff = 100.0 * (worst / rate(plain) - 1.0)
        limit = 2.0 * close(plain) + 10.0
        rows.append(f"| {spf} | {worst / 1e6:.2f} | {rate(plain) / 1e6:.2f} | {diff:+.1f} % "
                    f"| {close(conv):.1f} | {close(plain):.1f} |")
        if diff < -margin:
            bad.append(f"spf={spf}: the stream layer is {-diff:.1f} % behind the plain loop "
                       f"(limit {margin:g} %)")
        if close(conv) > limit:
            bad.append(f"spf={spf}: close of the stream takes {close(conv):.1f} ms (median) "
                       f"(limit {limit:.1f} ms)")
    return rows, bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("results")
    ap.add_argument("--margin", type=float, default=10.0, metavar="PCT")
    ap.add_argument("--summary", metavar="FILE")
    a = ap.parse_args(argv)
    with open(a.results, errors="replace") as f:
        results, bad = parse(f.read())
    if not results:
        bad.append("no result at all")
    rows, more = check(results, a.margin)
    bad += more
    out = ["| samples per frame | stream layer best MS/s | plain loop best MS/s | difference "
           "| median close ms (stream) | median close ms (plain) |", "|---|---|---|---|---|---|", *rows, "",
           *(f"- VIOLATION: {b}" for b in bad), f"check_results: {len(bad)} violation(s)"]
    print("\n".join(out))
    if a.summary:
        with open(a.summary, "a") as f:
            f.write("\n".join(out) + "\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
