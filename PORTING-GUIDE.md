# Porting a Cakewalk DXi to VST3 (and AUv2)

Written after porting **SpaceDub** (2003, Visual C++ 6 / MFC, Cakewalk
DirectX Plug-In Wizard). It is the method and the trap list, separated from
that plug-in's specifics. The SpaceDub port itself is the worked example:
`../Spaceduo-VSTi`, and its `PORTING-NOTES.md` shows what a filled-in version
of this looks like.

Read §1 and §2 before writing any code. §7 is the section that saves days.

---

## 1. What a DXi actually is, and what survives

A DXi is a COM object built on Microsoft's DirectShow/DMO plumbing with
Cakewalk's MFX extensions on top. Very little of that survives, and that is
the point — most of the volume of a DXi project is host plumbing, not
plug-in.

| DXi | VST3 | Survives? |
|---|---|---|
| `CDXi` / `CSoftSynth` / `IMfxSoftSynth2` | `AudioEffect` (processor) | **No** — deleted |
| `CMediaParams` / `CParamEnvelope` | `Parameter` / `EditController` | **No** — replaced |
| `PersistLoad` / `PersistSave` | `getState` / `setState` on `IBStreamer` | Rewritten |
| `COlePropertyPage` (the UI) | `VSTGUIEditor` | Rewritten from the `.rc` |
| The `.rc` dialog | positions for `CView`s | **Yes** — as data, see §6 |
| `res/*.bmp` | `resource/*.png` | Yes, converted |
| MFX tempo map access | `ProcessContext::tempo` | Replaced, see §7 |
| The DSP class(es) | the same class, portable C++ | **Yes** — this is the port |
| Custom MFC control classes | VSTGUI controls | **No** — see §6 |

The realistic split is that 80–90% of the source tree is deleted and the
remaining DSP is carried across nearly line for line.

### First decision: is it actually a synth?

Check what the DSP reads, not what the plug-in registered as. A DXi that
registered as a synth (`CSoftSynth`) but whose processing loop needs an input
signal is an **audio effect** that was using the synth interface for something
else — usually to reach the host's tempo map, which VST3 gives you directly on
`ProcessContext`.

SpaceDub was exactly this: `CSoftSynth::OnEvents` queued note data the DSP
never read. It became a plain `Fx|Delay` effect. Getting this right first
saves porting an entire MIDI layer that nothing uses.

Look for: does `Process()` use its input buffer? Does anything read the note
queue? If the answers are yes and no, it is an effect.

### Second decision: which copy of the loop is the real one

DXi projects of this vintage often contain two or three near-identical copies
of the processing loop, left over from successive versions (SpaceDub had
`SpaceDub.cpp` and `SpaceDubMidi.cpp`). Diff them and port the **latest** —
identify it by the features only it has, not by the filename.

---

## 2. Order of work

This order matters; each step's output is the next step's input, and doing
them out of order means redoing them.

1. **Read the `.rc` file and `resource.h` first.** They are the most complete
   surviving statement of what the plug-in has: every control, every label,
   every display. Extract the dialog geometry now (§6) — it is pure data and
   you will want it before you design anything.
2. **Build the parameter table** (§4). It is the contract between everything
   else; the DSP, the editor and the state all key off it.
3. **Port the DSP class** with the parameter table's *internal* ranges as its
   input. Keep it free of any SDK type, so it compiles and can be tested
   standalone (§8).
4. **Get a silent plug-in loading and validating** — processor, controller,
   entry point, no editor. Fix everything the validator says here, while there
   is almost no code to search.
5. **Wire the DSP in** and check it makes the right noise.
6. **Build the editor last.** It is the biggest, fiddliest part and it depends
   on everything above being settled.

---

## 3. Getting the sources out of a 2003 project

The `.dsp`/`.dsw` project files tell you which sources are real and which are
abandoned. Beyond that:

* `resource.h` is the index of the interface. Control IDs that appear there
  but not in the shipped dialog are features that were cut — worth knowing
  about, and sometimes worth reinstating (SpaceDub's value displays were
  exactly this).
* Bitmaps are `.bmp`, often with a magic-colour transparency key. Convert to
  PNG with real alpha; VSTGUI draws transparency natively and you can then
  delete every custom "draw a transparent control" class.
* Anything named `MemDC`, `BtnST`, `VMBitmap`, `Label`, `*ToolTip*` or similar
  is Win32 drawing scaffolding. None of it ports — VSTGUI does all of it.

---

## 4. Parameters: the two-range problem

Every DXi parameter has **two** ranges and you must keep both:

* the **external** range the user saw (from `Parameters.h`, e.g. 0–200 %),
* the **internal** range the DSP was handed, via `ParamInfo::MapToInternal`
  (e.g. 0–2).

