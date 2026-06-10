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

rc=0
for t in movie hqr voc midi; do
    echo "=== fuzz_$t (${SECS}s) ==="
    mkdir -p "$OUT/corpus_$t"
    if ! "$OUT/fuzz_$t" -max_total_time="$SECS" -rss_limit_mb=3000 -timeout=20 \
        "$OUT/corpus_$t" 2>&1 | tail -2; then
        echo "fuzz_$t FAILED - see $OUT/crash-*"
        rc=1
    fi
done
[ $rc -eq 0 ] && echo "all fuzz targets clean"
exit $rc
