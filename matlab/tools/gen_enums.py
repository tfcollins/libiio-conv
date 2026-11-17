#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""gen_enums.py <iio.h> <out.c>

Writes the enum name tables of the iio_mex gateway (gw_enum_table, gw_enum_name of
matlab/mex/mx_util.h) from the enum definitions of iio.h. Only the enumerator names are
taken from the header; the generated C file uses the names, so the compiler supplies the
values. Runs at build time (matlab/CMakeLists.txt). Python standard library only."""

import os
import re
import sys

# (enum gw_enum member, C enum tag), in the order of enum gw_enum.
ENUMS = [
    ("GW_ENUM_CHAN_TYPE", "iio_chan_type"),
    ("GW_ENUM_MODIFIER", "iio_modifier"),
    ("GW_ENUM_EVENT_TYPE", "iio_event_type"),
    ("GW_ENUM_EVENT_DIRECTION", "iio_event_direction"),
    ("GW_ENUM_HWMON_CHAN_TYPE", "hwmon_chan_type"),
]

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


class Error(Exception):
    pass


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def enumerators(text, tag):
    """Names of the enumerators of 'enum <tag> { ... }', in header order."""
    m = re.search(r"\benum\s+%s\s*\{([^{}]*)\}" % re.escape(tag), text)
    if not m:
        raise Error("enum %s not found" % tag)
    body = m.group(1)
    if "#" in body:
        raise Error("enum %s: preprocessor directive inside the enum" % tag)
    names = []
    for item in body.split(","):
        name = item.split("=", 1)[0].strip()
        if not name:
            continue  # after the trailing comma
        if not IDENT.match(name):
            raise Error("enum %s: cannot parse enumerator %r" % (tag, name))
        if name in names:
            raise Error("enum %s: enumerator %s twice" % (tag, name))
        names.append(name)
    if not names:
        raise Error("enum %s has no enumerators" % tag)
    return names


def generate(text):
    """(C source, number of entries)."""
    text = strip_comments(text)
    out = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated from iio.h by matlab/tools/gen_enums.py. Do not edit. */",
        '#include "mx_util.h"',
        "",
    ]
    total = 0
    for _, tag in ENUMS:
        names = enumerators(text, tag)
        total += len(names)
        out.append("static const struct gw_enum_entry tab_%s[] = {" % tag)
        out.extend('\t{ "%s", %s },' % (n, n) for n in names)
        out.append("};")
        out.append("")
    out.append("static const struct {")
    out.append("\tconst struct gw_enum_entry *entries;")
    out.append("\tsize_t count;")
    out.append("} tables[GW_ENUM_COUNT] = {")
    for member, tag in ENUMS:
        out.append("\t[%s] = { tab_%s, sizeof(tab_%s) / sizeof(tab_%s[0]) }," % (member, tag, tag, tag))
    out.append("};")
    out.append("""
const struct gw_enum_entry *gw_enum_table(enum gw_enum which, size_t *count)
{
	if ((unsigned int)which >= GW_ENUM_COUNT) {
		*count = 0;
		return NULL;
	}
	*count = tables[which].count;
	return tables[which].entries;
}

/* The first name of a value, in header order. */
const char *gw_enum_name(enum gw_enum which, long long value)
{
	size_t n;
	const struct gw_enum_entry *t = gw_enum_table(which, &n);

	for (size_t i = 0; i < n; i++)
		if (t[i].value == value)
			return t[i].name;
	return NULL;
}""")
    return "\n".join(out) + "\n", total


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: gen_enums.py <iio.h> <out.c>\n")
        return 2
    try:
        with open(argv[1], encoding="utf-8") as f:
            source, total = generate(f.read())
        tmp = argv[2] + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(source)
        os.replace(tmp, argv[2])
    except (Error, OSError) as e:
        sys.stderr.write("gen_enums.py: %s: %s\n" % (argv[1], e))
        return 1
    print("gen_enums.py: %d entries in %d enums" % (total, len(ENUMS)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
