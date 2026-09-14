# Building the Project6 installer

`build-installer.sh` makes `installer/Project6-<version>.pkg`, which installs

    /Library/Audio/Plug-Ins/VST3/Project6.vst3
    /Library/Audio/Plug-Ins/Components/Project6.component

as two separately choosable components, so somebody who only wants one format
gets only that one.

## Running it

**macOS only.** `pkgbuild`, `productbuild` and `codesign` are Apple's and exist
nowhere else, so this cannot be run from the Linux side of a remote session.
Build the plug-in first:

```sh
./setup-xcode.sh --no-open
cmake --build build --config Release
installer/build-installer.sh
```

The version comes out of `PLUGIN_VERSION` in `CMakeLists.txt`. There is no
second copy of it to forget.

## The bug this script exists to avoid

Pointing `pkgbuild` straight at `build/VST3/Release` produces a `.pkg` that
works perfectly on the machine that built it and **installs a dead Audio Unit
everywhere else.**

Steinberg's AU wrapper has no plug-in code of its own — it loads the VST3 out
of its own bundle, from `Contents/Resources/plugin.vst3`. CMake puts a
**symlink** there, pointing at an absolute path in the build tree:

    Project6.component/Contents/Resources/plugin.vst3
        -> /Users/<you>/DXi-DEv/Project6-VSTi/build/VST3/Release/Project6.vst3

That is right for development: rebuild the VST3 and the AU follows. Copy it to
another machine and the link dangles, the wrapper finds nothing to load, and
the AU fails to instantiate with no useful error.

So the script replaces that link with a real copy of the VST3 bundle, re-signs
the `.component` (innermost bundle first, or the outer signature is invalidated
by the inner one), and then **fails the build if any symlink in the payload
still points at an absolute path** — which catches the next one somebody adds.

## Signing, and what "suitable for other machines" really needs

With no arguments the payload is ad-hoc signed and the `.pkg` is not signed at
all. That installs fine on the machine that built it, and is fine handed over
AirDrop or on a USB stick.

**A `.pkg` downloaded from the internet is quarantined**, and an unsigned,
un-notarised one is refused by Gatekeeper: the person has to go to System
Settings → Privacy & Security and allow it by hand, which is indistinguishable
from what a malicious installer asks them to do. Do not ask strangers to do
that.

Real distribution needs both halves of a Developer ID — they are two different
certificates — and a notarisation:

```sh
installer/build-installer.sh \
    --sign-app       "Developer ID Application: Your Name (TEAMID)" \
    --sign-installer "Developer ID Installer: Your Name (TEAMID)" \
    --notarize       my-notary-profile
```

`my-notary-profile` is stored once, in the keychain:

```sh
xcrun notarytool store-credentials my-notary-profile \
    --apple-id you@example.com --team-id TEAMID --password <app-specific-password>
```

Both certificates come from a paid Apple Developer account. Without one, an
installer can be built and used locally but cannot be distributed cleanly.

## Checking the result

```sh
pkgutil --payload-files installer/Project6-1.0.0.1.pkg   # what is really inside
pkgutil --check-signature installer/Project6-1.0.0.1.pkg # signed? notarised?
```

The built `.pkg` and the `build/` scratch directory are gitignored: they are
outputs, rebuildable from the tree in one command.
