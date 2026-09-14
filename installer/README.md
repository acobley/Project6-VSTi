# Building the Project6 installer

`build-installer.sh` makes `installer/Project6-<version>.pkg`, which installs

    /Library/Audio/Plug-Ins/VST3/Project6.vst3
    /Library/Audio/Plug-Ins/Components/Project6.component

as two separately choosable components, so somebody who only wants one format
gets only that one.

## Running it

**macOS only.** `pkgbuild`, `productbuild` and `codesign` are Apple's and exist
nowhere else, so this cannot be run from the Linux side of a remote session.

The whole sequence for a release:

```sh
tools/run-tests.sh                              # 1. before anything is built
./setup-xcode.sh --no-open --clean              # 2. clean, if compile flags changed
cmake --build build --config Release

strings -a build/VST3/Release/Project6.vst3/Contents/MacOS/Project6 \
    | grep -c "$HOME"                           # 3. want 0

installer/build-installer.sh \                  # 4. ONCE, signed
    --sign-app       "Developer ID Application: Your Name (TEAMID)" \
    --sign-installer "Developer ID Installer: Your Name (TEAMID)" \
    --notarize       your-notary-profile
```

**Run the installer build once, not twice.** An unsigned run writes the same
`installer/Project6-<version>.pkg` that a signed run does, so building
unsigned "to check it works" and then signing means that if the signed run
fails part way — a notarisation timeout, a wrong identity — the **unsigned**
package is still sitting at the final path, looking finished. Nothing in the
name distinguishes them. `pkgutil --check-signature` is the only way to tell,
and it is not a habit worth needing.

For a local build with no certificates, run it with no arguments instead.

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

## Signing and notarising, start to finish

Do this once. Afterwards it is one command per release.

### 1. Two certificates, and they are different things

You need **both**, and they are not interchangeable:

| Certificate | Signs | Passed as |
|---|---|---|
| **Developer ID Application** | the `.vst3` and `.component` | `--sign-app` |
| **Developer ID Installer** | the `.pkg` itself | `--sign-installer` |

Easiest route: **Xcode → Settings → Accounts → (your Apple ID) → Manage
Certificates → `+`** and create each in turn. They land in your login keychain
with their private keys, which is what matters — a certificate downloaded from
the developer portal without its key is useless.

Only the **Account Holder** of the team can create Developer ID certificates,
and there is a small limit on how many exist at once. If you ever need them on
a second machine, export them from Keychain Access as a `.p12` rather than
creating new ones.

#### If you made them on developer.apple.com instead

Then you have probably hit the one trap in that route, and the symptom is
**Xcode listing the certificates but saying they are not in the keychain**.

A certificate is **two halves**: the certificate Apple issues, and the
**private key**, which only ever exists on the Mac that generated the signing
request. The portal has only the first half and can never give you the second.
So a certificate created there is useless until both halves are on this Mac.

The check that settles it — an *identity* is by definition a certificate plus
its key, so if these list them, you are fine:

```sh
security find-identity -v -p codesigning        # Developer ID Application
security find-identity -v | grep "Developer ID" # both, including Installer
```

In the GUI: Keychain Access → **login** → **My Certificates**. Not
"Certificates" — **My Certificates** is the list of ones you hold the key for,
and each should open to reveal a private key underneath.

If they are missing, which half you are missing decides the fix:

* **You generated the CSR from Keychain Access on this Mac.** The key is here
  and you simply never installed the issued certificate. developer.apple.com →
  Certificates → click each → **Download**, then double-click the `.cer`. It
  goes into the login keychain and pairs with the key by itself.
* **You did not generate a CSR, or did it on another machine.** The key is not
  here and nothing can recover it from the portal. Either import a `.p12`
  exported from the machine that has it, or **revoke both certificates and let
  Xcode create them** as above — Xcode generates the key locally and installs
  both halves, which is the whole reason to prefer that route. Revoking unused
  Developer ID certificates is harmless when nothing signed with them has
  shipped, and it frees up the limited slots.

