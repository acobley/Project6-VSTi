#!/bin/bash
#-----------------------------------------------------------------------------
# Everything in PORTING-NOTES section 6, as one command.
#
#   tools/run-tests.sh
#
# Seven SDK-free test suites, every source compiled to a real object file, the
# undefined-symbol sweep, the editor invariant and the routing diagram. Run it
# before every commit, and certainly before every release - a release is the
# one build where "I was fairly sure" is not good enough.
#
# It needs a VST3 SDK checkout only for ParamsTests, and finds one itself.
#-----------------------------------------------------------------------------
set -u

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAILED=0
pass () { printf '  ok    %s\n' "$1"; }
fail () { printf '  FAIL  %s\n' "$1"; FAILED=$((FAILED + 1)); }

#-----------------------------------------------------------------------------
# An SDK, for the one suite that needs Steinberg's headers.
#-----------------------------------------------------------------------------
SDK="${VST3_SDK_ROOT:-}"
if [ -z "$SDK" ]; then
    for candidate in "$ROOT/external/vst3sdk" "$ROOT/../VocalFilter-VSTi/external/vst3sdk"; do
        if [ -f "$candidate/CMakeLists.txt" ]; then SDK="$candidate"; break; fi
    done
fi

echo "Project6 checks"
[ -n "$SDK" ] && echo "  SDK: $SDK" || echo "  SDK: none found - ParamsTests and the object sweep will be skipped"
echo

#-----------------------------------------------------------------------------
echo "1. The test suites"
#-----------------------------------------------------------------------------
suite () {
    name="$1"; shift
    if c++ -std=c++17 -O2 -Wall -Isource "$@" -o "$WORK/$name" 2>"$WORK/$name.err"; then
        if "$WORK/$name" >"$WORK/$name.out" 2>&1; then
            pass "$name - $(tail -1 "$WORK/$name.out")"
        else
            fail "$name"; grep -E "FAILED" "$WORK/$name.out" | head -5
        fi
    else
        fail "$name did not compile"; head -5 "$WORK/$name.err"
    fi
}

suite dsptests       tests/DspTests.cpp source/Project6Dsp.cpp source/Project6Sample.cpp \
                     source/Project6Slots.cpp source/Project6Stretch.cpp
suite slottests      tests/SlotTests.cpp source/Project6Slots.cpp
suite wavtests       tests/WavTests.cpp source/Project6Sample.cpp
suite transporttests tests/TransportTests.cpp source/Project6Transport.cpp
suite stretchtests   tests/StretchTests.cpp source/Project6Stretch.cpp
suite miditests      tests/MidiTests.cpp source/Project6Midi.cpp

if [ -n "$SDK" ]; then
    suite paramstests -I"$SDK" tests/ParamsTests.cpp source/Project6Params.cpp \
                      source/Project6Dsp.cpp source/Project6Sample.cpp \
                      source/Project6Transport.cpp source/Project6Stretch.cpp
fi

#-----------------------------------------------------------------------------
echo
echo "2. Every source to a real object file, and the symbol sweep"
#-----------------------------------------------------------------------------
# Object files rather than -fsyntax-only, which happily passes a header that
# declares a function nobody ever defined.
if [ -n "$SDK" ]; then
    mkdir -p "$WORK/obj"
    objfail=0
    for f in source/*.cpp; do
        if ! g++ -c -std=c++17 -DLINUX=1 -DRELEASE=1 \
                 -I"$SDK" -I"$SDK/vstgui4" -Isource \
                 -o "$WORK/obj/$(basename "$f" .cpp).o" "$f" 2>"$WORK/cc.err"; then
            fail "$f did not compile"; head -5 "$WORK/cc.err"; objfail=1
        fi
    done
    [ "$objfail" -eq 0 ] && pass "all $(ls source/*.cpp | wc -l | tr -d ' ') sources compiled"

    nm -C "$WORK"/obj/*.o | grep " U " | grep "Project6::" | sed 's/.* U //' | sort -u > "$WORK/undef"
    nm -C "$WORK"/obj/*.o | grep -E " [TtWwVvDdBb] " | grep "Project6::" \
        | sed 's/.* [TtWwVvDdBb] //' | sort -u > "$WORK/def"
    missing="$(comm -23 "$WORK/undef" "$WORK/def")"
    if [ -z "$missing" ]; then
        pass "every Project6:: symbol used is defined"
    else
        fail "undefined Project6:: symbols:"; printf '%s\n' "$missing"
    fi
fi

#-----------------------------------------------------------------------------
echo
echo "3. The checks no runtime test can reach"
#-----------------------------------------------------------------------------
if python3 tools/check-editor.py >"$WORK/editor.out" 2>&1; then
    pass "$(cat "$WORK/editor.out")"
else
    fail "check-editor"; cat "$WORK/editor.out"
fi

if python3 tools/render-routing.py >"$WORK/routing.out" 2>&1; then
    pass "render-routing"
else
    fail "render-routing"; cat "$WORK/routing.out"
fi

#-----------------------------------------------------------------------------
echo
if [ "$FAILED" -eq 0 ]; then
    echo "All checks passed."
    exit 0
fi
echo "$FAILED check(s) FAILED."
exit 1
