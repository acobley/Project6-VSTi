#!/bin/bash
#-----------------------------------------------------------------------------
# installer/build-release.sh - tests, a clean universal Release build, the
# checks that matter, and ONE signed, notarised, stapled .pkg.
#
#   installer/build-release.sh                  the whole sequence
#   installer/build-release.sh --unsigned       local build, no certificates
#   installer/build-release.sh --skip-tests     when you have just run them
#   installer/build-release.sh --allow-dirty    a build you will throw away
#
# Then:   installer/publish-release.sh --dry-run
#         installer/publish-release.sh
#
# MUST RUN ON macOS. pkgbuild, codesign, spctl and notarytool are Apple's.
#
# This is build-installer.sh plus everything that has to be true BEFORE it:
# the versions agree, the tag is not already taken, the suites pass, the AU
# presets match the VST3 ones, the configure is fresh, no home-directory
# paths are in the binaries, both architectures are present, and no earlier
# package is lying around to be mistaken for this one.
#
# THE IDENTITIES ARE LOOKED UP, not written down here. A machine with two
# Developer ID certificates - or none - should be told, not guessed at.
# Override with --sign-app / --sign-installer; the notary profile defaults
# to project6-notary, or pass --notarize <profile> / set NOTARY_PROFILE.
#-----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

NOTARY_PROFILE="${NOTARY_PROFILE:-project6-notary}"
SIGN_APP=""
SIGN_INSTALLER=""
UNSIGNED=0
SKIP_TESTS=0
ALLOW_DIRTY=0

while [ $# -gt 0 ]; do
	case "$1" in
		--sign-app)       SIGN_APP="$2"; shift 2 ;;
		--sign-installer) SIGN_INSTALLER="$2"; shift 2 ;;
		--notarize)       NOTARY_PROFILE="$2"; shift 2 ;;
		--unsigned)       UNSIGNED=1; shift ;;
		--skip-tests)     SKIP_TESTS=1; shift ;;
		--allow-dirty)    ALLOW_DIRTY=1; shift ;;
		-h|--help)        sed -n '2,26p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-release: unknown argument '$1'" >&2; exit 1 ;;
	esac
done

