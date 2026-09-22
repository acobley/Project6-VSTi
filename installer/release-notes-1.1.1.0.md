# Project6 1.1.1.0

An **8 × 8 bank of looping sample pads, launched on the bar**, for macOS, as a
**VST3** and an **Audio Unit**.

## What changed since 1.1.0.0

- **The maker is now "AE Cobley"**, in both formats — it was "A. E. Cobley".
  REAPER files an Audio Unit's presets under its name cut off at the first
  full stop, so every A. E. Cobley Audio Unit shared one REAPER preset list
  and showed the others' presets, which did nothing when chosen. The
  plug-in's identity codes are unchanged, so **projects saved with earlier
  versions still open** with Project6 in place.
- **The version is reported correctly.** Earlier builds told hosts they were
  1.0.0.1 (VST3) and 1.0.0 (Audio Unit) whatever the release; this one says
  1.1.1.0.

Nothing in the sound changed.

**After upgrading**, some things stay where earlier versions left them:

- Presets **you** saved in a host that files them by maker are in
  `~/Library/Audio/Presets/A. E. Cobley/Project6`. Move them to
  `~/Library/Audio/Presets/AE Cobley/Project6` to see them again.
- In REAPER, AU presets you saved earlier stay in REAPER's old shared list
  and will not appear under the new name. The VST3 is unaffected.
- Some hosts list the plug-in under its maker; look under **AE Cobley**.

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

Download `Project6-1.1.1.0.pkg` and open it. The installer offers the two
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
SHA-256: 7a7b732e7b0f69f4b1ea6ff460ee8525b1b85b64c614b68e5c86f7b310e341b7
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
