#!/usr/bin/env python3
"""Mechanical fixes applied to the fetched Eclipse, BetterSunshineEngine and
SunshineHeaderInterface sources before the port compiles them (nothing of
theirs is kept in this repository). Each entry: a glob under the source root,
a regular expression and its replacement, and why. Idempotent.

    fixup_sources.py ECLIPSE_ROOT BSE_ROOT SHI_ROOT
"""
import glob
import os
import re
import sys

ECLIPSE_FIXES = [
    # SunshineHeaderInterface named obj_hit_info's third field (May 2026);
    # Eclipse still initialises it by its old placeholder name.
    ("src/*/*.cpp", r"(obj_hit_info\s+\w+\s*=?\s*\{[^}]*?)\._08(\s*=)", r"\1.mVisualOfsY\2",
     "obj_hit_info._08 is mVisualOfsY"),
]
BSE_FIXES = []
SHI_FIXES = [
    # The decomp's JUTRect has a user-provided copy constructor, so the port
    # passes it by value through a hidden reference; SunshineHeaderInterface's
    # must say so too or J2DFillBox(JUTRect, ...) reads garbage.
    ("include/JSystem/JUtility/JUTRect.hxx", r"(\n(\s*)JUTRect\(\);\n)(?!\s*JUTRect\(const JUTRect)",
     r"\1\2JUTRect(const JUTRect &);\n", "JUTRect is not trivially copyable in the port"),
    # The write-gather pipe is not memory natively: the port's proxy forwards
    # stores to its graphics layer.
    ("include/Dolphin/GX.h", r"extern WGPipe volatile wgPipe;", r"#include <sms_mod_wgpipe.h>",
     "wgPipe stores go to the port's graphics layer"),
]


def apply(root, fixes):
    changed = 0
    for pattern, rx, repl, why in fixes:
        for path in glob.glob(os.path.join(root, pattern)):
            with open(path, encoding="utf-8", errors="surrogateescape") as f:
                text = f.read()
            new, n = re.subn(rx, repl, text)
            if n:
                with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(new)
                changed += n
    return changed


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    n = apply(sys.argv[1], ECLIPSE_FIXES) + apply(sys.argv[2], BSE_FIXES) + apply(sys.argv[3], SHI_FIXES)
    print("fixup_sources: %d replacements" % n)
