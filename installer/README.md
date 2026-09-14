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

## Before handing it to a tester

**"It installed on another Mac" is not the same as "it is ready".** Two
different things have to be true, and a second machine of your own usually
tests only the first.

### 1. Does the payload actually work there?

The installer finishing means files were copied. It says nothing about whether
they load. Run this on the far machine:

```sh
pkgutil --pkgs | grep -i project6
ls -ld /Library/Audio/Plug-Ins/VST3/Project6.vst3
ls -ld /Library/Audio/Plug-Ins/Components/Project6.component/Contents/Resources/plugin.vst3
auval -v aumu Prj6 AECo
```

The third line is the important one. **It must be a directory, not a symlink.**
If it shows an `l` and an arrow, the AU is carrying a link into a build tree
that does not exist there, and it will fail to load however clean the install
looked. That is the whole reason `build-installer.sh` exists.

Then open a DAW and load both formats. An AU that installs and does not
instantiate is the failure this is guarding against.

### 2. Was Gatekeeper ever actually asked?

An unsigned package is only refused if it arrives **quarantined**, and files
that travel by iCloud Drive, a local copy, or a shared volume usually are not.
Signing into the same Apple ID makes no difference either way — Gatekeeper
looks at the file's attributes, not at who is logged in.

```sh
xattr -p com.apple.quarantine /path/to/Project6-<version>.pkg
```

* **`No such xattr`** — the file was never quarantined, Gatekeeper never
  engaged, and this install proved nothing about how it behaves for somebody
  who downloads it.
* **A value is printed** — it *was* quarantined and it installed anyway, which
  is the real test.

To test it honestly, send the package the way a tester will actually receive
it: a download link, or email. Then try it on a Mac that has never had the
build tree.

### So is it ready for another user?

* **A colleague or friend who will take a phone call** — yes, unsigned, as long
  as you tell them up front that macOS will object and how to allow it. Verify
  §1 above on a machine that has never built it first.
* **Anyone else** — no. Sign and notarise it. Asking a stranger to override
  Gatekeeper for an unsigned installer is asking them to do the exact thing
  they should refuse, and it teaches a habit worth not teaching.

## If it fails on someone else's Mac

`com.apple.installer.pagecontroller error -1` means Installer could not set up
its panes — usually because it rejected the distribution, not because anything
is wrong with the payload. **The error names none of that, but the log does.**

On the machine that fails, open the package, and when the error appears:

* **Installer → Window → Installer Log**, or `⌘L`, set to **Show All Logs**;
* or afterwards, `/var/log/install.log` —
  `log show --predicate 'process == "Installer"' --last 30m`

That names the actual reason. In the one well-documented case of this error
the log said *"Invalid Distribution File/Package"* with an XML parse failure,
which is nothing you could have guessed from the dialog.

Worth establishing first, because it splits the problem in half: **does the
same `.pkg` install on the machine that built it?**

* Fails on both → the package. The log will say why.
* Works locally, fails elsewhere → the environment: quarantine, an older
  macOS, or Gatekeeper refusing an unsigned package (see above).

`build-installer.sh` now validates the distribution twice — once as written,
and once as it ends up *inside* the product archive, which is the copy the far
machine actually reads — and fails the build if it is malformed or names a
component package that is not embedded.

## Checking the result

```sh
pkgutil --payload-files installer/Project6-1.0.0.1.pkg   # what is really inside
pkgutil --check-signature installer/Project6-1.0.0.1.pkg # signed? notarised?
```

The built `.pkg` and the `build/` scratch directory are gitignored: they are
outputs, rebuildable from the tree in one command.
