#!/bin/bash
set -e
cd /home/manas/build/edk2
source edksetup.sh BaseTools > /dev/null 2>&1
export PACKAGES_PATH="/home/manas/build/edk2:/home/manas/build/uefi-driver-fuzzer"
build -p VendorConfigPkg/VendorConfigPkg.dsc -a X64 -t GCC5 -b DEBUG
