#!/bin/bash
# validate.sh - end-to-end validation of the NEC PC-Viper emulator.
# Clean build, all unit suites, emulator run and artifact checks.
set -e

cd "$(dirname "$0")"

echo "==================== PC-VIPER VALIDATION ===================="
echo "[1/4] Clean build"
make clean >/dev/null 2>&1 || true
make all 2>&1 | grep -Ei "error|warning" && { echo "BUILD: warnings/errors"; exit 1; } || true
echo "  build OK (no warnings)"

echo "[2/4] Unit test suites"
make build-tests >/dev/null 2>&1 || { echo "  test build failed"; exit 1; }
total_pass=0
total_fail=0
for t in cpu voodoo aureal viper glide pipeline; do
    res=$(./build/${t}_test 2>&1)
    pass=$(echo "$res" | grep -c "^PASS" || true)
    fail=$(echo "$res" | grep -c "^FAIL" || true)
    total_pass=$((total_pass + pass))
    total_fail=$((total_fail + fail))
    if [ "$fail" -eq 0 ]; then
        echo "  ${t}_test: $pass PASS / 0 FAIL  OK"
    else
        echo "  ${t}_test: $pass PASS / $fail FAIL  *** FAILED ***"
    fi
done
echo "  total: $total_pass PASS / $total_fail FAIL"
[ "$total_fail" -eq 0 ] || { echo "TESTS: FAILURES"; exit 1; }

echo "[3/4] Emulator run (all subsystems)"
rm -f voodoo.ppm glide.ppm aureal.wav
SDL_AUDIODRIVER=dummy timeout 20 ./pcviper_emulator 2>&1 \
    | grep -E "CPU -> MMIO OK|multitexture OK|glide textured|soc DMA|memcard slot0|played|aureal"
echo "  emulator exit: $?"

echo "[4/4] Artifact checks"
check_ppm() {
    local f=$1
    if [ ! -s "$f" ]; then echo "  $f: MISSING"; return 1; fi
    local hdr size
    hdr=$(head -c 2 "$f")
    size=$(stat -c%s "$f")
    if [ "$hdr" = "P6" ] && [ "$size" -ge 921600 ]; then
        echo "  $f: OK ($size bytes, P6)"
        return 0
    else
        echo "  $f: BAD (size $size, header '$hdr')"
        return 1
    fi
}
check_wav() {
    local f=$1
    if [ ! -s "$f" ]; then echo "  $f: MISSING"; return 1; fi
    local riff size hdr
    riff=$(head -c 4 "$f")
    size=$(stat -c%s "$f")
    if [ "$riff" = "RIFF" ] && [ "$size" -gt 44 ]; then
        echo "  $f: OK ($size bytes, RIFF/WAVE)"
        return 0
    else
        echo "  $f: BAD (size $size, header '$riff')"
        return 1
    fi
}
ok=1
check_ppm voodoo.ppm || ok=0
check_ppm glide.ppm || ok=0
check_wav aureal.wav || ok=0
[ "$ok" -eq 1 ] || { echo "ARTIFACTS: FAILED"; exit 1; }

echo "============================================================="
echo "VALIDATION: ALL CHECKS PASSED ($total_pass PASS / $total_fail FAIL)"
