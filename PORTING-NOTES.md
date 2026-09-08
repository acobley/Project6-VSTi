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

# 1. All five test suites, from a clean build.
c++ -std=c++17 -O2 -Wall -Isource tests/DspTests.cpp \
    source/Project6Dsp.cpp source/Project6Sample.cpp \
    -o /tmp/dsptests && /tmp/dsptests

c++ -std=c++17 -O2 -Wall -Isource \
    tests/SlotTests.cpp source/Project6Slots.cpp -o /tmp/slottests && /tmp/slottests

c++ -std=c++17 -O2 -Wall -Isource \
    tests/WavTests.cpp source/Project6Sample.cpp -o /tmp/wavtests && /tmp/wavtests

c++ -std=c++17 -O2 -Wall -Isource tests/TransportTests.cpp \
    source/Project6Transport.cpp -o /tmp/transporttests && /tmp/transporttests

SDK=~/DXi-DEv/VocalFilter-VSTi/external/vst3sdk        # any existing checkout
c++ -std=c++17 -O2 -Wall -Isource -I$SDK \
    tests/ParamsTests.cpp source/Project6Params.cpp source/Project6Dsp.cpp \
    source/Project6Sample.cpp -o /tmp/paramstests && /tmp/paramstests

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

Results, at the transport commit: **all five suites all-pass**, all thirteen
translation units produced object files with no errors, all 50 undefined
`Project6::` symbols resolved within the set, and `check-editor` reported ok.
The panel's dimensions are checked by `static_assert` rather than by eye —
735 × 481, with the grid meeting both margins exactly.

Re-run all six before every commit. Adding a source file also means re-running
`./setup-xcode.sh --no-open` before the next Xcode build, or the project
compiles the old file list.

**What the tests cannot reach**, and what the first real listen is for: the
drag-and-drop itself (a platform drag package is not something a unit test can
manufacture), whether the panel looks right, whether five milliseconds is in
fact enough declick on real material, and whether a host's `ProcessContext`
says what this code assumes it says — `TransportTests` proves the arithmetic
is right about the numbers it is given, not that the numbers arrive. That last
one is the first thing to check in a real DAW: if the display reads
"No transport" or the playhead does not move, the flags in
`getProcessContextRequirements` are where to look.

Everything the tests *do* reach — every WAV format, the loop wrap, the rate
conversion, the envelope, the bar arithmetic in five time signatures, the
parameter blocks' bounds — is asserted rather than assumed.

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

## 11. The SDK

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
