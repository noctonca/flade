# flade

A small standalone player for **Adeline movies**, rendered with **SDL3**. It
plays two of Adeline's cutscene formats:

- **FLA**: *Little Big Adventure 1* (1994), with its `FLASAMP.HQR` sound effects;
- **ACF / XCF**: *Time Commando* (1996), an 8x8-tile codec (video only so far).

It reads loose movie files or pulls them straight out of a raw CD image (LBA1
`LBA.DOT`, Time Commando `GAME.GOG`), with no extraction step needed. ACF audio
(embedded streaming PCM) is not wired up yet, so ACF plays silently for now.

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

From a raw CD image (LBA1 `LBA.DOT`, Time Commando `GAME.GOG`, ...):

```bash
# list the movies inside the image (.fla / .acf), with their paths
./build/flade --cd /path/to/GAME.GOG --list

# play by full in-image path, or by a bare name searched on the disc
./build/flade --cd /path/to/GAME.GOG /SEQUENCE/BIGINTRO.ACF
./build/flade --cd /path/to/LBA.DOT  INTROD
```

From a loose movie file:

```bash
./build/flade INTROD.FLA          # FLASAMP.HQR alongside it, or --flasamp
./build/flade ACTIVISI.ACF
```

Options: `--scale N` (window size), `--no-audio`, `--volume 0..1`.

Transport (every decoded frame is cached, so rewind and scrub are instant and
frame-accurate):

| key | action |
|---|---|
| `Space` | pause / play |
| `←` / `→` | seek back / forward ~5s |
| `,` / `.` | step one frame back / forward |
| `R` | reverse direction (play backwards) |
| `-` / `=` | slower / faster (0.125x .. 8x) |
| `Home` | restart |
| `F` | fullscreen, `Esc`/`Q` quit |

## How it works

| Module | Responsibility |
|---|---|
| `movie.c` | format detection + the generic decoder interface (`movie.h`) |
| `fla.c` | FLA decoder (LBA1): palette / key-frame RLE / delta patches |
| `acf.c` | ACF/XCF decoder (Time Commando): 64-opcode 8x8 tile codec |
| `iso9660.c` | read / list / walk a raw (2352) or cooked (2048) CD image |
| `hqr.c` | HQR archive table + LZSS/LZMIT (`ExpandLZ`) expansion |
| `voc.c` | Creative Voice File → signed-16 mono PCM (FLA samples) |
| `audio.c` | SDL3 software mixer (FLA sound-effect cues) |
| `main.c` | input source, window/texture, generic play loop |

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

### ACF / XCF format, briefly

A flat list of chunks (`tag[8]` + `size:u32` + payload): `FrameLen`, `Format`
(320x200 or 320x240, fps), `Palette` (768-byte full 8-bit VGA), `KeyFrame` /
`DltFrame`, plus `Sound*` / `Camera` / `Recouvre`. Each frame is a 320xH grid of
8x8 tiles; a 6-bit opcode per tile (packed 4-per-3-bytes) selects one of 64
decode routines (raw / fills / packed-index tiles / motion copies + sparse
residual passes), double-buffered against the previous frame. Verified
pixel-identical to the reference Python decoder across key and delta frames.

## Provenance & licence

**GPL-2.0** (see `LICENSE`). flade adapts GPLv2 code, so it carries the same
licence:

- the HQR LZSS expander (`expand_lz` in `src/hqr.c`) is ported from the GPLv2
  LBA engine source (`ExpandLZ`, LBALab/lba2-classic-community);
- the FLA frame decoders (`src/fla.c`) follow the GPLv2 TwinEngine
  implementation of the key-frame and delta painters;
- the ACF/XCF tile decoder (`src/acf.c`) is adapted from the GPLv2 Time
  Commando player (`timeco/src/acf.c`, LBALab), itself based on the
  Defence-Force ACF2PCX notes and the Adeline `DEC_XCF` source.

The remaining code (CD-image reader, VOC decoder, SDL3 video/audio, CLI) is
original to this project, but because the parts above are derived from GPLv2
sources the whole is licensed GPL-2.0.

The name is a nod to the original **FLADE** ("FLA DEcoder", 2000), a DOS FLA
player by CoLdBLooD & Darkstealth. Only the name is shared with that tool; its
source is preserved alongside this project in [`og/`](og/README.md).
