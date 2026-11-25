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


def scale_cpu(text, mode, spf, factor):
    def f(m):
        return f"{m.group(1)}{float(m.group(2)) * factor:.3f}"
    return edit(rf"^(RESULT {mode} spf={spf} .*? cpu_s=)([\d.]+)", f, text)


# The committed file has two passes; a third one is the second again
THREE = GOOD + GOOD[GOOD.index("PASS 2:"):].replace("PASS 2:", "PASS 3:")


class CheckResults(unittest.TestCase):
    def test_committed_results_pass(self):
        rc, out = run(GOOD)
        self.assertEqual(rc, 0, out)
        self.assertIn("| 1048576 | -1 |", out)
        self.assertIn("0 violation(s)", out)

    def test_stream_layer_slower_than_margin_fails(self):
        rc, out = run(scale_rate(GOOD, "conv", 262144, 0.85))
        self.assertEqual(rc, 1, out)
        self.assertIn("spf=262144", out)
        self.assertIn("behind the plain loop", out)
        self.assertIn("2 violation(s)", out)    # both timeout settings

    def test_margin_is_an_option(self):
        slow = scale_rate(GOOD, "conv", 262144, 0.85)
        self.assertEqual(run(slow, "--margin", "20")[0], 0)
        self.assertEqual(run(GOOD, "--margin", "0.1")[0], 1)
        # the default is 10: 8 % behind passes, 12 % fails
        self.assertEqual(run(scale_rate(GOOD, "conv", 1024, 0.92))[0], 0)
        self.assertEqual(run(scale_rate(GOOD, "conv", 1024, 0.88))[0], 1)

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

    def test_stream_layer_a_little_faster_passes(self):
        self.assertEqual(run(scale_rate(GOOD, "conv", 1024, 1.2))[0], 0)

    def test_stream_layer_faster_than_frames_can_arrive_fails(self):
        rc, out = run(scale_rate(GOOD, "conv", 1048576, 3))
        self.assertEqual(rc, 1, out)
        self.assertIn("ahead of the plain loop", out)
        self.assertEqual(run(scale_rate(GOOD, "conv", 1048576, 3), "--ahead", "250")[0], 0)

    def test_slow_in_most_passes_fails(self):
        # the best pass of each case stays, the two others are at 30 %
        text = edit(r"^(PASS [23]: conv .*\nRESULT conv spf=16384 .*?rate_steady_sps=)\d+", r"\g<1>3300000", THREE)
        rc, out = run(text, "--passes", "3")
        self.assertEqual(rc, 1, out)
        self.assertIn("median rate", out)

    def test_cpu_time_per_frame_fails_at_the_large_sizes_only(self):
        rc, out = run(scale_cpu(GOOD, "conv", 1048576, 2))
        self.assertEqual(rc, 1, out)
        self.assertIn("CPU time per frame", out)
        self.assertIn("spf=1048576", out)
        self.assertEqual(run(scale_cpu(GOOD, "conv", 1048576, 2), "--cpu-ratio", "4")[0], 0)
        self.assertEqual(run(scale_cpu(GOOD, "conv", 1048576, 1.2))[0], 0)
        self.assertEqual(run(scale_cpu(GOOD, "conv", 1024, 3))[0], 0)

    def test_number_of_passes_is_checked(self):
        rc, out = run(GOOD, "--passes", "3")
        self.assertEqual(rc, 1, out)
        self.assertIn("2 result(s), expected 3", out)
        self.assertEqual(run(THREE, "--passes", "3")[0], 0)

    def test_a_frame_size_that_is_not_there_fails(self):
        text = re.sub(r"^PASS \d: \w+ \S+ 262144 .*\nRESULT.*\nrc=0\n", "", GOOD, flags=re.M)
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("spf=262144 plain: 0 result(s)", out)

    def test_a_timeout_setting_that_is_not_there_fails(self):
        text = re.sub(r"^PASS \d: conv .* -1\nRESULT.*\nrc=0\n", "", GOOD, flags=re.M)
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("conv timeout -1: 0 result(s)", out)

    def test_error_output_of_rxbench_fails(self):
        text = edit(r"^(RESULT conv )", r"set_timeout: error -22\n\1", count=1)
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("unexpected line", out)

    def test_fewer_frames_than_run_sh_asks_for_fails(self):
        rc, out = run(edit(r"frames=50 valid=50", "frames=5 valid=5", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("expected 50", out)

    def test_first_error_fails(self):
        rc, out = run(edit(r"first_err=0", "first_err=-110", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("first_err=-110", out)

    def test_cut_or_zero_results_are_violations_not_tracebacks(self):
        rc, out = run(edit(r"^(RESULT conv spf=1024 .*?) cpu_s=.*", r"\1", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("cut short", out)
        self.assertNotIn("Traceback", out)
        rc, out = run(edit(r"^(RESULT plain spf=16384 .*?rate_steady_sps=)\d+", r"\g<1>0"))
        self.assertEqual(rc, 1, out)
        self.assertIn("no rate of the plain loop", out)
        self.assertNotIn("Traceback", out)

    def test_invalid_frame_fails(self):
        rc, out = run(edit(r"valid=50 invalid=0", "valid=49 invalid=1", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("invalid", out)

    def test_nonzero_exit_of_a_case_fails(self):
        rc, out = run(edit(r"^rc=0$", "rc=124", count=1))
        self.assertEqual(rc, 1, out)
        self.assertIn("rc=124", out)

    def test_case_without_a_result_fails(self):
        text = edit(r"^RESULT plain spf=16384 .*\n", "", count=1)
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("no result for 'plain", out)

    def test_last_case_cut_off_fails(self):
        cut = GOOD[:GOOD.rindex("RESULT ")]
        rc, out = run(cut)
        self.assertEqual(rc, 1, out)
        self.assertIn("no result for 'plain", out)

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
        text = edit(r"^(RESULT conv spf=1048576 .*?close_ms=)[\d.]+", r"\g<1>400.0", THREE, count=1)
        self.assertEqual(run(text, "--passes", "3")[0], 0)

    def test_slow_close_of_one_timeout_setting_fails(self):
        text = edit(r"^(RESULT conv spf=1048576 .*?close_ms=)[\d.]+( timeout_ms=-1 )", r"\g<1>150.0\2")
        rc, out = run(text)
        self.assertEqual(rc, 1, out)
        self.assertIn("conv timeout -1: close", out)

    def test_close_allowance_at_small_frames(self):
        ok = edit(r"^(RESULT conv spf=1024 .*?close_ms=)[\d.]+", r"\g<1>5.0")
        self.assertEqual(run(ok)[0], 0)
        slow = edit(r"^(RESULT conv spf=1024 .*?close_ms=)[\d.]+", r"\g<1>7.0")
        self.assertEqual(run(slow)[0], 1)

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

    def test_more_forms_of_lab_facts_go(self):
        sys.path.insert(0, str(HERE))
        import redact
        r = redact.redact
        self.assertEqual(r("connect to 192.0.2.7."), "connect to <ip>.")
        self.assertEqual(r("ip:[fe80::ba27:ebff:fe12:3456%enp3s0] and 2001:db8::1 ok"), "ip:[<ip>] and <ip> ok")
        self.assertEqual(r("mac b8:27:eb:12:34:56 x"), "mac <mac> x")
        self.assertEqual(r("RIG7.LOCAL my_host.local a.b.local"), "<host> <host> <host>")
        self.assertEqual(r("/root/x /Users/bob/y"), "~/x ~/y")
        self.assertEqual(r("serial = 104473222a87000b0e000a00f1ffa2eb21, unique_id: abc123"),
                         "serial = <id>, unique_id: <id>")
        # times, versions and the numbers of the results stay
        keep = "2026-11-24 22:23:12,582 libiio 1.1 cee4310 mean_ms=0.477 changes=2026/2046 0x2A00000 12:34:56"
        self.assertEqual(r(keep), keep)

    def test_an_argument_that_is_not_a_file_or_folder_is_an_error(self):
        for arg in ("", "/nonexistent/hw-out"):
            p = subprocess.run([sys.executable, str(HERE / "redact.py"), arg], capture_output=True, text=True)
            self.assertEqual(p.returncode, 2, arg)

    def test_raw_file_becomes_the_redacted_one(self):
        with tempfile.TemporaryDirectory() as d:
            raw = Path(d) / "results.raw.txt"
            raw.write_text("uri ip:10.9.8.7\n")
            p = subprocess.run([sys.executable, str(HERE / "redact.py"), d], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertEqual((Path(d) / "results.txt").read_text(), "uri ip:<ip>\n")


if __name__ == "__main__":
    unittest.main()
