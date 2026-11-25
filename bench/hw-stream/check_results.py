#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pass or fail a results.txt written by run.sh.

    check_results.py <results.txt> [--passes N] [--margin PCT] [--ahead PCT]
                     [--cpu-ratio X] [--summary FILE]

The stream layer (rxbench conv, with each of its two timeout settings) is
compared with the plain libiio block loop (rxbench plain) run in the same
session, so no limit depends on the link or on the board.

Completeness:
- the file holds nothing but the PASS, RESULT and rc= lines of run.sh (rxbench
  prints its errors, so any other line is one);
- every case of run.sh is there --passes times (default 2, as run.sh), ran to
  the end (rc=0), read all its frames and none of them invalid;
- the last frame of every case is live data (at least half of its samples
  differ from the sample before).

For every frame size and each timeout setting of the stream layer:
- its best steady rate is at most --margin percent (default 10) behind the
  best steady rate of the plain loop. The best of the passes is compared
  because the link is shared: other traffic only ever makes a pass slower;
- its best steady rate is at most --ahead percent (default 50) above that of
  the plain loop: frames cannot arrive faster than the plain loop gets them;
- its median steady rate is at least 60 % of that of the plain loop, a coarse
  guard against a stream that is slow in most passes;
- its median time to close is at most twice that of the plain loop plus 5 ms.

For the frame sizes of 262144 samples and more (below, the CPU time of a
pass is too short to measure):
- the median CPU time per frame of the stream layer is at most --cpu-ratio
  (default 1.75) times that of the plain loop. The CPU time does not depend
  on the link, so this is the limit that sees a slower conversion.

Prints a Markdown table (also appended to --summary) and the violations.
Exit status 0 when there is none, 1 otherwise.
"""

import argparse
import re
import statistics
import sys

FIELD = re.compile(r"(\w+)=(\S+)")
# The cases of run.sh
SIZES = (1024, 16384, 262144, 1048576)
FRAMES = 50
VARIANTS = (("conv", "5000"), ("conv", "-1"), ("plain", None))
NEEDED = ("spf", "frames", "valid", "invalid", "rate_steady_sps", "cpu_s", "close_ms",
          "first_err", "changes")
CPU_FROM = 262144
MEDIAN_FLOOR = 0.6


def name(mode, timeout):
    return "plain" if mode == "plain" else f"conv timeout {timeout}"


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
            got = True
            missing = [k for k in NEEDED if k not in r]
            if missing or (r["mode"] == "conv" and "timeout_ms" not in r):
                bad.append(f"'{r['case']}': RESULT line is cut short")
                continue
            results.append(r)
        elif line.startswith("rc="):
            if line != "rc=0":
                bad.append(f"'{case}' ended with {line}")
            if not got:
                bad.append(f"no result for '{case}'")
            case, got = None, True
        elif line.strip():
            bad.append(f"unexpected line in the results: {line.strip()[:100]}")
    if case is not None and not got:
        bad.append(f"no result for '{case}'")
    return results, bad


def check(results, passes, margin, ahead, cpu_ratio):
    bad, rows = [], []
    for r in results:
        if int(r["invalid"]) or int(r["first_err"]):
            bad.append(f"'{r['case']}': invalid={r['invalid']} first_err={r['first_err']}")
        if int(r["frames"]) != FRAMES or int(r["valid"]) != FRAMES:
            bad.append(f"'{r['case']}': frames={r['frames']} valid={r['valid']}, expected {FRAMES}")
        changed, total = map(int, r["changes"].split("/"))
        if 2 * changed < total:
            bad.append(f"'{r['case']}': changes={r['changes']}, the last frame is not live data")
    rate = lambda rs: [float(r["rate_steady_sps"]) for r in rs]
    close = lambda rs: statistics.median(float(r["close_ms"]) for r in rs)
    cpu = lambda rs: statistics.median(float(r["cpu_s"]) / FRAMES for r in rs) * 1e3
    for spf in SIZES:
        sets = {}
        for mode, timeout in VARIANTS:
            rs = [r for r in results if int(r["spf"]) == spf and r["mode"] == mode
                  and (mode == "plain" or r["timeout_ms"] == timeout)]
            if len(rs) != passes:
                bad.append(f"spf={spf} {name(mode, timeout)}: {len(rs)} result(s), expected {passes}")
            sets[(mode, timeout)] = rs
        plain = sets[("plain", None)]
        if not plain or max(rate(plain)) <= 0:
            bad.append(f"spf={spf}: no rate of the plain loop to compare with")
            continue
        for mode, timeout in VARIANTS[:2]:
            conv, who = sets[(mode, timeout)], f"spf={spf} {name(mode, timeout)}"
            if not conv:
                continue
            diff = 100.0 * (max(rate(conv)) / max(rate(plain)) - 1.0)
            mid = statistics.median(rate(conv)) / statistics.median(rate(plain))
            limit = 2.0 * close(plain) + 5.0
            ratio = cpu(conv) / cpu(plain) if spf >= CPU_FROM and cpu(plain) > 0 else None
            rows.append(f"| {spf} | {timeout} | {max(rate(conv)) / 1e6:.2f} | {max(rate(plain)) / 1e6:.2f} "
                        f"| {diff:+.1f} % | {'%.2f' % ratio if ratio else '-'} "
                        f"| {close(conv):.1f} | {close(plain):.1f} |")
            if diff < -margin:
                bad.append(f"{who}: the stream layer is {-diff:.1f} % behind the plain loop "
                           f"(limit {margin:g} %)")
            if diff > ahead:
                bad.append(f"{who}: the stream layer is {diff:.1f} % ahead of the plain loop "
                           f"(limit {ahead:g} %), frames cannot arrive that fast")
            if mid < MEDIAN_FLOOR:
                bad.append(f"{who}: the median rate of the stream layer is {100 * mid:.0f} % of "
                           f"that of the plain loop (limit {100 * MEDIAN_FLOOR:.0f} %)")
            if ratio is not None and ratio > cpu_ratio:
                bad.append(f"{who}: CPU time per frame is {ratio:.2f} times that of the plain "
                           f"loop (limit {cpu_ratio:g})")
            if close(conv) > limit:
                bad.append(f"{who}: close of the stream takes {close(conv):.1f} ms (median, "
                           f"limit {limit:.1f} ms)")
    return rows, bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("results")
    ap.add_argument("--passes", type=int, default=2, metavar="N")
    ap.add_argument("--margin", type=float, default=10.0, metavar="PCT")
    ap.add_argument("--ahead", type=float, default=50.0, metavar="PCT")
    ap.add_argument("--cpu-ratio", type=float, default=1.75, metavar="X")
    ap.add_argument("--summary", metavar="FILE")
    a = ap.parse_args(argv)
    with open(a.results, errors="replace") as f:
        results, bad = parse(f.read())
    if not results:
        bad.append("no result at all")
    rows, more = check(results, a.passes, a.margin, a.ahead, a.cpu_ratio)
    bad += more
    out = ["| samples per frame | timeout ms | stream layer best MS/s | plain loop best MS/s "
           "| difference | CPU per frame, stream / plain | median close ms (stream) "
           "| median close ms (plain) |", "|---|---|---|---|---|---|---|---|", *rows, "",
           *(f"- VIOLATION: {b}" for b in bad), f"check_results: {len(bad)} violation(s)"]
    print("\n".join(out))
    if a.summary:
        with open(a.summary, "a") as f:
            f.write("\n".join(out) + "\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
