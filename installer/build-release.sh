#!/bin/bash
#-----------------------------------------------------------------------------
# Build a release: tests, a clean universal Release build, the checks that
# matter, and one signed, notarised .pkg.
#
#   installer/build-release.sh                  the whole sequence
#   installer/build-release.sh --unsigned       local build, no certificates
#   installer/build-release.sh --skip-tests     when you have just run them
#   installer/build-release.sh --allow-dirty    a build you will throw away
#
# MUST RUN ON macOS. pkgbuild, codesign, spctl and notarytool are Apple's.
#
# This is build-installer.sh plus everything that has to be true BEFORE it:
# the suites pass, the configure is fresh, no home-directory paths are in the
# binary, both architectures are present, and no earlier package is lying
# around to be mistaken for this one.
#
# THE IDENTITIES ARE LOOKED UP, not written down here. A Developer ID
# certificate is public, but a machine with two of them - or none - should
# be told, not guessed at. Override with --sign-app / --sign-installer, or
# use --unsigned for a local build that cannot be notarised.
#-----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
# ABSOLUTE, because the next line moves us and $BASH_SOURCE is whatever the
# caller typed. `./build-release.sh --help` from inside installer/ cannot
# find itself once we have cd'd to the root.
SELF="$HERE/$(basename "${BASH_SOURCE[0]}")"
cd "$ROOT"

NOTARY_PROFILE="${NOTARY_PROFILE:-project6-notary}"
SIGN_APP=""
SIGN_INSTALLER=""
UNSIGNED=0
SKIP_TESTS=0
ALLOW_DIRTY=0

# The leading comment block IS the help, printed without hunting for line
# numbers - a hard-coded sed range goes stale the first time somebody adds a
# sentence to the header and nobody notices, because nothing tests --help.
usage () {
	awk 'NR == 1 { next }
	     /^#/   { sub(/^# ?/, ""); print; next }
	            { exit }' "$SELF"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--sign-app)       SIGN_APP="$2"; shift 2 ;;
		--sign-installer) SIGN_INSTALLER="$2"; shift 2 ;;
		--notarize)       NOTARY_PROFILE="$2"; shift 2 ;;
		--unsigned)       UNSIGNED=1; shift ;;
		--skip-tests)     SKIP_TESTS=1; shift ;;
		--allow-dirty)    ALLOW_DIRTY=1; shift ;;
		-h|--help)        usage; exit 0 ;;
		*) echo "build-release: unknown argument '$1'" >&2; exit 1 ;;
	esac
done

if [ "$(uname -s)" != "Darwin" ]; then
	echo "build-release: this needs macOS - pkgbuild and codesign are Apple's." >&2
	exit 1
fi

VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' CMakeLists.txt)"
NAME="$(sed -n 's/^[[:space:]]*set(PLUGIN_NAME[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] && [ -n "$NAME" ] || {
	echo "build-release: could not read PLUGIN_NAME/PLUGIN_VERSION from CMakeLists.txt" >&2
	exit 1
}

echo "==> $NAME $VERSION"

#-----------------------------------------------------------------------------
# THE TAG WILL POINT AT THIS COMMIT, so the tree had better be this commit.
# A release built from uncommitted work is one nobody can rebuild.
#-----------------------------------------------------------------------------
if [ "$ALLOW_DIRTY" -eq 0 ] && [ -d .git ] && [ -n "$(git status --porcelain)" ]; then
	echo "build-release: the working tree has uncommitted changes." >&2
	git status --short >&2
	echo >&2
	echo "Commit them first, or pass --allow-dirty for a build you will throw away." >&2
	exit 1
fi

#-----------------------------------------------------------------------------
# SAID NOW RATHER THAN AFTER THE BUILD. A universal Release build takes the
# best part of half an hour, and finding out at the end of it that the one
# thing publish-release.sh cannot write for you is missing is a bad trade for
# the two lines it costs to look.
#-----------------------------------------------------------------------------
if [ ! -f "$HERE/release-notes-$VERSION.md" ]; then
	echo "    note: installer/release-notes-$VERSION.md does not exist yet."
	echo "          publish-release.sh will refuse without it. Write it while"
	echo "          this builds - installer/release-notes-1.0.0.1.md is the shape."
	echo
fi

