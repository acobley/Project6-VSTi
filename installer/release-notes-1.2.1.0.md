# Project6 1.2.1.0

An **8 × 8 bank of looping sample pads, launched on the bar**, for macOS, as a
**VST3** and an **Audio Unit**.

## What changed since 1.2.0.0

- **Fast playback no longer aliases.** When a pad reads its file faster than
  one frame per output sample (Varispeed above 1×, or a file recorded at a
  higher rate than the session's), Project6 used plain linear interpolation,
  which lets high frequencies fold back down. At 2× an 18 kHz hi-hat
  component came back at 12 kHz at full level. Those pads now go through a
  **band-limited resampler**, and the same case measures −79 dB. At 1× or
  slower the old read is untouched, and renders are byte-identical to
  1.2.0.0.
- **A fader at the bottom no longer costs CPU.** A row, pad or output fader
  pulled right down used to settle, about 15 seconds later, on a tiny
  "denormal" number rather than zero, and on Intel Macs every sample then
  took the slow path for good: eight muted rows went from about 1 ms of CPU
  per second of audio to 30–37 ms. The level now lands on exactly zero.
- **Flush-to-zero on the audio thread.** Each processing block now runs with
  the CPU's flush-to-zero mode on, and puts the host's setting back
  afterwards, so a signal decaying towards silence can never slow the
  plug-in down.

**What to expect.** Pads playing faster than 1× sound cleaner at the top
and **cost more CPU**: 64 pads at 1.2× measured 48 ms per second of audio
against 16 ms before. Nothing else changes; projects and presets are
exactly as in 1.2.0.0, and files embedded in projects still travel with
them.

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

Download `Project6-1.2.1.0.pkg` and open it. The installer offers the
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
SHA-256: <paste from: shasum -a 256 installer/Project6-1.2.1.0.pkg>
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
