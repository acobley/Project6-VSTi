# Project6 1.2.0.0

An **8 × 8 bank of looping sample pads, launched on the bar**, for macOS, as a
**VST3** and an **Audio Unit**.

## What changed since 1.1.1.0

- **Projects travel.** Each pad's file is now saved *inside* the project, so a
  project sent to someone else opens with every pad playing, whether or not
  they have your files. The copy in the project is the one that plays, so a
  project always sounds as it was saved; to pick up a sample you have edited
  since, drop it on the pad again. Moving, copying or swapping pads takes the
  saved copy with it.
- **Factory samples folder.** Files in `/Users/Shared/Project6/Samples` — the
  same path on every Mac — are *not* copied into projects, because every copy
  of Project6 can have them. The installer fills that folder when a release
  ships factory samples.
- **Clear a pad**: right-click (or Control-click) it and choose **Clear pad**.
  The file leaves the project too.
- **"File not found"** is now told apart from "could not be read" on a pad
  whose file has gone.

**Projects saved with earlier versions still open**, reading their files from
disk as before; save them once in 1.2.0.0 and their files are saved in too.
Projects saved with 1.2.0.0 open in earlier versions, which ignore the saved
files and read from disk.

**Projects are larger** by the size of the files on their pads. In REAPER,
which saves the plug-in's state at every undo step, a project with many long
WAVs may make editing feel slower.

## What it does

Drag a `.wav` onto a slot and click it to arm it; it comes in when the
transport crosses the next grid line, and stops on the line after that. Each
pad picks its own grid — `1/1`, `1/2`, `1/4` or `1/8` of a bar, so everything
nests and a 1/8 pad and a 1/1 pad start together on the downbeat. A box above
each column launches that whole column at once.

A slot can hold a `.mid` instead, looping on the same grid and playing out of
its row's MIDI output — row A on channel 1 through row H on channel 8 — with a
transpose of up to two octaves in place of a level bar.

Each row sums through its own fader, and there are **nine output buses**: the
main stereo mix plus a per-row direct out, tapped *before* the row fader, so
the fader balances the mix without changing what leaves for the desk.

Which pads were armed is saved with the project, and comes back armed. A pad
whose file has gone missing since it was saved comes back dark rather than lit
and silent.

## Installing

Download `Project6-1.2.0.0.pkg` and open it. The installer offers the
formats separately:

    /Library/Audio/Plug-Ins/VST3/Project6.vst3
    /Library/Audio/Plug-Ins/Components/Project6.component

Quit your DAW first — a running host holds the old copy open. Signed with a
Developer ID and notarised by Apple.

Requires macOS 10.13 or later.

## Two host quirks, neither of them this plug-in

* **In REAPER, use the VST3.** REAPER never asks an Audio Unit whether it has
  MIDI output and never sets a callback for one, so a MIDI pad's notes have
  nowhere to go there. The plug-in emits them; the host is not listening. The
  VST3 works in REAPER and the AU works in Logic.
* **Apple's AUMIDISynth goes silent at a transport loop point** and stays
  silent until the transport is stopped, while the MIDI goes on arriving. Use
  a different synth if you loop.

Both were traced property by property rather than guessed at;
`PORTING-NOTES.md` §0 and §0a have the investigations.

## Uninstalling

A `.pkg` never will, so:

```sh
sudo rm -rf /Library/Audio/Plug-Ins/VST3/Project6.vst3
sudo rm -rf /Library/Audio/Plug-Ins/Components/Project6.component
```

## Checksum

```
SHA-256: <paste from: shasum -a 256 installer/Project6-1.2.0.0.pkg>
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
