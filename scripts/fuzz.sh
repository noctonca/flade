#!/usr/bin/env bash
# Build and run the libFuzzer targets over flade's parsers (needs clang with
# libFuzzer + ASan/UBSan). Each target runs for $1 seconds (default 60). A crash
# is written as build/fuzz/crash-* and fails the run.
#
#   scripts/fuzz.sh           # 60s per target
#   scripts/fuzz.sh 300       # 5 min per target
set -euo pipefail
cd "$(dirname "$0")/.."
SECS=${1:-60}

FF="-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -g -O1 -Isrc"
LIBSMK="src/libsmacker/smacker.c src/libsmacker/smacker_compat.c \
        src/libsmacker/smk_bitstream.c src/libsmacker/smk_hufftree.c"
OUT=build/fuzz
mkdir -p "$OUT"

# shellcheck disable=SC2086
clang $FF -o "$OUT/fuzz_movie" tests/fuzz/fuzz_movie.c \
    src/movie.c src/fla.c src/acf.c src/smk.c $LIBSMK src/hqr.c src/iso9660.c src/voc.c -lm
# shellcheck disable=SC2086
clang $FF -o "$OUT/fuzz_hqr" tests/fuzz/fuzz_hqr.c src/hqr.c
# shellcheck disable=SC2086
clang $FF -o "$OUT/fuzz_voc" tests/fuzz/fuzz_voc.c src/voc.c
# shellcheck disable=SC2086
clang $FF -o "$OUT/fuzz_midi" tests/fuzz/fuzz_midi.c src/xmidi.c src/midi.c -lm

run_one() { # target -> 0 clean, 1 finding
    local t=$1
    echo "=== fuzz_$t (${SECS}s) ==="
    mkdir -p "$OUT/corpus_$t"
    "$OUT/fuzz_$t" -max_total_time="$SECS" -rss_limit_mb=2500 -malloc_limit_mb=600 \
        -timeout=25 "$OUT/corpus_$t" 2>&1 | tail -2
    return "${PIPESTATUS[0]}"
}

# Gating: the movie file decoders and the archive readers - these parse the
# user-facing inputs and must stay clean.
rc=0
for t in movie hqr voc; do
    run_one "$t" || { echo "fuzz_$t FAILED - see $OUT/crash-*"; rc=1; }
done

# Best-effort: the XMI->MIDI path only ever sees the game's MIDI_MI.HQR. Its
# input parsers are bounds-hardened, but the vendored 2-pass XMI->SMF converter
# is not fully robust to hostile input, so a finding here is reported, not fatal.
run_one midi || echo "fuzz_midi found something (non-gating; see $OUT/crash-*)"

[ $rc -eq 0 ] && echo "gating fuzz targets (movie, hqr, voc) clean"
exit $rc
