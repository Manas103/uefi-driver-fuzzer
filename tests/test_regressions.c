/** Replays every minimized crash reproducer in fuzz/crashes/ (one per
    planted defect, see docs/defects.md) through the now-fixed
    ParseVendorConfigTable, and asserts two things for each: the call
    returns without crashing (true by construction, since this binary
    runs under AddressSanitizer: if it crashed, the test binary itself
    would abort) and ParseStatus is a real rejection code, not
    VCP_STATUS_OK (a fix that merely stopped the crash by returning a
    wrong-but-successful parse would be a worse bug, a silent one).
**/
#include <stdio.h>
#include <stdlib.h>

#include "../VendorConfigPkg/Common/VendorConfigParser.h"

static int  gChecks   = 0;
static int  gFailures = 0;

static void
Check (int Condition, const char *What)
{
  gChecks++;
  if (!Condition) {
    gFailures++;
    printf("FAIL: %s\n", What);
  }
}

static void
ReplayFile (const char *Path)
{
  FILE                        *f;
  VCP_UINT8                   Buffer[70000];
  size_t                      Size;
  VENDOR_CONFIG_PARSE_RESULT  Result;

  f = fopen(Path, "rb");
  if (f == NULL) {
    printf("FAIL: could not open %s\n", Path);
    gFailures++;
    gChecks++;
    return;
  }

  Size = fread(Buffer, 1, sizeof(Buffer), f);
  fclose(f);

  ParseVendorConfigTable(Buffer, (VCP_UINT16)Size, &Result);
  Check(Result.ParseStatus != VCP_STATUS_OK, Path);
}

int
main (void)
{
  const char  *Files[] = {
    "fuzz/crashes/defect1_device_list",
    "fuzz/crashes/defect2_string",
    "fuzz/crashes/defect3_nested",
    "fuzz/crashes/defect4_vendor_ext",
    "fuzz/crashes/defect5_padding",
    "fuzz/crashes/defect6_legacy_blob_overflow",
    "fuzz/crashes/defect7_checksum_off_by_one",
  };
  size_t  i;

  for (i = 0; i < sizeof(Files) / sizeof(Files[0]); i++) {
    ReplayFile(Files[i]);
  }

  printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