VST3 has *normalised* (0–1) and *plain* (what the host displays). So carry
three, in one table:

```c++
struct ParamDef {
    ParamID id; const char* title; const char* units; ParamType type;
    double plainMin, plainMax, plainDefault;   // the DXi external range
    double internalMin, internalMax;           // what the DSP expects
    int stepCount; bool smoothed;
    double toPlain    (double norm) const;
    double toInternal (double norm) const;     // reproduces MapToInternal
};
```

Then `plain` matches the numbers the original showed, and `toInternal()`
hands the DSP numerically identical values to what it always got. Anything
less and you are re-voicing the plug-in by accident.

Points worth planning for:

* **Rename misleading parameters, don't perpetuate them.** DXi parameter
  labels drift from what they do. SpaceDub had `LeftLoopsGain` (feeding
  `SetScale`, a regeneration amount) next to `LeftLoopsOutGain` (an output
  level). Rename in VST3, and record the mapping in your notes.
* **Add `kIsBypass`.** VST3 hosts expect one. Keep any "enabled" switch the
  DXi had as a *separate* parameter, with its original default.
* **Keep enum step counts even when values repeat.** If a DXi `switch` mapped
  cases 6 and 7 to the same thing, keep eight steps so automation values line
  up.
* **Old presets cannot be read.** The stream layout and the ranges both
  differ. Decide early that this is acceptable; do not try.
* **Never index the table with an arbitrary `ParamID`.** `kBypass` is
  conventionally 1000, far past the end of the array. Range-check before
  every lookup.

---

## 5. The DSP: keep it identical, fix only what is broken

The DSP is the plug-in. Port it line for line and resist improving it —
filter coefficients, dither tables, odd gain staging and apparent bugs are all
"the sound". SpaceDub's filter replaces tap 5 and scales it by the *main* out
gain rather than the taps gain; that looks like a bug and was kept.

The exceptions — the things that are genuinely broken rather than
characterful — recur across plug-ins of this era:

* **`memset(buf, 0, sizeof(buf) * n)` where `buf` is a pointer.** Correct by
  accident on 32-bit, overruns 2× on 64-bit. Look for `sizeof` applied to a
  pointer anywhere.
* **Allocation on the audio thread.** `SetSamplesPerSec` doing
  `delete[] / new[]`, called from inside `Process()`. Move it to
  `setupProcessing`.
* **No bounds on feedback.** Feedback over 100 %, resonance past a filter's
  stability limit, gains in the hundreds of percent: 2003 code often just let
  the numbers explode. Add a ceiling well above any musical level (±16.0, about
  +24 dBFS, worked well) on the recursive state and on anything written back
  into a buffer. Verify the clamp changes nothing at normal settings — a
  default-settings render should be bit-identical with and without it.
* **No denormal or non-finite handling.** Flush both.
* **Index wrapping that is only safe by arithmetic accident.** A single
  `+= length - 1` that works only because some other quantity is bounded.
  Clamp explicitly so a later change cannot walk off the buffer.
* **32-bit float only.** `IsValidInputFormat` typically rejected everything
  else. VST3 hosts offer 64-bit buffers; handle both even if the line stays
  float.
* **Mono.** Usually unsupported. Accepting a mono arrangement is cheap.

### Parameter smoothing

The DXi got first and second derivatives from its automation envelopes
(`GetParamDeltas`) and stepped them per sample. VST3 gives you sample-accurate
automation points instead. Take the value at the end of each block as the
target and ramp linearly across the buffer from the previous block's value.
Smooth the same parameters the DXi interpolated — no more, no fewer.

### The release tail

Don't port `MoreTailsAvailable`/`Get`. Declare `getTailSamples()` and let the
host feed you silence; the line flushes through the normal path.

---

## 6. The interface: recovering the layout exactly

The `.rc` dialog is in **dialog units**, not pixels, and the conversion is
what lets you reproduce the layout precisely instead of eyeballing it.

Find a control in the dialog whose bitmap you also have, and solve for the
scale. For an MS Sans Serif 8 pt dialog the factors came out as:

```
x_px = (x_dlu - x_origin) * 1.5        y_px = y_dlu * 1.625
```

confirmed by a static whose 106 × 25 DLU matched a 160 × 40 pixel bitmap
exactly. Derive the same two numbers for your dialog from its own font, check
them against a second control, then run *every* position through the formula.
Do not place anything by hand.

Watch for a dialog that was **smaller than its background bitmap** — the
Windows version may have been clipping artwork that your VST3 window will now
show. That is usually an improvement, but it will surprise you.

### Control mapping

