# Project6 — porting notes

Nothing has been *ported* yet — no DXi is behind any of this. The file exists
because it records the decisions that are **permanent from the first shipped
build**, and the traps that were handled deliberately rather than discovered.
It is the file that outlives the session that made the project.

What the plug-in currently is: an 8 × 8 bank of looping sample pads, launched
on the bar. Drag a `.wav` onto a slot, click it to arm it, and it starts when
the transport next crosses a bar line. §8 is the grid, §9 the playback and
§10 the transport.

Scaffolded from `~/DXi-DEv/vst3-port-template` on 8 September 2026, following
the `blank-vst3-plugin` skill; the slot grid and the playback were added the
same day. `PORTING-GUIDE.md` and `PORT-CHECKLIST.md` came
with the template and are unchanged; read the guide before writing a real
processing loop.

---

## 0. OPEN: MIDI goes silent at a transport loop point

**NOT FIXED. NOT CONFIRMED FIXED. Do not close this without a test that
reproduces the original failure and then does not.**

*Reported:* in Reaper, with the transport in loop mode and a MIDI pad playing
into Apple's AUMIDISynth, the synth falls completely silent at the loop point
and stays silent. A drum machine (ICON) in the same place is fine. The MIDI
goes on arriving. Stopping and restarting the **pad** does not bring it back;
only a full transport stop does. With the transport **not** looping it plays
indefinitely.

*Four fixes have been shipped against it. None is confirmed to be the one.*

| commit | what it fixed | was it the bug? |
|---|---|---|
| `2329ada` | four faults that made a pad stop permanently | no — still failed |
| `459146a` | note-off sharing a sample with its own re-trigger | no — still failed |
| `f3668fc` | notes stranded where the loop crosses its own anchor | no — still failed |
| `ae7b4b0` | every event sent twice, on the merged bus and the row's | **unknown** |

All four were real defects found on the way. The first three are known not to
be *this* one.

**What the evidence says.** A log taken inside Reaper with the fault
reproduced (`PROJECT6_MIDI_LOG`, or create `~/p6-midi-log.txt`) showed the
plug-in behaving perfectly across 6,432 blocks and both loop points: max five
notes held at once against a file whose own polyphony is five, zero orphan
note-offs, zero left sounding, zero out of sample order, zero refused by the
host, and a note-on rate flat through both loops (8.5, 10.2, 11.2, 10.2 per
second in the four regions either side). The pad stayed armed, launched and
playing from first click to transport stop.

**The one thing that log did expose** was 1202 notes handed over as 2404
events — the doubling, fixed in `ae7b4b0`. That is the last thing this
plug-in was doing that a synth could object to.

**Where it stands.** It works in Logic Pro with the AU build. That result is
**confounded**: the host changed (Reaper → Logic), the format changed (VST3 →
AU), *and* the build contains `ae7b4b0`. It does not distinguish "the doubling
was the bug" from "Reaper is the problem".

**The next test, and it is cheap:** run the *current* build in Reaper again.

* Reaper now works → the doubling was it. Close this.
* Reaper still fails → take a log with the current build. If it looks like the
  last one — balanced, nothing stranded, nothing refused — then identical
  plug-in output producing different results in two hosts is conclusive that
  the fault is host-side, and the next move is Reaper's MIDI routing settings,
  not more changes here.

**Do not make further speculative changes to the MIDI path without a log.**
Three were made that way and all three were wrong.

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

The default patch is silent because a fresh instance has no samples in it.
**The second number is the one to keep repeating**: full scale in, full scale
out, at the default trim. A level that clips masks other faults and sends you
chasing the wrong bug.

The top of the trim's travel is unity, not a boost, for the same reason.

**Voices sum at unity.** A pad plays its file at the file's own level, and
sixty-four pads at once will clip — which is what the trim is for, and what
anyone running that many loops would expect. Scaling by the voice count
instead would make a single pad quieter every time another one started, which
is worse. `DspTests.cpp` §6 asserts the summing, so a later change to it is a
decision rather than an accident.

## 6. What was verified, and what could not be

The session reached this Mac through a Linux VM. **`cmake`, Xcode, the SDK
validator and `auval` are not available there** — none of them has been run,
and the first build is still ahead of you.

What was run, and passed:

```sh
cd ~/DXi-DEv/Project6-VSTi

# 1. All seven test suites, from a clean build.
c++ -std=c++17 -O2 -Wall -Isource tests/DspTests.cpp \
    source/Project6Dsp.cpp source/Project6Sample.cpp \
    source/Project6Slots.cpp source/Project6Stretch.cpp \
    -o /tmp/dsptests && /tmp/dsptests

c++ -std=c++17 -O2 -Wall -Isource \
    tests/SlotTests.cpp source/Project6Slots.cpp -o /tmp/slottests && /tmp/slottests

c++ -std=c++17 -O2 -Wall -Isource \
    tests/WavTests.cpp source/Project6Sample.cpp -o /tmp/wavtests && /tmp/wavtests

c++ -std=c++17 -O2 -Wall -Isource tests/TransportTests.cpp \
    source/Project6Transport.cpp -o /tmp/transporttests && /tmp/transporttests

c++ -std=c++17 -O2 -Wall -Isource tests/StretchTests.cpp \
    source/Project6Stretch.cpp -o /tmp/stretchtests && /tmp/stretchtests

c++ -std=c++17 -O2 -Wall -Isource tests/MidiTests.cpp \
    source/Project6Midi.cpp -o /tmp/miditests && /tmp/miditests

SDK=~/DXi-DEv/VocalFilter-VSTi/external/vst3sdk        # any existing checkout
c++ -std=c++17 -O2 -Wall -Isource -I$SDK \
    tests/ParamsTests.cpp source/Project6Params.cpp source/Project6Dsp.cpp \
    source/Project6Sample.cpp source/Project6Transport.cpp \
    source/Project6Stretch.cpp -o /tmp/paramstests && /tmp/paramstests

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

# 5. And the diagram, which reads the headers and fails rather than guessing.
python3 tools/render-routing.py
```

`-DRELEASE=1` is required or `fdebug.h` refuses to compile.

Results, at the one-shot commit: **all seven suites all-pass**, all fifteen
translation units produced object files with no errors, every undefined
`Project6::` symbol resolved within the set, `check-editor` reported ok, and
`render-routing` regenerated the diagram from the headers.
The panel's dimensions are checked by `static_assert` rather than by eye —
1071 × 624, with the grid meeting the fader column, the faders meeting the
right margin, the launch boxes clear of the pads, and the level bar, the
division box, the fit box and the loop switch filling a cell's width
exactly.

Re-run all eight before every commit. Adding a source file also means re-running
`./setup-xcode.sh --no-open` before the next Xcode build, or the project
compiles the old file list.

**What the tests cannot reach**, and what the first real listen is for: the
drag-and-drop itself (a platform drag package is not something a unit test can
manufacture), whether the panel looks right, whether five milliseconds is in
fact enough declick on real material, whether a host actually exposes nine event
outputs or only the first, and whether a host's `ProcessContext`
says what this code assumes it says — `TransportTests` proves the arithmetic
is right about the numbers it is given, not that the numbers arrive. That last
one is the first thing to check in a real DAW: if the display reads
"No transport" or the playhead does not move, the flags in
`getProcessContextRequirements` are where to look.

Everything the tests *do* reach — every WAV format, the loop wrap, the rate
conversion, the envelope, the bar arithmetic in five time signatures, the
parameter blocks' bounds, and the *measured pitch* out of both fit modes — is
asserted rather than assumed.