### 2. Find their exact names

```sh
installer/build-installer.sh --list-identities
```

The Application one appears under code-signing identities; the Installer one
does **not** — it is not a code-signing certificate — so the script lists both
sets. You want the full string including the team ID in brackets:

    Developer ID Application: A. E. Cobley (ABCDE12345)
    Developer ID Installer: A. E. Cobley (ABCDE12345)

### 3. An app-specific password, and the notary profile

Notarisation is a separate Apple service with its own login. It will not take
your Apple ID password.

1. **appleid.apple.com** → Sign-In and Security → **App-Specific Passwords** →
   generate one, and copy it.
2. Your **Team ID** is the bracketed code in the identities above, and is also
   on developer.apple.com → Membership. **Take it from the certificate** where
   you can: the team you give notarytool must be the team that owns the
   Developer ID you sign with, or the submission is rejected.

   ```sh
   security find-identity -v | grep "Developer ID"
   #   Developer ID Application: A. E. Cobley (ABCDE12345)
   #                                           ^^^^^^^^^^ this
   ```

   From a downloaded `.cer` instead, it is the `OU=` field:

   ```sh
   openssl x509 -inform der -in developerID_application.cer -noout -subject
   ```

   **The Issuer ID of an App Store Connect API key is NOT the Team ID.** One is
   a UUID, the other is ten alphanumeric characters; they come from different
   systems and are not interchangeable. (And the API-key route takes no team at
   all — see below.) If you belong to more than one team, make sure you are
   reading the paid one: a free "Personal Team" cannot create Developer ID
   certificates in the first place.
3. Store it all once, in the keychain:

```sh
xcrun notarytool store-credentials project6-notary \
    --apple-id you@example.com \
    --team-id ABCDE12345 \
    --password abcd-efgh-ijkl-mnop
```

`project6-notary` is just a label you choose. The password is stored in the
keychain, so it never appears in a command again.

#### If that fails with HTTP 500

```
Validating your credentials...
Error: HTTP status code: 500. Internal Server Error
```

**CHECK THE TEAM ID FIRST. A wrong one produces exactly this error.** That is
what it was here: not an outage, not the password, not the certificates — the
`--team-id` did not belong to the Apple ID. Apple returns a *500 Internal
Server Error* rather than an honest "no such team", which sends you looking in
completely the wrong place, so check it before you believe anything else:

```sh
security find-identity -v | grep "Developer ID"
#   Developer ID Application: A. E. Cobley (ABCDE12345)
#                                           ^^^^^^^^^^ use this
```

It happens before anything is uploaded, at the point notarytool checks the
Apple ID, team and password — so whatever the cause, it says nothing about
your certificates.

If the Team ID is definitely right, then it may genuinely be Apple's end.
Their DTS engineer, on this error: *"The notary service is quite reliable IME,
and when I do see errors like this they often get fixed based on internal
monitoring."* Retry; these often clear by themselves. The status page at
developer.apple.com/system-status is coarse and routinely misses partial
failures, so "available" there is not a contradiction.

If it persists, Apple's recommended workaround is to **authenticate with an App
Store Connect API key instead of an app-specific password** — a different
authentication path entirely. It is the better route anyway: it is what works
unattended in CI, and it does not break when a password is rotated.

App Store Connect → **Users and Access** → **Integrations** → App Store Connect
API → generate a team key. You need three things from it: the **Key ID**, the
**Issuer ID** (above the key list), and the `AuthKey_XXXXXXXX.p8` file — **which
downloads exactly once**, so keep it somewhere safe and backed up.

```sh
xcrun notarytool store-credentials project6-notary \
    --key ~/private_keys/AuthKey_XXXXXXXX.p8 \
    --key-id XXXXXXXX \
    --issuer aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
```

