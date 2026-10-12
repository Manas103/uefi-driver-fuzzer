#!/bin/bash
set -e
# Builds a FAT disk image directory: the UEFI Shell as the default
# removable-media boot file, plus VendorConfigDxe.efi and a startup
# script that loads it and prints a sentinel string this project's
# boot scripts grep for.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SHELL_EFI="${SHELL_EFI:-/home/manas/build/edk2/Build/Shell/DEBUG_GCC5/X64/ShellPkg/Application/Shell/Shell/OUTPUT/Shell.efi}"
DXE_EFI="${DXE_EFI:-/home/manas/build/edk2/Build/VendorConfigPkg/DEBUG_GCC5/X64/VendorConfigDxe.efi}"
DISK_DIR="$PROJECT_DIR/build/disk"

rm -rf "$DISK_DIR"
mkdir -p "$DISK_DIR/EFI/BOOT"
cp "$SHELL_EFI" "$DISK_DIR/EFI/BOOT/BOOTX64.EFI"
cp "$DXE_EFI" "$DISK_DIR/VendorConfigDxe.efi"

cat > "$DISK_DIR/startup.nsh" <<'EOF'
@echo -off
load fs0:\VendorConfigDxe.efi
echo BOOT_SCRIPT_DONE
EOF

echo "disk image directory ready at $DISK_DIR"
