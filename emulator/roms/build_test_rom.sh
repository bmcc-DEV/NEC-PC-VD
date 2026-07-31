#!/bin/bash
# Build test ROM: assemble, pad to 256KB, add reset vector
set -e

ROM_DIR="$(dirname "$0")"
nasm -f bin -o "$ROM_DIR/test_code.bin" "$ROM_DIR/test_boot.asm"

# Create 256KB file
dd if=/dev/zero bs=256K count=1 of="$ROM_DIR/pcvd_bios.bin" 2>/dev/null

# Pad code to start at offset 0x3C000 (physical 0xFC000 in shadow)
# File layout:
#   0x00000 - 0x3BFFF: padding (0s)
#   0x3C000 - 0x3FFEF: code from test_code.bin
#   0x3FFF0 - 0x3FFFF: reset vector (jmp far 0xFC00:0x0000)
CODE_SIZE=$(stat -c%s "$ROM_DIR/test_code.bin")

if [ "$CODE_SIZE" -gt 16368 ]; then
    echo "Error: code too large ($CODE_SIZE bytes, max 16368)"
    exit 1
fi

# Write code at offset 0x3C000
dd if="$ROM_DIR/test_code.bin" of="$ROM_DIR/pcvd_bios.bin" bs=1 seek=$((0x3C000)) conv=notrunc 2>/dev/null

# Write reset vector at offset 0x3FFF0: jmp far 0xFC00:0x0000
# EA = jmp far opcode, then 4 bytes: offset (2) + segment (2)
printf '\xEA\x00\x00\x00\xFC' | dd of="$ROM_DIR/pcvd_bios.bin" bs=1 seek=$((0x3FFF0)) conv=notrunc 2>/dev/null

echo "ROM built: $ROM_DIR/pcvd_bios.bin ($(stat -c%s "$ROM_DIR/pcvd_bios.bin") bytes)"
echo "Code at offset 0x3C000 ($CODE_SIZE bytes)"

# Cleanup
rm -f "$ROM_DIR/test_code.bin"