die () { echo "build-release: $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "this needs macOS - pkgbuild and codesign are Apple's."

VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' CMakeLists.txt)"
# The name the package is built under: build-installer.sh's NAME=, which is
# what it names the .pkg after. PLUGIN_NAME in CMakeLists.txt if it has none.
NAME="$(sed -n 's/^NAME="\([^"]*\)".*/\1/p' "$HERE/build-installer.sh" | head -1)"
[ -n "$NAME" ] || NAME="$(sed -n 's/^[[:space:]]*set(PLUGIN_NAME[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] && [ -n "$NAME" ] || die "could not read the plug-in name and PLUGIN_VERSION"
TAG="v$VERSION"

echo "==> $NAME $VERSION"

#-----------------------------------------------------------------------------
# 0. Things that are cheap to check and expensive to find out about after a
#    fifteen-minute notarisation.
#-----------------------------------------------------------------------------
# The version is written in three files. The AU host cache keys on the AU
# one, so a mismatch there ships a changed plug-in under an old identity.
# The maker's name is checked alongside: the AU and the VST3 must agree on it.
python3 tools/check-versions.py || die "the version numbers or the maker's name disagree - fix them first."

# A published tag is never moved. If this version is already released,
# the answer is a new version number, and it is better said now.
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
	die "tag $TAG already exists - $VERSION has been released.
  Bump PLUGIN_VERSION (and source/version.h, resource/au-info.plist) first."
fi

[ -f "$HERE/release-notes-$VERSION.md" ] || die \
	"no installer/release-notes-$VERSION.md. Write the notes before the build:
  publish-release.sh will refuse without them, after the notarisation."

grep -q "TODO" "$HERE/release-notes-$VERSION.md" && die \
	"installer/release-notes-$VERSION.md still says TODO. Finish the notes first:
  they are what the release page shows."

# THE TAG WILL POINT AT THIS COMMIT, so the tree had better be this commit.
if [ "$ALLOW_DIRTY" -eq 0 ] && [ -n "$(git status --porcelain)" ]; then
	git status --short >&2
	die "the working tree has uncommitted changes.
  Commit them first, or pass --allow-dirty for a build you will throw away."
fi

#-----------------------------------------------------------------------------
# The identities, unless this is a local build.
#-----------------------------------------------------------------------------
findOne () {
	# $1: what to look for, $2: the find-identity flags to use
	local wanted="$1" listing count
	listing="$(security find-identity -v $2 2>/dev/null | grep "$wanted" || true)"
	count="$(printf '%s\n' "$listing" | grep -c "$wanted" || true)"
	if [ "$count" -eq 0 ]; then
		echo "build-release: no \"$wanted\" certificate in the keychain." >&2
		echo "  installer/build-installer.sh --list-identities shows what you have," >&2
		echo "  or build locally with --unsigned." >&2
		exit 1
	fi
	if [ "$count" -gt 1 ]; then
		echo "build-release: more than one \"$wanted\" certificate - say which:" >&2
		printf '%s\n' "$listing" >&2
		exit 1
	fi
	# The SHA-1 hash, which cannot be mistyped or mangled by a stray space.
	printf '%s' "$(printf '%s\n' "$listing" | awk '{print $2}')"
}

if [ "$UNSIGNED" -eq 0 ]; then
	[ -n "$SIGN_APP" ]       || SIGN_APP="$(findOne 'Developer ID Application' '-p codesigning')"
	# NOT -p codesigning: an installer certificate is not a code-signing one.
	[ -n "$SIGN_INSTALLER" ] || SIGN_INSTALLER="$(findOne 'Developer ID Installer' '')"
	echo "==> signing with $SIGN_APP (application) and $SIGN_INSTALLER (installer)"

	xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 || die \
		"the notary profile \"$NOTARY_PROFILE\" does not work.
  Store one with: xcrun notarytool store-credentials $NOTARY_PROFILE \\
      --apple-id <you> --team-id <TEAMID> --password <app-specific>"
	echo "==> notary profile $NOTARY_PROFILE answers"
fi

#-----------------------------------------------------------------------------
# 1. The tests, all of them, before anything is built.
#-----------------------------------------------------------------------------
if [ "$SKIP_TESTS" -eq 0 ]; then
	echo "==> running the test suites"
	# Whichever runner this project has - they grew up in different places.
	runner=""
	for r in tools/run-tests.sh tests/run-tests.sh; do
		[ -f "$r" ] && { runner="$r"; break; }
	done
	[ -n "$runner" ] || die "no tools/run-tests.sh or tests/run-tests.sh to run."
	[ -x "$runner" ] || die "$runner is not executable - chmod +x it."
	log="$(mktemp "${TMPDIR:-/tmp}/release-tests.XXXXXX")"
	"$runner" >"$log" 2>&1 || {
		# The failures, wherever they are - a tail can scroll them off.
		grep -n -A4 -E 'FAIL|BUILD FAILED|NOT EXECUTABLE' "$log" >&2 || tail -40 "$log" >&2
		die "tests failed - the whole log is $log"
	}
	tail -1 "$log"
	rm -f "$log"
fi

#-----------------------------------------------------------------------------
# 2. The factory presets. Every .vstpreset needs an .aupreset made from the
#    same bytes, or the AU and the VST3 ship different factory sounds.
#-----------------------------------------------------------------------------
if [ -f tools/make-presets.py ] && ls installer/presets/*.vstpreset >/dev/null 2>&1; then
	python3 tools/make-presets.py --check || die \
		"the .aupreset files are missing or stale. Run tools/make-presets.py,
  listen to the result, commit it, and build again."
	echo "==> $(ls installer/presets/*.vstpreset | wc -l | tr -d ' ') factory preset(s), AU copies current"
fi

#-----------------------------------------------------------------------------
# 3. A CLEAN configure. Compile flags and the version are baked in at
#    configure time, and a new source file is only noticed then.
#-----------------------------------------------------------------------------
echo "==> configuring (clean) and building Release"
./setup-xcode.sh --no-open --clean
cmake --build build --config Release

VST3_BIN="build/VST3/Release/$NAME.vst3/Contents/MacOS/$NAME"
AU_BIN="build/VST3/Release/$NAME.component/Contents/MacOS/$NAME"
[ -f "$VST3_BIN" ] || die "$VST3_BIN was not built."
[ -f "$AU_BIN" ]   || die "$AU_BIN was not built - the AU target needs the Xcode generator."

#-----------------------------------------------------------------------------
# 4. The two things about a binary that are invisible once it ships - for
#    both of them: the AU wrapper is compiled here too.
#-----------------------------------------------------------------------------
# ONE KNOWN EXCEPTION. Apple's AudioUnitSDK is built as its own Xcode
# project, which our -ffile-prefix-map does not reach, so its assert
# macros leave two __FILE__ paths under external/AudioUnitSDK in the AU
# wrapper. 1.0.0.1 shipped with them. They name your home directory and
# nothing else; they are reported, not fatal. Any OTHER path is fatal.
for bin in "$VST3_BIN" "$AU_BIN"; do
	all="$(strings -a "$bin" | grep -c "$HOME" || true)"
	sdk="$(strings -a "$bin" | grep "$HOME" | grep -c "/external/AudioUnitSDK/" || true)"
	paths=$((all - sdk))
	[ "$paths" -eq 0 ] || die "$bin carries $paths absolute paths naming your home
  directory. -ffile-prefix-map only takes effect on a fresh configure."
	[ "$sdk" -eq 0 ] || echo "==> note: $sdk AudioUnitSDK source path(s) in $(basename "$bin") (known; see step 4)"

	archs="$(lipo -archs "$bin")"
	if [ "$UNSIGNED" -eq 0 ]; then
		case "$archs" in
			*x86_64*arm64*|*arm64*x86_64*) ;;
			*) die "$bin is $archs, but the installer advertises both.
  On an Intel Mac, or under Rosetta, it would install and load nothing." ;;
		esac
	fi
	echo "==> $(basename "$(dirname "$(dirname "$(dirname "$bin")")")"): $archs, no build paths"
done

#-----------------------------------------------------------------------------
# 5. No earlier package left where this one goes. An unsigned build writes
#    the same path as a signed one and nothing in the name tells them apart.
#-----------------------------------------------------------------------------
rm -f "$HERE/$NAME-"*.pkg "$HERE/.built-from"

#-----------------------------------------------------------------------------
# 6. The package itself, ONCE, signed. build-installer.sh writes
#    installer/.built-from, which is what publish-release.sh tags.
#-----------------------------------------------------------------------------
if [ "$UNSIGNED" -eq 1 ]; then
	echo "==> building an UNSIGNED package (local use only)"
	"$HERE/build-installer.sh"
else
	"$HERE/build-installer.sh" \
		--sign-app "$SIGN_APP" \
		--sign-installer "$SIGN_INSTALLER" \
		--notarize "$NOTARY_PROFILE"
fi

PKG="$HERE/$NAME-$VERSION.pkg"
[ -f "$PKG" ] || die "$PKG is not there after the build."

echo
echo "    $PKG"
shasum -a 256 "$PKG"
echo
if [ "$UNSIGNED" -eq 1 ]; then
	echo "    UNSIGNED. Fine on this Mac; Gatekeeper refuses it if it is downloaded."
	echo "    publish-release.sh will refuse it too."
else
	echo "    Next:  installer/verify-install.sh   (after installing it here)"
	echo "           installer/publish-release.sh --dry-run"
	echo "           installer/publish-release.sh"
fi
