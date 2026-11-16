#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""G4: static check of matlab/api-map.tsv against iio.h, the gateway registry and the M files.

usage: check_api.py --iio-h PATH [--root DIR] [--group NAME | --strict]

Rules
  1. the public functions of iio.h (__api functions and static inline helpers) are
     exactly the c_function values of the map, each once
  2. row shape: five tab-separated fields; "exposed" has a command and MATLAB members;
     "internal" and "not_exposed" have neither, and a note
  3. registry (GW_CMD("name" / GW_XCMD("name" in matlab/mex/*.c): no name twice; the
     command of an exposed row is a GW_CMD; every GW_CMD is in a row; no GW_XCMD is
  4. every iio_mex( call in a .m file under matlab/ has a single-quoted literal first
     argument that is a registered command
  5. every registered command is called from a file under matlab/+iio (a command of
     group "test": from a file under matlab/tests)
  6. no registration names gw_stub
  7. every MATLAB member of a row exists: matlab/+iio/<Class or function>.m is a file
     and, for iio.Class.member, contains "member" as a whole word outside comments

Rules 1 to 4 are always errors. Rules 5 to 7 are errors with --strict. With --group G
only rule 6 for the file of group G (cmd_<G>.c; iio_mex.c is group "mex") is an error
and the rest of 5 to 7 is pending; with neither option all of 5 to 7 is pending.

Output: one "check_api: <message>" line per error, one "check_api: pending: <message>"
line per pending item, and last "check_api: <rows> rows, <commands> commands, <n>
error(s)", followed by "; pending: <k>" when k > 0. Exit 0 without errors, else 1.
"""
import argparse
import os
import re
import sys

HEADER = ["c_function", "command", "matlab", "status", "notes"]
STATUSES = ("exposed", "internal", "not_exposed")
COMMAND_RE = re.compile(r"[a-z][a-z0-9_]*\Z")
MEMBER_RE = re.compile(r"iio\.([A-Za-z]\w*)(?:\.([A-Za-z]\w*))?\Z")
API_RE = re.compile(r"__api\b[^;{]*?\b(\w+)\s*\(")
INLINE_RE = re.compile(r"static\s+inline\b[^;{]*?\b(\w+)\s*\(")
REG_RE = re.compile(r'\bGW_(X?)CMD\s*\(\s*"([^"]*)"\s*,\s*(\w+)')
CALL_RE = re.compile(r"(?<![\w.])iio_mex\s*\(")
MASK = "\x00"   # stands for one character of a string in masked M code


def strip_c_comments(text):
    """Remove /* */ and // comments; string literals are kept, newlines too."""
    def repl(m):
        s = m.group(0)
        if s.startswith("/"):
            return re.sub(r"[^\n]", " ", s)
        return s
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"', repl, text, flags=re.S)


def mask_m(text):
    """M code of the same length with comments blanked and string contents masked.

    Comments (%, %{ ... %} blocks, the rest of a line after "...") become spaces. The
    characters between the quotes of a char vector or string become MASK; the quotes
    stay. Newlines are kept, so offsets and line numbers match the original text.
    """
    out = []
    depth = 0   # nesting of %{ ... %} block comments
    for line in text.split("\n"):
        stripped = line.strip()
        if depth:
            if stripped == "%{":
                depth += 1
            elif stripped == "%}":
                depth -= 1
            out.append(" " * len(line))
            continue
        if stripped == "%{":
            depth = 1
            out.append(" " * len(line))
            continue
        res = []
        i, n = 0, len(line)
        while i < n:
            c = line[i]
            if c == "%":
                break
            if line.startswith("...", i):
                break
            if c == '"' or (c == "'" and not _is_transpose(line, i)):
                j = i + 1
                while j < n:
                    if line[j] == c:
                        if j + 1 < n and line[j + 1] == c:
                            j += 2
                            continue
                        break
                    j += 1
                if j >= n:   # unterminated: leave the rest of the line as code
                    res.append(line[i:])
                    i = n
                    break
                res.append(c + MASK * (j - i - 1) + c)
                i = j + 1
                continue
            res.append(c)
            i += 1
        res.append(" " * (n - i))
        out.append("".join(res))
    return "\n".join(out)


def _is_transpose(line, i):
    """A quote directly after an operand is the transpose operator, not a char vector."""
    if i == 0:
        return False
    p = line[i - 1]
    return p.isalnum() or p in "_)]}'."


class Checker:
    def __init__(self, root, iio_h, group, strict):
        self.root = root
        self.iio_h = iio_h
        self.group = group
        self.strict = strict
        self.errors = []
        self.pending = []
        self.rows = []          # (line number, c_function, command, [members], status)
        self.n_rows = 0
        self.commands = {}      # name -> dict(extra, fn, file, group)
        self.m_cache = {}

    def error(self, msg):
        self.errors.append(msg)

    def soft(self, msg, is_error):
        (self.errors if is_error else self.pending).append(msg)

    def rel(self, path):
        return os.path.relpath(path, self.root).replace(os.sep, "/")

    def read(self, path, what):
        try:
            with open(path, encoding="utf-8") as f:
                return f.read()
        except (OSError, UnicodeDecodeError) as e:
            self.error("%s: cannot read %s: %s" % (what, path, e))
            return None

    # rules 1 and 2
    def check_map(self):
        map_path = os.path.join(self.root, "matlab", "api-map.tsv")
        text = self.read(map_path, "api-map.tsv")
        header_text = self.read(self.iio_h, "iio.h")
        if text is None or header_text is None:
            return
        lines = text.split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        if not lines or lines[0].split("\t") != HEADER:
            self.error("api-map.tsv:1: the header line must be %s (tab-separated)" % " ".join(HEADER))
        seen = {}
        for no, line in enumerate(lines[1:], start=2):
            self.n_rows += 1
            f = line.split("\t")
            where = "api-map.tsv:%d" % no
            if len(f) != 5 or not f[0].strip():
                self.error("%s: a row has five fields separated by tabs, the first not empty (found %d)" % (where, len(f)))
                continue
            name, command, matlab, status, notes = f
            if name in seen:
                self.error("%s: %s: more than one row (first at line %d)" % (where, name, seen[name]))
            else:
                seen[name] = no
            members = []
            if status not in STATUSES:
                self.error("%s: %s: unknown status '%s' (exposed, internal or not_exposed)" % (where, name, status))
                continue
            if status == "exposed":
                if not COMMAND_RE.match(command):
                    self.error("%s: %s: an exposed row needs a command matching [a-z][a-z0-9_]*, not '%s'"
                               % (where, name, command))
                    command = None
                if matlab == "-" or not matlab:
                    self.error("%s: %s: an exposed row needs a MATLAB member" % (where, name))
                else:
                    for m in matlab.split(", "):
                        if MEMBER_RE.match(m):
                            members.append(m)
                        else:
                            self.error("%s: %s: malformed MATLAB member '%s' (iio.Class.member, iio.Class or "
                                       "iio.function, separated by ', ')" % (where, name, m))
            else:
                if command != "-":
                    self.error("%s: %s: %s must not have a command ('%s')" % (where, name, status, command))
                if matlab != "-":
                    self.error("%s: %s: %s must not have a MATLAB member ('%s')" % (where, name, status, matlab))
                if not notes.strip():
                    self.error("%s: %s: %s needs a note" % (where, name, status))
                command = None
            self.rows.append((no, name, command, members, status))

        # Comments go first, then preprocessor lines: "#define __api __attribute__((...))"
        # would otherwise read as a function named __attribute__.
        code = re.sub(r"^[ \t]*#(?:[^\n\\]|\\.|\\\n)*", "", strip_c_comments(header_text), flags=re.M)
        in_header = []
        for n in API_RE.findall(code) + INLINE_RE.findall(code):
            if n not in in_header:
                in_header.append(n)
        if not in_header:
            self.error("iio.h: no public function found in %s" % self.iio_h)
        for n in in_header:
            if n not in seen:
                self.error("%s: without a row in api-map.tsv" % n)
        known = set(in_header)
        for n, no in seen.items():
            if n not in known:
                self.error("api-map.tsv:%d: %s: not in iio.h" % (no, n))

    # rule 3 (registry part)
    def read_registry(self):
        mex_dir = os.path.join(self.root, "matlab", "mex")
        try:
            files = sorted(f for f in os.listdir(mex_dir) if f.endswith(".c"))
        except OSError as e:
            self.error("registry: cannot list %s: %s" % (mex_dir, e))
            return
        for fname in files:
            path = os.path.join(mex_dir, fname)
            text = self.read(path, "registry")
            if text is None:
                continue
            stem = fname[:-2]
            if stem == "iio_mex":
                group = "mex"
            elif stem.startswith("cmd_"):
                group = stem[4:]
            else:
                group = stem
            for m in REG_RE.finditer(strip_c_comments(text)):
                name = m.group(2)
                if name in self.commands:
                    self.error("%s: registered twice (%s and %s)" % (name, self.commands[name]["file"], self.rel(path)))
                    continue
                self.commands[name] = {"extra": bool(m.group(1)), "fn": m.group(3),
                                       "file": self.rel(path), "group": group}

    def check_registry(self):
        in_rows = set()
        for no, name, command, members, status in self.rows:
            if command is None:
                continue
            in_rows.add(command)
            c = self.commands.get(command)
            if c is None:
                self.error("api-map.tsv:%d: %s: command %s: is not registered (no GW_CMD(\"%s\", ...) in matlab/mex/*.c)"
                           % (no, name, command, command))
            elif c["extra"]:
                self.error("api-map.tsv:%d: %s: command %s: is an extra command (GW_XCMD in %s); a row needs a GW_CMD"
                           % (no, name, command, c["file"]))
        for name, c in self.commands.items():
            if not c["extra"] and name not in in_rows:
                self.error("%s: registered with GW_CMD but in no row of api-map.tsv (%s)" % (name, c["file"]))

    # rules 4 and 5
    def m_files(self):
        base = os.path.join(self.root, "matlab")
        found = []
        for d, dirs, files in os.walk(base):
            dirs.sort()
            for f in sorted(files):
                if f.endswith(".m"):
                    found.append(os.path.join(d, f))
        return found

    def masked(self, path):
        if path not in self.m_cache:
            text = self.read(path, "M file")
            self.m_cache[path] = (text, mask_m(text)) if text is not None else None
        return self.m_cache[path]

    def check_calls(self):
        called = {}   # command -> set of relative files
        for path in self.m_files():
            got = self.masked(path)
            if got is None:
                continue
            text, code = got
            rel = self.rel(path)
            for m in CALL_RE.finditer(code):
                line = code.count("\n", 0, m.start()) + 1
                lit = re.compile(r"\s*'(%s*)'\s*(?=[,)])" % MASK).match(code, m.end())
                if not lit:
                    self.error("%s:%d: iio_mex( call without a literal command (the first argument must be "
                               "a single-quoted command name)" % (rel, line))
                    continue
                name = text[lit.start(1):lit.end(1)]
                if name not in self.commands:
                    self.error("%s:%d: iio_mex( call with unknown command '%s'" % (rel, line, name))
                    continue
                called.setdefault(name, set()).add(rel)
        return called

    def members_of(self, command):
        members = []
        for no, name, cmd, mem, status in self.rows:
            if cmd == command:
                for m in mem:
                    if m not in members:
                        members.append(m)
        return members

    def member_file(self, member):
        m = MEMBER_RE.match(member)
        return "matlab/+iio/%s.m" % m.group(1)

    def describe(self, command):
        """Members and files of a command, so that the owner of a pending item can be found."""
        c = self.commands[command]
        members = self.members_of(command)
        if not members:
            return "no MATLAB member in api-map.tsv; registered in %s" % c["file"]
        files = []
        for m in members:
            f = self.member_file(m)
            if f not in files:
                files.append(f)
        return "members: %s; files: %s; registered in %s" % (", ".join(members), ", ".join(files), c["file"])

    def check_usage(self, called):
        for name, c in self.commands.items():
            want = "matlab/tests/" if c["group"] == "test" else "matlab/+iio/"
            if not any(f.startswith(want) for f in called.get(name, ())):
                self.soft("%s: is not called from a file under %s (%s)" % (name, want.rstrip("/"), self.describe(name)),
                          self.strict)

    # rule 6
    def check_stubs(self):
        for name, c in self.commands.items():
            if c["fn"] == "gw_stub":
                self.soft("%s: is still a stub (%s)" % (name, self.describe(name)),
                          self.strict or (self.group is not None and c["group"] == self.group))

    # rule 7
    def check_members(self):
        done = {}
        for no, name, command, members, status in self.rows:
            for m in members:
                done.setdefault(m, []).append(command or "?")
        for member, cmds in done.items():
            rel = self.member_file(member)
            path = os.path.join(self.root, *rel.split("/"))
            word = MEMBER_RE.match(member).group(2)
            ok = os.path.isfile(path)
            why = "no such file"
            if ok and word:
                got = self.masked(path)
                ok = got is not None and re.search(r"(?<!\w)%s(?!\w)" % re.escape(word), got[1]) is not None
                why = "no '%s' outside comments and strings" % word
            if not ok:
                uniq = []
                for c in cmds:
                    if c not in uniq:
                        uniq.append(c)
                self.soft("%s: not found in %s (%s; command %s)" % (member, rel, why, ", ".join(uniq)), self.strict)

    def run(self):
        self.check_map()
        self.read_registry()
        self.check_registry()
        called = self.check_calls()
        self.check_usage(called)
        self.check_stubs()
        self.check_members()
        for e in self.errors:
            print("check_api: " + e)
        for p in self.pending:
            print("check_api: pending: " + p)
        last = "check_api: %d rows, %d commands, %d error(s)" % (self.n_rows, len(self.commands), len(self.errors))
        if self.pending:
            last += "; pending: %d" % len(self.pending)
        print(last)
        return 1 if self.errors else 0


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Static check of matlab/api-map.tsv (gate G4).")
    ap.add_argument("--iio-h", required=True, metavar="PATH", help="the iio.h of the pinned libiio")
    ap.add_argument("--root", metavar="DIR", default=os.path.normpath(os.path.join(here, "..", "..")),
                    help="repository root (default: two levels above this script)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--group", metavar="NAME", help="a stub in the file of this command group is an error")
    mode.add_argument("--strict", action="store_true", help="rules 5 to 7 are errors")
    args = ap.parse_args(argv)
    return Checker(os.path.abspath(args.root), args.iio_h, args.group, args.strict).run()


if __name__ == "__main__":
    sys.exit(main())
