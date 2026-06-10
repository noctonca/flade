# flade

A small standalone player for **Adeline movies**, rendered with **SDL3**. It
plays two of Adeline's cutscene formats:

- **FLA**: *Little Big Adventure 1* (1994), with its `FLASAMP.HQR` sound effects
  (and the cutscene MIDI flute in FLUTE2/GLASS2, given a soundfont);
- **ACF / XCF**: *Time Commando* (1996), an 8x8-tile codec with embedded audio;
- **SMK**: *Little Big Adventure 2* (1997) Smacker cinematics (video + music and
  voice), via libsmacker.

It reads loose movie files or pulls them straight out of a raw CD image (LBA1
`LBA.DOT`, Time Commando `GAME.GOG`, LBA2 `LBA2.GOG`), with no extraction step
needed. Every decoded frame is cached, so rewind and scrub are instant and
frame-accurate.

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
./build/flade cutscene.smk
```

LBA2's Smacker cinematics live in `VIDEO.HQR` (one `.smk` per entry). `--list`
names them (from the `RESS.HQR` catalogue the game uses); play one by name, or
point flade at the HQR and pick an entry with `--index`:

```bash
./build/flade --cd /path/to/LBA2.GOG --list      # INTRO, CRASH, END, ...
./build/flade --cd /path/to/LBA2.GOG INTRO
./build/flade --cd /path/to/LBA2.GOG /LBA2/VIDEO/VIDEO.HQR --index 16
```

A Smacker carries music on track 0 and one voice per language on tracks 1..3
(FR/DE/EN). flade plays the music plus a single voice (the first by default,
or `--voice 3`) on its own audio channel, so you can switch language **live**
during playback with keys `1`/`2`/`3` - the music keeps playing.

A couple of FLA cutscenes (FLUTE2, GLASS2) are scored with a MIDI flute. To
hear it you need `MIDI_MI.HQR` (read automatically from `--cd`, or pass
`--midi MIDI_MI.HQR`) and a General MIDI soundfont (a system `.sf2` is found
automatically, or pass `--soundfont my.sf2`). Steam/GOG re-releases ship MP3
music instead of the MIDI, so the flute is only available from the CD data.

Options: `--scale N` (window size), `--no-audio`, `--volume 0..1`,
`--midi <MIDI_MI.HQR>`, `--soundfont <.sf2>`, `--index <n>`, `--voice <1..3>`.

Transport (every decoded frame is cached, so rewind and scrub are instant and
frame-accurate; audio plays during normal forward and is muted for reverse / FF
/ scrub):

| key | action |
|---|---|
| `Space` | pause / play |
| `←` / `→` | seek back / forward ~5s |
| `,` / `.` | step one frame back / forward |
| `R` | reverse direction (play backwards) |
| `-` / `=` | slower / faster (0.125x .. 8x) |
| `1` / `2` / `3` | switch voice language, live (Smacker) |
| `Home` | restart |
| `F` | fullscreen, `Esc`/`Q` quit |

## How it works

| Module | Responsibility |
|---|---|
| `movie.c` | format detection + the generic decoder interface (`movie.h`) |
| `fla.c` | FLA decoder (LBA1): palette / key-frame RLE / delta patches |
| `acf.c` | ACF/XCF decoder (Time Commando): 64-opcode 8x8 tile codec |
| `smk.c` | Smacker decoder (LBA2) via vendored libsmacker; mixes audio tracks |
| `iso9660.c` | read / list / walk a raw (2352) or cooked (2048) CD image |
| `hqr.c` | HQR archive table + LZSS/LZMIT (`ExpandLZ`) expansion |
| `voc.c` | Creative Voice File → signed-16 mono PCM (FLA samples) |
| `midi.c` | render FLA cutscene MIDI (XMI → SMF → TinySoundFont) |
| `audio.c` | SDL3 software mixer (FLA cues) + streaming channel (ACF/MIDI) |
| `main.c` | input source, window/texture, generic play loop |

### FLA format, briefly

```
header:  "V1.3"  numFrames:u32  speed:u8  var1:u8  xsize:u16  ysize:u16
         samplesInFla:u16  pad:u16   then samplesInFla x (num:u16, off:u16)
frame:   numOps:u8  dummy:u8  blockSize:u32   then blockSize bytes of opcodes
opcode:  type:u8  pad:u8  size:u16  payload[size]
```

Opcode types (after the engine's `type-1` normalisation): `0` load palette,
`1` info (sub-value: 2 = fade-to-black, 1/4 = MIDI cues, 3 = flag), `2` play
sample, `4` stop sample, `5` delta frame, `7` key frame.
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

The `Sound*` chunks hold one continuous 8-bit PCM track whose length matches the
video; flade concatenates it and plays it alongside the wall-clock video, so the
two stay in sync without polling. (SCENE backgrounds carry no audio.)

## Testing & hardening

The decoders parse untrusted binary, so robustness matters. The whole decode
core is SDL-free, which makes it directly sanitisable and fuzzable headless.

- `tools/movscan.c` opens any movie (loose / in a CD image / an HQR entry),
  decodes every frame, checks audio, and prints a stable digest with exit codes.
- `scripts/test-corpus.sh` decodes every retail movie (FLA + ACF + SMK) through a
  sanitised (ASan+UBSan) movscan and diffs the digests against
  `tests/golden-digests.txt`, so a decode change can't slip in. Needs the games;
  configure paths via env, `--update` to refresh the baseline.
- `scripts/test-negative.sh` feeds malformed inputs and bad CLI args through the
  sanitised tools and asserts none crash. Asset-free.
- `scripts/fuzz.sh [seconds]` builds and runs libFuzzer targets (`tests/fuzz/`)
  over the movie front, the HQR/LZSS reader, the VOC parser and the XMI path,
  all under ASan+UBSan.

The negative tests and a fuzz smoke run in CI (`.github/workflows/ci.yml`); the
full sanitised corpus sweep is local, since it needs the game data. Fuzzing the
decoders turned up and fixed several memory bugs (ACF/FLA frame-buffer overflows
on malformed motion data, a double-free in vendored libsmacker); the decoders
are clean on the whole corpus and survive the fuzzers.

## Provenance & licence

**GPL-2.0** (see `LICENSE`). flade adapts GPLv2 code, so it carries the same
licence:

- the HQR LZSS expander (`expand_lz` in `src/hqr.c`) is ported from the GPLv2
  LBA engine source (`ExpandLZ`, LBALab/lba2-classic-community);
- the FLA frame decoders (`src/fla.c`) follow the GPLv2 TwinEngine
  implementation of the key-frame and delta painters;
- the ACF/XCF tile decoder (`src/acf.c`) is adapted from the GPLv2 Time
  Commando player (`timeco/src/acf.c`, LBALab), itself based on the
  Defence-Force ACF2PCX notes and the Adeline `DEC_XCF` source;
- MIDI uses vendored `src/xmidi.c` (XMI→SMF, GPLv2, from TwinEngine /
  ScummVM / Exult), `src/tml.h` (MIDI loader, zlib) and `src/tsf.h`
  (TinySoundFont synth, MIT), both by Bernhard Schelling;
- Smacker decoding uses vendored `src/libsmacker/` (LGPL 2.1).

The remaining code (CD-image reader, VOC decoder, SDL3 video/audio, CLI) is
original to this project, but because the parts above are derived from GPLv2
sources the whole is licensed GPL-2.0.

The name is a nod to the original **FLADE** ("FLA DEcoder", 2000), a DOS FLA
player by CoLdBLooD & Darkstealth. Only the name is shared with that tool; its
source is preserved alongside this project in [`og/`](og/README.md).
