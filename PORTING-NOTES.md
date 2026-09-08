# Project6 — porting notes

Nothing has been ported yet. This file exists anyway, because it records the
decisions that are **permanent from the first shipped build** and the traps
that were deliberately handled before there was any code to hide them in. It
is the file that outlives the session that made the project.

Scaffolded from `~/DXi-DEv/vst3-port-template` on 8 September 2026, following
the `blank-vst3-plugin` skill, and given its 8 × 8 sample slot grid the same
day (§9). `PORTING-GUIDE.md` and `PORT-CHECKLIST.md` came
with the template and are unchanged; read the guide before writing a real
processing loop.

---

## 1. The four decisions

| Decision | Choice | Why, and what it reaches |
|---|---|---|
| Effect or instrument | **Instrument** | `PlugType::kInstrumentSynth` in `Project6Entry.cpp`, AU type `aumu` in `resource/au-info.plist`, and an event input in `Project6Processor::initialize`. |
| Bus layout | **0 in / 2 out** | `setBusArrangements` accepts `numIns == 0` and one stereo output, and `AudioUnit SupportedNumChannels` says `0 / 2`. The template ships `2/2` **and** `1/1`; both were cut. A layout left in the plist that the processor rejects is what `auval` finds. |
| Name and codes | **Project6** | Folder `Project6-VSTi`, namespace `Project6`, target and bundle `Project6`. Four-character codes below. |
| Editor now or later | **Now** | Against the skill's recommendation, and for one reason: the control set was being lifted from VocalFilter anyway (see §3), so the editor cost a layout rather than a design. The silent-and-validating milestone was still reached first — the processor, controller and entry compiled and the DSP suite passed before the editor was written. |

## 2. The permanent identity

**Do not change any of these once a build has shipped.** Hosts store the class
UIDs in the project file and macOS stores the four-character codes; a change to
either makes every existing session silently lose the plug-in.

| | |
|---|---|
| Processor UID | `0x5719455B, 0x0531CEF2, 0xEF57C7B2, 0xA1E72CD8` |
| Controller UID | `0x063D7358, 0x66DE8BEF, 0x5B3C7E20, 0x11A89909` |
| AU type | `aumu` (instrument) |
| AU subtype | `Prj6` |
| AU manufacturer | `AECo` — shared across all of these plug-ins; only the subtype distinguishes them |
| VST3 bundle id | `audio.project6.vst3` |
| AU bundle id | `audio.project6.audiounit` |

Subtypes already taken in `~/DXi-DEv`: `FTrn` (ForTran), `SDub` (SpaceDub),
`SpyB` (SpyBand), `VcFl` (VocalFilter). `Prj6` clashes with none of them.

The UIDs were generated fresh from `os.urandom`, not copied from another
project.

## 3. What was lifted, and from where

The UI came from `~/DXi-DEv/VocalFilter-VSTi`, which had them from
`SpyBand-VSTi`, which had them from a DXi property page that drew everything
with GDI rectangles and text.

| File | Source | Change |
|---|---|---|
| `Project6Controls.{h,cpp}` | `VocalFilterControls.*` | **Namespace only.** Not one line of drawing or mouse handling differs. The class names are deliberately still `SpySlider`, `SpyToggle`, `SpySelector`, `SpyPresetButton`, so `diff` against either older copy shows only what genuinely differs. |
| `Project6Display.{h,cpp}` | `VocalFilterDisplay.*` | Rewritten around the same skeleton: plate, caption band, decibel grid, and `drawPolyline()` with its floor-crossing interpolation kept intact. Everything vocal-tract-specific removed. It draws the output trim, which is the only thing this plug-in computes. |
| `Project6Editor.{h,cpp}` | `VocalFilterEditor.*` | Layout rewritten for one control; the `showValue` discipline, the complete edit gesture in `setParameter`, and the pull-on-a-timer display refresh all preserved. |
| `tools/check-editor.py` | same | Retargeted at `Project6Editor.cpp`. |

