# flade

A small standalone player for **Adeline FLA movies**, the full-motion cutscene
format used by *Little Big Adventure 1* (1994), rendered with **SDL3** for
video and audio.

It decodes the FLA stream (palette / key-frame RLE / delta-frame patches),
plays the VOC samples from `FLASAMP.HQR`, and can read everything straight out
of a raw LBA1 CD image, with no extraction step needed.

## Building

Needs a C11 compiler, CMake ≥ 3.16, and SDL3.

```bash
cmake -B build
cmake --build build
```

If SDL3 isn't installed system-wide, point CMake at it:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/SDL3
```

## Playing

From a raw LBA1 CD image (e.g. the retail `LBA.DOT`):

```bash
# list the movies inside the image
./build/flade --cd /path/to/LBA.DOT --list

# play one by name
./build/flade --cd /path/to/LBA.DOT INTROD
```

From a loose `.fla` file (with `FLASAMP.HQR` alongside it, or via `--flasamp`):

```bash
./build/flade INTROD.FLA
./build/flade INTROD.FLA --flasamp /path/to/FLASAMP.HQR
```

Options: `--scale N` (window size), `--no-audio`, `--volume 0..1`.
Keys: `Esc`/`Q` quit, `Space`/`Enter` skip, `F` fullscreen.

## How it works

| Module | Responsibility |
|---|---|
| `iso9660.c` | read a file/list a dir from a raw (2352) or cooked (2048) CD image |
| `hqr.c` | HQR archive table + LZSS (`ExpandLZ`) expansion |
| `voc.c` | Creative Voice File → signed-16 mono PCM |
| `fla.c` | FLA header + per-frame opcode decode into an 8-bit indexed picture |
| `audio.c` | SDL3 software mixer (resamples each VOC to the device rate) |
| `main.c` | input source, window/texture, play loop, sample cues, scene fades |

### FLA format, briefly

```
header:  "V1.3"  numFrames:u32  speed:u8  var1:u8  xsize:u16  ysize:u16
         samplesInFla:u16  pad:u16   then samplesInFla x (num:u16, off:u16)
frame:   numOps:u8  dummy:u8  blockSize:u32   then blockSize bytes of opcodes
opcode:  type:u8  pad:u8  size:u16  payload[size]
```

Opcode types (after the engine's `type-1` normalisation): `0` load palette,
`1` fade, `2` play sample, `4` stop sample, `5` delta frame, `7` key frame.
Frames are paced at `1000/(speed+1)` ms. Palettes are 6-bit VGA values stored
pre-shifted into 8-bit, used directly (and bit-replicated up to full white).

## Provenance & licence

**GPL-2.0** (see `LICENSE`). flade adapts GPLv2 code, so it carries the same
licence:

- the HQR LZSS expander (`expand_lz` in `src/hqr.c`) is ported from the GPLv2
  LBA engine source (`ExpandLZ`, LBALab/lba2-classic-community);
- the FLA frame decoders (`src/fla.c`) follow the GPLv2 TwinEngine
  implementation of the key-frame and delta painters.

The remaining code (CD-image reader, VOC decoder, SDL3 video/audio, CLI) is
original to this project, but because the parts above are derived from GPLv2
sources the whole is licensed GPL-2.0.

The name is a nod to the original **FLADE** ("FLA DEcoder", 2000), a DOS FLA
player by CoLdBLooD & Darkstealth. Only the name is shared with that tool; its
source is preserved alongside this project in [`og/`](og/README.md).
