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
SKIPPED=0
pass () { printf '  ok    %s\n' "$1"; }
fail () { printf '  FAIL  %s\n' "$1"; FAILED=$((FAILED + 1)); }
# A check that could not RUN is not a check that passed. It is counted and
# said again at the end, because the one line build-release.sh prints is the
# last one, and "all checks passed" with two of them never run is a lie.
skip () { printf '  skip  %s\n' "$1"; SKIPPED=$((SKIPPED + 1)); }

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

    # NOT A LIST OF LETTERS. It used to be one, taken from Linux, and it was
    # wrong in both directions. On Mach-O a coalesced vtable is S, which the
    # list did not contain, so on a Mac every vtable referenced from another
    # translation unit was reported undefined - two of them, loudly, on a
    # release build that was perfectly fine. And on Linux the list counted w
    # and v, which are weak UNDEFINED, as defined, while u - GNU's unique
    # global, which is a definition - was in neither list and so was silently
    # ignored. A list of letters can only ever be as complete as the platforms
    # it was written on, and three platforms disagree about lowercase.
    #
    # An ADDRESS is the thing that means the same everywhere: a symbol nm
    # prints an address for is defined here, and one it does not is wanted
    # from somewhere else. The type letter is then only used to tell a real
    # symbol line from nm's "file.o:" headers.
    : > "$WORK/undef.raw"; : > "$WORK/def.raw"
    nm -C "$WORK"/obj/*.o | awk -v u="$WORK/undef.raw" -v d="$WORK/def.raw" '
        { if ($1 ~ /^[0-9a-fA-F]+$/) { defined = 1; t = $2; i = 3 }
          else                       { defined = 0; t = $1; i = 2 }
          if (i > NF || length (t) != 1) next
          name = $i
          for (j = i + 1; j <= NF; ++j) name = name " " $j
          if (name !~ /Project6::/) next
          if (defined) print name > d; else print name > u }'
    sort -u "$WORK/undef.raw" > "$WORK/undef"
    sort -u "$WORK/def.raw"   > "$WORK/def"
    missing="$(comm -23 "$WORK/undef" "$WORK/def")"
    if [ -z "$missing" ]; then
        pass "every Project6:: symbol used is defined"
    else
        fail "undefined Project6:: symbols:"; printf '%s\n' "$missing"
    fi
else
    skip "paramstests and the object sweep - no VST3 SDK found"
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

# Pillow is not part of a Python install and not part of a Mac. Missing, this
# check cannot run - which is not the same as the diagram being wrong, and not
# a reason to stop a release, but it IS a reason to say so rather than print a
# traceback and call it a failure.
if ! python3 -c 'import PIL' >/dev/null 2>&1; then
    skip "render-routing and render-panel - Pillow is not installed (pip3 install --user Pillow)"
else
    for diagram in routing panel; do
        if python3 "tools/render-$diagram.py" >"$WORK/$diagram.out" 2>&1; then
            pass "render-$diagram"
        else
            fail "render-$diagram"; cat "$WORK/$diagram.out"
        fi
    done
fi

#-----------------------------------------------------------------------------
#-----------------------------------------------------------------------------
# The release kit: the version and maker's name agree everywhere, and the
# release scripts' own refusals still refuse. None of this needs a Mac.
#-----------------------------------------------------------------------------
printf '\n===== check-versions\n'
python3 "$ROOT/tools/check-versions.py" || FAILED=$((FAILED + 1))
for t in test-build-release test-publish; do
	printf '\n===== installer/%s\n' "$t"
	if [ -x "$ROOT/installer/$t.sh" ]; then
		"$ROOT/installer/$t.sh" || FAILED=$((FAILED + 1))
	else
		# A script that will not run is not a check that passed.
		echo "NOT EXECUTABLE: installer/$t.sh - chmod +x installer/*.sh"
		FAILED=$((FAILED + 1))
	fi
done

echo
if [ "$FAILED" -eq 0 ]; then
    if [ "$SKIPPED" -eq 0 ]; then
        echo "All checks passed."
    else
        echo "All checks passed - but $SKIPPED could not RUN and was skipped."
    fi
    exit 0
fi
echo "$FAILED check(s) FAILED."
exit 1
