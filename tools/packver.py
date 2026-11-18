# SPDX-License-Identifier: GPL-3.0-or-later
"""The pack format version (src/packver.h) and the PACKS.VER marker file.

The number lives in src/packver.h (PACK_FORMAT_VERSION), which the engine
includes; the tools read it from there. The engine checks PACKS.VER at
startup (src/startup.c).
"""

import os
import re
import sys

MARKER_FILE = "PACKS.VER"
MARKER_TEXT = "XCOM-XT PACKS "


def src_dir():
    """The folder holding the headers: tools/../src, or the copy that
    XCOMCONV.EXE carries."""
    here = os.path.dirname(os.path.abspath(__file__))
    for base in (getattr(sys, "_MEIPASS", None), os.path.join(here, "..")):
        if base and os.path.isfile(os.path.join(base, "src", "packver.h")):
            return os.path.join(base, "src")
    raise FileNotFoundError("src/packver.h")


def version():
    with open(os.path.join(src_dir(), "packver.h"), encoding="ascii") as f:
        m = re.search(r"^#define\s+PACK_FORMAT_VERSION\s+(\d+)", f.read(), re.M)
    if not m:
        raise ValueError("src/packver.h: PACK_FORMAT_VERSION not found")
    return int(m.group(1))


def write_marker(outdir):
    """Writes PACKS.VER: the marker text, the version and CR LF."""
    path = os.path.join(outdir, MARKER_FILE)
    with open(path, "wb") as f:
        f.write(("%s%d\r\n" % (MARKER_TEXT, version())).encode("ascii"))
    return path


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        raise SystemExit("usage: packver.py <pack folder>   (writes PACKS.VER)")
    print(write_marker(argv[0]), version())


if __name__ == "__main__":
    main()