| Windows | VSTGUI |
|---|---|
| `msctls_trackbar32`, `TBS_VERT` | a `CControl` slider — **check the direction** |
| `msctls_trackbar32`, horizontal | the same, horizontal |
| `BS_AUTOCHECKBOX \| BS_PUSHLIKE` | two-frame bitmap toggle |
| owner-drawn spin / text buttons | a stepped text control |

**Vertical sliders are usually inverted.** Property pages commonly read them
as `value = (max - GetPos()) / scale`, i.e. dragging *up* increases the value.
Check the `OnVScroll` handler and preserve whatever it did.

**Link switches and anything else the property page did itself.** Features
implemented in the dialog rather than in the DSP — "link left and right" being
the classic — have no parameter behind them and are easy to miss. Port them in
the editor, and issue a `performEdit` for the partner parameter so the host
records both sides.

### Value readouts

`resource.h` often lists display controls that are not in the shipped dialog.
Putting them back is usually worth it. Two things learned the hard way:

* **Tooltips may simply not appear on macOS.** Every link in the chain can
  check out and they still never show. Always-visible readouts need none of
  that machinery and are more useful.
* **Small text over artwork needs a background, not a shadow.** An opaque
  plate sized to the string — not to the view — with a hairline border. Size
  it to the string so short readings don't blot out the panel, and drop a font
  size rather than clip a long one.

---

## 7. The traps

These are the ones that cost real time. Each is quiet: no error, no warning,
no validator complaint.

### `ProcessContext` is opt-in since VST3 3.7

A host may leave every field unset — including `kTempoValid` — unless the
plug-in declares what it needs through `IProcessContextRequirements`.
`Vst::AudioEffect` implements the interface for you, but its
`processContextRequirements` member defaults to **no flags at all**, meaning
"I need nothing". Tempo sync then silently falls back to a default in every
host. In the processor's constructor:

```c++
processContextRequirements.needTempo ();
```

Ask for the minimum. The validator does not flag this — it prints
`ProcessContextRequirements: - None` among its messages. If a host-supplied
value seems to be missing, read that line first.

### `sendMessage` from the audio thread is discarded

`IMessage` compiles, returns success, and does nothing. Hosts route
component-to-controller messages through a proxy that drops anything not on
the UI thread:

```c++
// public.sdk/source/vst/hosting/connectionproxy.cpp
if (threadChecker && threadChecker->test ())
    return dstConnection->notify (message);
return kResultFalse;
```

The sanctioned per-block processor → controller path is
`data.outputParameterChanges`: register a parameter `kIsReadOnly`, add a queue
and a point, and the host delivers it to `setParamNormalized` on the right
thread. Messages remain fine from `setActive`, `setState` and `initialize`,
which VST3 documents as `[UI-thread]` — check the interface's thread
annotation in the SDK headers before choosing.

### A null title or units segfaults `RangeParameter`

Its constructor hands both to `UString::assign`, which reads `src[0]` with no
null check. A null title crashes inside `EditController::initialize` — which
presents as the *validator* dying with `Segmentation fault: 11` during the
post-build step, long before anything audio-related runs. Always pass
`USTRING(...)`.

When the validator crashes, attach a debugger to it directly rather than
reading the Xcode log:

```sh
lldb -- build/bin/Debug/validator build/VST3/Debug/YourPlugin.vst3
(lldb) run
(lldb) bt
```

### "Bundle does not export the required 'GetPluginFactory' function"

The plug-in compiled, linked and signed, and then the SDK's post-build check
rejected it. Nine times out of ten this does not mean the factory is wrong —
it means **the file containing it was never compiled**, and the reason is the
Xcode generator's interaction with `file(GLOB ... CONFIGURE_DEPENDS)`.

`CONFIGURE_DEPENDS` re-globs at *build* time, via the `ZERO_CHECK` target. So
on the first build after you add a source file, the sequence is:

1. `ZERO_CHECK` re-globs, finds the new file, and rewrites `project.pbxproj`;
2. Xcode carries on building **from the file list it loaded when the build
   started** — the old one;
3. the link succeeds, because a VST3 bundle with no factory is still a valid
   dylib;
4. the post-build check is the first thing that notices.

**The fix is to build again.** The project file on disk is already correct by
then.
**The template catches this before it happens** — but only because of a
detail that is easy to undo. `CMakeLists.txt` records the globbed file list
in `build/source-manifest.txt` at configure time, and a PRE_BUILD step
re-globs and compares, so adding a file stops the next build with the names
of what changed and the command that fixes it.

That works **only because the globs are plain `file(GLOB ...)`**. The first
version of this guard used `CONFIGURE_DEPENDS`, which re-globs at build time
through `ZERO_CHECK` — rewriting the manifest *and* the project together, so
the guard's two witnesses always agreed and it could never fire. It shipped,
and the next added file went straight past it. Reintroduce
`CONFIGURE_DEPENDS` and you switch the guard off.

