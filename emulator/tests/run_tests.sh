#!/bin/bash
# Build and run the emulator unit test suite.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== Building and running unit tests =="
echo

g++ -std=c++20 -g -O0 -I"$SRC" -o "$TMP/cpu_test" \
    "$ROOT/tests/cpu_test.cpp" "$SRC/cpu/i386.cpp" "$SRC/bus/memory_bus.cpp"
echo "--- cpu_test ---"
"$TMP/cpu_test"

g++ -std=c++20 -g -O0 -I"$SRC" -o "$TMP/mem_test" \
    "$ROOT/tests/mem_test.cpp" "$SRC/bus/memory_bus.cpp"
echo "--- mem_test ---"
"$TMP/mem_test"

g++ -std=c++20 -g -O0 -I"$SRC" -o "$TMP/voodoo_test" \
    "$ROOT/tests/voodoo_test.cpp" \
    "$SRC/voodoo/voodoo2.cpp" "$SRC/voodoo/voodoo_fifo.cpp" \
    "$SRC/voodoo/tmu.cpp" "$SRC/bus/memory_bus.cpp" "$SRC/bus/pci_bus.cpp"
echo "--- voodoo_test ---"
"$TMP/voodoo_test"

g++ -std=c++20 -g -O0 -I"$SRC" -o "$TMP/display_test" \
    "$ROOT/tests/display_test.cpp" "$SRC/mediagx/display_ctrl.cpp"
echo "--- display_test ---"
"$TMP/display_test"

echo
echo "== All unit tests passed =="