#-----------------------------------------------------------------------------
# The identities, unless this is a local build.
#-----------------------------------------------------------------------------
findOne () {
	# $1: what to look for, $2: the find-identity flags to use
	local wanted="$1" listing found count
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
	found="$(printf '%s\n' "$listing" | awk '{print $2}')"
	printf '%s' "$found"
}

if [ "$UNSIGNED" -eq 0 ]; then
	[ -n "$SIGN_APP" ]       || SIGN_APP="$(findOne 'Developer ID Application' '-p codesigning')"
	# NOT -p codesigning: an installer certificate is not a code-signing one.
	[ -n "$SIGN_INSTALLER" ] || SIGN_INSTALLER="$(findOne 'Developer ID Installer' '')"
	echo "==> signing with $SIGN_APP (application) and $SIGN_INSTALLER (installer)"

	if ! xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
		echo "build-release: the notary profile \"$NOTARY_PROFILE\" does not work." >&2
		echo "  Store one with: xcrun notarytool store-credentials $NOTARY_PROFILE \\" >&2
		echo "      --apple-id <you> --team-id <TEAMID> --password <app-specific>" >&2
		echo >&2
		echo "  If it was working yesterday, check the TEAM ID first. A wrong one" >&2
		echo "  comes back as an HTTP 500 from Apple, which reads like their fault" >&2
		echo "  and is not." >&2
		exit 1
	fi
	echo "==> notary profile $NOTARY_PROFILE answers"
fi

#-----------------------------------------------------------------------------
# 1. The tests, all of them, before anything is built.
#-----------------------------------------------------------------------------
if [ "$SKIP_TESTS" -eq 0 ]; then
	echo "==> running the test suites"
	# Spelled out rather than `mktemp -t project6-tests`: that form means
	# different things on macOS and on GNU, and this one means the same on
	# both.
	LOG="$(mktemp "${TMPDIR:-/tmp}/project6-tests.XXXXXX")"
	tools/run-tests.sh >"$LOG" 2>&1 || {
		tail -40 "$LOG" >&2
		echo "build-release: tests failed - see $LOG" >&2
		exit 1
	}
	tail -1 "$LOG"
	rm -f "$LOG"
fi

#-----------------------------------------------------------------------------
# 2. A CLEAN configure. Compile flags and the version are baked in at
#    configure time, and a new source file is only noticed then.
#-----------------------------------------------------------------------------
echo "==> configuring (clean) and building Release"
./setup-xcode.sh --no-open --clean
cmake --build build --config Release

BINARY="build/VST3/Release/$NAME.vst3/Contents/MacOS/$NAME"
[ -f "$BINARY" ] || { echo "build-release: $BINARY was not built." >&2; exit 1; }

#-----------------------------------------------------------------------------
# 3. The two things about the binary that are invisible once it ships.
#-----------------------------------------------------------------------------
paths="$(strings -a "$BINARY" | grep -c "$HOME" || true)"
if [ "$paths" -ne 0 ]; then
	echo "build-release: the binary carries $paths absolute paths naming your home" >&2
	echo "  directory. -ffile-prefix-map only takes effect on a fresh configure." >&2
	exit 1
fi
echo "==> no build paths in the binary"

archs="$(lipo -archs "$BINARY")"
if [ "$UNSIGNED" -eq 0 ]; then
	case "$archs" in
		*x86_64*arm64*|*arm64*x86_64*) echo "==> universal: $archs" ;;
		*)
			echo "build-release: this build is $archs, but the installer advertises both." >&2
			echo "  On an Intel Mac, or under Rosetta, it would install and load nothing." >&2
			echo "  Check PORT_UNIVERSAL, and that this was a clean configure." >&2
			exit 1 ;;
	esac
else
	echo "==> architectures: $archs"
fi

#-----------------------------------------------------------------------------
# 4. No earlier package left where this one goes. An unsigned build writes
#    the same path as a signed one and nothing in the name tells them apart.
#-----------------------------------------------------------------------------
rm -f "$HERE/$NAME-"*.pkg

#-----------------------------------------------------------------------------
# 5. The package itself, ONCE, signed.
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
[ -f "$PKG" ] || { echo "build-release: $PKG is not there after the build." >&2; exit 1; }

echo
echo "    $PKG"
shasum -a 256 "$PKG"
echo
if [ "$UNSIGNED" -eq 1 ]; then
	echo "    UNSIGNED. Fine on this Mac; Gatekeeper refuses it if it is downloaded."
else
	echo "    Next:  installer/publish-release.sh"
fi
