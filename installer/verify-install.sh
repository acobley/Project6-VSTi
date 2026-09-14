#!/bin/bash
#-----------------------------------------------------------------------------
# Check that an INSTALLED Project6 actually works on this machine.
#
# Copy this one file to the Mac you are testing on, install the .pkg, and run
# it. It needs nothing else - not the repo, not the build tree, not the
# installer. That is the point: the machine that must not have the build tree
# is exactly the machine this has to run on.
#
#   ./verify-install.sh
#
# It answers the question the installer cannot: the installer finishing means
# files were COPIED. This says whether they LOAD.
#-----------------------------------------------------------------------------
set -u

NAME="Project6"
VST3="/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
AU="/Library/Audio/Plug-Ins/Components/$NAME.component"
NESTED="$AU/Contents/Resources/plugin.vst3"

FAILED=0
pass () { printf '  ok    %s\n' "$1"; }
fail () { printf '  FAIL  %s\n' "$1"; FAILED=$((FAILED + 1)); }
note () { printf '        %s\n' "$1"; }

echo "Project6 install check - $(sw_vers -productName 2>/dev/null) $(sw_vers -productVersion 2>/dev/null) on $(uname -m)"
echo

#-----------------------------------------------------------------------------
echo "1. Both bundles are where the installer said"
#-----------------------------------------------------------------------------
[ -d "$VST3" ] && pass "$VST3" || fail "$VST3 is not there"
[ -d "$AU" ]   && pass "$AU"   || fail "$AU is not there"

#-----------------------------------------------------------------------------
echo
echo "2. THE ONE THAT MATTERS: the AU's nested VST3 is real"
#-----------------------------------------------------------------------------
# Steinberg's AU wrapper has no plug-in code of its own - it loads the VST3
# from inside its own bundle. In the BUILD TREE that is a symlink to an
# absolute path, which is right for development and dangles on every other
# machine. An installer that shipped the symlink installs perfectly and then
# the AU will not instantiate, saying nothing useful about why.
if [ -L "$NESTED" ]; then
    fail "plugin.vst3 is a SYMLINK, not a copy"
    note "-> $(readlink "$NESTED")"
    note "The AU will not load on this machine. The .pkg was built without"
    note "the staging step - rebuild it with installer/build-installer.sh."
elif [ -d "$NESTED" ]; then
    pass "plugin.vst3 is a real directory inside the .component"
    if [ -f "$NESTED/Contents/MacOS/$NAME" ]; then
        pass "and it contains an executable"
    else
        fail "but it has no executable at Contents/MacOS/$NAME"
    fi
else
    fail "plugin.vst3 is missing entirely from the .component"
fi

#-----------------------------------------------------------------------------
echo
echo "3. Architectures"
#-----------------------------------------------------------------------------
for binary in "$VST3/Contents/MacOS/$NAME" "$AU/Contents/MacOS/$NAME"; do
    if [ -f "$binary" ]; then
        archs="$(lipo -archs "$binary" 2>/dev/null)"
        case "$archs" in
            *"$(uname -m)"*) pass "$(basename "$(dirname "$(dirname "$binary")")") - $archs" ;;
            "")              fail "could not read the architectures of $binary" ;;
            *)               fail "$binary is $archs - nothing for this $(uname -m) Mac" ;;
        esac
    else
        fail "no executable at $binary"
    fi
done

#-----------------------------------------------------------------------------
echo
echo "4. Signatures"
#-----------------------------------------------------------------------------
for bundle in "$VST3" "$AU"; do
    [ -d "$bundle" ] || continue
    label="$(basename "$bundle")"

    if codesign --verify --deep --strict "$bundle" 2>/dev/null; then
        pass "$label - signature is valid"
    else
        fail "$label - signature does not verify"
    fi

    authority="$(codesign -dv --verbose=2 "$bundle" 2>&1 | grep -m1 '^Authority=' || true)"
    if [ -n "$authority" ]; then
        note "$label ${authority}"
    else
        note "$label is ad-hoc signed (no certificate authority)"
        note "  fine for local use; it cannot have been notarised"
    fi
done

#-----------------------------------------------------------------------------
echo
echo "5. Installer receipts"
#-----------------------------------------------------------------------------
receipts="$(pkgutil --pkgs 2>/dev/null | grep -i project6 || true)"
if [ -n "$receipts" ]; then
    while IFS= read -r r; do pass "$r"; done <<< "$receipts"
else
    note "no Project6 receipts - installed by hand rather than by the .pkg?"
fi

#-----------------------------------------------------------------------------
echo
echo "6. Does the Audio Unit actually load?"
#-----------------------------------------------------------------------------
# THE REAL TEST OF SECTION 2. auval instantiates the component, which is
# precisely what a dangling nested VST3 makes impossible.
if command -v auval >/dev/null 2>&1; then
    echo "   running auval, this takes a few seconds..."
    if auval -v aumu Prj6 AECo 2>&1 | grep -q "AU VALIDATION SUCCEEDED"; then
        pass "auval -v aumu Prj6 AECo - AU VALIDATION SUCCEEDED"
    else
        fail "auval did not report success"
        note "Run it yourself for the detail:  auval -v aumu Prj6 AECo"
        note "If it cannot find the component at all, try:"
        note "  killall -9 AudioComponentRegistrar"
    fi
else
    note "auval not found - skipped"
fi

#-----------------------------------------------------------------------------
echo
if [ "$FAILED" -eq 0 ]; then
    echo "All checks passed."
    echo
    echo "One thing no script can do for you: open a DAW and load BOTH formats."
    echo "In REAPER use the VST3 - REAPER does not take MIDI output from an"
    echo "Audio Unit. The AU is for Logic and other AU hosts."
    exit 0
fi

echo "$FAILED check(s) FAILED. Do not ship this build."
exit 1