**The AU has now run.** Logic Pro is AU-only, so loading Project6 there
exercises Steinberg's wrapper for the first time: nine audio buses built as AU
elements, and MIDI out through `kAudioUnitProperty_MIDIOutputCallback`. It
loaded and played a MIDI pad into a synth through a looping transport. That
closes the longest-standing unknown in this file — though it is a report from
one session, not a validator run, and `auval` is still the thing that says so
properly.

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
| ~~`processContextRequirements`~~ | **Done — see §10.** The trap was exactly as written down here before it was needed: opt-in since VST3 3.7, default no flags, every field silently invalid, tempo reads 120 everywhere, and the validator says nothing about it. A plug-in that launches on the bar would simply never have launched. |
| Processor → controller messages beyond the sample rate | **A message sent from `process()` is silently discarded** by the host's connection proxy — it returns success and does nothing. Per-block values go out through `data.outputParameterChanges` as hidden read-only parameters instead. Both halves of that rule are written into `Project6IDs.h` and `Project6Params.h`. |
| Published ("live") parameters | None are needed yet. When the first one is appended, `kNumStoredParams` moves with it, and **the range check on it must be bounded by its own end, not by `kNumParams`** — a predicate reading `id < kNumParams` misclassified the next parameter appended after such a block in VocalFilter, and the symptom was a control that wrote its parameter, was seen by the host, and did nothing. |
| `IMidiMapping`, and notes | No CC handling, and no pitched playback: the event input exists and consumes events so nothing can hang, but a note-on does not start a pad. Mapping notes to slots is the obvious next step, and the trigger parameters are the thing to move — a note-on writing `slotPlayParam(n)` gets automation and the panel for nothing. ForTran is the worked example for CCs. |
| ~~Voices, and a conditional `silenceFlags`~~ | **Done — see §9.** `process()` now flags silence only when `mDsp.soundingVoiceCount() == 0`, and a voice counts as sounding through its fade-out. A synth that flags silence while something is playing is silenced by the host, which presents as a pad that lights up and cannot be heard. |

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
put samples in. This section is the **file path**: how it gets into a slot,
how it is saved, and how it reaches the half of the plug-in that can open it.
§9 is what happens to the audio once it is read.

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
  `SlotBank::clear`, the message and the state stream already handle it, so it
  is a mouse override and a handler away.

## 9. Playing a slot

Click a loaded slot and its file loops; click it again and it stops. Three
separate mechanisms make that work, and they are separate on purpose.

> **Since the transport arrived (§10), a click no longer starts anything
> directly.** It sets the trigger parameter; the next bar line is what turns
> that into sound. Everything in this section still holds — it is the
> mechanism the bar line acts on.

### The trigger is a PARAMETER, not a message

`kSlotPlayBase + 0 … + 63`, appended after the output trim, two states each.
That follows this project's own rule, written down when the slot *paths* had
to travel as a message: **anything that can be expressed as a number is a
parameter**, because a message is lost in a host that does not connect the
two components and a parameter is not. A play/stop is a number.

What it buys, all through machinery that already existed:

* automation, host undo and a recordable gesture, because the click goes
  through `beginEdit / setValueNormalized / valueChanged / endEdit` like any
  other control;
* a panel that follows an automation lane, because `setParamNormalized` →
  `updateControl` → `showValue` already moves whatever is in `mControls`, and
  the slot is now in `mControls`;
* delivery that cannot silently fail.

What it costs: sixty-four rows in a host's generic parameter list. That is the
honest price of pads a DAW can automate. They are named **`Slot A1 Play` …
`Slot H8 Play`** — lettered by row, numbered by column — so a name in an
automation lane says where the pad is on the panel rather than making the
reader divide by eight.

**They are deliberately NOT saved.** `kNumStoredParams` stops at the trim, and
both `setState` and `setComponentState` reset *every* parameter past it to its
default before reading. A project that reopened with six pads already looping
would be a project nobody could open quietly, and "what was playing when you
saved" is a moment, not a setting. Automation still restores them, because
automation lives in the host.

The definitions are a **block described once**, not sixty-four table rows
differing in one character: `slotPlayDef()` is the shared `ParamDef` and
`paramTitle()` supplies the names. `paramDef(id)` is now the accessor
everywhere — `kParams[id]` only covers the individually described parameters.

And the bound: `isSlotPlayParam` is `id >= kSlotPlayBase && id < kSlotPlayEnd`.
**Not `< kNumParams`.** VocalFilter's equivalent predicate was bounded by the
end of the table, survived one append, and then misclassified the parameter
added after it — the control wrote its parameter, the host saw the write, and
nothing happened. `ParamsTests.cpp` §7 fails if that bound ever becomes the
table's end again.

### The audio thread never opens a file, allocates, or frees one

This is the whole thread story, and it is worth reading before changing any of
it.

```
UI thread                                   audio thread
─────────                                   ────────────
notify() / setState()
   loadWavFile()  ← opens, allocates, decodes
   make_shared<const SampleBuffer>
   publishSlot()
     mSamples[i] = new buffer   (owned here, never freed on the audio thread)
     mDsp.setSlotSample(i, ptr) ──── atomic store, release ────▶
     mRetired.push_back(old, blockCounter)                     atomic load, acquire
     collectRetired()                                          renderVoices() reads
                                                               process() bumps blockCounter
```

* A `SampleBuffer` is **immutable once published**. That is the only reason a
  bare pointer is safe to hand across at all.
* The store is **release** and the load **acquire**, so a thread that sees the
  new pointer also sees the bytes behind it. Without the ordering the pointer
  can arrive before its contents on a weakly ordered machine — which is every
  Apple Silicon Mac.
* The buffer a publish **replaces** cannot be freed there: a block may be half
  way through reading it. It goes on `mRetired` with the block counter's value
  at the swap, and is freed once the counter has reached `at + 2` — by which
  point the block that might have held it has certainly returned, because
  `process()` runs one block at a time. `+2` and not `+1` because the block
  running at the swap may have started at `at` itself.
* When the plug-in is **inactive** no block is running, so `setActive(false)`
  and `terminate()` free the whole retire list at once.

### The voices

One per slot, in `Project6Dsp`. Each is a playhead into its slot's buffer, a
declick envelope, and nothing else.

| | |
|---|---|
| Start | From **frame 0**, at the bar line that launches it. A pad you click plays its sample, not the middle of it. |
| Loop | The playhead wraps with `fmod`, and **the interpolation's second tap wraps to frame 0** — so the loop is continuous rather than fading into the last frame and jumping. A file whose ends do not match will still click; that is the file's business, not the player's. |
| Pitch | The playhead advances `file rate / session rate` per output frame, linearly interpolated. A 48 k file in a 44.1 k session plays at pitch instead of a fifth flat. Linear interpolation is a gentle low-pass upward and mild aliasing downward — anything better is a resampler, and a resampler is a decision about latency and cost that belongs with the rest of the DSP. |
| Declick | A **5 ms linear** ramp in on start and out on stop. A loop does not start at zero, and cutting one in or out at full gain is a click — sixty-four of which is what makes a sampler sound cheap. Linear, not the one-pole the trim uses, because an exponential approaches zero without reaching it and a voice asked to stop would never actually finish. |
| Re-click mid-fade | A change of mind: the envelope reverses **where it is**, and the playhead carries on. Jumping back to frame 0 there would be exactly the click the fade exists to prevent. |
| Emptied while playing | Stops dead. There is nothing left to fade out of. |
| Mix | Voices **sum** at unity, ahead of the output trim. |

### The WAV reader

`Project6Sample.{h,cpp}`, SDK-free, and **the parse is split from the file
read** so `parseWav()` can be handed bytes. That is what lets `WavTests.cpp`
cover a truncated chunk, an odd-length metadata chunk and a four-channel file
without anyone first having to find one.

It reads PCM at 8, 16, 24 and 32 bits, IEEE float at 32 and 64, any channel
count, and `WAVE_FORMAT_EXTENSIBLE` — which is what a 24-bit file from most
modern editors actually is. Everything comes out as **interleaved stereo float
at the file's own rate**, so the audio thread deals with one layout however
odd the file was.

Four things in there that are not obvious, each with a test:

* **8-bit PCM is unsigned.** Silence is 128. Read as signed, a quiet file
  becomes a loud square wave.
* **Chunks are word-aligned**: an odd-length chunk carries a pad byte the size
  field does not count. Miss it and the next chunk's id is read one byte late,
  which loses everything after the first metadata chunk — that is, most files.
* **Divide by negative full scale** (32768, not 32767), or a file already at
  full scale clips on load.
* **Mono is duplicated, not panned left**, and more than two channels takes
  the first two.

Files longer than **60 seconds are refused**, not truncated, because a slot is
filled by dropping whatever was under the pointer and sixty-four accidental
album sides would be several gigabytes resident.

### A slot that will not play says so

A failed load **keeps its path** — the file may simply be on a drive that is
not plugged in today — so the project still remembers it and the slot still
shows its name. What it does not get is audio, so:

* the processor reports a `SampleStatus` back to the controller
  (`kProject6SlotStatusMessage`), and **re-sends all sixty-four from
  `setActive`**, which is the only thing that makes a panel opened later, or a
  project restored before the components were connected, show the truth;
* the slot draws its name in a muted red rather than white, ignores clicks,
  and puts the reason in its tooltip under the path.

A slot that took the drop, showed the name and did nothing when clicked would
be the failure this project keeps coming back to: a control that looks live
and is not.

## 10. The transport, and launching on the bar

Pads are **gated on the transport** and **launched on the bar line**. A click
arms; the bar starts. A second click arms the stop, and the next bar ends it.

### First, the trap the scaffold wrote down before it was needed

`getProcessContextRequirements()` now declares `needTransportState`,
`needProjectTimeMusic`, `needTempo` and `needTimeSignature`.

**Without that declaration none of it arrives.** The `ProcessContext` has been
opt-in since VST3 3.7 and the default is no flags: `data.processContext` comes
through with nothing valid in it, the tempo reads 120 in every host, no bar
line is ever found, and the validator prints `- None` rather than complaining.
A plug-in built to launch on the bar would simply never launch, in every host,
with nothing anywhere saying why. `AudioEffect` already implements
`IProcessContextRequirements`; only the flags were missing.

### Armed is not playing

Two arrays in the processor, and one function that copies one into the other:

```
mArmed[64]     ← the trigger parameters, every block. What was clicked.
mLaunched[64]  ← what the DSP has been told. What is sounding.

applyBarLine() ← the ONLY thing that copies armed into launched.
```

`applyBarLine()` acts only on slots where the two differ, which makes it
**idempotent**: a bar line that arrives twice — a host repeating a block, a
cycle wrapping straight back onto it — cannot restart a pad that is already
running, and a pad free-running past the bar is left alone.

### Where the bar line is

`Project6Transport.{h,cpp}`, SDK-free, and it is the file that most needed to
be. Every way of getting this wrong is silent:

| The mistake | What you would see |
|---|---|
| Bar length hard-coded to four | Right in 4/4, wrong everywhere else. 7/8 is 3.5 quarter notes. |
| A line at offset 0 not counted | Every launch a bar late — but only when playback starts exactly on a bar, which is most of the time. |
| A line fired by two blocks | A pad launched and relaunched a sample apart. |
| A line never fired again | A one-bar cycle that works exactly once. |
| Launching at the block edge instead of the sample | Up to 11 ms late at 512 samples, worse with the buffer size, audible against a click. |

All five are covered by `tests/TransportTests.cpp`, which is why the
arithmetic takes plain doubles and knows nothing about VST3. The processor's
job is only to copy the `ProcessContext` into a `TransportInfo` and act on the
sample offsets that come back — and it renders **segment by segment**, exactly
as it already did for events, so a launch lands on its own sample.

Two details worth keeping:

* The "have I already fired this line" guard is cleared by a **jump
  backwards**, compared against the previous block's **start** and not its
  end. Against the end, a host handing over the same position twice would look
  like a locate; against the start, a cycle wrap still does.
* `BarClock::reset()` is called whenever the transport is not rolling. Without
  it, rewinding to the bar you just played and pressing play launches nothing,
  because that line is remembered as already fired.

### Each pad picks its own grid line

`1/1`, `1/2`, `1/4`, `1/8` — **fractions of a bar**, not note values. Outside
4/4 the two part company, and the fraction is the right answer: a quarter of a
7/8 bar is 0.875 quarter notes, and *every division line then still nests
inside the bar*. Quarter notes in 7/8 would drift against the bar and a "1/4"
pad would sometimes launch off the downbeat, which is not a launch grid. In
4/4 the two readings coincide, which is why this only shows up in the odd
meters.

Because they nest, **one list of lines serves all sixty-four slots**: the clock
finds every eighth-of-a-bar line and says which *step* of the bar each one is,
and `divisionFires(division, step)` is the whole rule —

```cpp
return step >= 0 && (step % divisionSteps (division)) == 0;
```

Step 0 is the bar line and every division fires on it. That single fact is what
keeps a 1/8 pad and a 1/1 pad in phase instead of drifting past each other, and
it is what lets the degraded paths below say `applyGridLine(0)` and mean "apply
everything".

The default is a **whole bar** — the coarsest choice, and what the plug-in did
before there was a choice, so a project saved before this parameter existed
loads behaving exactly as it did.

`TransportTests` §7 pins the nesting down: a coarser division's steps are a
subset of a finer one's, a bar slot fires once a bar and an eighth slot eight
times, and a bar slot does **not** fire half way through.

### The three levels of knowledge, degrading separately

| What the host gives | What happens |
|---|---|
| No `ProcessContext` at all | **Launch immediately**, as the plug-in did before it had a transport. A sampler that is silent in a host reporting no transport looks broken, not careful. The panel says "No transport". |
| A context, but no tempo / signature / position | Gate on the transport, but launch as soon as it rolls. Closer to what was asked for than never launching. |
| Everything | Gate on the transport, launch on the bar. |

Tempo, time signature and position are treated as **one flag**, not three: a
tempo without a position cannot locate a bar either, so there is one thing for
every caller to check.

### Stopping the transport

The voices are silenced and **the arming is left alone**. A pad stays lit
while the transport is stopped and comes back in on the next bar line when it
rolls again, from the top of its sample — which is what makes a rewind
something you can do without re-clicking eight pads.

`setupProcessing` and `setActive(true)` both clear `mLaunched`, because both
reset the DSP and every voice with it. Miss that and `applyBarLine()` sees
"already launched" and never starts anything again.

### Showing the playhead

A bar along the bottom of each playing pad, filled to where its file has got
to. It exists because "which pads are running" and "where they are in their
loops" are different questions and the lit well only answered the first.

**Three mechanisms were available and the third is the right one**, which is
worth writing down because the first two are the ones already in use here:

| | |
|---|---|
| A message pushed from `process()` | **Impossible.** The host's connection proxy discards it — the rule at the top of `Project6IDs.h`. |
| Publishing through `data.outputParameterChanges` | Possible, and wrong. Sixty-four *continuously changing* values would put thousands of points a second into a host's queue to move bars that redraw thirty times a second. The bar phase is already quantised to 1/128 for exactly this reason, and that is one parameter. |
| **A request and a reply, both on the UI thread** | What it does. The controller asks on the editor's timer, the processor answers with all sixty-four in one binary blob. |

That last is ForTran's scope idiom, and the general rule it embodies is: **a
value that only the panel wants, only while it is open, and only at the rate
it can draw, is a value to ask for rather than to publish.** Nothing is paid
for it when the panel is shut, because the timer is the only thing that asks.

Across the thread boundary each voice keeps a `std::atomic<float> progress`,
written **once a block** by the audio thread — relaxed, because a bar drawn
from a value one block old is right to within eleven milliseconds and nothing
else depends on it. A store per *sample* would be sixty-four atomic writes a
frame to move a bar a pixel every few hundred.

Three details worth keeping:

* it is **zeroed when a voice starts and when it stops**, so a bar never
  lingers on a pad that has gone quiet — the panel claiming something the
  audio is not doing is the failure this project keeps coming back to;
* it is drawn from **`sounding`, not from the trigger**, so an armed pad
  waiting for its grid line shows no bar. The two must never contradict each
  other;
* `setProgress` **quantises to the pixel the bar will actually be drawn at**
  and skips the redraw otherwise. Sixty-four pads at thirty frames a second is
  1920 potential redraws a second, most of which would change nothing. The
  comparison is against the last *drawn* value and the skipped difference
  accumulates, so a ten-minute sample still advances — a threshold that reset
  each tick would freeze the bar on anything long.

### Showing the wait

A pad that has been clicked and has not started yet must not look like a pad
that ignored the click. So the processor **publishes what is actually
sounding**, through the mechanism §8 described and did not yet need:
`data.outputParameterChanges`, as a hidden read-only block appended after the
triggers — the transport state, the position through the bar, the beats in a
bar, and one flag per slot.

**This is the append the trigger block's bound was written for.**
`isSlotPlayParam` reads `id >= kSlotPlayBase && id < kSlotPlayEnd`, and now
that something follows the triggers that bound is load-bearing rather than
merely careful. `ParamsTests` §7 and §8 fail if it ever becomes `kNumParams`.
One enum trap came with it: an enumerator following an explicitly valued one
carries on from *that* value, so `kLiveTransport` is anchored to
`kSlotPlayEnd` by hand or the published block starts one id past the gap.

