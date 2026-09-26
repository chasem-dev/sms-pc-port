#!/usr/bin/env python3
"""Mechanical fixes applied to the fetched Eclipse, BetterSunshineEngine and
SunshineHeaderInterface sources before the port compiles them (nothing of
theirs is kept in this repository). Each entry: a glob under the source root,
a regular expression and its replacement, and why. Idempotent.

    fixup_sources.py ECLIPSE_ROOT BSE_ROOT SHI_ROOT [MOVESET_ROOT]
"""
import glob
import os
import re
import sys

# A call through a literal retail address: the port's function for it
# (platform/mods/eclipse/rawfn_trampolines.cpp, tools/mods/gen_rawfn.py).
RAWADDR_FIX = ("src/**/*.cpp", r"(\(\s*\([^;{}()]*\(\s*\*\s*\)\s*\([^;{}()]*\)\s*\)\s*)(0x8[0-3][0-9A-Fa-f]{6})(\s*\)\s*\()",
               r"\1sms_mod_rawaddr(\2)\3", "retail addresses called go to the port's functions")

# Textures built into the code as byte arrays are converted to host byte
# order in place when the game first stores them (JUTTexture::storeTIMG), so
# they cannot be read-only; static keeps the internal linkage const gave them.
# (The memory card banner and icon are declared extern and never stored.)
TEXTURE_FIXES = [
    (glob_, r"(?<!static )\bconst u8 SMS_ALIGN\(32\) (?!gSaveBnr\b|gSaveIcon\b)(\w+)\[\]",
     r"static u8 SMS_ALIGN(32) \1[]", "embedded textures are converted in place")
    for glob_ in ("src/**/*.cpp", "src/**/*.hxx", "include/**/*.hxx")
]

# Its caller passes the particle id in a full register (0x113); declared u8,
# it only works on the PowerPC, where the value is used unmasked.
PARTICLE_FIXES = [
    (glob_, r"(smParticleInit\(JPAResourceManager \*\s*\w*,\s*const char \*\s*\w*,\s*)u8(\s*\w*\))",
     r"\1u32\2", "the particle id is 16 bits")
    for glob_ in ("src/**/*.cpp", "include/**/*.hxx")
]

# Two TGCConsole2::checkChangeTelopArray switch-table entries are PowerPC
# assembly: store a news list in the console (r30) and jump back to the end
# of the switch. The port calls the entry with the console and continues
# after the switch itself, so they become the store alone.
DEBS_FIXES = [
    ("src/stage/behavior.cpp",
     r"SMS_ASM_FUNC static void (set\w+DEBSList)\(TGCConsole2 \*console2\) \{\n"
     r"\s*SMS_ASM_BLOCK\(\"lis 3, (\w+)@h[^;]*\);\n\}",
     r"static void \1(TGCConsole2 *console2) {\n    *(s32 **)((u8 *)console2 + 0x574) = \2;\n}",
     "news list setters without assembly"),
]

ECLIPSE_FIXES = TEXTURE_FIXES + PARTICLE_FIXES + DEBS_FIXES + [
    RAWADDR_FIX,

    # SunshineHeaderInterface named obj_hit_info's third field (May 2026);
    # Eclipse still initialises it by its old placeholder name.
    ("src/*/*.cpp", r"(obj_hit_info\s+\w+\s*=?\s*\{[^}]*?)\._08(\s*=)", r"\1.mVisualOfsY\2",
     "obj_hit_info._08 is mVisualOfsY"),
]
BSE_FIXES = TEXTURE_FIXES + [
    RAWADDR_FIX,
    # Declared bool, but the game reads the float the function leaves in f1.
    ("src/patches/sun.cpp", r"static bool scaleGlowToLightness\(", r"static f32 scaleGlowToLightness(",
     "the lens glow scale is a float"),
    # The memory card banner and icon are built into the code as big-endian
    # BTI files and copied to the card as they are; only their image offset
    # is read, and it has to be read in their byte order.
    ("src/settings.cpp", r"\+ info\.(mBannerImage|mIconTable)->mTextureOffset",
     r"+ __builtin_bswap32(info.\1->mTextureOffset)", "card banner and icon offsets are big-endian"),
    # Run-time rewrites of the retail game's instructions: the port has no
    # retail code, so each goes to the patch registry for the decomp hooks
    # that port it (platform/mods/modhooks.cpp) instead of into memory.
    # TMarioAnimeData::isPumpOK's replacement is PowerPC assembly: the FLUDD
    # animation id against BSE's (extended) animation count.
    ("src/player.cpp",
     r"static SMS_ASM_FUNC void isPumpOk\(\) \{\n\s*SMS_ASM_BLOCK\(\"lhz       3, 2 \(3\)[^;]*\);\n\}",
     r"static bool isPumpOk(const u8 *animeData) {\n    return *(const u16 *)(animeData + 2) < sPlayerAnimeInfosSize;\n}",
     "isPumpOk without assembly"),
    ("src/memory.cpp",
     r"(BETTER_SMS_FOR_EXPORT void BetterSMS::PowerPC::writeU(8|16|32)\(u\d+ \*ptr, u\d+ value\) \{\n)"
     r"\s*\*ptr = value;\n\s*BetterSMS::Cache::store\(ptr, sizeof\(u\d+\)\);",
     r'extern "C" void sms_mod_code_write(uint32_t, uint32_t, int);\n'
     r'\1    sms_mod_code_write((uint32_t)(uintptr_t)ptr, value, \2 / 8);',
     "code writes go to the patch registry"),
]
MOVESET_FIXES = TEXTURE_FIXES + [
    RAWADDR_FIX,
]
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
    # Retail functions called by their CodeWarrior name are casts of their
    # retail addresses, where the port has no code: point each at the port's
    # trampoline of that name (platform/mods/eclipse/rawfn_trampolines.cpp,
    # generated by tools/mods/gen_rawfn.py for the names the mods use).
    ("include/SMS/raw_fn.hxx", r"#define\s+(\w+)(\s+)\(\((\w+) \(\*\)\(\.\.\.\)\)(0x[0-9A-Fa-f]+)\)",
     r'extern "C" void sms_rawfn_\1(void);\n#define \1\2((\3 (*)(...))sms_rawfn_\1) /* \4 */',
     "retail functions by name go to the port's functions"),
]


def apply(root, fixes):
    changed = 0
    for pattern, rx, repl, why in fixes:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            with open(path, encoding="utf-8", errors="surrogateescape") as f:
                text = f.read()
            new, n = re.subn(rx, repl, text)
            if n:
                with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(new)
                changed += n
    return changed


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    n = apply(sys.argv[1], ECLIPSE_FIXES) + apply(sys.argv[2], BSE_FIXES) + apply(sys.argv[3], SHI_FIXES)
    if len(sys.argv) == 5:
        n += apply(sys.argv[4], MOVESET_FIXES)
    print("fixup_sources: %d replacements" % n)