`Project6SlotView.{h,cpp}` is **not** lifted — it is the first control that is
unique to this plug-in, which is exactly why it is a file of its own rather
than an addition to `Project6Controls.*`. That file has to stay diffable
against the other two copies. For the same reason the slot draws its own
recessed rectangle instead of calling `Project6Controls.cpp`'s file-local
`draw3dRect`: a well wants the two edge colours the other way round from a
raised bar, and exporting a helper would have meant editing a header that is
meant to be byte-comparable.

**No control uses a bitmap**, which is why `resource/` needs no artwork and the
panel is resolution-independent. Keep it that way.

One colour to know about when diffing against SpyBand: `Colours::kValue` is
near-white `(232,232,232)`, not the DXi's red `(192,50,50)`. That change was
made in VocalFilter so the readouts would not compete with a display's
saturated traces, and it is carried here.

## 4. Why the DSP is SDK-free

`Project6Dsp.{h,cpp}` include no Steinberg header, and must not start.

* It compiles and runs standalone, so its numbers were checked before anything
  was built and long before a host was involved — see §6.
* VST3 splits the processor from the controller into two objects that do not
  share memory. **Anything the editor displays that the DSP also computes must
  come from one shared function both call**, or the panel agrees with the audio
  today and diverges at some sample rate nobody tests. `dbToLinear()` and
  `linearToDb()` are the first such pair: the output stage scales by them and
  `SpyDisplay::draw` converts by them, so "off" means the same thing in both.

## 5. The measured default output level

Measured before anyone played it, by `tests/DspTests.cpp` §5:

```
default patch, rendered  : silence (-inf dBFS)
output stage, full scale : 0.00 dBFS
```

The instrument is silent because nothing is synthesised yet. **The second
number is the one the first real DSP must repeat**: full scale in, full scale
out, at the default trim. A level that clips masks other faults and sends you
chasing the wrong bug, so re-run that measurement the moment there is anything
to hear — and if it has moved, find out why before listening to anything.

The top of the trim's travel is unity, not a boost, for the same reason.

## 6. What was verified, and what could not be

The session reached this Mac through a Linux VM. **`cmake`, Xcode, the SDK
validator and `auval` are not available there** — none of them has been run,
and the first build is still ahead of you.

What was run, and passed:

```sh
cd ~/DXi-DEv/Project6-VSTi

# 1. All three test suites, from a clean build.
c++ -std=c++17 -O2 -Wall -Isource \
    tests/DspTests.cpp source/Project6Dsp.cpp -o /tmp/dsptests && /tmp/dsptests

c++ -std=c++17 -O2 -Wall -Isource \
    tests/SlotTests.cpp source/Project6Slots.cpp -o /tmp/slottests && /tmp/slottests

SDK=~/DXi-DEv/VocalFilter-VSTi/external/vst3sdk        # any existing checkout
c++ -std=c++17 -O2 -Wall -Isource -I$SDK \
    tests/ParamsTests.cpp source/Project6Params.cpp source/Project6Dsp.cpp \
    -o /tmp/paramstests && /tmp/paramstests

# 2. Every source compiled to an OBJECT FILE - not -fsyntax-only, which
#    passes a header that declares a function nobody defined.
for f in source/*.cpp; do
  g++ -c -std=c++17 -DLINUX=1 -DRELEASE=1 -I$SDK -I$SDK/vstgui4 -Isource \
      -o /tmp/$(basename $f .cpp).o $f || echo "FAILED $f"
done

# 3. Every undefined Project6:: symbol must be defined by another of them.
nm -C /tmp/*.o | grep " U " | grep "Project6::"

# 4. The editor invariant no runtime test can reach.
python3 tools/check-editor.py
```

`-DRELEASE=1` is required or `fdebug.h` refuses to compile.

Results, at the slot-grid commit: **all three suites all-pass**, all eleven
translation units produced object files with no errors, all 34 undefined
`Project6::` symbols resolved within the set, and `check-editor` reported ok.
The panel's dimensions are checked by `static_assert` rather than by eye —
735 × 481, with the grid meeting both margins exactly.

