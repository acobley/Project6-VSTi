# Project6 1.1.0.0

An **8 × 8 bank of looping sample pads, launched on the bar**, for macOS, as a
**VST3** and an **Audio Unit**.

## New in 1.1.0.0

**Rest the pointer on a pad and you see what is in it**, behind the main
display: a **waveform** for a `.wav`, a **piano roll** for a `.mid`. Dropping
a file on a pad shows it too, at once.

Pointing costs nothing and makes no sound, so looking at what a pad holds is
no longer a matter of playing it. It takes a moment's dwell rather than the
crossing of a border, so sweeping across the bank leaves the display alone
rather than strobing through eight pads on the way past.

The piano roll scales to **the range of pitches the file actually uses**, not
to a full keyboard: a two-octave bass line and a two-octave melody both fill
the rectangle, and the notes read in relation to each other. Long patterns are
thinned by taking every Nth note rather than the first few hundred, so the
shape of the whole thing survives.

Both summaries are computed **once, when the file loads**, and never on the
audio thread. Neither is in the signal path at all.

This release is **universal (arm64 + x86_64)**, which is what the installer
has always advertised.

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

Download `Project6-1.1.0.0.pkg` and open it. The installer offers the two
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
<paste the sha256 here>
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