**`build-installer.sh` needs no change either way.** `store-credentials` writes
the same kind of keychain profile whichever authentication it is given, and
`notarytool submit --keychain-profile` reads both identically — so
`--notarize project6-notary` stays exactly as it is.

To test the credentials without building anything:

```sh
xcrun notarytool history --keychain-profile project6-notary
```

### 4. Build it

```sh
installer/build-installer.sh \
    --sign-app       "Developer ID Application: A. E. Cobley (ABCDE12345)" \
    --sign-installer "Developer ID Installer: A. E. Cobley (ABCDE12345)" \
    --notarize       project6-notary
```

Notarisation waits on Apple and usually takes a few minutes. The script then
staples the ticket to the package, so it validates even on a machine that is
offline.

### What the script changes when you sign for real

Signing for distribution is not the same command with a different name in it.
With a Developer ID the script switches to:

* **`--timestamp`** — a secure timestamp, countersigned by Apple's timestamp
  server, so the signature stays valid after the certificate expires.
* **`--options runtime`** — the hardened runtime.

**Notarisation requires both**, and it checks what is *inside* the package as
well as the package itself: a payload signed the ad-hoc way and then wrapped in
a properly signed `.pkg` is rejected, with a message about the payload rather
than about these flags. Ad-hoc signing cannot carry a timestamp at all — there
is no certificate for a timestamp authority to countersign — which is why
`--timestamp=none` is right for local builds and only for those.

### The check that actually matters

After stapling, the script asks **Gatekeeper on your own machine the same
question the downloading machine will ask**:

```sh
spctl --assess --type install -vv installer/Project6-<version>.pkg
```

It must say `source=Notarized Developer ID`. **Anything else and the build
fails**, because everything before that point only proves the paperwork is in
order — this proves the answer is yes. A package that is signed and stapled and
still refused by Gatekeeper is exactly the failure worth catching at home.

Then verify it end to end the honest way: send it to yourself as a **download**,
and check `xattr -p com.apple.quarantine` shows it arrived quarantined before
you install it. Quarantined, signed and notarised, installing without a murmur
is the whole goal.

## Before handing it to a tester

**"It installed on another Mac" is not the same as "it is ready".** Two
different things have to be true, and a second machine of your own usually
tests only the first.

### 1. Does the payload actually work there?

The installer finishing means files were **copied**. It says nothing about
whether they **load**.

Copy `verify-install.sh` to the machine you are testing on, install the
`.pkg`, and run it:

```sh
./verify-install.sh
```

It needs nothing else — not this repo, not the build tree, not the installer.
That is deliberate: the machine that must not have the build tree is exactly
the machine it has to run on. It checks that both bundles are present, that
the architectures suit the Mac, that the signatures verify and what signed
them, that the installer receipts are there, and that **the AU actually
instantiates** under `auval`.

The check it exists for is this one:

```sh
ls -ld /Library/Audio/Plug-Ins/Components/Project6.component/Contents/Resources/plugin.vst3
```

**It must be a directory, not a symlink.** If it shows an `l` and an arrow,
the AU is carrying a link into a build tree that does not exist on that
machine, and it will not load however clean the install looked — that is the
whole reason `build-installer.sh` has a staging step.

Then the one thing no script can do for you: **open a DAW and load both
formats**. An AU that installs and will not instantiate is the failure all of
this guards against.

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

**On a package you built yourself, `No such xattr` is expected and means
nothing.** Quarantine is stamped on by whatever *downloads* a file, and you
never download your own build. It is only informative on a copy that has
actually travelled — and only some routes set it:

| Sets quarantine | Does not |
|---|---|
| Safari, Chrome, Firefox downloads | `curl` / `wget` / `scp` |
| Mail and Messages attachments | iCloud Drive sync |
| AirDrop | SMB / network share copy |
| | USB stick |

So send it the way a tester will actually receive it — a download link, or
email — and try it on a Mac that has never had the build tree.

To force the check without a real download, the attribute can be stamped on by
hand:

```sh
xattr -w com.apple.quarantine "0081;00000000;Safari;" Project6-<version>.pkg
```

That is a fair approximation, but a genuine browser download remains the honest
test: it exercises the whole path rather than a synthesised attribute.

**And note this is the weaker check of the two.** `spctl --assess --type
install -vv`, which the build script runs automatically after stapling, asks
Gatekeeper for its verdict directly and does not depend on how the file
travelled at all. If the signed build completed, that already passed.

### So is it ready for another user?

* **A colleague or friend who will take a phone call** — yes, unsigned, as long
  as you tell them up front that macOS will object and how to allow it. Verify
  §1 above on a machine that has never built it first.
* **Anyone else** — no. Sign and notarise it. Asking a stranger to override
  Gatekeeper for an unsigned installer is asking them to do the exact thing
  they should refuse, and it teaches a habit worth not teaching.

## Publishing it on GitHub

Signed and notarised, a GitHub download is a perfectly reasonable channel — a
browser sets the quarantine attribute, the stapled ticket satisfies Gatekeeper
without contacting Apple, and it installs without a murmur.

**As a Release asset, not as a commit.** `.gitignore` excludes
`installer/*.pkg` on purpose. Git stores a binary as a fresh blob every
version, forever, in every clone, and it cannot be removed afterwards without
rewriting history. A release asset is versioned, replaceable, counted, and
outside the history. **Tag the commit the binary was built from**, so a release
maps to source — the machinery here is in the repo, so that is genuinely
reproducible.

### The point of no return

The moment somebody else installs it, these stop being editable:

    audio.project6.vst3        audio.project6.audiounit
    aumu / Prj6 / AECo         and the VST3 class UIDs

Change any of them afterwards and every project that used the plug-in loses
it, silently. This is the last cheap moment to be sure.

### Before the first release

* Verify the payload **on a Mac that has never had the build tree** — see
  *Before handing it to a tester* above. An AU that installs and will not
  instantiate is the failure the whole script exists to prevent.
* Publish a checksum beside the asset: `shasum -a 256 Project6-<version>.pkg`.
* Put the two known host quirks in the release notes — REAPER not taking MIDI
  output from an Audio Unit, and AUMIDISynth going silent at a loop point.
  Both are other people's, both will otherwise be reported as yours.
* Say how to uninstall, because a `.pkg` never will:

  ```sh
  sudo rm -rf /Library/Audio/Plug-Ins/VST3/Project6.vst3
  sudo rm -rf /Library/Audio/Plug-Ins/Components/Project6.component
  ```

### What you are taking on

* **The signing key becomes the asset to protect.** Leaked, it lets somebody
  sign malware as you, and Apple's remedy is revoking the Developer ID — which
  invalidates everything you have ever signed with it. Export it as an
  encrypted `.p12`, keep it offline, and keep it out of CI unless the secrets
  are handled properly.
* **Notarisation is not endorsement.** It means an automated scan found no
  malware. It says nothing about whether the plug-in is any good, and it will
  not help if a bug costs somebody a session.
* **The binary used to carry your build paths.** VSTGUI's assert macros bake
  `__FILE__` into the executable — around fifty absolute paths, all naming the
  home directory it was built in. Harmless, and standard across shipped
  plug-ins, but public once published. `CMakeLists.txt` now passes
  `-ffile-prefix-map` so they read `Project6/external/...` instead. Check it
  after a rebuild:

  ```sh
  strings -a build/VST3/Release/Project6.vst3/Contents/MacOS/Project6       | grep -c "$HOME"        # want 0
  ```

  (`-fdebug-prefix-map` will *not* do this — it rewrites debug info and leaves
  `__FILE__` alone.)
* **The licence starts binding other people.** CC BY-SA 4.0 is what this
  inherited from VocalFilter; Creative Commons themselves advise against CC
  licences for software, since the terms are written for creative works and
  carry no patent grant. Worth a decision before the first download rather than
  after.

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
