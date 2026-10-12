/** Coverage-guided host fuzz harness over VendorConfigParser.c, the
    exact same source file the real DXE driver runs. No UEFI, no QEMU:
    this is a plain libFuzzer target, built with
    `clang -fsanitize=fuzzer,address`, so every crash it finds is a
    real AddressSanitizer report with a stack trace, not a guess.
**/
/* VCP_HOST_BUILD is passed on the compile line (-DVCP_HOST_BUILD) so
   it applies identically to this file and to VendorConfigParser.c,
   which is compiled as its own translation unit. */
#include "../VendorConfigPkg/Common/VendorConfigParser.h"

int
LLVMFuzzerTestOneInput (
  const unsigned char  *Data,
  unsigned long         Size
  )
{
  VENDOR_CONFIG_PARSE_RESULT  Result;

  if (Size > 65535) {
    /* VendorConfigParser.h's Size parameter is a VCP_UINT16; anything
       libFuzzer hands us longer than that can't be a real input to
       this parser's real callers (the DXE driver reads a HOB-sized
       table), so it is excluded from the input space here rather than
       truncated, which would silently fuzz a different, shorter input
       than the one actually generated. */
    return 0;
  }

  ParseVendorConfigTable(Data, (VCP_UINT16)Size, &Result);
  return 0;
}
