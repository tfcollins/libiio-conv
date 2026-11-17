#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Self-test of the gate scripts check_api.py (G4), check_mlint.sh (G3) and check_junit.py (CI).

Each check_api case builds a tiny source tree in a temporary directory (a fake iio.h
with three functions, matlab/api-map.tsv, matlab/mex/cmd_x.c, matlab/+iio/*.m) and runs
check_api.py --root on it. Run:  python3 matlab/tools/test_tools.py
"""
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
CHECK_API = os.path.join(TOOLS, "check_api.py")
CHECK_MLINT = os.path.join(TOOLS, "check_mlint.sh")
CHECK_JUNIT = os.path.join(TOOLS, "check_junit.py")
sys.path.insert(0, TOOLS)
sys.dont_write_bytecode = True   # no __pycache__ in the source tree
import check_api  # noqa: E402

HEADER = "c_function\tcommand\tmatlab\tstatus\tnotes\n"

IIO_H = """\
/* A fake iio.h. __api int iio_in_a_comment(void); is not a function. */
#define __api __attribute__((visibility("default")))
struct iio_context;
// __api int iio_in_a_line_comment(void);

/** @brief Create a context */
__api __check_ret struct iio_context *
iio_create_context(const struct iio_context_params *params, const char *uri);

__api __check_ret __pure const char *iio_thing_get_name(const struct iio_thing *thing);

static inline __check_ret bool iio_err(const void *ptr)
{
	return (unsigned long)ptr >= (unsigned long)-4095;
}
"""

ROWS = [
    "iio_create_context\tcreate_context\tiio.Context\texposed\t",
    "iio_thing_get_name\tthing_get_name\tiio.Thing.name, iio.thing_name\texposed\ttext",
    "iio_err\t-\t-\tinternal\tused by the gateway on every pointer result",
]

CMD_X = """\
#include "mx_util.h"
/* GW_CMD("in_a_comment", gw_stub, 0, 0, 0) is not a registration */
static const struct gw_cmd cmds[] = {
	GW_CMD("create_context", cmd_create_context, 2, 2, 1),
	GW_CMD("thing_get_name", cmd_thing_get_name, 1, 1, 1),
	GW_XCMD("x_extra", cmd_x_extra, 0, 0, 1),
};
"""

CONTEXT_M = """\
classdef Context < handle
    % iio_mex(cmd) in a comment is not a call
    methods
        function obj = Context(uri)
            obj.Handle = iio_mex('create_context', [], char(uri));
            s = 'it''s iio_mex(cmd) in a char vector'; %#ok<NASGU>
            t = "and iio_mex(cmd) in a string";        %#ok<NASGU>
        end
    end
end
"""

THING_M = """\
classdef Thing < handle
    properties (Dependent)
        name
    end
    methods
        function v = get.name(obj)
            v = char(iio_mex( ...
                'thing_get_name', obj.Handle));
        end
        function n = extra(~)
            n = iio_mex('x_extra');
        end
    end
end
"""

THING_NAME_M = """\
function v = thing_name(t)
v = t.name;
end
"""


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


class CheckApi(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.iio_h = os.path.join(self.root, "iio.h")
        self.rows = list(ROWS)
        write(self.root, "iio.h", IIO_H)
        write(self.root, "matlab/mex/cmd_x.c", CMD_X)
        write(self.root, "matlab/+iio/Context.m", CONTEXT_M)
        write(self.root, "matlab/+iio/Thing.m", THING_M)
        write(self.root, "matlab/+iio/thing_name.m", THING_NAME_M)
        self.write_map()

    def tearDown(self):
        self._tmp.cleanup()

    def write_map(self, header=HEADER):
        write(self.root, "matlab/api-map.tsv", header + "".join(r + "\n" for r in self.rows))

    def put(self, rel, text):
        write(self.root, rel, text)

    def run_api(self, *args):
        # In this process: a Python start per run would make the self-test take seconds.
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            try:
                rc = check_api.main(["--iio-h", self.iio_h, "--root", self.root, *args])
            except SystemExit as e:   # argparse: usage error
                rc = e.code
        return rc, out.getvalue()

    def ok(self, *args):
        rc, out = self.run_api(*args)
        self.assertEqual(rc, 0, out)
        return out

    def fails(self, text, *args):
        rc, out = self.run_api(*args)
        self.assertEqual(rc, 1, out)
        hits = [l for l in out.splitlines() if text in l]
        self.assertTrue(hits, "no line contains %r in:\n%s" % (text, out))
        for l in hits:
            self.assertTrue(l.startswith("check_api: "), l)
            self.assertFalse(l.startswith("check_api: pending: "), l)
        self.assertRegex(out.splitlines()[-1], r"^check_api: \d+ rows, \d+ commands, [1-9]\d* error\(s\)")
        return out

    # the correct tree
    def test_correct_tree(self):
        for args in (["--strict"], [], ["--group", "x"]):
            out = self.ok(*args)
            self.assertEqual(out.splitlines(), ["check_api: 3 rows, 3 commands, 0 error(s)"], args)

    def test_usage_errors(self):
        rc, _ = self.run_api("--strict", "--group", "x")
        self.assertEqual(rc, 2)
        # as a program, once: exit status and output of the script itself
        p = subprocess.run([sys.executable, CHECK_API, "--root", self.root],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(p.returncode, 2)
        p = subprocess.run([sys.executable, CHECK_API, "--iio-h", self.iio_h, "--root", self.root, "--strict"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual((p.returncode, p.stdout), (0, "check_api: 3 rows, 3 commands, 0 error(s)\n"))
        os.remove(os.path.join(self.root, "matlab/+iio/thing_name.m"))
        p = subprocess.run([sys.executable, CHECK_API, "--iio-h", self.iio_h, "--root", self.root, "--strict"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(p.returncode, 1, p.stdout)

    def test_missing_input_is_an_error(self):
        os.remove(os.path.join(self.root, "matlab/api-map.tsv"))
        self.fails("api-map.tsv")
        self.write_map()
        os.remove(self.iio_h)
        self.fails("iio.h")

    # rule 1
    def test_rule1_function_without_row(self):
        del self.rows[2]
        self.write_map()
        self.fails("iio_err: without a row")

    def test_rule1_row_not_in_header(self):
        self.rows.append("iio_gone\t-\t-\tnot_exposed\tremoved from libiio")
        self.write_map()
        self.fails("iio_gone: not in iio.h")

    def test_rule1_function_twice(self):
        self.rows.append(self.rows[2])
        self.write_map()
        self.fails("iio_err: more than one row")

    def test_rule1_commented_functions_are_ignored(self):
        self.rows.append("iio_in_a_comment\t-\t-\tnot_exposed\tnot a function")
        self.write_map()
        self.fails("iio_in_a_comment: not in iio.h")

    # rule 2
    def test_rule2_header_line(self):
        self.write_map(header="c_function\tcommand\tmatlab\tstatus\n")
        self.fails("header")

    def test_rule2_field_count(self):
        self.rows[2] = "iio_err\t-\t-\tinternal"
        self.write_map()
        self.fails("five fields")

    def test_rule2_not_exposed_with_command(self):
        self.rows[2] = "iio_err\tcreate_context\t-\tnot_exposed\tpointer test, nothing to call"
        self.write_map()
        self.fails("must not have a command")

    def test_rule2_internal_with_member(self):
        self.rows[2] = "iio_err\t-\tiio.Context\tinternal\tpointer test"
        self.write_map()
        self.fails("must not have a MATLAB member")

    def test_rule2_note_required(self):
        self.rows[2] = "iio_err\t-\t-\tinternal\t"
        self.write_map()
        self.fails("needs a note")

    def test_rule2_exposed_needs_command_and_member(self):
        self.rows[0] = "iio_create_context\t-\tiio.Context\texposed\t"
        self.write_map()
        self.fails("needs a command")
        self.rows[0] = "iio_create_context\tCreate-Context\tiio.Context\texposed\t"
        self.write_map()
        self.fails("needs a command")
        self.rows[0] = "iio_create_context\tcreate_context\t-\texposed\t"
        self.write_map()
        self.fails("needs a MATLAB member")
        self.rows[0] = "iio_create_context\tcreate_context\tContext\texposed\t"
        self.write_map()
        self.fails("malformed MATLAB member")

    def test_rule2_unknown_status(self):
        self.rows[2] = "iio_err\t-\t-\tlater\tsome day"
        self.write_map()
        self.fails("unknown status")

    # rule 3
    def test_rule3_command_not_registered(self):
        self.put("matlab/mex/cmd_x.c", CMD_X.replace('GW_CMD("thing_get_name"', 'GW_CMD("thing_name"'))
        out = self.fails("thing_get_name: is not registered")
        # the renamed command is called by no row, and the M file now calls an unknown one
        self.assertIn("thing_name: registered with GW_CMD but in no row", out)
        self.assertIn("unknown command 'thing_get_name'", out)

    def test_rule3_extra_command_in_row(self):
        self.put("matlab/mex/cmd_x.c", CMD_X.replace('GW_CMD("thing_get_name"', 'GW_XCMD("thing_get_name"'))
        self.fails("thing_get_name: is an extra command")

    def test_rule3_registered_twice(self):
        self.put("matlab/mex/cmd_y.c", 'static const struct gw_cmd c[] = {\n\tGW_XCMD("x_extra", cmd_y, 0, 0, 0),\n};\n')
        self.fails("x_extra: registered twice")

    def test_rule3_gw_cmd_without_row(self):
        self.put("matlab/mex/cmd_y.c", 'static const struct gw_cmd c[] = {\n\tGW_CMD("orphan", cmd_y, 0, 0, 0),\n};\n')
        self.put("matlab/+iio/orphan.m", "function orphan()\niio_mex('orphan');\nend\n")
        self.fails("orphan: registered with GW_CMD but in no row")

    def test_rule3_group_names(self):
        # iio_mex.c is group "mex": a stub there is an error only for --group mex
        self.put("matlab/mex/iio_mex.c", 'static const struct gw_cmd c[] = {\n\tGW_XCMD("mex_count", gw_stub, 0, 0, 1),\n};\n')
        self.put("matlab/+iio/count.m", "function n = count()\nn = iio_mex('mex_count');\nend\n")
        self.fails("mex_count: is still a stub", "--group", "mex")
        out = self.ok("--group", "x")
        self.assertIn("check_api: pending: mex_count: is still a stub", out)
        self.assertEqual(out.splitlines()[-1], "check_api: 3 rows, 4 commands, 0 error(s); pending: 1")

    # rule 4
    def test_rule4_variable_command(self):
        self.put("matlab/+iio/call.m", "function r = call(cmd, x)\nr = iio_mex(cmd, x);\nend\n")
        out = self.fails("without a literal command")
        self.assertIn("call.m:2", out)

    def test_rule4_unknown_command(self):
        self.put("matlab/+iio/nope.m", "function nope()\niio_mex('nope');\nend\n")
        out = self.fails("unknown command 'nope'")
        self.assertIn("nope.m:2", out)

    def test_rule4_forms_that_are_not_literals(self):
        for arg in ('"create_context"', "['create_' 'context']", "'create_context' + 0", ""):
            self.put("matlab/+iio/call.m", "function r = call()\nr = iio_mex(%s);\nend\n" % arg)
            self.fails("without a literal command")

    def test_rule4_call_between_two_transposes(self):
        # the quotes of a' and x.' are operators: the call between them is code, not text
        for line in ("b = a'; r = iio_mex(cmd); c = a';", "b = a.'; r = iio_mex(cmd); c = a.';",
                     "b = f(a)'; r = iio_mex(cmd); c = {a}';"):
            self.put("matlab/+iio/call.m", "function [r, b, c] = call(a, cmd)\n%s\nend\n" % line)
            self.fails("without a literal command")

    def test_rule4_covers_tests_and_examples(self):
        self.put("matlab/tests/tThing.m", "classdef tThing\n    methods\n        function t(~)\n"
                 "            iio_mex('nope');\n        end\n    end\nend\n")
        out = self.fails("unknown command 'nope'")
        self.assertIn("tThing.m:4", out)

    def test_rule4_comments_and_strings_are_not_calls(self):
        self.put("matlab/+iio/doc.m",
                 "function doc()\n"
                 "%DOC iio_mex(cmd)\n"
                 "%{\n"
                 "iio_mex(cmd)\n"
                 "%}\n"
                 "a = [1 2]'; b = a'; % iio_mex(cmd) after two transposes\n"
                 "c = {'x', 'iio_mex(cmd)'}; ... iio_mex(cmd) after a continuation\n"
                 "disp(numel(b) + numel(c));\n"
                 "my_iio_mex(cmd);\n"
                 "end\n")
        self.ok("--strict")

    # rule 5
    def test_rule5_command_not_called(self):
        self.put("matlab/+iio/Thing.m", THING_M.replace("n = iio_mex('x_extra');", "n = 0;"))
        self.fails("x_extra: is not called from", "--strict")
        out = self.ok()
        self.assertIn("check_api: pending: x_extra: is not called from", out)
        self.assertEqual(out.splitlines()[-1], "check_api: 3 rows, 3 commands, 0 error(s); pending: 1")
        self.ok("--group", "x")

    def test_rule5_call_from_tests_does_not_count(self):
        self.put("matlab/+iio/Thing.m", THING_M.replace("n = iio_mex('x_extra');", "n = 0;"))
        self.put("matlab/tests/tThing.m", "function tThing()\niio_mex('x_extra');\nend\n")
        self.fails("x_extra: is not called from", "--strict")

    def test_rule5_pending_line_names_members_and_file(self):
        self.put("matlab/+iio/Thing.m", THING_M.replace("'thing_get_name'", "'x_extra'"))
        out = self.ok()
        line = [l for l in out.splitlines() if "thing_get_name" in l]
        self.assertEqual(len(line), 1, out)
        self.assertTrue(line[0].startswith("check_api: pending: thing_get_name: is not called from"), line[0])
        for part in ("iio.Thing.name", "iio.thing_name", "Thing.m", "thing_name.m", "cmd_x.c"):
            self.assertIn(part, line[0])

    def test_rule5_test_group_is_called_from_tests(self):
        self.put("matlab/mex/cmd_test.c", 'static const struct gw_cmd c[] = {\n\tGW_XCMD("test_hook", cmd_h, 0, 0, 0),\n};\n')
        self.put("matlab/+iio/hook.m", "function hook()\niio_mex('test_hook');\nend\n")
        self.fails("test_hook: is not called from a file under matlab/tests", "--strict")
        os.remove(os.path.join(self.root, "matlab/+iio/hook.m"))
        self.put("matlab/tests/tHook.m", "function tHook()\niio_mex('test_hook');\nend\n")
        self.ok("--strict")

    # rule 6
    def test_rule6_stub(self):
        self.put("matlab/mex/cmd_x.c", CMD_X.replace("cmd_thing_get_name", "gw_stub"))
        self.fails("thing_get_name: is still a stub", "--group", "x")
        self.fails("thing_get_name: is still a stub", "--strict")
        for args in (["--group", "y"], []):
            out = self.ok(*args)
            self.assertIn("check_api: pending: thing_get_name: is still a stub", out)
            self.assertIn("cmd_x.c", out)
            self.assertEqual(out.splitlines()[-1], "check_api: 3 rows, 3 commands, 0 error(s); pending: 1")

    # rule 7
    def test_rule7_member_missing(self):
        self.put("matlab/+iio/Thing.m", THING_M.replace("name", "label").replace("thing_get_label", "thing_get_name"))
        out = self.fails("iio.Thing.name: not found in", "--strict")
        self.assertIn("Thing.m", out)
        out = self.ok()
        self.assertIn("check_api: pending: iio.Thing.name: not found in", out)
        self.assertIn("pending: 1", out.splitlines()[-1])

    def test_rule7_member_only_in_a_comment(self):
        self.put("matlab/+iio/Thing.m", THING_M.replace("name", "label").replace("thing_get_label", "thing_get_name")
                 + "% name is mentioned here only\n")
        self.fails("iio.Thing.name: not found in", "--strict")

    def test_rule7_file_missing(self):
        os.remove(os.path.join(self.root, "matlab/+iio/thing_name.m"))
        out = self.fails("iio.thing_name: not found in", "--strict")
        self.assertIn("thing_name.m", out)
        os.remove(os.path.join(self.root, "matlab/+iio/Context.m"))
        self.put("matlab/+iio/other.m", "function h = other()\nh = iio_mex('create_context', [], 'x');\nend\n")
        self.fails("iio.Context: not found in", "--strict")
        self.ok()


def junit(*cases):
    """A JUnit document as matlab.unittest writes it: one testsuite per test class."""
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<testsuites>\n'
            '  <testsuite errors="0" failures="0" name="tThing" skipped="0" tests="%d" time="0.5">\n%s'
            '  </testsuite>\n</testsuites>\n' % (len(cases), "".join("    %s\n" % c for c in cases)))


def case(name, time="0.25", classname="tThing", inner=""):
    head = '<testcase classname="%s" name="%s" time="%s"' % (classname, name, time)
    return head + ("/>" if not inner else ">%s</testcase>" % inner)


class CheckJunit(unittest.TestCase):
    LIMITS = ("--min-tests", "2", "--max-seconds", "4", "--max-skipped", "0")

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.xml = os.path.join(self._tmp.name, "matlab.xml")

    def tearDown(self):
        self._tmp.cleanup()

    def run_junit(self, text, *limits):
        if text is None:   # no file at all
            if os.path.exists(self.xml):
                os.remove(self.xml)
        else:
            with open(self.xml, "w", encoding="utf-8") as f:
                f.write(text)
        p = subprocess.run([sys.executable, CHECK_JUNIT, self.xml, *(limits or self.LIMITS)],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return p.returncode, p.stdout

    def violations(self, text, *parts, limits=()):
        rc, out = self.run_junit(text, *limits)
        self.assertEqual(rc, 1, out)
        lines = out.splitlines()
        for l in lines:
            self.assertTrue(l.startswith("check_junit: "), l)
        self.assertRegex(lines[-1], r"^check_junit: \d+ tests, %d violation\(s\)$" % (len(lines) - 1))
        for part in parts:
            self.assertTrue([l for l in lines[:-1] if part in l], "no line contains %r in:\n%s" % (part, out))
        return lines

    def test_four_documents_and_a_missing_file(self):
        # the plan's case: 0, 1 (failure), 1 (slow), 1 (skipped), 2 (no file)
        rc, out = self.run_junit(junit(case("a"), case("b")))
        self.assertEqual((rc, out), (0, "check_junit: 2 tests, 0 violation(s)\n"))
        self.violations(junit(case("a"), case("b", inner='<failure type="x">no</failure>')), "tThing.b: failure")
        self.violations(junit(case("a"), case("b", time="5.2")), "tThing.b: took 5.2 s")
        self.violations(junit(case("a"), case("b", inner="<skipped/>")), "1 skipped")
        rc, out = self.run_junit(None)
        self.assertEqual(rc, 2, out)
        self.assertIn("check_junit: ", out)

    def test_too_few_tests(self):
        lines = self.violations(junit(case("a")), "1 tests, fewer than the minimum 2")
        self.assertEqual(len(lines), 2)
        # an empty result file is the same violation, not a pass
        self.violations("<testsuites/>", "0 tests, fewer than the minimum 2")

    def test_error_element_and_one_line_per_violation(self):
        lines = self.violations(junit(case("a", inner='<error type="x">boom</error>'),
                                      case("b", inner="<failure>no</failure>"), case("c", time="4")),
                                "tThing.a: error", "tThing.b: failure", "tThing.c: took 4 s")
        self.assertEqual(len(lines), 4)

    def test_time_limit(self):
        # the limit itself is a violation; tTiming is exempt, and only that class
        rc, out = self.run_junit(junit(case("a", time="3.999"), case("b", time="60", classname="tTiming")))
        self.assertEqual(rc, 0, out)
        self.violations(junit(case("a"), case("b", time="4.0", classname="tTimingOther")), "tTimingOther.b: took 4 s")
        # a time that cannot be compared proves nothing
        for t in ("", "fast", "nan", "inf"):
            self.violations(junit(case("a"), case("b", time=t)), "tThing.b: time")
        self.violations(junit(case("a"), '<testcase classname="tThing" name="b"/>'), "tThing.b: time")

    def test_skipped_allowance(self):
        doc = junit(case("a"), case("b", inner="<skipped/>"), case("c", inner="<skipped/>"))
        rc, out = self.run_junit(doc, "--min-tests", "2", "--max-seconds", "4", "--max-skipped", "2")
        self.assertEqual((rc, out), (0, "check_junit: 3 tests, 0 violation(s)\n"))
        self.violations(doc, "2 skipped, more than the 1 allowed",
                        limits=("--min-tests", "2", "--max-seconds", "4", "--max-skipped", "1"))

    def test_single_testsuite_root(self):
        doc = '<testsuite name="tThing">%s%s</testsuite>' % (case("a"), case("b"))
        rc, out = self.run_junit(doc)
        self.assertEqual((rc, out), (0, "check_junit: 2 tests, 0 violation(s)\n"))

    def test_unusable_input_and_usage(self):
        for text in ("", "<testsuites><testsuite>", "not xml"):
            rc, out = self.run_junit(text)
            self.assertEqual(rc, 2, out)
            self.assertTrue(out.startswith("check_junit: "), out)
            self.assertNotIn("violation(s)", out)
        # every limit is required: a check without a minimum would pass an empty run
        rc, out = self.run_junit(junit(case("a"), case("b")), "--max-seconds", "4", "--max-skipped", "0")
        self.assertEqual(rc, 2, out)


class CheckMlint(unittest.TestCase):
    def setUp(self):
        self.mlint = os.environ.get("MLINT") or (
            os.path.join(os.environ["MATLAB_ROOT"], "bin/glnxa64/mlint") if os.environ.get("MATLAB_ROOT") else
            "/usr/local/MATLAB/R2025b/bin/glnxa64/mlint")
        if not os.access(self.mlint, os.X_OK):
            self.skipTest("mlint not found")
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        if hasattr(self, "_tmp"):
            self._tmp.cleanup()

    def run_mlint(self, *files, env=None):
        e = dict(os.environ)
        e.pop("MATLAB_ROOT", None)
        e["MLINT"] = self.mlint
        if env is not None:
            e.pop("MLINT")
            e.update(env)
        p = subprocess.run([CHECK_MLINT, *files], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=e)
        return p.returncode, p.stdout

    def file(self, name, text):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def test_syntax_error(self):
        warn = self.file("warn.m", "function warn()\nx = 1;\nend\n")
        bad = self.file("bad.m", "function bad()\nx = 1 +;\nend\n")
        good = self.file("good.m", "function y = good()\ny = 1;\nend\n")
        rc, out = self.run_mlint(warn, bad, good)
        self.assertEqual(rc, 1, out)
        self.assertIn("SYNER", out)
        self.assertIn("bad.m: L 2", out)
        self.assertIn("check_mlint: FAILED", out)
        self.assertNotIn("warn.m", out)
        self.assertNotIn("good.m", out)

    def test_warning_only(self):
        warn = self.file("warn.m", "function warn()\nx = 1;\nend\n")
        rc, out = self.run_mlint(warn)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), "check_mlint: 1 file(s), no errors")

    def test_file_that_cannot_be_checked(self):
        rc, out = self.run_mlint(os.path.join(self.dir, "missing.m"))
        self.assertEqual(rc, 1, out)

    def test_mlint_that_reports_nothing(self):
        # A program that runs but is not mlint (or an mlint that cannot start) proves nothing.
        good = self.file("good.m", "function y = good()\ny = 1;\nend\n")
        rc, out = self.run_mlint(good, env={"MLINT": "/bin/true"})
        self.assertEqual(rc, 1, out)
        self.assertIn("check_mlint: mlint reported on 0 of 1 file(s)", out)
        self.assertNotIn("no errors", out)

    def test_mlint_not_available(self):
        good = self.file("good.m", "function y = good()\ny = 1;\nend\n")
        rc, out = self.run_mlint(good, env={})
        self.assertEqual(rc, 77, out)
        rc, out = self.run_mlint(good, env={"MATLAB_ROOT": self.dir})
        self.assertEqual(rc, 77, out)
        self.assertIn("mlint not found", out)


if __name__ == "__main__":
    unittest.main()
