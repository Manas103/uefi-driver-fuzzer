/** Hand-rolled check suite (no GoogleTest, mirroring the style used
    elsewhere in this portfolio for C): positive tests that every
    well-formed table of every tag parses correctly and that malformed
    headers are rejected cleanly (not just "don't crash", but "return
    the right status code"). See test_regressions.c for the fixed-bug
    replay tests built from the fuzzer's own minimized crashers.
**/
#include <stdio.h>
#include <string.h>

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

/* Builds Buffer[] in place and returns its length. Mirrors
   fuzz/make_seed_corpus.py's table()/entry() helpers in C so this file
   has no Python dependency. */
static VCP_UINT16
BuildTable (VCP_UINT8 *Buffer, const VCP_UINT8 *Entries, VCP_UINT16 EntriesLen, VCP_UINT8 EntryCount)
{
  VCP_UINT16  Offset;
  VCP_UINT16  i;
  VCP_UINT8   Checksum;

  Buffer[0] = 'V';
  Buffer[1] = 'C';
  Buffer[2] = 1;
  Buffer[3] = EntryCount;
  Offset    = 4;
  for (i = 0; i < EntriesLen; i++) {
    Buffer[Offset + i] = Entries[i];
  }

  Offset  += EntriesLen;
  Checksum = 0;
  for (i = 0; i < Offset; i++) {
    Checksum ^= Buffer[i];
  }

  Buffer[Offset] = Checksum;
  Offset++;
  return Offset;
}

static void
TestEmptyTable (void)
{
  VCP_UINT8                   Buffer[8];
  VCP_UINT16                  Size;
  VENDOR_CONFIG_PARSE_RESULT  Result;

  Size = BuildTable(Buffer, NULL, 0, 0);
  ParseVendorConfigTable(Buffer, Size, &Result);
  Check(Result.ParseStatus == VCP_STATUS_OK, "empty table parses OK");
  Check(Result.ChecksumValid == 1, "empty table checksum valid");
}

static void
TestDeviceList (void)
{
  VCP_UINT8                   Entries[] = { 1, 8, 0, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00 };
  VCP_UINT8                   Buffer[32];
  VCP_UINT16                  Size;
  VENDOR_CONFIG_PARSE_RESULT  Result;

  Size = BuildTable(Buffer, Entries, sizeof(Entries), 1);
  ParseVendorConfigTable(Buffer, Size, &Result);
  Check(Result.ParseStatus == VCP_STATUS_OK, "device list table parses OK");
  Check(Result.DeviceCount == 2, "device list reports 2 devices");
  Check(Result.ChecksumValid == 1, "device list checksum valid");
}

static void
TestString (void)
{
  VCP_UINT8                   Entries[] = { 2, 3, 0, 'a', 'b', 'c' };
  VCP_UINT8                   Buffer[32];
  VCP_UINT16                  Size;
  VENDOR_CONFIG_PARSE_RESULT  Result;

  Size = BuildTable(Buffer, Entries, sizeof(Entries), 1);
  ParseVendorConfigTable(Buffer, Size, &Result);
  Check(Result.ParseStatus == VCP_STATUS_OK, "string table parses OK");
  Check(Result.StringBytesTotal == 3, "string table reports 3 bytes");
}

static void
TestBadMagic (void)
{
  VCP_UINT8                   Buffer[8] = { 'X', 'X', 1, 0, 0 };
  VENDOR_CONFIG_PARSE_RESULT  Result;

  ParseVendorConfigTable(Buffer, 5, &Result);
  Check(Result.ParseStatus == VCP_STATUS_BAD_MAGIC, "bad magic rejected");
}

static void
TestBadVersion (void)
{
  VCP_UINT8                   Buffer[8] = { 'V', 'C', 9, 0, 0 };
  VENDOR_CONFIG_PARSE_RESULT  Result;

  ParseVendorConfigTable(Buffer, 5, &Result);
  Check(Result.ParseStatus == VCP_STATUS_BAD_VERSION, "bad version rejected");
}

static void
TestTooShort (void)
{
  VCP_UINT8                   Buffer[4] = { 'V', 'C', 1, 0 };
  VENDOR_CONFIG_PARSE_RESULT  Result;

  ParseVendorConfigTable(Buffer, 4, &Result);
  Check(Result.ParseStatus == VCP_STATUS_TRUNCATED_HEADER, "too-short buffer rejected");
}

static void
TestChecksumTag (void)
{
  VCP_UINT8                   Entries[] = { 3, 4, 0, 0xef, 0xbe, 0xad, 0xde };
  VCP_UINT8                   Buffer[32];
  VCP_UINT16                  Size;
  VENDOR_CONFIG_PARSE_RESULT  Result;

  Size = BuildTable(Buffer, Entries, sizeof(Entries), 1);
  ParseVendorConfigTable(Buffer, Size, &Result);
  Check(Result.ParseStatus == VCP_STATUS_OK, "checksum-tag table parses OK");
}

int
main (void)
{
  TestEmptyTable();
  TestDeviceList();
  TestString();
  TestBadMagic();
  TestBadVersion();
  TestTooShort();
  TestChecksumTag();

  printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
