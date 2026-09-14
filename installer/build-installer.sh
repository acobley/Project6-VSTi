#!/bin/bash
#-----------------------------------------------------------------------------
# Build a distributable .pkg installer for Project6.
#
# MUST RUN ON macOS: pkgbuild, productbuild and codesign are Apple's, and none
# of them exists anywhere else. Build the plug-in first, then run this.
#
#   ./setup-xcode.sh --no-open && cmake --build build --config Release
#   installer/build-installer.sh
#
# The result is installer/Project6-<version>.pkg, which installs
#
#   /Library/Audio/Plug-Ins/VST3/Project6.vst3
#   /Library/Audio/Plug-Ins/Components/Project6.component
#
# as two separately choosable components, so somebody who only wants one
# format gets only that one.
#
# SIGNING - read this before sending the result to anyone:
#
#   With no arguments the payload is ad-hoc signed and the .pkg is not signed
#   at all. That installs fine on THIS machine and is fine over AirDrop or a
#   USB stick. A .pkg DOWNLOADED from the internet is quarantined, and an
#   unsigned, un-notarised one is refused by Gatekeeper - the person has to go
#   to System Settings -> Privacy & Security and allow it by hand, which is
#   exactly what an installer from an untrusted stranger looks like.
#
#   For real distribution you need BOTH halves of a Developer ID and a
#   notarisation:
#
#     installer/build-installer.sh \
#       --sign-app       "Developer ID Application: Your Name (TEAMID)" \
#       --sign-installer "Developer ID Installer: Your Name (TEAMID)" \
#       --notarize       my-notary-profile
#
#   where my-notary-profile was stored once with
#     xcrun notarytool store-credentials my-notary-profile \
#         --apple-id you@example.com --team-id TEAMID --password <app-specific>
#-----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

NAME="Project6"
VST3_ID="audio.project6.vst3"
AU_ID="audio.project6.audiounit"

#-----------------------------------------------------------------------------
# THE VERSION COMES OUT OF CMakeLists.txt, not out of a second copy here. Two
# places to edit is one place to forget.
#-----------------------------------------------------------------------------
VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt")"
if [ -z "$VERSION" ]; then
    echo "build-installer: could not read PLUGIN_VERSION out of CMakeLists.txt" >&2
    exit 1
fi

SIGN_APP="-"          # ad-hoc, which is what the build itself uses
SIGN_INSTALLER=""
NOTARY_PROFILE=""
CONFIG="Release"

while [ $# -gt 0 ]; do
    case "$1" in
        --sign-app)       SIGN_APP="$2"; shift 2 ;;
        --sign-installer) SIGN_INSTALLER="$2"; shift 2 ;;
        --notarize)       NOTARY_PROFILE="$2"; shift 2 ;;
        --config)         CONFIG="$2"; shift 2 ;;
        -h|--help)        sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "build-installer: unknown argument '$1'" >&2; exit 1 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "build-installer: this needs macOS - pkgbuild and productbuild are Apple's." >&2
    exit 1
fi

for tool in pkgbuild productbuild codesign; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-installer: $tool not found. Install the Xcode command line tools." >&2
        exit 1
    }
done

BUILT="$ROOT/build/VST3/$CONFIG"
VST3_SRC="$BUILT/$NAME.vst3"
AU_SRC="$BUILT/$NAME.component"

for bundle in "$VST3_SRC" "$AU_SRC"; do
    [ -d "$bundle" ] || {
        echo "build-installer: $bundle is not there." >&2
        echo "  Build first:  ./setup-xcode.sh --no-open && cmake --build build --config $CONFIG" >&2
        exit 1
    }
done

WORK="$HERE/build"
rm -rf "$WORK"
mkdir -p "$WORK/root-vst3" "$WORK/root-au" "$WORK/pkgs"

echo "==> Project6 $VERSION, from $CONFIG"

#-----------------------------------------------------------------------------
# STAGE. cp -R and not a move: the build tree is left exactly as it was, so
# running this never costs you a rebuild.
#-----------------------------------------------------------------------------
cp -R "$VST3_SRC" "$WORK/root-vst3/$NAME.vst3"
cp -R "$AU_SRC"   "$WORK/root-au/$NAME.component"

#-----------------------------------------------------------------------------
# THE SYMLINK THAT WOULD HAVE SHIPPED A DEAD AUDIO UNIT.
#
# Steinberg's AU wrapper has no plug-in code of its own: it loads the VST3 out
# of its own bundle, at Contents/Resources/plugin.vst3. CMake puts a SYMLINK
# there, pointing at an ABSOLUTE PATH inside this machine's build tree:
#
#   .../Project6.component/Contents/Resources/plugin.vst3
#       -> /Users/<you>/DXi-DEv/Project6-VSTi/build/VST3/Release/Project6.vst3
#
# That is right for development - rebuild the VST3 and the AU follows - and it
# is fatal in an installer. Copied as-is onto another machine the symlink
# dangles, the wrapper finds nothing to load, and the AU fails to instantiate
# with no useful error. A .pkg built by pointing pkgbuild straight at the
# build directory has exactly this bug and looks perfect until somebody else
# tries it.
#
# So the link is replaced with a REAL COPY of the VST3 bundle, and the
# .component becomes self-contained.
#-----------------------------------------------------------------------------
AU_RESOURCES="$WORK/root-au/$NAME.component/Contents/Resources"
if [ -L "$AU_RESOURCES/plugin.vst3" ] || [ -e "$AU_RESOURCES/plugin.vst3" ]; then
    rm -rf "$AU_RESOURCES/plugin.vst3"