Re-run all four before every commit. Adding a source file also means
re-running `./setup-xcode.sh --no-open` before the next Xcode build, or the
project compiles the old file list.

**Still to do on a Mac**, in this order:

```sh
cd ~/DXi-DEv/Project6-VSTi && ./setup-xcode.sh
# then the SDK validator, then:
auval -v aumu Prj6 AECo
```

If a build fails with *"Bundle does not export the required
'GetPluginFactory' function"*, a source file was added after the Xcode project
was generated: re-run `./setup-xcode.sh --no-open` and build again.

## 7. Deliberate omissions, and the trap behind each

Each of these is **absent on purpose**. The trap is what bites when it is
added carelessly.

| Left out | The trap |
|---|---|
| `processContextRequirements` | Since VST3 3.7 the `ProcessContext` is **opt-in and the default is no flags**. Anything that reads the tempo silently gets 120 in every host, and the validator prints `- None` rather than complaining. Where to add it is written into the banner of `Project6Processor.h`. |
| Processor → controller messages beyond the sample rate | **A message sent from `process()` is silently discarded** by the host's connection proxy — it returns success and does nothing. Per-block values go out through `data.outputParameterChanges` as hidden read-only parameters instead. Both halves of that rule are written into `Project6IDs.h` and `Project6Params.h`. |
| Published ("live") parameters | None are needed yet. When the first one is appended, `kNumStoredParams` moves with it, and **the range check on it must be bounded by its own end, not by `kNumParams`** — a predicate reading `id < kNumParams` misclassified the next parameter appended after such a block in VocalFilter, and the symptom was a control that wrote its parameter, was seen by the host, and did nothing. |
| `IMidiMapping` | No CC handling yet. ForTran is the worked example if channel volume, pan or the wheel are wanted. |
| Voices, and a conditional `silenceFlags` | `process()` currently flags **every** block silent, which is correct while it is. **A synth that flags silence while a note is playing is silenced by the host** — make that flag conditional in the same commit that adds the first voice. |

Handled rather than omitted, and worth not undoing:

* `kBypass` is **1000**, far past the end of the table; every id is
  range-checked before indexing `kParams`.
* **Never a null title or units to `RangeParameter`** — it dereferences both
  without a check, and presents as the *validator* segfaulting in the
  post-build step. Empty string, never null.
* `setState` **resets anything a short stream does not mention back to its
  default**, or loading an old project after a new one inherits the new one's
  settings. The processor and the controller read the identical layout.
* **Append parameters, never insert.** An id that moves loads a saved
  project's value into the wrong control.
* `data.numSamples == 0` and 64-bit hosts are both handled, from a scratch
  buffer sized in `setupProcessing`, so **the audio thread never allocates**.
* `editorDestroyed` compares **upcast pointers, never `dynamic_cast`** —
  `~EditorView()` is one of its callers, by which time the derived sub-object
  is gone, the cast yields null, and the entry survives as a dangling pointer.

## 8. The sample slots

Added after the scaffold, at the point where the panel needed something to
put samples in. **Nothing is loaded from a slot yet**: dropping a `.wav` on
one records *where the file is*, saves that with the project, and draws its
name. Reading the audio is the next piece of work.

### What a drop actually does

```
  Finder  ──drag──▶  SpySampleSlot::onDrop
                          │  (takes the FIRST acceptable path in the package)
                          ▼
                     Project6Editor::slotDropped
                          ▼
                     Project6Controller::setSlotPath      ← the ONE place a slot changes
                          ├──▶ its own SlotBank            (what the panel draws)
                          ├──▶ message to the processor    (UI thread — legitimate)
                          └──▶ refreshSlots() on EVERY open editor
                                   ▼
                              SpySampleSlot::setPath
```

The slot **never sets its own text**, exactly as a parameter control never
sets its own value. A host may have two windows open on one instance; a view
that also updated itself would be right in the window that was dropped on and
right by luck in the other.

### The decisions inside it

