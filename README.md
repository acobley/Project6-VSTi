# Project6

A VST3 and Audio Unit **instrument** for macOS: an **8 × 8 bank of looping
sample pads**.

Drag a `.wav` onto a slot from the Finder and it loads. Click the slot and it
loops; click it again and it stops. The filename shows in white, the slot
lights while it plays, and the path is saved with the project. Every pad is
also a host parameter, so a DAW can automate, record and undo it.

Underneath that it is still the shell a bigger instrument goes inside — buses,
parameter plumbing, event handling, state and four test suites, with the traps
already handled. There is no note handling yet: the event input exists and
consumes events, but nothing is pitched.

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

## What is where

| Path | |
|---|---|
| `source/Project6IDs.h` | the two class UIDs, and the message rule |
| `source/Project6Dsp.{h,cpp}` | the audio line: 64 looping voices and the output trim — **no SDK header may enter these** |
| `source/Project6Sample.{h,cpp}` | the WAV reader and the decoded buffers — **SDK-free too** |
| `source/Project6Slots.{h,cpp}` | the 8 × 8 slot bank and its path rules — **and these** |
| `source/Project6SlotState.{h,cpp}` | the slot block of the state stream, written and read by one pair of functions |
| `source/Project6Params.{h,cpp}` | the parameter table: normalised, plain and internal ranges |
| `source/Project6Processor.{h,cpp}` | `AudioEffect` — buses, events, state, the authoritative slot bank |
| `source/Project6Controller.{h,cpp}` | `EditControllerEx1` — the host's parameter list, the panel's slot bank |
| `source/Project6Controls.{h,cpp}` | the control set, lifted from VocalFilter/SpyBand |
| `source/Project6Display.{h,cpp}` | the panel display — an empty canvas with a working grid |
| `source/Project6SlotView.{h,cpp}` | one slot: the drop target and how it draws a filename |
| `source/Project6Editor.{h,cpp}` | the panel |
| `source/Project6Entry.cpp` | the factory |
| `tests/` | `DspTests.cpp`, `SlotTests.cpp` and `WavTests.cpp`, SDK-free; `ParamsTests.cpp`, headers only |
| `tools/check-editor.py` | the editor guard |
| `resource/au-info.plist` | the AU's four-character identity and bus layouts |

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
