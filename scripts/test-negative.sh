#!/usr/bin/env bash
# Negative / robustness tests: malformed inputs and bad CLI args must fail
# gracefully (clean error, no crash). Asset-free, so it runs in CI. movscan is
# built with ASan+UBSan, so any real memory bug shows up here as a crash.
set -uo pipefail
cd "$(dirname "$0")/.."

echo "building sanitised movscan..."
cmake -S . -B build/san -DFLADE_SANITIZE=ON >/dev/null 2>&1
cmake --build build/san --target movscan >/dev/null 2>&1 || {
    echo "movscan build failed"
    exit 1
}
MS=build/san/movscan

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n=0

# A crash is a fatal signal: 132 ILL, 134 ABRT (sanitizer), 136 FPE, 139 SEGV,
# or 124 (our timeout = a hang). Anything else (clean reject or garbage decode)
# is acceptable robustness.
is_crash() { case $1 in 124 | 132 | 134 | 136 | 139) return 0 ;; *) return 1 ;; esac; }

check() { # name, movscan-args...
    local name=$1
    shift
    n=$((n + 1))
    timeout 20 "$MS" "$@" >/dev/null 2>&1
    local rc=$?
    if is_crash "$rc"; then
        echo "  CRASH rc=$rc   $name"
        fail=$((fail + 1))
    else
        echo "  ok    rc=$rc   $name"
    fi
}

fill() { head -c "$1" /dev/zero | tr '\000' "$2"; } # size, octal-byte

echo "=== malformed movie inputs (decoders must not crash) ==="
printf '' >"$TMP/empty"
check "empty file" "$TMP/empty"
printf 'V1.3' >"$TMP/fla_magic"
check "FLA magic only" "$TMP/fla_magic"
{ printf 'V1.3'; fill 8192 '\377'; } >"$TMP/fla_ff"
check "FLA magic + 8K 0xFF" "$TMP/fla_ff"
{ printf 'V1.3'; fill 8192 '\000'; } >"$TMP/fla_00"
check "FLA magic + 8K 0x00" "$TMP/fla_00"
printf 'FrameLen' >"$TMP/acf_magic"
check "ACF magic only" "$TMP/acf_magic"
{ printf 'FrameLen'; fill 16384 '\377'; } >"$TMP/acf_ff"
check "ACF magic + 16K 0xFF" "$TMP/acf_ff"
{ printf 'FrameLen'; fill 16384 '\125'; } >"$TMP/acf_55"
check "ACF magic + 16K 0x55" "$TMP/acf_55"
printf 'SMK2' >"$TMP/smk_magic"
check "SMK magic only" "$TMP/smk_magic"
{ printf 'SMK2'; fill 8192 '\377'; } >"$TMP/smk_ff"
check "SMK2 magic + 8K 0xFF" "$TMP/smk_ff"
{ printf 'SMK4'; fill 8192 '\000'; } >"$TMP/smk_00"
check "SMK4 magic + 8K 0x00" "$TMP/smk_00"
fill 65536 '\377' >"$TMP/nomagic"
check "64K 0xFF, no magic" "$TMP/nomagic"

echo "=== malformed HQR containers (--index treats input as an HQR) ==="
fill 8192 '\377' >"$TMP/hqr_ff"
check "HQR: 8K 0xFF, entry 0" "$TMP/hqr_ff" --index 0
check "HQR: 8K 0xFF, entry 9999" "$TMP/hqr_ff" --index 9999
printf 'FrameLen' >"$TMP/hqr_tiny"
check "HQR: tiny, entry 0" "$TMP/hqr_tiny" --index 0

echo "=== flade CLI argument handling (graceful, no crash) ==="
if [ -x build/flade ]; then
    export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
    fc=0
    fcheck() { # name, args...
        local name=$1
        shift
        n=$((n + 1))
        timeout 20 build/flade "$@" >/dev/null 2>&1
        local rc=$?
        if is_crash "$rc"; then
            echo "  CRASH rc=$rc   flade $name"
            fail=$((fail + 1))
        else
            echo "  ok    rc=$rc   flade $name"
        fi
    }
    fcheck "no args" </dev/null
    fcheck "nonexistent file" /no/such/movie.fla
    fcheck "empty file" "$TMP/empty"
    fcheck "garbage file" "$TMP/nomagic"
    fcheck "bad --index" "$TMP/hqr_ff" --index 9999
else
    echo "  (build/flade not present; skipping CLI checks - run 'make' first)"
fi

echo "----------------------------------------"
if [ "$fail" -eq 0 ]; then
    echo "PASS: $n cases handled gracefully (no crashes)"
    exit 0
else
    echo "FAIL: $fail of $n cases crashed"
    exit 1
fi
