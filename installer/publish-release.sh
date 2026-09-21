#!/bin/bash
#------------------------------------------------------------------------
# installer/publish-release.sh
#
# Publish a built, signed, notarised, stapled .pkg as a GitHub Release.
#
# MUST RUN ON macOS with `gh` authenticated: it calls stapler and spctl,
# which are Apple's, and `gh`, which needs a token. It does NOT build or
# sign anything - build-installer.sh does that, once, and this refuses
# to publish anything it is not happy with.
#
#     installer/publish-release.sh [--dry-run] [--remote <name>]
#
# --remote (or REMOTE=) pushes somewhere other than origin.
#
# WHAT IT REFUSES, and why each one is worth refusing:
#
#   * a package that is not stapled. Notarising without stapling leaves
#     a package that passes only while the downloading Mac is online and
#     able to reach Apple. Stapling is what makes it work offline;
#   * a package Gatekeeper does not accept. Everything else is
#     paperwork; this is the question the downloading machine asks;
#   * a checksum taken before stapling. Stapling CHANGES THE BYTES, so a
#     hash from the unstapled file is wrong and unverifiable;
#   * a package built from a dirty tree, or from a commit that is not
#     pushed. A tag that names a tree nobody else can fetch is not a
#     record of anything;
#   * release notes that still hold the SHA-256 placeholder;
#   * a tag that already exists HERE OR ON GITHUB at another commit. A
#     published tag is never moved - bump the version instead;
#   * any uncommitted change other than the checksum it writes itself;
#   * a missing or unauthenticated `gh` - checked FIRST, so that a push
#     and a tag never happen without the release that should follow them.
#------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DRY_RUN=0
REMOTE="${REMOTE:-origin}"
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        --remote)  REMOTE="$2"; shift 2 ;;
        -h|--help) sed -n '2,33p' "$0"; exit 0 ;;
        *) echo "publish-release: unknown argument '$1'" >&2; exit 1 ;;
    esac
done

die () { echo "publish-release: $*" >&2; exit 1; }
say () { echo "==> $*"; }

[ "$(uname -s)" = "Darwin" ] || die "this needs macOS - stapler and spctl are Apple's."

VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || die "could not read PLUGIN_VERSION out of CMakeLists.txt"

