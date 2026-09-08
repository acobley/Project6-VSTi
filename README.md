# Project6

A VST3 and Audio Unit **instrument** for macOS. Nothing is synthesised yet:
this is the empty, validating shell that a real instrument goes inside —
buses, parameter plumbing, event handling, state, an editor and two test
suites, with the traps already handled.

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
    -o /tmp/dsptests && /tmp/dsptests
```

`tests/ParamsTests.cpp` needs the SDK's **headers** only, `vsttypes.h` being
typedefs:

```sh
c++ -std=c++17 -O2 -Isource -Iexternal/vst3sdk \
    tests/ParamsTests.cpp source/Project6Params.cpp source/Project6Dsp.cpp \
    -o /tmp/paramstests && /tmp/paramstests
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
| `source/Project6Dsp.{h,cpp}` | the audio line — **no SDK header may enter these** |
| `source/Project6Params.{h,cpp}` | the parameter table: normalised, plain and internal ranges |
| `source/Project6Processor.{h,cpp}` | `AudioEffect` — buses, events, state |
| `source/Project6Controller.{h,cpp}` | `EditControllerEx1` — the host's parameter list |
| `source/Project6Controls.{h,cpp}` | the control set, lifted from VocalFilter/SpyBand |
| `source/Project6Display.{h,cpp}` | the panel display — an empty canvas with a working grid |
| `source/Project6Editor.{h,cpp}` | the panel |
| `source/Project6Entry.cpp` | the factory |
| `tests/` | `DspTests.cpp`, SDK-free; `ParamsTests.cpp`, headers only |
| `tools/check-editor.py` | the editor guard |
| `resource/au-info.plist` | the AU's four-character identity and bus layouts |

## The two rules worth repeating

1. **The DSP stays SDK-free.** Anything the editor displays that the DSP also
   computes comes from one shared function in `Project6Dsp.h` that both call.
2. **Append parameters, never insert**, and never change a class UID or a
   four-character code once a build has shipped.
