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
for t in cpu voodoo aureal viper glide pipeline voodoo_adv; do
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

echo "[2b] POST firmware (MIPS IV assembly)"
FW=""
if command -v mips64-elf-as >/dev/null 2>&1 || [ -x /opt/libdragon/mips64-elf/bin/as ]; then
    AS="${AS:-/opt/libdragon/mips64-elf/bin/as}"
    LD="${LD:-/opt/libdragon/mips64-elf/bin/ld}"
    OBJCOPY="${OBJCOPY:-/opt/libdragon/mips64-elf/bin/objcopy}"
    cd firmware
    "$AS" -EL -mips4 -o firmware.o firmware.S \
        && "$LD" -EL -T link.ld -o firmware.elf firmware.o \
        && "$OBJCOPY" -O binary firmware.elf pcviper_boot.bin
    cd ..
    if [ -s firmware/pcviper_boot.bin ]; then
        FW="firmware/pcviper_boot.bin"
        echo "  firmware built ($(stat -c%s firmware/pcviper_boot.bin) bytes)"
    else
        echo "  WARNING: firmware build failed, using built-in boot"
    fi
else
    echo "  WARNING: no mips64 cross-toolchain, using built-in boot"
fi

echo "[2c] demo3d interactive input path (host -> firmware)"
MIPS_GCC=
if command -v mips64-elf-gcc >/dev/null 2>&1; then MIPS_GCC="mips64-elf-gcc";
elif [ -x /opt/libdragon/bin/mips64-elf-gcc ]; then MIPS_GCC="/opt/libdragon/bin/mips64-elf-gcc"; fi
ppm_colored() {  # $1 = ppm path -> number of non-black pixels
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
i = d.find(b'255\n') + 4
print(sum(1 for j in range(i, len(d), 3) if d[j:j+3] != b'\x00\x00\x00'))
EOF
}
if [ -n "$MIPS_GCC" ]; then
    make firmware/demo3d.bin >/dev/null 2>&1
    if [ -s firmware/demo3d.bin ]; then
        timeout 60 ./pcviper_emulator firmware/demo3d.bin >/dev/null 2>&1
        cp cpu3d.ppm /tmp/cpu3d_default.ppm
        timeout 60 env PCVIPER_INPUT_CAMZ=8 ./pcviper_emulator firmware/demo3d.bin >/dev/null 2>&1
        cp cpu3d.ppm /tmp/cpu3d_far.ppm
        pd=$(ppm_colored /tmp/cpu3d_default.ppm)
        pf=$(ppm_colored /tmp/cpu3d_far.ppm)
        echo "  demo3d default=${pd}px camz=8=${pf}px"
        if [ "$pd" -gt 1000 ] && [ "$pf" -lt $((pd / 3)) ]; then
            echo "  demo3d input override: OK (zoom changes render)"
        else
            echo "  demo3d input override: FAIL"; exit 1
        fi
        if pkg-config --exists sdl2; then
            n=$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy PCVIPER_SDL_FRAMES=4 \
                timeout 20 ./pcviper_emulator firmware/demo3d.bin --sdl 2>&1 \
                | grep -o "interactive demo: [0-9]* frames shown" || true)
            echo "  SDL2 headless: $n"
            case "$n" in *"4 frames shown"*) echo "  SDL2 interactive: OK";;
                *) echo "  SDL2 interactive: FAIL"; exit 1;; esac
        else
            echo "  SDL2 not available, skipping interactive check"
        fi
    else
        echo "  WARNING: demo3d build failed, skipping input-path check"
    fi
else
    echo "  WARNING: no mips64 cross-toolchain, skipping demo3d check"
fi

echo "[3/4] Emulator run (all subsystems)"
rm -f voodoo.ppm glide.ppm aureal.wav
SDL_AUDIODRIVER=dummy timeout 20 ./pcviper_emulator $FW 2>&1 \
    | grep -E "CPU -> MMIO OK|CPU->SoC DMA|CPU->A3D channel regs|CPU->A3D audio|POST code|multitexture OK|glide textured|soc DMA DVD|memcard slot0|played|wrote aureal"
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
check_ppm advanced.ppm || ok=0
check_wav aureal.wav || ok=0
[ "$ok" -eq 1 ] || { echo "ARTIFACTS: FAILED"; exit 1; }

echo "============================================================="
echo "VALIDATION: ALL CHECKS PASSED ($total_pass PASS / $total_fail FAIL)"