Only values that have **moved** are published, and the bar phase is quantised
to 1/128 of a bar first — published raw it changes every block and would fill
the host's queue with hundreds of points a second to move a playhead two
pixels.

What the panel does with it:

* a slot is **amber** while armed and not sounding, **or** sounding and no
  longer armed — the same mark for both, because "about to start" and "about
  to stop" are the same fact; **red** while playing; plain otherwise;
* the display is now **one bar wide**, ruled into beats from the host's own
  numerator, with a playhead sweeping it and the transport state in words.
  Its horizontal axis had no meaning before this and the comment in the file
  said so; the transport is what gave it one.

If a host never forwards published values, the panel falls back to showing
what was **asked for** — the right pad lit early rather than the wrong one for
ever. That is VocalFilter's rule for the same problem, and the same fallback.

### A level per slot

A bar under each pad, `-40` to `+12` dB, default unity — the range VocalFilter
gives a formant's level and for the same reason: this is a **component** level
inside a mix. A component may need lifting; the stage that must not boost is
the output trim, which still tops out at unity and is where clipping is
answered. The bottom of the travel is **silence**, not −40 dB of leakage, via
the same `dbToLinear` the trim uses.

In the DSP it is a second gain per voice, ahead of the mix and behind the
declick envelope, and it is **snapped when a voice starts and smoothed
thereafter**. Both halves matter: a pad set to −20 dB must come in at −20 dB
rather than ramping there over the first ten milliseconds, and a bar dragged
while the pad is running must not step at every block boundary. At the default
0 dB the multiply is by exactly 1.0, so a pad at full envelope is still
bit-identical to its file — `DspTests` §6 checks that, which is what stops the
level stage quietly costing a bit of precision on every pad that never uses it.

A level of zero **silences a pad without stopping it**: the user did not
un-arm it, so it stays lit and keeps its place in the loop.

#### Why they are saved in a block of their own

The levels are the first thing here that is **both a parameter and a
setting**. The triggers are parameters that are deliberately not saved; the
published values are not saved either. A level is neither — a project must
reopen with the balance it was saved with.

But ids are never moved, so they are appended at 132…195, nowhere near the
contiguous run from id 0 that `kNumStoredParams` bounds. So they travel as
their **own block** in the state stream — `writeSlotLevels` / `readSlotLevels`
in `Project6SlotState.cpp`, one pair called by both sides, exactly as the slot
paths already are. Stream version 3.

VocalFilter hit this same shape when `kVoice` was appended past its published
values and had to be saved explicitly. `kNumStoredParams` therefore does not
mean "everything that is saved" — it means "where the run from zero stops",
and its comment now says so.

#### The bar itself

`SpySlotLevel` is a SlideSpin's bar with everything else taken off: no label,
no value text, no lamp — nine pixels of the DXi's own `Draw3dRect` and fill,
and the same drag law, **relative and one unit of a hundred per pixel**.
Absolute positioning on a bar this size would make every setting a coarse one,
and it is what the original did not do either. Shift is fine adjustment; the
wheel steps.

Nine pixels has nowhere to print a number, so **while the bar is dragged the
pad above it shows the level in decibels** in place of its filename, and puts
the filename back on mouse-up. That is the same trick VocalFilter's SpySlider
plays with its own value text: use space that is already there rather than
adding a readout to sixty-four cells. The readout appears on mouse-DOWN, not
on the first pixel of movement — a bar pressed and not yet moved should still
say what it is set to.

The grid's row pitch is now a **cell** — pad plus bar — so a bar belongs
visually to the pad above it rather than floating between two. The panel is
735 × 592.

### Row buses

The eight pads of a row sum, pass that **row's** level, and the eight rows sum
into the output trim:

```
pad → slot level → Σ row bus → row level → Σ mix → output trim → out
```

`docs/routing.png` is that drawn out, and `tools/render-routing.py` draws it
**from the headers** — every range, default and count on it is parsed out of
`Project6Dsp.h` and `Project6Slots.h` and the script fails loudly rather than
guessing. A diagram that disagrees with the code is a diagram that has not
been regenerated. Re-run it whenever a gain moves.

The row levels take the same range and law as the slot levels, one level up: a
submix inside a mix, which may lift, sitting in front of the one stage that
may not.

#### Why renderVoices works a row at a time

A row's level scales **the sum** of its pads, so the pads have to be summed
before it is applied. Adding every voice straight into the output, as before,
would leave nothing to scale.

Eight row buffers would be eight allocations this class must not make, so
there is **one row scratch, reused for all eight rows**. It is sized in
`setupProcessing`, where the host says how large a block will be, and
`renderVoices` chunks a block that is somehow larger rather than growing it —
because growing it would be an allocation on the audio thread. `DspTests` §6
renders the same material with a scratch of 7 frames and with one big enough
and asserts the two are **bit-identical**, so the fallback is proved rather
than assumed.

A row with nothing sounding **snaps** its gain to its target instead of
ramping: nothing can hear it, so there is nothing to smooth, and a fader moved
while a row is silent is then already in place when a pad on it starts rather
than sliding into position over the first ten milliseconds. That behaviour is
asserted, because "it sounds fine" would not have caught it.

At the default 0 dB a row's gain is exactly 1.0, so the existing bit-identical
checks still pass unchanged — which is the evidence that inserting a whole bus
stage cost no precision.

#### The direct outs

**Nine output buses**: the main mix, and one stereo **aux** bus per row.

The tap is **before the row fader** — `renderChunk` copies the row scratch to
that row's bus at the moment the pads and their own levels have been summed
and before `bus.gain` is anywhere near it. So the fader sets how much of the
row reaches the main mix, and the direct out carries the row itself whatever
the fader is doing, which is what makes the fader usable as a balance control
while the desk gets the untouched signal. `DspTests` §6 asserts exactly that:
pull a row to a tenth, the mix follows, the tap does not move — with a
negative control, because "the two are different" has to be able to fail.

The rows still go to the main mix as well. "Additional outputs" is additive;
nothing was taken away.

Four things that needed care:

* **`kAux`, not `kMain`.** VST3 has exactly one main output; a host reads the
  distinction to decide what to patch by default.
* **Default active.** A bus that arrives switched off looks to most people
  like a bus that is not there. A host can deactivate any of them, and
  `renderSegment` hands the DSP a null pointer for a bus it was not given
  rather than assuming — there is no point summing into a buffer nobody will
  read.
* **A silent row must send silence**, not whatever the host left in the buffer
  last block. A row with nothing sounding is skipped entirely, so `render()`
  clears every tap before it starts. Tested by handing it a dirty buffer.
* **Silence flags are per bus** now. Seven rows out of eight are usually
  silent, which is most of the value of the flag on a nine-bus plug-in — and a
  bus flagged silent while something is on it gets silenced by the host.
  `rowSounding()` is what answers it.

**What cannot be checked from here**: `auval`. Steinberg's AU wrapper builds
one AU element per VST3 bus from `getBusCount`, so `au-info.plist`'s
`SupportedNumChannels` still describes the main element only and is still
`0 in / 2 out` — correctly, but the eight extra elements are the first thing
to look at on a Mac.

#### The row faders

Eight ordinary `SpySlider`s down the right of the grid, through the same
`addSlider` the output trim uses — so they label themselves, read out in
decibels from the same table the host formats from, and follow automation with
nothing added. They are lettered `Row A`…`Row H` to match the first half of a
slot's own name: slot C6 is on row C, and that is C's fader.

They are the **second** block appended past `kNumStoredParams`, and they share
the slot levels' stream mechanism rather than adding a third: `writeSlotLevels`
became `writeLevelBlock(streamer, values, count)` and is called twice. Stream
version 4. Two near-identical blocks would have been two chances to get the
same twenty lines wrong.

### Launching a whole column

A box above each column arms every loaded slot in it — or stops them all if
they are all already armed. It goes through the same door everything else
does: it **writes the slots' own trigger parameters and nothing else**, so a
column press and eight separate clicks are indistinguishable to the processor,
and the bar-line rules apply to it without knowing it exists. Eight pads
arriving together on the next bar line is the only way eight loops can start
in time with each other.