| | |
|---|---|
| Grid | 8 × 8, **row-major**, so slot 0 is top-left and 63 bottom-right. The whole bank is on the panel at once, and a slot's position is how anyone remembers what is in it. `slotIndex()` is the only place that arithmetic lives. If it ever flips, every saved project transposes — `SlotTests.cpp` §1 checks all 64 cells map one-to-one, not just the corners. |
| Accepted files | `.wav` only, case-insensitive, as a **list with one entry** — adding `.aif` is one line in `Project6Slots.cpp` and nothing anywhere else, because the view, the drop handler and the tests all ask `isAcceptedSampleFile()`. |
| Multi-file drops | The **first** acceptable file, onto the slot it was dropped on. Spreading the rest across slots the user did not point at is a surprise, and an expensive one when it overwrites four that were already loaded. |
| What is shown | The filename in **white** — brighter than the sliders' near-white readouts, because a slot's name has nothing to compete with and sixty-four of them have to be scannable. Shortened in order of what is worth losing: the `.wav` first (every file here has one), then a font size, then characters off the end with an ellipsis. The **full path is a tooltip**, since two takes of the same sample usually differ only in the directory. |
| An empty slot | **Blank.** No index, no "drop a file here". Sixty-four copies of either is a wall of text with the four names that matter hidden in it. |
| Feedback | The slot outlines itself green while an acceptable file is over it, and stays plain for one that is not — decided in `onDragEnter`, so the pointer says no *before* the button is released. |

### The panel

The slot grid now **sets the width of the panel**: 8 × 84 + 7 × 5 = 707, plus
two 14-pixel margins, is 735 × 481. Everything above it is measured from that
— the display's width is *derived*, not a constant, so widening the grid
widens the display instead of leaving bare panel beside it. Two
`static_assert`s in `Project6Editor.h` fail the build if the grid stops
meeting its right or bottom margin.

84 pixels is what a filename needs to be worth reading. Below about 70 the
grid stops being a list of names and becomes a grid of ellipses.

### The state stream

**The layout changed, and both sides changed with it.** The stream is now:

```
int32   version              (2 — was 1)
int32   parameter count
double  × count
int32   bypass
int32   slot count           ┐
int32   byte length          │ the slot block, appended
bytes   UTF-8 path, no NUL   │ × slot count
                             ┘
```

Written by `Project6Processor::getState`, read by `Project6Processor::setState`
**and** by `Project6Controller::setComponentState` — through the *same pair of
functions*, `writeSlots` / `readSlots` in `Project6SlotState.{h,cpp}`. The
parameter block above it is still written twice with a comment saying the two
must agree; this block is kept in step by the compiler instead, which is the
better arrangement and the one to move the parameters to if they ever grow.

`readSlots` **empties the bank before it reads**, so a version 1 project — or
a truncated stream — loads with all sixty-four slots empty rather than
inheriting the samples of whatever was open before. That is the same rule the
parameters already follow.

### The limitation worth knowing

A file path is nothing a parameter can carry, so it travels to the processor
as a **message**. If a host never connects the two components, the drop
reaches the panel but not the saved project. That is true of every VST3
message, and it is why anything that *can* be expressed as a number is a
parameter instead.

### Not done, deliberately

* **No way to empty a slot from the panel.** Dropping a different file
  replaces one, which is the recovery path; a clear gesture was not asked for.
  `SlotBank::clear` and the state stream already handle it, so it is a mouse
  override and a handler away.
* **Nothing reads the file.** When it does: the load must happen on a
  non-audio thread and hand `process()` a prepared buffer. `mSlots` is a
  `SlotBank` of `std::string`, and a string assignment allocates — the audio
  thread must never touch it.

## 9. The SDK

**In-tree clone**, chosen deliberately over pointing at a sibling project's
checkout. The first `cmake` configure clones the VST3 SDK (~250 MB) into
`external/`, which is `.gitignore`d. A project whose SDK lives inside another
project's folder is not self-contained, and this one may ship separately.

`VocalFilter-VSTi/external/vst3sdk` was used for the **compile check** in §6
only. Nothing in the build refers to it.

To use an existing checkout anyway:

```sh
cmake -B build -G Xcode -DVST3_SDK_ROOT=/path/to/vst3sdk
```