# The name the package was built under - build-installer.sh's NAME=.
NAME="$(sed -n 's/^NAME="\([^"]*\)".*/\1/p' "$HERE/build-installer.sh" 2>/dev/null | head -1)"
[ -n "$NAME" ] || NAME="$(sed -n 's/^[[:space:]]*set(PLUGIN_NAME[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$NAME" ] || die "could not read the plug-in name"
PKG="$HERE/$NAME-$VERSION.pkg"
NOTES="$HERE/release-notes-$VERSION.md"
TAG="v$VERSION"

[ -f "$PKG" ]   || die "no package at $PKG - run build-release.sh first."

#--- gh first: nothing is pushed unless the release can follow it --------
command -v gh >/dev/null || die "gh is not installed - see cli.github.com"
gh auth status >/dev/null 2>&1 || die "gh is not authenticated - run: gh auth login"
[ -f "$NOTES" ] || die "no release notes at $NOTES."

#--- the commit the binary came from -------------------------------------
[ -f "$HERE/.built-from" ] || die \
    ".built-from is missing. It is written by build-installer.sh; without it
there is no record of which commit produced this package, and tagging HEAD
would name a tree that did not."

# shellcheck disable=SC1090
built_commit="$(sed -n 's/^commit=//p' "$HERE/.built-from")"
built_version="$(sed -n 's/^version=//p' "$HERE/.built-from")"
built_dirty="$(sed -n 's/^dirty=//p' "$HERE/.built-from")"

[ "$built_version" = "$VERSION" ] || die \
    "the package on disk is $VERSION but .built-from records $built_version.
Rebuild, rather than publishing a package whose provenance does not match."
[ "$built_dirty" = "no" ] || die \
    "this package was built from a DIRTY tree, so no commit describes it.
Commit, rebuild and publish that."

git -C "$ROOT" cat-file -e "$built_commit^{commit}" 2>/dev/null || die \
    "the recorded build commit $built_commit is not in this repository."

# Pushing the branch only publishes the build commit if it is ON the branch.
git -C "$ROOT" merge-base --is-ancestor "$built_commit" HEAD || die \
    "the build commit $built_commit is not an ancestor of HEAD. Check out the
branch it was built on - pushing this one would not publish it."

#--- nothing else uncommitted --------------------------------------------
# The notes are the one file this script is allowed to change and commit.
# Anything else would either be left out of the tag's story or swept in.
others="$(git -C "$ROOT" status --porcelain --untracked-files=no \
          | grep -v " installer/release-notes-$VERSION.md\$" || true)"
[ -z "$others" ] || die "there are uncommitted changes besides the notes:
$others
Commit or stash them first."

#--- the tag, here and on GitHub -----------------------------------------
# An annotated tag is listed twice: the tag object, and "^{}" - the commit
# it points at. The commit is the one to compare; a lightweight tag has
# only the first line, which is already a commit.
if remote_tag="$(git -C "$ROOT" ls-remote --tags "$REMOTE" 2>/dev/null)"; then
    remote_commit="$(printf '%s\n' "$remote_tag" | awk -v t="refs/tags/$TAG^{}" '$2 == t {print $1}')"
    [ -n "$remote_commit" ] || remote_commit="$(printf '%s\n' "$remote_tag" | awk -v t="refs/tags/$TAG" '$2 == t {print $1}')"
    if [ -n "$remote_commit" ] && [ "$remote_commit" != "$built_commit" ]; then
        die "tag $TAG is already on GitHub, at $remote_commit - not at the build
commit $built_commit. A published tag is never moved: bump the version."
    fi
else
    die "could not reach $REMOTE to check for an existing $TAG tag."
fi

#--- it must be stapled, and Gatekeeper must accept it -------------------
say "checking the package is stapled"
xcrun stapler validate "$PKG" >/dev/null 2>&1 || die \
    "$PKG is NOT stapled.
Signed is not enough: without a stapled ticket the package is accepted only
while the downloading Mac can reach Apple. Rebuild with --notarize."

say "asking Gatekeeper the question the downloading Mac will ask"
assessment="$(spctl --assess --type install -vv "$PKG" 2>&1 || true)"
echo "$assessment" | sed 's/^/    /'
printf '%s' "$assessment" | grep -q "source=Notarized Developer ID" || die \
    "Gatekeeper did not accept this package. Do not publish it."

#--- the checksum, from the finished file --------------------------------
say "taking the SHA-256 from the finished, stapled package"
sha="$(shasum -a 256 "$PKG" | awk '{print $1}')"
echo "    $sha"

if grep -q "<paste from:" "$NOTES"; then
    tmp="$(mktemp)"
    sed "s|SHA-256: <paste from:.*|SHA-256: $sha|" "$NOTES" > "$tmp"
    mv "$tmp" "$NOTES"
    say "wrote the checksum into $(basename "$NOTES")"
elif grep -q "SHA-256: $sha" "$NOTES"; then
    say "the notes already carry this checksum"
else
    die "the notes carry a SHA-256 that is not this package's.
Either the package was rebuilt after the notes were written, or the notes
describe a different build. Check before publishing."
fi

grep -q "SHA-256: [0-9a-f]\{64\}" "$NOTES" || die \
    "the notes still have no usable SHA-256 line."

#--- the tree has to be pushed -------------------------------------------
if [ -n "$(git -C "$ROOT" status --porcelain "$NOTES")" ]; then
    if [ "$DRY_RUN" = 1 ]; then
        say "[dry run] would commit the checksum into the notes"
    else
        git -C "$ROOT" add "$NOTES"
        git -C "$ROOT" commit -q -m "Release notes for $NAME $VERSION: the published SHA-256"
        say "committed the checksum"
    fi
fi

branch="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)"
if [ "$DRY_RUN" = 1 ]; then
    say "[dry run] would push $branch and tag $TAG at $built_commit"
else
    say "pushing $branch"
    git -C "$ROOT" push "$REMOTE" "$branch"
fi

# THE TAG NAMES THE COMMIT THE BINARY CAME FROM, not HEAD - the checksum
# commit above has already moved HEAD past it.
if git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    existing="$(git -C "$ROOT" rev-list -n1 "$TAG")"
    [ "$existing" = "$built_commit" ] || die \
        "tag $TAG already exists and points at $existing, not at the build
commit $built_commit. Bump the version rather than moving a published tag."
    say "tag $TAG already exists at the right commit"
else
    if [ "$DRY_RUN" = 1 ]; then
        say "[dry run] would create tag $TAG at $built_commit"
    else
        git -C "$ROOT" tag -a "$TAG" -m "$NAME $VERSION" "$built_commit"
        git -C "$ROOT" push "$REMOTE" "$TAG"
        say "tagged $built_commit as $TAG"
    fi
fi

#--- the release ---------------------------------------------------------
if [ "$DRY_RUN" = 1 ]; then
    say "[dry run] would run:"
    echo "    gh release create $TAG \"$PKG\" \\"
    echo "        --title \"$NAME $VERSION\" \\"
    echo "        --notes-file \"$NOTES\""
    echo
    say "[dry run] nothing was pushed, tagged or published."
    exit 0
fi

say "creating the release"
gh release create "$TAG" "$PKG" \
    --title "$NAME $VERSION" \
    --notes-file "$NOTES"

echo
say "published"
repo="$(git -C "$ROOT" remote get-url "$REMOTE" | sed 's#.*github.com[:/]##; s#\.git$##')"
echo
echo "    The .pkg is a RELEASE ASSET. It appears nowhere in the repository's"
echo "    file listing - it is under Releases:"
echo
echo "      https://github.com/$repo/releases/tag/$TAG"
echo "      https://github.com/$repo/releases/download/$TAG/$NAME-$VERSION.pkg"
echo
echo "    Download it from that URL in a browser and install from the download."
echo "    That is the only honest Gatekeeper test: a browser sets the quarantine"
echo "    attribute, curl and scp do not."
