# Project6 — porting notes

Nothing has been ported yet. This file exists anyway, because it records the
decisions that are **permanent from the first shipped build** and the traps
that were deliberately handled before there was any code to hide them in. It
is the file that outlives the session that made the project.

Scaffolded from `~/DXi-DEv/vst3-port-template` on 8 September 2026, following
the `blank-vst3-plugin` skill. `PORTING-GUIDE.md` and `PORT-CHECKLIST.md` came
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

# 1. Both test suites, from a clean build.
c++ -std=c++17 -O2 -Wall -Isource \
    tests/DspTests.cpp source/Project6Dsp.cpp -o /tmp/dsptests && /tmp/dsptests

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

Results: both suites all-pass, all eight translation units produced object
files with no errors, all 19 undefined `Project6::` symbols resolved within the
set, and `check-editor` reported ok.

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

## 8. The SDK

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
