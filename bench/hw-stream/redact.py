#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Take lab facts out of the outputs of a hardware run before they are printed
in a public job log or uploaded.

    redact.py <dir-or-file>...

- *.raw.log and *.raw.txt become *.log and *.txt (the raw file is left for the
  caller to delete); other *.log, *.txt, *.json and *.xml files are rewritten
  in place.
- IPv4 and IPv6 addresses become <ip>, MAC addresses <mac>, *.local names
  <host>, and the words in IIO_CONV_LAB_REDACT (space separated, from the
  runner's environment) become <host>. A host or place name that does not end
  in .local is only known from that list.
- The values of unique_id and serial (serial numbers) become <id>.
- The absolute path of this checkout becomes relative; /home/<user>/,
  /Users/<user>/ and /root/ become ~/.

Exit status 2 when an argument is not an existing file or folder, so that a
wrong path is not taken for "nothing to redact".
"""

import os
import re
import sys
from pathlib import Path

ROOT = str(Path(__file__).resolve().parents[2])
IPV4 = re.compile(r"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?!\d)")
# At least two colons and one "::" or seven of them, so that times (12:34:56) stay
IPV6 = re.compile(r"(?<![0-9A-Fa-f:.])(?=[0-9A-Fa-f:]*::|(?:[0-9A-Fa-f]{1,4}:){7})"
                  r"(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?:%\w+)?(?![0-9A-Fa-f:])")
MAC = re.compile(r"(?<![0-9A-Fa-f:])(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:])")
LOCAL = re.compile(r"\b[A-Za-z0-9_](?:[A-Za-z0-9_.-]*[A-Za-z0-9_])?\.local\b", re.I)
HOME = re.compile(r"(?:/home/[A-Za-z0-9_.-]+|/Users/[A-Za-z0-9_.-]+|/root)/")
SERIAL = re.compile(r"\b((?:unique_id|serial)\s*[=:]\s*)[0-9A-Za-z]+")
SUFFIXES = (".log", ".txt", ".json", ".xml")


def redact(text, words=()):
    text = text.replace(ROOT + "/", "").replace(ROOT, ".")
    text = MAC.sub("<mac>", text)
    text = IPV6.sub("<ip>", text)
    text = IPV4.sub("<ip>", text)
    text = LOCAL.sub("<host>", text)
    text = SERIAL.sub(r"\1<id>", text)
    for w in words:
        text = re.sub(rf"\b{re.escape(w)}\b", "<host>", text)
    return HOME.sub("~/", text)


def files(args):
    for a in args:
        p = Path(a)
        if not a or not p.exists():
            raise FileNotFoundError(a)
        if p.is_dir():
            yield from (f for f in sorted(p.rglob("*")) if f.is_file() and f.name.endswith(SUFFIXES))
        else:
            yield p


def main(argv):
    words = [w for w in os.environ.get("IIO_CONV_LAB_REDACT", "").split() if w]
    try:
        todo = list(files(argv))
    except FileNotFoundError as e:
        print(f"redact: '{e}' is not a file or folder", file=sys.stderr)
        return 2
    for p in todo:
        dest = p.with_name(p.name.replace(".raw.", "."))
        dest.write_text(redact(p.read_text(errors="replace"), words))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
