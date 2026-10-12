#!/bin/bash
# Repeats run_qemu_boot.sh N times (default 500) and counts clean
# boots, i.e. how many times OVMF booted to the shell, loaded
# VendorConfigDxe.efi, and the driver reported PARSE_OK, in one run.
# This is the real measurement behind the "500 boots clean" claim;
# CI runs a much smaller count on every push (boot time dominates CI
# minutes) and this script is what was actually run, once, to produce
# docs/many_boots_output.txt.
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
N=${1:-500}
clean=0
failed=0

for ((i = 1; i <= N; i++)); do
  if bash "$SCRIPT_DIR/run_qemu_boot.sh" > /dev/null 2>&1; then
    clean=$((clean + 1))
  else
    failed=$((failed + 1))
    echo "boot $i: FAILED"
  fi
  if (( i % 50 == 0 )); then
    echo "progress: $i/$N done, $clean clean so far"
  fi
done

echo "RESULT: $clean/$N boots clean, $failed failed"
