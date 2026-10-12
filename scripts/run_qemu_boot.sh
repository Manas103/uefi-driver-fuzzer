#!/bin/bash
# Boots OVMF with the disk image directory (EFI/BOOT/BOOTX64.EFI = our
# built UEFI Shell, startup.nsh loads VendorConfigDxe.efi), captures
# serial output, and checks for the two sentinels that mean "the
# driver actually ran and parsed correctly" and "the shell script
# reached its last line" (i.e. nothing hung or crashed QEMU).
#
# Exit 0: both sentinels found. Exit 1: boot did not complete cleanly.
set -u
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DISK_DIR="$PROJECT_DIR/build/disk"
OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_TEMPLATE=/usr/share/OVMF/OVMF_VARS_4M.fd
WORK_DIR=$(mktemp -d)
OVMF_VARS_RW="$WORK_DIR/OVMF_VARS.fd"
LOG="$WORK_DIR/serial.log"

cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS_RW"

timeout 30 qemu-system-x86_64 \
  -machine q35 -m 256M -cpu qemu64 -nographic \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$OVMF_VARS_RW" \
  -drive file=fat:rw:"$DISK_DIR",format=raw \
  -serial file:"$LOG" \
  > "$WORK_DIR/qemu_stdout.log" 2>&1

OK=1
if grep -q "VendorConfigDxe: PARSE_OK" "$LOG" && grep -q "BOOT_SCRIPT_DONE" "$LOG"; then
  OK=0
fi

if [ "${1:-}" == "-v" ]; then
  cat "$LOG"
fi

rm -rf "$WORK_DIR"
exit $OK
