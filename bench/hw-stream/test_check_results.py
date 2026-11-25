#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of check_results.py against the committed results.txt and edits of it."""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
GOOD = (HERE / "results.txt").read_text()


def run(text, *args):
    with tempfile.TemporaryDirectory() as d:
        f = Path(d) / "results.txt"
        f.write_text(text)
        p = subprocess.run([sys.executable, str(HERE / "check_results.py"), str(f), *args],
                           capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def edit(pattern, repl, text=GOOD, count=0):
    out, n = re.subn(pattern, repl, text, count=count, flags=re.M)
    assert n, pattern
    return out


def scale_rate(text, mode, spf, factor):
    def f(m):
        return f"{m.group(1)}{int(int(m.group(2)) * factor)}"
    return edit(rf"^(RESULT {mode} spf={spf} .*?rate_steady_sps=)(\d+)", f, text)


class CheckResults(unittest.TestCase):
    def test_committed_results_pass(self):
        rc, out = run(GOOD)
        self.assertEqual(rc, 0, out)
        self.assertIn("| 1048576 |", out)
        self.assertIn("0 violation(s)", out)

    def test_stream_layer_slower_than_margin_fails(self):
        rc, out = run(scale_rate(GOOD, "conv", 262144, 0.85))
        self.assertEqual(rc, 1, out)
        self.assertIn("spf=262144", out)
        self.assertIn("behind the plain loop", out)
        self.assertIn("1 violation(s)", out)

    def test_margin_is_an_option(self):
        slow = scale_rate(GOOD, "conv", 262144, 0.85)
        self.assertEqual(run(slow, "--margin", "20")[0], 0)
        self.assertEqual(run(GOOD, "--margin", "0.1")[0], 1)

    def test_one_slow_pass_on_a_busy_link_passes(self):
        text = edit(r"^(RESULT conv spf=262144 .*?rate_steady_sps=)\d+", r"\g<1>5273228", count=1)
        text = edit(r"^(RESULT plain spf=16384 .*?rate_steady_sps=)\d+", r"\g<1>4000000", text, count=1)
        rc, out = run(text)
        self.assertEqual(rc, 0, out)

    def test_one_timeout_setting_slow_in_every_pass_fails(self):
        text = edit(r"^(RESULT conv spf=16384 .*?rate_steady_sps=)\d+( .*timeout_ms=-1 )", r"\g<1>8000000\2")
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("spf=16384", out)

    def test_stream_layer_faster_passes(self):
        self.assertEqual(run(scale_rate(GOOD, "conv", 1024, 1.5))[0], 0)

    def test_invalid_frame_fails(self):
        rc, out = run(edit(r"valid=50 invalid=0", "valid=49 invalid=1", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("invalid", out)

    def test_nonzero_exit_of_a_case_fails(self):
        rc, out = run(edit(r"^rc=0$", "rc=124", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("rc=124", out)

    def test_missing_case_fails(self):
        text = edit(r"^RESULT plain spf=16384 .*\n", "")
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("no result", out)

    def test_data_that_does_not_change_fails(self):
        rc, out = run(edit(r"changes=\d+/2046", "changes=3/2046", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("changes", out)

    def test_slow_close_fails(self):
        text = edit(r"^(RESULT conv spf=1048576 .*?close_ms=)[\d.]+", r"\g<1>400.0")
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("close", out)

    def test_one_slow_close_passes(self):
        text = edit(r"^(RESULT conv spf=1048576 .*?close_ms=)[\d.]+", r"\g<1>400.0", count=1)
        self.assertEqual(run(text)[0], 0)

    def test_empty_file_fails(self):
        rc, out = run("")
        self.assertEqual(rc, 1, out)

    def test_summary_file_gets_the_table(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "r.txt"
            f.write_text(GOOD)
            s = Path(d) / "summary.md"
            p = subprocess.run([sys.executable, str(HERE / "check_results.py"), str(f),
                                "--summary", str(s)], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertIn("| 1024 |", s.read_text())


class Redact(unittest.TestCase):
    def test_addresses_hosts_words_and_paths_go(self):
        sys.path.insert(0, str(HERE))
        import redact
        text = ("PASS 1: conv ip:10.1.2.3 1024 50 -1\nboard at 192.168.0.7:30431 on rig7.local, "
                "place benchA, file /home/someone/x and " + redact.ROOT + "/bench\nversion 1.2.3\n")
        out = redact.redact(text, ["benchA"])
        for gone in ("10.1.2.3", "192.168.0.7", "rig7", "benchA", "someone", redact.ROOT):
            self.assertNotIn(gone, out)
        self.assertIn("ip:<ip> 1024 50 -1", out)
        self.assertIn("version 1.2.3", out)
        self.assertEqual(redact.redact("ctx attr unique_id = 1000abc\n"), "ctx attr unique_id = <id>\n")

    def test_raw_file_becomes_the_redacted_one(self):
        with tempfile.TemporaryDirectory() as d:
            raw = Path(d) / "results.raw.txt"
            raw.write_text("uri ip:10.9.8.7\n")
            p = subprocess.run([sys.executable, str(HERE / "redact.py"), d], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertEqual((Path(d) / "results.txt").read_text(), "uri ip:<ip>\n")


if __name__ == "__main__":
    unittest.main()
