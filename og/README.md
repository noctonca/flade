# FLADE (2000): the original

This folder preserves the **original FLADE** ("FLA DEcoder"), an early community
player for the **FLA** video format used by *Little Big Adventure* / *Relentless:
Twinsen's Adventure* (Adeline, 1994). It is kept here, unmodified, next to the
modern reimplementation in the parent directory, as a small piece of LBA
preservation.

These files are the authors' original work, included verbatim. Nothing here was
written by this project.

## History

- **Authors:** CoLdBLooD & Darkstealth
- **Source release:** v0.99.3 BETA, 14 November 2000
- **Later binary:** v0.99.6 BETA, 19 November 2000
- **Released on:** the MagicBall Network forum:
  <https://forum.magicball.net/t/flade-fla-video-player-source-release/189>

FLADE was one of the earliest public reverse-engineerings of Adeline's FLA
format, and predates the TwinEngine project by roughly two years. The source was
shared freely "in the hope someone makes use of it," with one wish attached:

> I would like to see a Win32 version of flade since I can't code for Win, yet.

In 2024 the archives (`flade993.zip`, `flade996.zip`, `flasource.zip`) were
recovered and re-posted to the same thread by **xesf**, which is how this source
survives today.

## What it did

A DOS utility (VGA mode 13h, Borland/Turbo Pascal with inline x86 assembler)
that could:

- play FLA videos full-screen, and
- dump them frame-by-frame to **PCX** or **BMP** for re-encoding (the readme
  suggests Smacker).

It deliberately did **not** play sound. The readme states the audio "is not
located within the FLA files," and the decoder bears this out: the per-frame
opcode `3` (the sample cue that points into `FLASAMP.HQR`) is parsed and then
skipped (`3 : ;` in `FLADEUNI.PAS`). The pointer was seen; it just wasn't
followed. Completing that path is the main thing the modern player adds.

## Files

| File | What it is |
|---|---|
| `FLADE.EXE` | the compiled DOS player (v0.99.6 BETA) |
| `flade.pas` | program entry point: menu, path entry, dump options |
| `FLADEUNI.PAS` | the FLA decoder: header parse, frame loop, key-frame (`rip_de`) and delta (`rip_de2`) painters |
| `GRAX_FLA.PAS` | VGA mode-13h graphics: virtual screens, palette, PCX/BMP load+save, sprite blits |
| `int8h_2.pas` | custom timer (INT 8h) for frame pacing |
| `readme.txt` | the original release notes |

The `*.Zone.Identifier` files are zero-byte Windows "downloaded from the
internet" markers and can be ignored.

## How the format held up

The original decode has been cross-checked against two independent sources (the
Adeline engine source and the GPL TwinEngine reimplementation) and agrees with
both in every detail that matters: the opcode numbers (`1` palette, `3` sample,
`6` delta, `8` key frame), the run-length conventions in the key-frame and delta
painters, and the 6-bit VGA palette (`setvgapal` shifts each stored byte right by
two to feed the DAC). For a from-scratch reverse-engineering done in 2000 with no
reference but the bytes, it got the format right.

## Licence

From the source headers, preserved as written:

> This source code may be used and distributed freely. A modified version of the
> source code or any part of it and/or its compiled binaries may only be
> distributed in any form if the authors are notified about it and credit is
> given to them with the modified distribution(s).

The modern player in the parent directory is an independent implementation. It
shares this name as a deliberate homage and reuses none of this code, so that
clause does not bind it, but the lineage is gratefully acknowledged all the same.
