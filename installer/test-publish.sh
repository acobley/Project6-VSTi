#!/bin/bash
#------------------------------------------------------------------------
# installer/test-publish.sh
#
# Exercise publish-release.sh's refusals by STUBBING the tools it calls.
# Runs anywhere bash and git do - it never calls stapler, spctl, shasum or
# gh for real, so it works on Linux where publish-release.sh itself cannot.
#
# WHY STUBS. Every guard in that script is about a condition you cannot
# conveniently produce on demand: a package that is signed but not
# stapled, one Gatekeeper refuses, a build from a dirty tree. Reading the
# checks proves nothing; a stub makes each condition a one-line
# environment variable and the check either fires or it does not.
#
# Publishing is the one irreversible step in the whole release - a tag
# and an asset other people can fetch - so its refusals are worth more
# than the usual amount of testing.
#------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/publish-release.sh"
[ -x "$SCRIPT" ] || { echo "test-publish: cannot execute $SCRIPT" >&2; exit 2; }
command -v git >/dev/null || { echo "test-publish: needs git" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin"

cat > "$TMP/bin/uname" <<'EOF'
#!/bin/bash
[ "${1:-}" = "-s" ] && echo "${FAKE_UNAME:-Darwin}" || /usr/bin/uname "$@"
EOF
cat > "$TMP/bin/xcrun" <<'EOF'
#!/bin/bash
[ "${FAKE_STAPLED:-1}" = "1" ] || { echo "The validate action failed" >&2; exit 65; }
EOF
cat > "$TMP/bin/spctl" <<'EOF'
#!/bin/bash
if [ "${FAKE_GATEKEEPER:-1}" = "1" ]; then
    echo "pkg: accepted"; echo "source=Notarized Developer ID"
else
    echo "pkg: rejected"; echo "source=no usable signature"
fi
EOF
cat > "$TMP/bin/shasum" <<'EOF'
#!/bin/bash
echo "${FAKE_SHA:-aaaabbbbccccddddeeeeffff00001111222233334444555566667777888899990}  pkg"
EOF
cat > "$TMP/bin/gh" <<'EOF'
#!/bin/bash
[ "$1" = "auth" ] && exit "${FAKE_GH_AUTH_FAIL:-0}"
echo "gh $*"
EOF
chmod +x "$TMP/bin"/*
export PATH="$TMP/bin:$PATH"

setup () {
    rm -rf "$TMP/proj"; mkdir -p "$TMP/proj/installer"
    cd "$TMP/proj" || exit 2
    printf 'set(PLUGIN_VERSION      "1.0.0.1")\n' > CMakeLists.txt
    cp "$SCRIPT" installer/ && chmod +x installer/publish-release.sh
    # The name comes from build-installer.sh's NAME=, as in every project.
    printf '#!/bin/bash\nNAME="Plug"\n' > installer/build-installer.sh
    printf 'notes\n\nSHA-256: <paste from: shasum -a 256 x>\n' \
        > installer/release-notes-1.0.0.1.md
    : > installer/Plug-1.0.0.1.pkg
    git init -q . && git config user.email t@t && git config user.name T
    git add -A && git commit -qm "build commit"
    # origin is a LOCAL bare repository, so the remote-tag check runs for
    # real without the network.
    rm -rf "$TMP/origin.git"; git init -q --bare "$TMP/origin.git"
    git remote add origin "$TMP/origin.git"
    git push -q origin HEAD
    { echo "commit=$(git rev-parse HEAD)"; echo "version=1.0.0.1"; echo "dirty=no"; } \
        > installer/.built-from
    cd - >/dev/null || exit 2
}

# macOS sed wants `-i ''`, GNU sed wants `-i` alone, and each takes the
# other's spelling as a different command. This is neither.
sedi () {   # sedi <sed-expression> <file>
    sed "$1" "$2" > "$2.sedi" && cat "$2.sedi" > "$2" && rm -f "$2.sedi"
}

pass=0; fail=0
case_ () {   # case_ <want-exit> <label> [VAR=value | --arg ...]
    local want="$1" label="$2"; shift 2
    local envs=() args=()
    for a in "$@"; do case "$a" in *=*) envs+=("$a") ;; *) args+=("$a") ;; esac; done
    setup >/dev/null 2>&1
    [ -n "${PREP:-}" ] && eval "$PREP"
    # ${a[@]+...}: bash 3.2, macOS's, calls an empty array unbound under set -u.
    ( cd "$TMP/proj" && env ${envs[@]+"${envs[@]}"} ./installer/publish-release.sh --dry-run ${args[@]+"${args[@]}"} ) \
        > "$TMP/out" 2>&1
    local got=$?
    if [ "$got" = "$want" ] && { [ -z "${EXPECT:-}" ] || grep -q -- "$EXPECT" "$TMP/out"; }; then
        printf '  ok    %s\n' "$label"; pass=$((pass+1))
    else
        printf '  FAIL  %s (wanted exit %s, got %s%s)\n' "$label" "$want" "$got" "${EXPECT:+, looking for: $EXPECT}"
        grep -m2 . "$TMP/out" | sed 's/^/        /'
        fail=$((fail+1))
    fi
    unset PREP EXPECT
}

echo "PUBLISH GUARDS - the refusals that stand between a bad build and a tag"
case_ 0 "a good package publishes (dry run)"
case_ 1 "refuses when not run on macOS"                        FAKE_UNAME=Linux
EXPECT="NOT stapled"
case_ 1 "refuses a package that is NOT stapled"                FAKE_STAPLED=0
EXPECT="did not accept"
case_ 1 "refuses a package Gatekeeper rejects"                 FAKE_GATEKEEPER=0

EXPECT=".built-from is missing"
PREP='rm -f "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when .built-from is missing"

EXPECT="DIRTY tree"
PREP='sedi s/dirty=no/dirty=yes/ "$TMP/proj/installer/.built-from"'
case_ 1 "refuses a package built from a DIRTY tree"

EXPECT="provenance"
PREP='sedi s/version=1.0.0.1/version=0.9.0.0/ "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when .built-from names a different version"

EXPECT="not in this repository"
PREP='sedi "s/^commit=.*/commit=0000000000000000000000000000000000000000/" "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when the build commit is not in the repo"

EXPECT="not this package"
PREP='printf "notes\n\nSHA-256: 1111111111111111111111111111111111111111111111111111111111111111\n" > "$TMP/proj/installer/release-notes-1.0.0.1.md"'
case_ 1 "refuses notes carrying a DIFFERENT checksum"

EXPECT="no package at"
PREP='rm -f "$TMP/proj/installer/Plug-1.0.0.1.pkg"'
case_ 1 "refuses when there is no package"

EXPECT="not authenticated"
case_ 1 "refuses when gh is not authenticated - before any push" FAKE_GH_AUTH_FAIL=1

EXPECT="besides the notes"
PREP='echo change >> "$TMP/proj/CMakeLists.txt"'
case_ 1 "refuses another uncommitted change besides the notes"

PREP='echo change >> "$TMP/proj/installer/release-notes-1.0.0.1.md"'
case_ 0 "an uncommitted change to the notes themselves is fine"

EXPECT="not an ancestor"
PREP='cd "$TMP/proj" && git checkout -q -b other HEAD~0 && git commit -q --allow-empty -m side && sedi "s/^commit=.*/commit=$(git rev-parse HEAD)/" installer/.built-from && git checkout -q -'
case_ 1 "refuses a build commit that is not on this branch"

EXPECT="already on GitHub"
PREP='cd "$TMP/proj" && git commit -q --allow-empty -m other && git tag -a v1.0.0.1 -m x && git push -q origin v1.0.0.1 && git tag -d v1.0.0.1 >/dev/null && git reset -q --hard HEAD~1'
case_ 1 "refuses when GitHub already has the tag at ANOTHER commit"

PREP='cd "$TMP/proj" && git tag -a v1.0.0.1 -m x "$(sed -n s/^commit=//p installer/.built-from)" && git push -q origin v1.0.0.1 && git tag -d v1.0.0.1 >/dev/null'
EXPECT="would push"
case_ 0 "carries on when GitHub has the tag at the build commit (a re-run)"

EXPECT="could not reach origin"
PREP='cd "$TMP/proj" && git remote set-url origin "$TMP/nowhere.git"'
case_ 1 "refuses when origin cannot be reached to check the tag"

PREP='cd "$TMP/proj" && git remote add up "$TMP/origin.git" && git remote set-url origin "$TMP/nowhere.git"'
EXPECT="would push"
case_ 0 "--remote pushes to, and checks tags on, the remote it is given" --remote up

echo
echo "--------------------"
echo "$((pass+fail)) cases, $fail failures"
[ "$fail" = 0 ]
