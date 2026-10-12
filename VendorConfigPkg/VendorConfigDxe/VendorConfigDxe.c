/** @file
  DXE driver entry point. Parses a vendor configuration table with the
  exact same VendorConfigParser.c the host fuzz harness runs, and
  prints what it found to the UEFI console, so it can be checked by a
  CI boot script that greps the serial/console log.

  mSyntheticVendorConfigTable below is a disclosed stand-in for a real
  platform vendor's configuration table (no real vendor hardware or
  firmware is available to this project): a well-formed, hand-built
  instance of the exact format VendorConfigParser.c parses, with one
  entry of each of the three tags that never had a planted defect
  (DEVICE_LIST, STRING, CHECKSUM), chosen because this driver's own
  boot-time run is demonstrating correct parsing of well-formed input,
  not exercising the planted defects (that is the fuzz harness's job,
  entirely offline from this driver). Generated with
  fuzz/gen_embedded.py.
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>

#include "../Common/VendorConfigParser.h"

STATIC CONST UINT8  mSyntheticVendorConfigTable[] = {
  0x56, 0x43, 0x01, 0x03, 0x01, 0x08, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x00, 0x02, 0x0b, 0x00, 0x76, 0x65, 0x6e, 0x64, 0x6f, 0x72,
  0x2d, 0x6e, 0x61, 0x6d, 0x65, 0x03, 0x04, 0x00, 0xef, 0xbe, 0xad, 0xde,
  0x2c
};

EFI_STATUS
EFIAPI
VendorConfigDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  VENDOR_CONFIG_PARSE_RESULT  Result;
  UINT8                       Status;

  Status = ParseVendorConfigTable(
             mSyntheticVendorConfigTable,
             (VCP_UINT16)sizeof(mSyntheticVendorConfigTable),
             &Result
             );

  Print(L"VendorConfigDxe: ParseStatus=%d\n", Status);
  Print(
    L"VendorConfigDxe: DeviceCount=%d StringBytesTotal=%d ChecksumValid=%d\n",
    Result.DeviceCount,
    Result.StringBytesTotal,
    Result.ChecksumValid
    );

  if ((Status == VCP_STATUS_OK) && (Result.ChecksumValid == 1) && (Result.DeviceCount == 2)) {
    Print(L"VendorConfigDxe: PARSE_OK\n");
  } else {
    Print(L"VendorConfigDxe: PARSE_UNEXPECTED\n");
  }

  return EFI_SUCCESS;
}
