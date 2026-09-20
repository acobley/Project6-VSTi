#!/bin/bash
#-----------------------------------------------------------------------------
# Publish the package installer/build-release.sh just made: checksum, push,
# tag, GitHub Release.
#
#   installer/publish-release.sh                do it
#   installer/publish-release.sh --dry-run      say what it would do
#   installer/publish-release.sh --remote up    push somewhere other than origin
#
# It refuses to publish anything Gatekeeper would refuse, so the order is:
# check the package, then write its checksum into the notes and commit that,
# then push, tag and release. Nothing is pushed until the package has passed.
#
# The tag points at the commit the package was BUILT from - which is HEAD,
# because build-release.sh will not build from a dirty tree - plus the one
# commit this script makes to record the checksum.
#
# The .pkg goes up as a RELEASE ASSET, not into the repository. A tag and a
# checksum say which source built it; a binary in the history would be there
# for good, in every clone, for every version anybody ever shipped.
#-----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
# Absolute, because the next line moves us. See build-release.sh.
SELF="$HERE/$(basename "${BASH_SOURCE[0]}")"
cd "$ROOT"

DRY_RUN=0
REMOTE="${REMOTE:-origin}"

usage () {
	awk 'NR == 1 { next }
	     /^#/   { sub(/^# ?/, ""); print; next }
	            { exit }' "$SELF"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) DRY_RUN=1; shift ;;
		--remote)  REMOTE="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "publish-release: unknown argument '$1'" >&2; exit 1 ;;
	esac
done

run () {
	if [ "$DRY_RUN" -eq 1 ]; then
		printf '    would run:'; printf ' %q' "$@"; printf '\n'
	else
		"$@"
	fi
}

VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' CMakeLists.txt)"
NAME="$(sed -n 's/^[[:space:]]*set(PLUGIN_NAME[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -1)"
PKG="$HERE/$NAME-$VERSION.pkg"
NOTES="$HERE/release-notes-$VERSION.md"
TAG="v$VERSION"

echo "==> $NAME $VERSION"

[ -f "$PKG" ]   || { echo "publish-release: $PKG is not there - run installer/build-release.sh." >&2; exit 1; }
[ -f "$NOTES" ] || { echo "publish-release: $NOTES is not there - the release needs notes." >&2; exit 1; }

command -v gh >/dev/null 2>&1 || { echo "publish-release: gh is not installed (brew install gh)." >&2; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "publish-release: gh is not logged in (gh auth login)." >&2; exit 1; }
git remote get-url "$REMOTE" >/dev/null 2>&1 || {
	echo "publish-release: there is no git remote called \"$REMOTE\"." >&2
	git remote -v >&2
	exit 1
}

#-----------------------------------------------------------------------------
# NOTHING GOES OUT THAT GATEKEEPER WOULD REFUSE. spctl asks, on this machine,
# the same question the downloading machine will ask. Everything else only
# proves the paperwork is in order.
#-----------------------------------------------------------------------------
if [ "$(uname -s)" = "Darwin" ]; then
	assessment="$(spctl --assess --type install -vv "$PKG" 2>&1 || true)"
	if ! printf '%s' "$assessment" | grep -q "source=Notarized Developer ID"; then
		echo "publish-release: Gatekeeper does not accept this package." >&2
		printf '%s\n' "$assessment" >&2
		echo >&2
		echo "Build it signed and notarised first: installer/build-release.sh" >&2
		exit 1
	fi
	echo "==> Gatekeeper accepts it as a notarised Developer ID package"
	pkgutil --check-signature "$PKG" | head -3
fi

#-----------------------------------------------------------------------------
# The tag must not already exist, here or there: a release is published once.
#-----------------------------------------------------------------------------
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
	echo "publish-release: the tag $TAG already exists locally." >&2
	echo "  Bump PLUGIN_VERSION, or delete it: git tag -d $TAG" >&2
	exit 1
fi
if git ls-remote --exit-code --tags "$REMOTE" "$TAG" >/dev/null 2>&1; then
	echo "publish-release: $TAG is already on $REMOTE - that version has shipped." >&2
	exit 1
fi

#-----------------------------------------------------------------------------
# The checksum goes in the notes BEFORE the tag, so the published notes
# describe the bytes that are published. Taken from the finished, stapled
# package - stapling changes them.
#
# Three cases, told apart on purpose: a placeholder to fill, the right
# checksum already there, and a checksum that is not this package's. The
# third is the interesting one - it means the notes are stale or the package
# was rebuilt after they were written - and saying so beats silently
# publishing a checksum nobody can reproduce.
#-----------------------------------------------------------------------------
SUM="$(shasum -a 256 "$PKG" | awk '{print $1}')"
echo "==> sha256 $SUM"

NOTES_CHANGED=0

if grep -q '<paste' "$NOTES"; then
	NOTES_CHANGED=1
	tmp="$(mktemp)"
	sed "s|<paste.*>|$SUM  $NAME-$VERSION.pkg|" "$NOTES" > "$tmp"
	run cp "$tmp" "$NOTES"
	rm -f "$tmp"
	# A plain `if`, not `[ x ] && echo`: bash exempts the left-hand side of
	# an && from errexit so that spelling is safe HERE, but it is only safe
	# because of what follows it, and the day somebody moves it to the end
	# of a function it starts returning 1 instead.
	if [ "$DRY_RUN" -eq 0 ]; then
		echo "==> checksum written into $(basename "$NOTES")"
	fi
elif grep -q "$SUM" "$NOTES"; then
	echo "==> the notes already carry this checksum"
elif grep -qE '\b[0-9a-f]{64}\b' "$NOTES"; then
	echo "publish-release: the notes carry a checksum that is not this package's." >&2
	echo "  Either the notes are stale or the package was rebuilt. Fix one of them." >&2
	exit 1
else
	echo "publish-release: the notes have no checksum and nowhere to put one." >&2
	echo "  Give them a line reading <paste the sha256 here> and run this again." >&2
	exit 1
fi

# The second test is what a real run needs; the first is what makes a DRY
# run honest, since a dry run never writes the notes and so never has
# anything for git to notice.
if [ "$NOTES_CHANGED" -eq 1 ] || [ -n "$(git status --porcelain "$NOTES")" ]; then
	run git add "$NOTES"
	run git commit -m "Record the $VERSION checksum"
fi

# Anything ELSE uncommitted means the tag would not describe the tree.
if [ -n "$(git status --porcelain)" ]; then
	echo "publish-release: the working tree has other uncommitted changes:" >&2
	git status --short >&2
	exit 1
fi

#-----------------------------------------------------------------------------
# Push, tag, release.
#-----------------------------------------------------------------------------
run git push "$REMOTE" HEAD
run git tag -a "$TAG" -m "$NAME $VERSION"
run git push "$REMOTE" "$TAG"

run gh release create "$TAG" "$PKG" \
	--title "$NAME $VERSION" \
	--notes-file "$NOTES"

if [ "$DRY_RUN" -eq 1 ]; then
	echo
	echo "    dry run - nothing was pushed"
	exit 0
fi

SLUG="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || true)"
echo
echo "    Published $TAG"
if [ -n "$SLUG" ]; then
	echo "    https://github.com/$SLUG/releases/tag/$TAG"
	echo "    https://github.com/$SLUG/releases/download/$TAG/$NAME-$VERSION.pkg"
fi
echo
echo "    The asset is under Releases, not in the repository's file listing."
echo "    Download it in a browser and open it: that is the only honest test"
echo "    that notarisation is doing its job, because only a browser sets the"
echo "    quarantine attribute Gatekeeper reads."
