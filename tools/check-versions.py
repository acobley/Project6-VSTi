#!/usr/bin/env python3
"""tools/check-versions.py - the version and the maker's name each live in
several places; say if any of them disagree.

    CMakeLists.txt          PLUGIN_VERSION "M.S.R.B"   (names the .pkg and the tag)
    source/version.h        MAJOR / SUB / RELEASE / BUILD  (what a VST3 host shows)
    resource/au-info.plist  CFBundleVersion, CFBundleShortVersionString "M.S.R",
                            AudioComponents version  = M<<16 | S<<8 | R,
                            "AudioUnit Version"      = the same, as hex

The AU half matters more than it looks. An AU host caches a component by
its version: ship a changed Audio Unit under the old number and Logic, and
anything else built on the AU cache, may go on showing the old name and the
old validation result. The build number has no place in the AU version, so
a release that changes the AU should move RELEASE, not just BUILD.

THE MAKER'S NAME, too. The VST3 vendor (stringCompanyName in version.h,
and PLUGIN_COMPANY / COMPANY_NAME in CMakeLists.txt where present) must be
the manufacturer part of the AU name ("AE Cobley: X"). Hosts list a plug-in
under that name and look for its presets in /Library/Audio/Presets/<it>/X;
if the formats disagree the plug-in appears under two makers with its
presets in two folders. And no full stops in the AU name: REAPER cuts it at
the first one, so "A. E. Cobley: X" became "A" and every such plug-in
shared one preset list.

Exit 0 if they agree, 1 (with every disagreement listed) if not.
"""
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    problems = []

    m = re.search(r'^set\(PLUGIN_VERSION\s+"([^"]+)"', (ROOT / "CMakeLists.txt").read_text(), re.M)
    if not m:
        print("check-versions: no PLUGIN_VERSION in CMakeLists.txt")
        return 1
    cmake = m.group(1)
    parts = cmake.split(".")
    if len(parts) != 4 or not all(p.isdigit() for p in parts):
        print(f"check-versions: PLUGIN_VERSION {cmake!r} is not M.S.R.B")
        return 1
    major, sub, rel, build = (int(p) for p in parts)

    vh = (ROOT / "source" / "version.h").read_text()
    for name, want in (("MAJOR_VERSION", major), ("SUB_VERSION", sub),
                       ("RELEASE_NUMBER", rel), ("BUILD_NUMBER", build)):
        for suffix, fmt in (("_INT", r"(\d+)"), ("_STR", r'"(\d+)"')):
            mm = re.search(rf"^#define\s+{name}{suffix}\s+{fmt}", vh, re.M)
            got = int(mm.group(1)) if mm else None
            if got != want:
                problems.append(f"source/version.h {name}{suffix} is {got}, CMakeLists says {want}")

    with open(ROOT / "resource" / "au-info.plist", "rb") as f:
        pl = plistlib.load(f)
    short = f"{major}.{sub}.{rel}"
    for key in ("CFBundleVersion", "CFBundleShortVersionString"):
        if pl.get(key) != short:
            problems.append(f"au-info.plist {key} is {pl.get(key)!r}, want {short!r}")
    au_int = (major << 16) | (sub << 8) | rel
    for i, comp in enumerate(pl.get("AudioComponents", [])):
        if comp.get("version") != au_int:
            problems.append(f"au-info.plist AudioComponents[{i}].version is "
                            f"{comp.get('version')}, want {au_int} (0x{au_int:08X} == {short})")
    hexv = pl.get("AudioUnit Version", "")
    try:
        ok = int(hexv, 16) == au_int
    except ValueError:
        ok = False
    if not ok:
        problems.append(f"au-info.plist 'AudioUnit Version' is {hexv!r}, want {au_int:08X}")

    # --- the maker's name -------------------------------------------------
    cm = (ROOT / "CMakeLists.txt").read_text()
    vendor = re.search(r'^#define\s+stringCompanyName\s+"([^"]*)"', vh, re.M)
    vendor = vendor.group(1) if vendor else None
    if vendor is None:
        problems.append("source/version.h has no stringCompanyName")
    for i, comp in enumerate(pl.get("AudioComponents", [])):
        au_name = comp.get("name", "")
        if ":" not in au_name:
            problems.append(f"au-info.plist AudioComponents[{i}].name {au_name!r} is not 'Maker: Plug-in'")
            continue
        maker = au_name.split(":", 1)[0].strip()
        if "." in au_name:
            problems.append(f"au-info.plist AU name {au_name!r} has a full stop - REAPER cuts the name there")
        if vendor is not None and maker != vendor:
            problems.append(f"the AU maker is {maker!r} but the VST3 vendor (stringCompanyName) is {vendor!r}")
    for label, rx in (("PLUGIN_COMPANY", r'^\s*set\(PLUGIN_COMPANY\s+"([^"]*)"'),
                      ("COMPANY_NAME", r'COMPANY_NAME\s+"([^"$]*)"')):
        for m in re.finditer(rx, cm, re.M):
            if vendor is not None and m.group(1) != vendor:
                problems.append(f"CMakeLists.txt {label} is {m.group(1)!r}, but stringCompanyName is {vendor!r}")

    if problems:
        print(f"check-versions: CMakeLists.txt says {cmake}, but:")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check-versions: {cmake} everywhere (AU {short}, 0x{au_int:08X}); maker {vendor!r} everywhere")
    return 0


if __name__ == "__main__":
    sys.exit(main())
