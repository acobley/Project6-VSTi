# Project6

A VST3 and Audio Unit **instrument** for macOS: an **8 × 8 bank of looping
sample pads, launched on the bar**.

Drag a `.wav` onto a slot from the Finder and it loads. Click the slot to arm
it; it starts **when the transport crosses the next grid line**. Click again
and it stops on the line after that. Nothing sounds while the transport is
stopped, and a pad left armed comes back in when it rolls again.

**Each pad chooses its own grid**: `1/1`, `1/2`, `1/4` or `1/8` — fractions of
a bar, so every division nests inside the bar and a 1/8 pad and a 1/1 pad
launch together on the downbeat. The little box beside each level bar sets it;
click to step forward, right-click to step back.

**A box above each column launches the whole column** — every loaded pad in
it, together on the next bar line, which is the only way eight loops can start
in time with each other. Press it again to stop them.

**A level bar under each pad** sets that slot's own volume, −40 to +12 dB.
Drag it the way the VocalFilter sliders drag — relative, not jump-to-pointer —
and the pad above shows the value in decibels while you do.

**A slot can hold a `.mid` instead of a `.wav`.** It loops the same way, on
the same grid, and plays out of that row's **MIDI output** — row A on channel
1 through row H on channel 8 — for a synth in the host to answer. A file that
does not end on a bar line has its loop run out to the end of the bar, so a
pattern repeats in time rather than drifting. Where an audio pad has its level
bar, a MIDI pad has a **transpose**: up to two octaves either way, every note
moved by the same interval — a pattern moved, not a key change. Double-click
it to go back to the file's own pitch.

**Apple's AUMIDISynth and a looping transport: use a different synth.**
AUMIDISynth goes completely silent at a transport loop point and stays silent
until the transport is stopped — while the MIDI goes on arriving. It is the
synth, not this plug-in and not the host: a MIDI monitor in the chain
immediately in front of it shows a clean, balanced note stream continuing
across the loop point with nothing injected by the host, and another synth
fed the identical stream in the identical slot plays on through it.
`PORTING-NOTES.md` §0 has the whole investigation.

**MIDI out and Reaper: use the VST3.** Reaper never asks an Audio Unit
whether it has MIDI output and never sets a callback for one, so a MIDI pad's
notes have nowhere to go there — the plug-in emits them and the host is not
listening. This is Reaper's side, not the plug-in's: traced property by
property, and written up in `PORTING-NOTES.md` §0a. The VST3 works in Reaper
and the AU works in Logic.

**Each row sums to a fader on the right.** The eight pads of a row go through
their own levels, sum, pass that row's level, and the eight rows sum into the
output trim.

**Nine output buses**: the main stereo mix, plus one stereo aux bus per row
carrying that row's **direct out — tapped before the row fader**, so the fader
balances the mix without touching what leaves for the desk.
`docs/routing.png` is the whole path drawn out, and it is generated from the
headers, so it cannot quietly disagree with the code.

Levels are saved with the project and automatable like everything else, and
so is **which pads are armed** — reopen a project and the same cells are lit
and waiting, coming in together on the next bar line when you roll the
transport. A pad whose file has gone missing since it was saved comes back
dark rather than lit and silent.

An armed slot glows amber while it waits and red while it plays, so the wait
is visible rather than mysterious, and **a bar along the bottom of a playing
pad shows how far through its file it is**. A column box fills in proportion
to how much of its column is sounding. The display beside the pads is one bar
wide, ruled into beats, with a playhead showing how long that wait has left.

Every pad is also a host parameter, so a DAW can automate, record and undo it.
Underneath, it is still the shell a bigger instrument goes inside — buses,
parameter plumbing, event handling, state and seven test suites, with the
traps already handled. There is no *synthesis* yet: a MIDI pad emits notes for
something else to play, and the event input consumes what arrives without
sounding it.