The lesson generalises: a check is only as good as the independence of the
thing it checks against.

When diagnosing it by hand, confirm rather than guess: compare the mtime of
`build/<Name>.xcodeproj/project.pbxproj` against your new source file, and
look at which objects actually exist:

```sh
find build -name "*.o" -path "*<Name>.build*" | sed 's/.*\///' | sort -u
```

If only some of your sources are listed there, this is what happened. If they
are all there and the factory is still missing, then it really is the factory
— check that `BEGIN_FACTORY_DEF`/`END_FACTORY` are present and that the entry
file is not excluded by an `#if`.

### `dynamic_cast` is useless inside `~EditorView()`

`EditorView` notifies the controller in two places: `removedFromParent()` →
`editorRemoved`, and the **destructor** → `editorDestroyed`. The destructor
call is the safety net for hosts that release the view without calling
`removed()` — and it is where the obvious implementation stops working:

```c++
if (auto* e = dynamic_cast<MyEditor*> (editor))   // always null here
    mEditors.erase (...);
```

By then the derived sub-object is destroyed and the dynamic type *is*
`EditorView`, so the cast yields null and your list keeps a dangling pointer.
Compare upcast pointers instead — `static_cast<EditorView*> (e) == editor` —
which is well defined at every point in the destruction sequence.

### `CMAKE_MODULE_PATH` does not propagate up from `add_subdirectory`

The VST3 SDK appends its own `cmake/modules` inside its own scope, so
`include(SMTG_AddVST3AuV2)` in *your* CMakeLists fails with "include could not
find requested file". One line fixes it:

```cmake
list(APPEND CMAKE_MODULE_PATH "${VST3_SDK_ROOT}/cmake/modules")
```

The asymmetry that makes this easy to miss: CMake *functions* are global once
defined, so `smtg_add_vst3plugin` works in the parent scope with no help. Only
the module search path needs it. (The template already does this.)

### Editor and DSP drifting apart

Anything the editor displays that the DSP computes — a delay time in
milliseconds, a cutoff in hertz — must come from **one** function that both
call, declared `static` and stateless on the DSP class. A DXi could read the
DSP's state directly through a back-channel because it was one in-process
object; VST3 splits the processor and controller into components that may not
share an address space, so the editor has to recompute. Copy the arithmetic
into the editor and it *will* diverge — usually at a sample rate or a setting
you never test.

---

## 8. Testing without a host

Keep the DSP class free of SDK types and it compiles against nothing but the
standard library. That buys you a test binary that needs no SDK, no host and
no window server:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -I../source ../source/DelayLine.cpp Tests.cpp -o tests && ./tests
```

Worth asserting: impulse response tap positions, any quantisation grid,
control-mapping functions at several sample rates, and — whenever you
deliberately change DSP behaviour — that the change is a no-op across the
range that normal use covers. `../Spaceduo-VSTi/tests/DelayLineTests.cpp` is a
worked example of that last one.

The **SDK validator** is the closest thing to an integration test. Leave it on
during development, and run `auval -v <type> <subtype> <manu>` for the AU — it
is stricter than most hosts about channel layouts and parameter round-tripping.

---

## 9. Build, Audio Unit and signing

All handled by the template's `CMakeLists.txt`; the comments in it explain
each decision. The three things to know:

* **The AudioUnitSDK version is load-bearing.** Steinberg's wrapper compiles
  at C++17; AudioUnitSDK 1.3.0+ uses `std::span` and concepts in its headers
  and fails with *"no member named 'span' in namespace 'std'"*. The template
  pins **1.1.0** and raises the standard automatically if you point it at a
  newer one.
* **The AUv2 helper only exists under the Xcode generator.** Makefile and
  Ninja builds get the VST3 only. That is Steinberg's constraint.
* **Code signing had to be taken off the SDK.** It defaults
  `SMTG_CODE_SIGN_IDENTITY_MAC` to `"Apple Development"` and signs the AU
  unconditionally, so a machine with no certificate fails the build *after*
  everything has compiled. The template ad-hoc signs instead unless you give it
  a real identity.

Also worth carrying over: the installed `.component` contains a **symlink** into
your build tree. Replace it with the real `.vst3` bundle before handing the
plug-in to anyone.

---

## 10. Things to write down as you go

Keep a `PORTING-NOTES.md` in the new project recording, at minimum:

* the parameter mapping table, old id → new name → both ranges → default;
* every place you deliberately diverged from the original, and why;
* the class UIDs and the AU four-character codes, marked **never change**;
* every trap you hit, with the symptom you actually saw.

That last one is what this file is made of, and it is the part that pays for
itself on the next port.