**It carries no parameter of its own**, and that is the design rather than an
omission. A column "state" would be a second opinion about the same eight
triggers, and the moment somebody clicked one pad out of a launched column the
two would disagree with nothing to say which was right. The precedent is
VocalFilter's `SpyPresetButton`: writes nine parameters, holds none, and a
host sees the writes and never sees the button. So it is a `CView`, not a
`CControl`, and what it *shows* is a summary the editor hands it.

The half-armed case is the only real decision, and it lives in
`Project6Slots.h` as `columnClickArms()` with a test beside it:

> Three of eight playing, and you press the column: it arms the other five
> rather than stopping the three, because the button's job is to **play** the
> column — stopping is what the second press is for. The other reading, "any
> playing means stop", makes the first press on a half-full column do the
> opposite of what the button is called.

It is a function rather than an `if` inside the handler precisely so that
changing it is something someone decides rather than something someone tidies.

Two smaller things:

* only slots that can actually play are counted, and only the triggers that
  actually **change** are written — eight automation points per press saying
  nothing is worse than none;
* the box fills in proportion to how much of its column is sounding (three of
  eight playing is three eighths lit), and takes the same amber edge as a pad
  while anything under it is waiting for the bar. A lamp could only have said
  "some".

## 11. Fitting a file to the project's tempo

A loop recorded at 100 BPM in a 90 BPM project has to be made 90 BPM. There
are only two honest ways to do it, they sound completely different, and
neither is better — so **the pad chooses**, and what is *not* offered is a
third option that pretends to be free.

* **Varispeed** — read the file at a different rate. Slower means longer *and
  lower*, the way a tape machine slowed down is lower. Perfect quality, wrong
  pitch. It is what a hardware sampler does and what most people mean.
* **Keep pitch** — WSOLA: hold the reading rate and repeatedly splice the
  playhead forward or back so the file takes a different amount of time to get
  through. Right pitch, and the splices are audible on some material.

The **default is varispeed**, and the reason is not quality. A pad whose tempo
detection was wrong sounds *obviously* wrong varispeeded — it is in the wrong
key, somebody hears it and switches it off. The same wrong detection in
keep-pitch mode sounds like a slightly ragged loop, which is far easier to
leave in a finished track by mistake.

### Where the file's tempo comes from

`Project6Sample` now reads the **`acid` chunk** most loop libraries embed —
twenty-four bytes carrying a beat count, a tempo, and the field that matters
most, a **one-shot flag**. A one-shot is a hit, not a loop; its length says
nothing about a tempo, and it is **never fitted whatever the pad is set to**.

Failing that, the tempo is **inferred from the file's length**, assuming a
whole number of beats. That is a guess, so it is bounded three ways: only the
beat counts a loop is actually cut to are tried (powers of two and their
triple-time neighbours), the implied tempo has to land between 70 and 180 BPM,
and the winner is the candidate nearest a reference — the *project's* tempo
when there is one — measured **in log space**, because tempo is a ratio and a
linear distance would call 180 nearer to 120 than 80 is.

If neither works, `tempoBpm` stays **zero and the file plays exactly as it
arrived**. Zero is not a missing value here, it is an answer: a wrong guess
would stretch a sample that was already right, and *that* is the failure
nobody notices.

Which of the three it was is recorded and shown — "100.0 BPM (ACID chunk)"
against "90.0 BPM (inferred from length)" mean very different things to
somebody wondering why a pad sounds wrong.

### The stretcher

`Project6Stretch.{h,cpp}` — SDK-free like the rest of the audio line, and
**it owns the playhead for both modes**. That is the point of the class: the
DSP asks for one output frame at a time and never has to know which mode
produced it, and the pad's progress bar reads the same number either way,
because that number is the **musical** position — where we are in the loop —
and not where either of the stretcher's two read heads is sitting.

Keep-pitch keeps two positions:

| | advances by | is |
|---|---|---|
| `mIdeal` | `step × speed` | where the music should be; what `position()` reports |
| `mRead` | `step` | where the file is actually read; why the pitch survives |

They drift apart at exactly the rate the tempo differs by. Every hop (~34 ms)
the read head jumps towards the ideal head, and the jump is placed at the
offset — within a ±6 ms search — where the waveform *after* it best continues
the waveform *before* it, by normalised cross-correlation. The correlation is
**normalised by the candidate's own energy** or every splice lands on the
loudest moment in the window instead of the best-matching one, which is how a
stretcher turns a drum loop into a stutter on the kick. The two heads are
cross-faded over 12 ms — **linearly**, not equal-power, because WSOLA has
already put them in phase and an equal-power curve would bulge at every join.

Because the jump is measured from the *live* drift, the timing
**self-corrects**: a hop that had to settle for a poor offset is made up by
the next one.

Two fallbacks, both to varispeed: a file shorter than two overlaps has nowhere
to put a splice, and a speed of exactly 1.0 never drifts, so no splice ever
fires and the output is **bit-identical** to the file. `DspTests` still passes
its unity assertions unchanged, which is the evidence.

The costs are bounded rather than fixed: 64 correlation taps and at most 257
candidate offsets **whatever the sample rate**, because a hop that quadrupled
its work at 192 k would be a deadline missed on a thread that has one.

### What the tests actually measure

`tests/StretchTests.cpp` **measures the pitch**, by counting zero crossings of
a sine that went in at 441 Hz:

> varispeed at 0.9× comes out at 396.9 Hz; keep-pitch at 0.9× comes out at
> 441 Hz; **both** end up nine tenths of the way through the file.

That contrast is the negative control of the whole feature. Trusting the
mode's name would let a stretcher that had been quietly switched off pass
every other assertion in the file.

### The wiring, and the two things it needed

The fit is applied **at playback, not baked in at load**, so it tracks a tempo
change live and costs nothing at drop time. The speed is
`projectBpm / fileBpm`, clamped to 0.25–4.0× — two octaves either way is
already absurd for a *tempo*, and anything past it is a detection failure, not
a tempo difference.

Two facts are kept **separate all the way into the stretcher**: the resampling
step (a fact about the file's sample rate) and the fit speed (a fact about its
tempo). Multiplying them together earlier would make it impossible to keep the
pitch while changing the length, which is exactly what one of the two modes
does.

`fitSpeed()` is **the shared function** — the DSP multiplies its read step by
it and the pad's tooltip reports it. A second copy in the editor would be a
second opinion about what the user is hearing.

Getting it there needed two small things:

* **`TransportInfo::tempoKnown`**, separate from `musical`. Locating a bar
  needs the tempo *and* the position *and* the signature; fitting a sample
  needs only the tempo, and needs it **while the transport is stopped**.
  `tempoBpm` defaults to 120 so the bar arithmetic has something to divide by,
  so a caller that must not invent a tempo has to read the flag and not the
  value.
* **`kLiveTempo`**, and the ugly part of this commit. It is a *published*
  value that sits **outside the published block**, because ids are never moved
  and it was needed long after that block closed. So `isLiveParam` grew a
  second clause. That is uglier than a contiguous run, and it is the price of
  never breaking a saved project — a parameter that moved would load a host's
  automation lane onto the wrong control. `ParamsTests` §13 asserts both the
  clause and its negative control.

### On the panel

The cell grew from 84 to 96 pixels, and **the twelve pixels went to the strip,
not to the name**: it now carries a level bar, the launch box *and* the fit
box, and a level bar under about thirty pixels is a bar you cannot set. Three
things in one strip rather than two strips, because a second row under every
pad would cost sixteen pixels per row and make the grid taller than a laptop
screen. `static_assert` still adds the strip up.

The fit box **dims when it is not doing anything** — mode Off, no detected
tempo, a one-shot, or a file already at the project's tempo. All four come out
of `fitSpeed()` as exactly 1.0, and in every one of them a bright "spd" would
be the panel telling a lie about the sound. Which of the four it was is in the
tooltip, on the box *and* on the pad above it, because the pad is the bigger
target.

Stream **version 6**: a fourth value block, through the same
`writeValueBlock`/`readValueBlock` the levels and divisions already use.

## 12. Moving a pad onto another pad

A loaded pad can be picked up and dropped on another one. Plain drag
**moves** it, Control or Alt **copies** it, and the whole cell travels —
level, launch division and tempo fit — because a pad is what you set up and
dragging it should rearrange the bank rather than the filenames.

