#!/usr/bin/env bash
# Sanitised corpus sweep + golden-digest regression for flade's decoders.
#
# Decodes every movie in the retail game data under ASan+UBSan (via movscan),
# prints a stable per-movie digest, and diffs it against tests/golden-digests.txt
# so a decode change can never slip in unnoticed. Needs the games; configure
# paths via env. Use --update to (re)generate the golden file.
#
#   scripts/test-corpus.sh            # verify against golden
#   scripts/test-corpus.sh --update   # refresh golden after an intended change
set -uo pipefail
cd "$(dirname "$0")/.."

TLBA_FLA=${TLBA_FLA:-/mnt/e/Games/tlba-classic/Common/Fla}
LBA2_GOG=${LBA2_GOG:-$HOME/code/lba-hacking/LBA2-GOG/LBA2.GOG}
TC_GOG=${TC_GOG:-/mnt/e/GOG/Time Commando DRM-free/GAME.GOG}
ISO_BIN=${ISO_BIN:-../lba2-classic-community/scripts/dev/iso_bin.py}
GOLDEN=tests/golden-digests.txt

UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

echo "building sanitised movscan..."
cmake -S . -B build/san -DFLADE_SANITIZE=ON >/dev/null 2>&1
cmake --build build/san --target movscan >/dev/null 2>&1 || {
    echo "movscan build failed"
    exit 1
}
MS=build/san/movscan

results=$(mktemp)
scan() { # label, movscan-args...
    local label=$1
    shift
    local d
    # quiet mode prints just the digest, or nothing on a failed decode
    d=$("$MS" "$@" -q 2>/dev/null)
    if [ -z "$d" ]; then
        echo "  FAIL (no decode)  $label"
        echo "$label FAIL" >>"$results"
    else
        echo "$label $d" >>"$results"
    fi
}

for f in "$TLBA_FLA"/*.FLA; do [ -e "$f" ] && scan "fla:$(basename "$f")" "$f"; done
if [ -e "$LBA2_GOG" ]; then
    for i in $(seq 0 33); do scan "smk:$(printf %02d "$i")" --cd "$LBA2_GOG" /LBA2/VIDEO/VIDEO.HQR --index "$i"; done
fi
if [ -e "$TC_GOG" ] && [ -e "$ISO_BIN" ]; then
    for p in $(python3 "$ISO_BIN" "$TC_GOG" --tree 2>/dev/null |
        grep -oE "/(SEQUENCE|STAGE[0-9A-Z]+/RUN[0-9])/[A-Z0-9_]+\.ACF"); do
        scan "acf:$p" --cd "$TC_GOG" "$p"
    done
else
    echo "  (skipping ACF: set TC_GOG and ISO_BIN to include Time Commando)"
fi

sort -o "$results" "$results"
n=$(wc -l <"$results")

if [ $UPDATE -eq 1 ]; then
    cp "$results" "$GOLDEN"
    echo "wrote $GOLDEN ($n movies)"
    rm -f "$results"
    exit 0
fi
if [ ! -e "$GOLDEN" ]; then
    echo "no $GOLDEN yet - run: scripts/test-corpus.sh --update"
    rm -f "$results"
    exit 1
fi
if diff -u "$GOLDEN" "$results"; then
    echo "PASS: $n movies decode ASan+UBSan-clean and match golden digests"
    rm -f "$results"
    exit 0
else
    echo "FAIL: digests differ from golden (decode changed, or a movie failed)"
    rm -f "$results"
    exit 1
fi