* **`PORTING-NOTES.md`** — the decisions, the permanent identity values, what
  was measured, what was deliberately left out and the trap behind each. Read
  it before changing anything structural.
* **`PORTING-GUIDE.md`** — how to get a DXi's DSP, parameters and dialog
  across. Came with the template; read it before writing a processing loop.
* **`PORT-CHECKLIST.md`** — the tickable working copy of that guide.

## Build

```sh
./setup-xcode.sh
```

The first configure clones the VST3 SDK into `external/` (~250 MB, ignored by
git) and Apple's AudioUnitSDK beside it, then generates and opens an Xcode
project. `./setup-xcode.sh --no-open` skips the opening.

Both bundles are ad-hoc signed, which needs no developer certificate. To sign
properly:

```sh
cmake -B build -G Xcode -DPORT_CODE_SIGN_IDENTITY="Developer ID Application: …"
```

Then run the SDK validator, and:

```sh
auval -v aumu Prj6 AECo
```

**The Audio Unit side of the nine buses is the thing to check there.**
Steinberg's wrapper builds one AU element per VST3 bus from `getBusCount`, so
`SupportedNumChannels` in `resource/au-info.plist` describes only the main
element and is deliberately still `0 in / 2 out`. Whether the eight extra
elements appear correctly cannot be established from a Linux VM.

**Adding a source file later regenerates the Xcode project mid-build and
compiles the old file list.** The symptom is *"Bundle does not export the
required 'GetPluginFactory' function"*. Re-run `./setup-xcode.sh --no-open`
and build again.

## Tests

Neither suite needs the plug-in built, and both should pass before every
commit.

`tests/DspTests.cpp` is **SDK-free** — `Project6Dsp.{h,cpp}` include no
Steinberg header, on purpose — so it runs anywhere:

```sh
c++ -std=c++17 -O2 -Isource tests/DspTests.cpp source/Project6Dsp.cpp \
    source/Project6Sample.cpp -o /tmp/dsptests && /tmp/dsptests
```

`tests/SlotTests.cpp` is SDK-free too — it covers which paths the slots
accept, what a slot is called on screen, and that the bank refuses a bad
index rather than clamping it:

```sh
c++ -std=c++17 -O2 -Isource tests/SlotTests.cpp source/Project6Slots.cpp \
    -o /tmp/slottests && /tmp/slottests
```

`tests/WavTests.cpp` is SDK-free *and* disk-free — every file it reads is
built byte by byte in memory, which is how a truncated chunk, an odd-length
metadata chunk and a four-channel file get covered without anyone first
finding one:

```sh
c++ -std=c++17 -O2 -Isource tests/WavTests.cpp source/Project6Sample.cpp \
    -o /tmp/wavtests && /tmp/wavtests
```

`tests/TransportTests.cpp` is SDK-free too, and is the one to run first after
touching anything about launching: every way of getting bar detection wrong is
silent.

```sh
c++ -std=c++17 -O2 -Isource tests/TransportTests.cpp \
    source/Project6Transport.cpp -o /tmp/transporttests && /tmp/transporttests
```

`tests/ParamsTests.cpp` needs the SDK's **headers** only, `vsttypes.h` being
typedefs:

```sh
c++ -std=c++17 -O2 -Isource -Iexternal/vst3sdk \
    tests/ParamsTests.cpp source/Project6Params.cpp source/Project6Dsp.cpp \
    source/Project6Sample.cpp -o /tmp/paramstests && /tmp/paramstests
```

`tools/check-editor.py` guards one editor invariant no runtime test can reach:
`setValueNormalized` may appear in `Project6Editor.cpp` only inside
`showValue`, which pairs it with `invalid()`.

```sh
python3 tools/check-editor.py
```

`tools/render-routing.py` redraws `docs/routing.png`. **Every number on it is
parsed out of `Project6Dsp.h` and `Project6Slots.h`** and the script fails
loudly rather than guessing, so a diagram that disagrees with the code is one
that has not been regenerated. Re-run it whenever a gain moves:

```sh
python3 tools/render-routing.py
```

## What is where

| Path | |
|---|---|
| `source/Project6IDs.h` | the two class UIDs, and the message rule |
| `source/Project6Dsp.{h,cpp}` | the audio line: 64 looping voices and the output trim — **no SDK header may enter these** |
| `source/Project6Sample.{h,cpp}` | the WAV reader and the decoded buffers — **SDK-free too** |
| `source/Project6Midi.{h,cpp}` | the standard MIDI file reader and the note playhead — **and these** |
| `source/Project6Stretch.{h,cpp}` | fitting a loop to the project's tempo, both ways — **and these** |
| `source/Project6Slots.{h,cpp}` | the 8 × 8 slot bank and its path rules — **and these** |
| `source/Project6Transport.{h,cpp}` | the bar clock: where a bar line falls in a block — **and these** |
| `source/Project6SlotState.{h,cpp}` | the slot block of the state stream, written and read by one pair of functions |
| `source/Project6Params.{h,cpp}` | the parameter table: normalised, plain and internal ranges |
| `source/Project6Processor.{h,cpp}` | `AudioEffect` — buses, events, state, the authoritative slot bank |
| `source/Project6Controller.{h,cpp}` | `EditControllerEx1` — the host's parameter list, the panel's slot bank |
| `source/Project6Controls.{h,cpp}` | the control set, lifted from VocalFilter/SpyBand |
| `source/Project6Display.{h,cpp}` | the panel display — an empty canvas with a working grid |
| `source/Project6SlotView.{h,cpp}` | the grid's own controls: a pad, its level bar, and the launch box above each column |
| `source/Project6Editor.{h,cpp}` | the panel |
| `source/Project6Entry.cpp` | the factory |
| `tests/` | `DspTests`, `SlotTests`, `WavTests`, `TransportTests`, `StretchTests` and `MidiTests`, SDK-free; `ParamsTests`, headers only |
| `tools/check-editor.py` | the editor guard |
| `tools/render-routing.py` | draws `docs/routing.png` from the headers |
| `docs/routing.png` | the signal path: pad → slot level → row bus → row level → mix → trim |
| `resource/au-info.plist` | the AU's four-character identity and bus layouts |
| `LICENSE` | CC BY-SA 4.0, and what it does not cover |

## The two rules worth repeating

1. **The DSP stays SDK-free.** Anything the editor displays that the DSP also
   computes comes from one shared function in `Project6Dsp.h` that both call.
   The same goes for the slots: what counts as a loadable file, and what a
   slot is called on screen, are answered in `Project6Slots.h` and nowhere
   else.
2. **Append parameters, never insert**, and never change a class UID or a
   four-character code once a build has shipped. The state stream is appended
   to the same way — the slot block sits after everything a version 1 stream
   held.
3. **The panel never sets its own text.** A slot's filename comes back through
   the controller, and a control's position through `setParamNormalized`. (A
   `CControl` does set its own *value* on a click — it has to — but nothing
   else does.) A view that also updated itself would be right in one window
   and right by luck in a second.
4. **The audio thread never allocates, opens a file, or frees one.** Samples
   are loaded on the UI thread, published to the DSP as a bare pointer, and
   the buffer they replace is retired until the block counter proves no block
   can still be reading it. `PORTING-NOTES.md` §9 has the whole handoff.
5. **A click arms; the bar line launches.** What was asked for and what is
   sounding are two different facts, and the panel shows both — see
   `PORTING-NOTES.md` §10. If you change anything about that, run
   `TransportTests` first: bar detection fails silently.

---

Copyright 2026 A. E. Cobley. Licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) — see
[`LICENSE`](LICENSE). Credit it, and share anything you build on it under the
same terms.

The Steinberg VST3 SDK and VSTGUI are not covered by that: they are fetched
into `external/` at configure time and carry their own licence terms.

VST is a trademark of Steinberg Media Technologies GmbH.