A move is a **swap**, not an overwrite. The file that was in the destination
comes back to where the drag started, so a mis-aimed drag across a full grid
can never destroy a slot. A copy still overwrites: there is nothing to swap a
copy with.

Both pads whose file changes are **stopped** on the way through — both on a
move, the destination alone on a copy. A voice left running through a buffer
that was swapped underneath it is a click at best, and a pad playing a file
nobody started it with is the worst thing this panel could do.

### The launch moved off the mouse-down, and had to

A pad used to toggle its loop on mouse **down**, deliberately — "a pad should
sound the instant it is hit". A drag starts from that same press, so the two
could no longer both happen: a press that launched immediately and then turned
into a drag would have started a loop the user was only trying to move.

So the launch is on **release**, and only when the pointer travelled less than
four pixels. Nothing is audibly later for it — a pad does not sound when it is
clicked in any case, it sounds on the next grid line, and the few milliseconds
between a press and its release are nothing beside a bar. That is the *only*
reason this was affordable, and it is worth knowing before anyone moves the
launch back.

### Why the drag payload is text, and one entry rather than two

The obvious design is what Finder does: put the file's path on the pasteboard
as a `kFilePath` entry and the source slot's number beside it. **It does not
survive the round trip on macOS.**

VSTGUI unpacks a dropped pasteboard item by asking `availableTypeFromArray`
for the first of `NSPasteboardTypeString`, `…FileURL`, `…Color` that the item
offers — and an item written from an `NSURL` offers a **string as well as** a
file URL. String is first in that array, so a file path packed by this plug-in
comes back reported as `kText`, holding the URL form
`file:///Users/andy/My%20Loops/x.wav` rather than a path anything can open.

Two consequences:

* an internal drag carries **one text entry of its own shape** —
  `Project6/slot:<index>\n<path>` — which is unambiguous whatever type the
  platform decides to call it, and whose decoding is the only thing that has
  to be right. Dragging a pad *out* into another application then yields plain
  text naming the file, which is honest; dragging a pad out was never the
  feature.
* `firstAcceptedPath` **stopped trusting the type**. It reads any entry that
  has text in it and asks `sampleFilePathFromDragText` — which handles a plain
  POSIX path, a `file://` URL with its percent escapes, and several of either
  separated by newlines, of which it takes the first. A reader that insisted
  on `kFilePath` would refuse every drag there is on the platform this ships
  on, and nothing would have caught it until the first build.

Percent escapes are **only** decoded in the URL form. `100%.wav` is a legal
filename, and decoding a plain path would turn a real file into one that is
not there. `SlotTests` §6 has that as a negative control, along with the
malformed payloads — a bad index, a number that would not fit in an `int`,
no path at all — every one of which is *refused* rather than half-read,
because these cross a platform callback and a throw there would leave the
drag machinery mid-gesture.

Both ends of this are in `Project6Slots.{h,cpp}` and therefore **SDK-free and
tested**: it is string parsing, and string parsing is the part that can be
checked without a host, a mouse or a platform drag package. Thirty-one
assertions cover it, which is thirty-one more than a drop handler usually
gets.

### Two smaller things

The **modifier is read at the drop**, not remembered from when the drag began
— the platform's own rule, and it means someone can change their mind half way
across the grid. `onDragMove` returns `Copy` or `Move` live, so the cursor's
badge says which it will be before the button comes up.

**Alt counts as Control.** Control is what was asked for, but macOS turns a
Control-click into a right-click before the view ever sees it, and a copy that
could not be asked for on the platform this ships on would be a feature that
is not there. Alt is that platform's own modifier for the same idea.

### A bug the wiring caught

Routing fit-mode changes through `refreshFit` fixed something the previous
commit had shipped: switching a pad from *off* to *varispeed* put "spd" in the
box and **left it dimmed**, because `mFitted` was only recomputed when a
status message arrived. The panel was saying the setting was idle at the exact
moment it stopped being. `updateControl` now recomputes it whenever a fit
parameter or the published tempo moves.

## 13. A slot that holds a .mid instead

A pad takes either kind of file. An audio pad plays into its row's **audio**
bus; a MIDI pad plays into its row's **event** bus. Nothing else about a pad
differs — same launch quantise, same bar lines, same drag and drop, same state
stream — and `SlotFileKind` is the one place the difference is named.

### Everything is in quarter notes, and that decides the rest

A `.mid` file carries its own tempo in a meta event, and a naive reader
converts its ticks to seconds using it. Do that and every MIDI pad needs the
same varispeed-or-stretch machinery a sample does — and all of it is waste,
because notes have no waveform to resample.

Converting ticks to **quarter notes** at load throws the file's tempo away as
the irrelevance it is. Playback is then a matter of following the host's own
musical position: a project at 90 BPM plays the loop at 90, change it to 140
mid-bar and the loop is at 140 from that sample on, with no fitting, no
artefacts and nothing to configure. The tempo-fit box on a MIDI pad is
therefore **inert and says so** — it is not that the fit is off, it is that
the pad is already at the project's tempo by construction.

The file's tempo meta event is read and kept only to be shown; its time
signature likewise.

### Running the loop out to the end of the bar

The request. A file that stops half way through its last bar — which is most
files, because a DAW exports the region you selected — would otherwise loop
early and put every repetition one beat further out of step. `content` is what
the file actually holds; `loopLengthQuarters(content, barQuarters)` rounds it
**up to a whole bar** and the tail is silence.

Two decisions inside that:

* it is the **project's** bar, not the file's, because that is the grid the
  pad launches on and a loop measured against any other would drift against
  everything else on the panel;
* it is applied **at playback, not baked in at load**, so a time-signature
  change changes the loop with nothing reloaded. The same lesson the tempo fit
  taught.

The tolerance is what makes "already a whole number of bars" true: a four-bar
clip whose ticks divided out to 3.9999999996 bars is four bars, and without
the epsilon every loop in the bank would be padded a bar too long. That has
its own negative control in `MidiTests` §5.

### The playhead is musical

A pad launched on a grid line remembers the **project position** it started
at. Where it is in its loop is then simply how far the project has moved
since — so it cannot drift, it survives a tempo change with no arithmetic, and
it follows the host when somebody drags the playhead. `applyGridLine` grew a
sample offset for exactly this: a pad launched at sample 500 of a block starts
its loop at *that* sample's project position, not at the top of the block.

The one thing this costs: **without a musical context a MIDI pad cannot play
at all**. An audio pad falls back to launching at once, because a sample can
be played at the rate it was recorded; a MIDI loop has no such fallback, and
inventing a timeline would put every pad out of step the moment the tempo
moved.

### The question every assertion in MidiTests §7 is a version of

*Is a note left on?* It is the failure mode of every MIDI looper, it is silent
until somebody notices a drone, and it is the only thing here a user cannot
fix after the fact.

`MidiVoice` counts every note it starts, per pitch — a **count**, not a flag,
because one pitch can be struck again before the first is released and a flag
loses the second off. Everything counted is turned off when the loop reaches
the note's end, when the loop wraps past it, when the pad is stopped at a grid
line, when the transport stops, when the host locates, when the file is taken
away and when the plug-in is bypassed. Every one of those is one call to
`stopMidiVoice`, which is the point of counting rather than remembering.

`reset()` is the exception and the only one: it forgets without emitting,
because it is the deactivate path and there is no block to put note-offs in.

Two rules fall out of the same discipline:

* **no orphan note-offs.** A voice that never sent an on never sends the
  matching off — some instruments answer one by cutting off a note *another*
  pad is playing. This is what makes a locate survivable.
* **a note is cut off at the loop end.** Stated as an explicit flush when a
  piece reaches the boundary, not as an interval's edge case. It is also the
  backstop: an off that went missing for any other reason is caught within one
  repetition rather than hanging.

Both note-on and note-off intervals are **half-open**, by the same rule, so an
event falling exactly on a block boundary belongs to the block that starts
there. The other convention puts every off one sample early *and* uses a
different rule for ons and offs, which is the kind of asymmetry that hides an
off-by-one for years.

### Four bugs that made a pattern stop at random, and never come back

Found in a session, not here. The report was *"the MIDI drops out at what
sounds like random; the bar keeps moving but the events stop"* — and it was
four separate faults, three of which conspired to make one another permanent.