fi
mkdir -p "$AU_RESOURCES"
cp -R "$VST3_SRC" "$AU_RESOURCES/plugin.vst3"
echo "==> embedded a real copy of $NAME.vst3 inside the .component"

#-----------------------------------------------------------------------------
# AND THE GUARD, because the above fixes the symlink we know about and this
# catches the one somebody adds later. Any symlink pointing at an absolute
# path is a link to this machine and cannot survive the trip.
#-----------------------------------------------------------------------------
escapes=""
while IFS= read -r link; do
    target="$(readlink "$link")"
    case "$target" in
        /*) escapes="$escapes
  $link -> $target" ;;
    esac
done < <(find "$WORK/root-vst3" "$WORK/root-au" -type l)

if [ -n "$escapes" ]; then
    echo "build-installer: absolute symlinks in the payload - these point at THIS" >&2
    echo "machine and would dangle on any other:$escapes" >&2
    exit 1
fi
echo "==> no symlink in the payload points outside it"

#-----------------------------------------------------------------------------
# SIGN, INNERMOST FIRST. The nested plugin.vst3 has to be signed before the
# .component that contains it, or signing the outer bundle seals a signature
# that is then invalidated by signing the inner one.
#-----------------------------------------------------------------------------
echo "==> signing payload as: $SIGN_APP"
codesign --force --timestamp=none --sign "$SIGN_APP" "$AU_RESOURCES/plugin.vst3"
codesign --force --timestamp=none --sign "$SIGN_APP" "$WORK/root-au/$NAME.component"
codesign --force --timestamp=none --sign "$SIGN_APP" "$WORK/root-vst3/$NAME.vst3"

codesign --verify --deep --strict "$WORK/root-vst3/$NAME.vst3"
codesign --verify --deep --strict "$WORK/root-au/$NAME.component"
echo "==> signatures verify"

#-----------------------------------------------------------------------------
# COMPONENT PACKAGES, one per format, so the choice pane can offer them
# separately.
#-----------------------------------------------------------------------------
pkgbuild --quiet \
    --root "$WORK/root-vst3" \
    --identifier "$VST3_ID.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$WORK/pkgs/$NAME-VST3.pkg"

pkgbuild --quiet \
    --root "$WORK/root-au" \
    --identifier "$AU_ID.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    --scripts "$HERE/scripts-au" \
    "$WORK/pkgs/$NAME-AU.pkg"

echo "==> component packages built"

#-----------------------------------------------------------------------------
# THE PRODUCT ARCHIVE. distribution.xml carries the panes and the two
# choices; the version is substituted rather than duplicated.
#-----------------------------------------------------------------------------
mkdir -p "$WORK/resources"
cp "$HERE/resources/welcome.html" "$HERE/resources/conclusion.html" "$WORK/resources/"
cp "$ROOT/LICENSE" "$WORK/resources/license.txt"

sed "s/__VERSION__/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

UNSIGNED="$WORK/$NAME-$VERSION-unsigned.pkg"
FINAL="$HERE/$NAME-$VERSION.pkg"

productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK/pkgs" \
    --resources "$WORK/resources" \
    "$UNSIGNED"

if [ -n "$SIGN_INSTALLER" ]; then
    echo "==> signing the installer as: $SIGN_INSTALLER"
    productsign --sign "$SIGN_INSTALLER" "$UNSIGNED" "$FINAL"
    pkgutil --check-signature "$FINAL"
else
    cp "$UNSIGNED" "$FINAL"
    echo "==> NOT SIGNED. Fine locally; Gatekeeper will refuse it if it is"
    echo "    downloaded. See --sign-installer in the header of this script."
fi

if [ -n "$NOTARY_PROFILE" ]; then
    [ -n "$SIGN_INSTALLER" ] || {
        echo "build-installer: --notarize needs --sign-installer; Apple will not" >&2
        echo "notarise an unsigned package." >&2
        exit 1
    }
    echo "==> submitting for notarisation (this waits, and can take minutes)"
    xcrun notarytool submit "$FINAL" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$FINAL"
    xcrun stapler validate "$FINAL"
    echo "==> notarised and stapled"
fi

rm -rf "$WORK"

echo
echo "    $FINAL"
echo
echo "    Installs:  /Library/Audio/Plug-Ins/VST3/$NAME.vst3"
echo "               /Library/Audio/Plug-Ins/Components/$NAME.component"
echo
echo "    Check what is really in it with:"
echo "      pkgutil --payload-files \"$FINAL\" | head"