**1. A stop that did not unlaunch.** `applyGridLine` only acts on a slot whose
armed state *differs* from what is launched. `renderMidi`'s bail-out stopped
the voices without clearing `mLaunched`, which left a pad armed **and**
launched **and** not playing — a state no grid line would ever act on again.
Silent for ever, while the transport rolled on and the panel went on saying it
was armed. `silenceForTransport` had always cleared it, which is exactly why
the audio pads recovered from the same situations and the MIDI pads did not.

The fix is two functions rather than a flag: `stopMidiVoice` quiets the notes,
`unlaunchMidiVoice` quiets them *and* clears the launch. The difference is the
bug, so it is a name and not a parameter.

**2. `quartersPerSample` asked for too much.** It gated on `musical`, which is
`haveTempo && haveSig && havePos`. Placing a note needs the **tempo and the
position**; the time signature only decides where a loop is *rounded* to, and
`barQuartersFor` falls back to 4/4 — a slightly wrong loop length, not silence.
A host that reports its tempo and position but does not flag its time
signature on every block therefore stopped every MIDI pad in the bank. That is
where the "at random" came from: one block with an incomplete context was
enough, and fault 1 made it permanent.

`TransportInfo` gained `posKnown` beside `tempoKnown` so the question can be
asked properly. `musical` still gates the **bar lines**, which genuinely do
need all three.

**3. A locate backwards was a one-way door.** `MidiVoice::render` returned
early when the whole block was behind the pad's launch point. A pad launched
at bar 5 and then rewound to bar 1 went silent — not stopped, still playing,
still launched — until the project crawled back past bar 5. Any cycle that
began before a pad was launched did it every pass.

The loop is now **anchored** at the launch point rather than started there: it
repeats in both directions, so rewinding plays the same loop in the same
phase. Which is what "locked to the project's timeline" has to mean if it is
to mean anything. Only the whole-block-behind case wraps, so a pad launching
*part way through* the current block still starts at its own sample.

**4. Changing a pad's kind under it hung its notes.** `renderMidi` skips a
slot that is not a MIDI slot, so dropping a `.wav` on a playing `.mid` pad
left a voice that would never be rendered again, never reach its own
note-offs, and hang. Replacing a file is not one of the ways a note is allowed
to be left on. `loadSlot` now raises a flag when the *kind* changes and the
audio thread stops whichever machinery was running and unlaunches the pad, so
the next grid line brings it back on the right one.

Two things came out of this beyond the fixes:

* **a starved block now HOLDS rather than stops.** A rolling host that has not
  said where it is for one block is a resync, not a reason to put a hole in
  every pattern. The notes go on sounding and the pads carry on the moment the
  context returns; only a sustained run of them lets the notes go.
* **the invariant is re-asserted once a block.** A pad the processor believes
  is launched must have a voice that is playing. Anything that stops a voice
  without unlaunching it — a path added later, a case not thought of — now
  shows up as one missed bar rather than as a pattern that never comes back.

`MidiTests` §9 covers what is reachable without a host: the backwards locate,
a cycle running entirely before the launch point, the phase being kept across
it, and the negative control that a mid-block launch is still not wrapped.

### The bug a drum machine cannot show you

Reported as a mystery, and it is a good one: *"with the transport looping, a
drum machine is fine at the loop point, but Apple's AUMIDISynth goes silent —
though the MIDI appears to still be received."*

The two synths are the diagnosis. **A drum machine ignores note-offs**, and its
notes are short enough to end mid-bar anyway. A sustaining synth does not. So
whatever was wrong had to be about note-offs, and about notes that are still
held when the loop comes round — which is what a pad, a string patch or a bass
note is, and what a drum pattern never is.

It was this: **a note-off and its own re-trigger landed on the same sample.**

A note held to the bar line is cut off there and struck again at the start of
the next pass. Both events were emitted at the identical sample offset, in two
places — the pad's own loop boundary, and the flush when the host locates,
which is the transport loop point where it was heard.

Nothing in the list was *wrong*. The off is before the on, and a reader that
respects list order gets it right. But a great deal downstream does not: a
wrapper that sorts events by timestamp unstably, a host that merges two buses,
a synth that processes one timestamp's events in its own order. Any of them
can deliver the on first and the off second — and then the note is killed the
instant it starts, for ever, every pass.

The fix is what every sequencer does: separate them **in time** as well as in
order. `MidiVoice` remembers the last sample it emitted a note-off at for each
pitch in the block, and a note-on for that pitch is nudged one sample past it.
One sample is unambiguous and far too short to hear. It is the **on** that
moves, because a re-trigger a sample late is nothing and a note-off a sample
early would shorten the note it belongs to.

`MidiTests` §11 asserts the invariant directly — no note-on at or before a
note-off of the same pitch — at the pad's loop point and at a locate, with a
drum pattern as the control: it has no collisions to fix, and its downbeat is
still exactly on the beat.

The same commit closed the other way a synth can go silent while a drum
machine does not. `queueMidi` dropped events when the block's queue filled,
and **a dropped note-off is a note nobody can stop** — the voice that emitted
it has already decremented its own count, so nothing will ever send it again.
An off now displaces the most recently queued note-ON instead of being thrown
away. A missing note is a hole in one bar; a stuck one is silence a few voices
later.

### Notes stranded where the loop crosses its own anchor

The second half of the same report, and the half that was actually causing
it. The symptom that gave it away was not the silence but the **recovery**:
*stop the transport and it plays again.* A host sends a panic on stop, and a
panic frees notes somebody stranded.

**Nobody starts the transport and the pad at the same instant.** You roll the
transport, click a pad, and it comes in on the next bar line — so its anchor
is *after* the start of the cycle, and every cycle wrap lands *before* it.
That is not an edge case, it is what happens every single time.

A negative `from` — the project earlier than the pad's anchor — means two
completely different things that cannot be told apart from `from` alone:

* the pad is **launching part way through this very block**, which is the
  normal case on every launch, and it must start at its own sample;
* the project has **moved back**, and the loop (being anchored rather than
  started) should repeat backwards and play its own tail.

The old test was "is the whole block behind the anchor". True of a rewind by
more than a block; **false of a rewind by less** — and in that gap the pad
clamped to loop position zero and jumped there from wherever it was, *without
passing the loop boundary*. The note-offs waiting at that boundary were never
sent. One or two notes stranded on every cycle.

`mHaveLast` is the honest answer: it is false only on the first block after
`start()`, which is the only block that can be a launch. Everything else with
a negative `from` is a locate.

A drum machine frees its own voices when the sample ends and never notices. A
synth with a voice limit runs out and goes **completely silent** while the
MIDI goes on arriving — and comes back the moment the transport stops.

### How it was actually found

`tools/midi-trace.cpp` — it loads a real `.mid` through the real reader,
drives a real `MidiVoice` with a looping transport, and prints every event
with the sample it lands on. It asserts nothing and nothing runs it; it is
for the question a unit test is bad at.

The measurement that caught this was **held notes against the file's own
polyphony**. The reported file never has more than five notes sounding at
once. The trace showed **six** — and six is not a timing bug or a taste
question, it is a note that should have ended and did not. After the fix it
shows five, exactly.

It also showed the thing that explains why nobody had seen it before: with the
pad launched at bar 1 — the anchor on the cycle start — the count never
exceeds three, before or after. **The bug needs the pad to have been clicked
after the transport started**, which is why it was invisible to every test
written from the inside.

The trace has a `--launch` option for exactly that reason. It is the detail a
test written from the inside never thinks of.

### ppqPosition was a lie, and the log that should have existed sooner

Two things were wrong in every MIDI event this plug-in has ever sent.

**`Event::ppqPosition` was a flat zero.** It is documented as "position in
project time music", and zero is not a missing value — it is a *wrong* one,
saying every note happened at the very start of the project. A host has no
reason to consult it while the transport runs in a straight line; the sample
offset is enough. A **looping** transport is exactly when a host would look,
to work out where an event falls relative to the loop region — which is the
one condition under which this was reported to go wrong, and the only
condition: *"if the transport is not looping, the file plays forever."*

**`Event::kIsLive` was set.** That flag means "played live, directly from a
keyboard". These notes were read out of a file and placed on a grid. Saying
otherwise invites a host to treat them as unsequenced input, which among
other things is a reason to ignore the position above.

Both are now correct. Neither is a theory about the fault; both are simply
false statements the plug-in was making.

### Turning the log on

Three fixes for one reported fault were wrong in a row — each reasoned from
the code, each plausible, each shipped. That is the point at which guessing
again is the wrong thing to do. What was missing was never another theory. It
was **evidence of what the plug-in actually sends, from inside the host where
it goes wrong.**

**Create `~/p6-midi-log.txt` and restart the host.** Delete it to stop. The
file is its own switch *and* its own destination, and it is only ever opened
if it already exists, so a plug-in nobody asked to log never creates
anything. (`PROJECT6_MIDI_LOG=/some/path` also works, if the host is launched
from a shell.)

The environment variable was the first design and was nearly useless on
macOS: `export` reaches the processes that terminal starts, and a DAW is
started from the Dock, from Finder or from Spotlight, all of which inherit
launchd's environment instead. It was set correctly and never arrived —
**a diagnostic whose failure looks exactly like the thing it was meant to
diagnose**, which is the worst way for one to fail.

Every block is written to it: the
transport's context flags, its position, tempo and meter; every MIDI pad's
armed, launched, playing and loop state and where its playhead is; and every
event with its sample offset, pitch, channel, project position and whether
the host **accepted** it on each bus. Unset — which it is unless somebody
deliberately sets it — and it costs one null check.

The block header is written in `renderMidi` and not beside the events,
because **the block that matters most is the one with no events in it**. A
log that only recorded blocks that sent something would fall silent at
exactly the moment the plug-in did, and say nothing about why.

It writes from the audio thread, which is the one thing the rest of this
plug-in refuses to do. That is deliberate, and it is why it is off by
default: a diagnostic that queued its lines through a lock-free ring would be
a second thing that could be wrong, and the question is what the *first* one
is doing. Expect glitches while it is on.

### Eight event outputs, and the merged one that had to go

It was nine: a merged bus carrying every row, then one per row, so that a
host showing only the first event output could still reach all eight. Every
event was therefore **sent twice**.

A log taken inside Reaper settled it: **1202 notes handed over as 2404
events.** And every host that matters merges them anyway — Steinberg's own AU
wrapper ignores `Event::busIndex` completely and converts every event in the
list to MIDI on its single output, and on this evidence so does Reaper.

So the merge is what the **host** does, not something to do twice and hope.
Eight buses, **bus index is the row**, one event each. In a host that flattens
them you get every note once with its row on the channel — which is exactly
what the merged bus was for, obtained by not fighting for it. In a host with
real per-bus routing the rows stay separate, as before.

The cost, stated plainly: a host that exposes only the *first* event output
now reaches row A alone. That is the price of not sending everything twice to
every host that does not.

### The reader

`parseMidiFile` handles what a loop library and a DAW export actually contain:
formats 0 and 1 (merged onto one timeline, each track walked from its own
zero), running status, note-on-with-velocity-zero, SysEx and meta events
skipped by their own declared lengths, and a note still held when the track
ends. Note-offs pair with the **oldest** matching note-on, so one pitch struck
twice is two notes rather than one long one.

Refused rather than half-read: **SMPTE division** (the file is timed in frames
and seconds, with no musical grid to loop to), **format 2** (independent
sequences, not one song — merging them would be inventing a piece of music),
and anything past the caps.

MIDI is **big-endian** and WAV is little-endian, which is the classic first bug
in an SMF reader and does not announce itself: a division of 480 read the
wrong way is 61440, and every note lands in the first hundredth of a bar with
no error anywhere. `MidiTests` §1 has that as a negative control.

`SampleStatus` grew `NotMidi` and `NoNotes` rather than the panel growing a
second code path: a pad shows one tooltip, and the person reading it does not
care which of two enums the answer came out of.

### On the panel

A MIDI pad's well is a cooler, slightly violet grey — colour, because the well
is the biggest thing on a cell and colour is what the eye finds across
sixty-four of them, where a badge or a letter would be one more small thing to
read. Live and waiting keep their own colours: three more would be saying the
same two things twice.

Its **level bar dims** and its **fit box dims**, because neither does anything
to notes. Both are still real parameters, still saved, and both start working
the moment an audio file lands in that pad — what they must not do is look
live while doing nothing. The bar is dimmed rather than hidden: a strip that
changed shape under a MIDI pad would be a worse way to say the same thing.

The tooltip says how many notes, how long the file is, how long it *loops* for
once run out to the bar, and which channel it leaves on.

And each row fader carries a **`MIDI ch n` label above it**. A row is a bus,
and its channel and its level are two facts about the same bus — so they
belong together rather than the channel being something to look up in these
notes. It earns its place most in a host that shows only the merged MIDI
output, where the channel *is* the routing and there is nothing else on screen
to say which row is which.

The number comes from `midiChannelNumberForRow`, which is **derived from**
`midiChannelForRow` — the one the processor stamps on its events. Two
functions for one fact, because VST3 counts channels from zero and people
count them from one, and sharing a single number would mean one of the two
callers is wrong on its own terms. It is what stops the label reading
"channel 3" while the notes go out on 4.

The label sits inside the cell the fader already had: the fader moves down to
make room and the pair is centred where the fader alone used to be, so the
grid beside it is untouched. A `static_assert` is what fails if either height
changes enough to stop fitting.

## 14. Loop, or play once and stop

A switch on every pad. **Default loop** — what this instrument is for, and what
every pad did before there was a choice, so a project saved before the
parameter existed reopens behaving exactly as it did.

### One click, one hit

A one-shot that has played **turns its own trigger off**. The pad goes dark and
the next click is another hit, rather than the "off" half of a toggle that has
to be clicked twice. The processor writes the trigger back through
`data.outputParameterChanges`, which is how a plug-in reports a control it
moved itself — so a host recording automation sees the pad turn itself off.

Not through `publishOne`. That only sends what *moved*, and would therefore go
silent the second time a pad finished at the same value. This is an **event**,
not a published state, and the difference is a pad that fires once and then
never again.

Everything the processor believes about the pad goes back to stopped **before**
the host is told — `mLaunched`, `mArmed`, and its own copy of the parameter —
so nothing can see it half way between the two states and start it again.

### Where the end is

* **Audio:** the moment the playhead wraps. `TimeStretcher::takeWrapped` reads
  and clears, so the wrap is acted on once and cannot be seen twice. It watches
  `mIdeal`, the *musical* position — in the pitch-preserving mode the read head
  wraps whenever it feels like it, and it is the music that has finished, not
  the reading. `StretchTests` §8 has that as its negative control: four
  thousand samples at half speed is **two** passes, not the four the read head
  did.
* **MIDI:** one pass of the loop, which is the file **run out to the end of its
  bar**. Ending at the last note instead would cut the silence the file was
  written with, and a pattern whose last hit is on beat three would end early.

The audio stop is the ordinary **fade-out, not a cut**. The last sample of a
file is no more likely to be at zero than the first, and a one-shot that
clicked when it finished would be worse than one that simply looped.

Switching a running pad to one-shot lets it **finish the pass it is on** rather
than cutting it. The setting is read once a block, like the launch division and
the tempo fit, and acted on at the next end of the file.

### What it cost

The cell went 96 → 112 and the window 943 → 1071. Every pixel past the
original 84 has gone to the **strip**, not the name: it now carries a level
bar, a launch box, a fit box and this switch, and a level bar under about
thirty pixels is a bar you cannot set. The switch is **thirteen pixels and one
character** — `L` or `1` — because a fourth three-character box would have cost
another twenty pixels on every cell and a hundred and sixty on the panel.

One-shot is the state that is **marked**: loop is the default and sixty-three
pads out of sixty-four will be on it, so `L` is drawn dim and the `1` is what
the eye should find.

Both mouse buttons do the same thing on it. The two boxes beside it step
forwards on a left click and backwards on a right one because they have three
and four choices; with two there is nowhere to go except back, and a
right-click that appeared to do something different would be a lie about a
control this small.

### One thing deliberately not done

A `.wav` whose ACID chunk carries the **one-shot flag** does *not* default this
switch to one-shot. That flag is about **tempo fitting** — it means "do not
stretch this" — and quietly making it mean something else as well would be the
kind of helpfulness nobody asked for and nobody could find. The default is
Loop, as specified, for every file.

Stream **version 7**: a fifth value block, through the same
`writeValueBlock`/`readValueBlock` the other four use.

## 15. The SDK

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
