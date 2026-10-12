/** See VendorConfigParser.h. Table layout:

      byte 0-1   magic 'V','C'
      byte 2     version (must be 1)
      byte 3     EntryCount
      ...        EntryCount entries, each:
                     byte 0   Tag
                     byte 1-2 Length (little-endian)
                     byte 3.. Length bytes of payload
      last byte  footer checksum (XOR of every preceding consumed byte)

    This file, verbatim, is compiled into both the real DXE driver
    (VendorConfigDxe.c, built under EDK II, running inside OVMF at
    real UEFI boot) and the host libFuzzer harness
    (fuzz/fuzz_harness.c, built with clang -fsanitize=fuzzer,address).

    This is the fixed version. See docs/defects.md for the 7 defects
    the fuzz campaign (and, for one of them, code review) found in the
    first version of this function, each with its minimized
    reproducer (fuzz/crashes/, under 16 bytes each) and the exact
    change that fixed it; tests/test_regressions.c replays all 7
    reproducers against this fixed version and asserts none of them
    crash or misparse.

    Every entry-consuming branch below checks
    `(VCP_UINT32)PayloadStart + Length > (VCP_UINT32)Size - 1` in
    32-bit arithmetic specifically because that is the one computation
    the original Tag 7 handler got wrong (in 16-bit arithmetic,
    silently wrapping); doing the bound check itself consistently, in
    a width wide enough that the attacker-controlled Length value
    (max 65535) can never make it overflow, is the fix for Defects
    1-6 all at once, not six separate ad hoc checks.
**/
#include "VendorConfigParser.h"

VCP_UINT8
ParseVendorConfigTable (
  const VCP_UINT8             *Buffer,
  VCP_UINT16                  Size,
  VENDOR_CONFIG_PARSE_RESULT  *Result
  )
{
  VCP_UINT16  Cursor;
  VCP_UINT8   EntryIndex;
  VCP_UINT8   EntryCount;

  Result->DeviceCount          = 0;
  Result->StringBytesTotal     = 0;
  Result->NestedBlocksSeen     = 0;
  Result->VendorExtBytesTotal  = 0;
  Result->PaddingBytesSkipped  = 0;
  Result->LegacyBlobBytesTotal = 0;
  Result->ChecksumValid        = 0;
  Result->ParseStatus          = VCP_STATUS_OK;

  if (Size < 5) {
    /* header (4) + at least a footer byte (1) */
    Result->ParseStatus = VCP_STATUS_TRUNCATED_HEADER;
    return Result->ParseStatus;
  }

  if ((Buffer[0] != 'V') || (Buffer[1] != 'C')) {
    Result->ParseStatus = VCP_STATUS_BAD_MAGIC;
    return Result->ParseStatus;
  }

  if (Buffer[2] != 1) {
    Result->ParseStatus = VCP_STATUS_BAD_VERSION;
    return Result->ParseStatus;
  }

  EntryCount = Buffer[3];
  Cursor     = 4;

  for (EntryIndex = 0; EntryIndex < EntryCount; EntryIndex++) {
    VCP_UINT16  EntryHeaderStart;
    VCP_UINT8   Tag;
    VCP_UINT16  Length;
    VCP_UINT16  PayloadStart;
    VCP_UINT32  Index;

    EntryHeaderStart = Cursor;

    /* Bound check for the 3-byte entry header itself, in 32-bit
       arithmetic. */
    if ((VCP_UINT32)EntryHeaderStart + 3 > (VCP_UINT32)Size - 1) {
      Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
      return Result->ParseStatus;
    }

    Tag          = Buffer[EntryHeaderStart];
    Length       = (VCP_UINT16)(Buffer[EntryHeaderStart + 1] | (Buffer[EntryHeaderStart + 2] << 8));
    PayloadStart = (VCP_UINT16)(EntryHeaderStart + 3);

    /* Fixed Defects 1, 2, 4, 5, 6 (see docs/defects.md): every tag
       whose payload this function reads, in whole or in part, is
       bound-checked here, once, before any handler runs, in 32-bit
       arithmetic so a Length near 65535 cannot wrap the check the way
       it wrapped the original Tag 7 handler's own 16-bit check. */
    if ((VCP_UINT32)PayloadStart + Length > (VCP_UINT32)Size - 1) {
      Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
      return Result->ParseStatus;
    }

    switch (Tag) {
      case VCP_TAG_DEVICE_LIST:
        for (Index = 0; Index + 4 <= Length; Index += 4) {
          volatile VCP_UINT32  DeviceId;
          DeviceId = (VCP_UINT32)Buffer[PayloadStart + Index] |
                     ((VCP_UINT32)Buffer[PayloadStart + Index + 1] << 8) |
                     ((VCP_UINT32)Buffer[PayloadStart + Index + 2] << 16) |
                     ((VCP_UINT32)Buffer[PayloadStart + Index + 3] << 24);
          (void)DeviceId;
          Result->DeviceCount++;
        }

        break;

      case VCP_TAG_STRING:
        for (Index = 0; Index < Length; Index++) {
          volatile VCP_UINT8  Ch;
          Ch = Buffer[PayloadStart + Index];
          (void)Ch;
        }

        Result->StringBytesTotal += Length;
        break;

      case VCP_TAG_CHECKSUM:
        if (Length != 4) {
          Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
          return Result->ParseStatus;
        }

        break;

      case VCP_TAG_NESTED:
        /* Fixed Defect 3: the outer bound check above already
           guarantees PayloadStart..PayloadStart+Length-1 is inside
           the buffer; this handler additionally keeps every nested
           mini-entry inside that same Length-bounded region, which
           the original version never checked at all. */
        {
          VCP_UINT8   NestedCount;
          VCP_UINT16  NestedCursor;
          VCP_UINT16  NestedRegionEnd;
          VCP_UINT8   NestedIndex;

          NestedRegionEnd = (VCP_UINT16)(PayloadStart + Length);

          if ((Length < 1) || (PayloadStart >= NestedRegionEnd)) {
            Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
            return Result->ParseStatus;
          }

          NestedCount  = Buffer[PayloadStart];
          NestedCursor = (VCP_UINT16)(PayloadStart + 1);
          for (NestedIndex = 0; NestedIndex < NestedCount; NestedIndex++) {
            VCP_UINT8  NestedLen;

            if ((VCP_UINT32)NestedCursor + 2 > (VCP_UINT32)NestedRegionEnd) {
              Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
              return Result->ParseStatus;
            }

            NestedLen = Buffer[NestedCursor + 1];
            if ((VCP_UINT32)NestedCursor + 2 + NestedLen > (VCP_UINT32)NestedRegionEnd) {
              Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
              return Result->ParseStatus;
            }

            NestedCursor = (VCP_UINT16)(NestedCursor + 2 + NestedLen);
          }

          Result->NestedBlocksSeen++;
        }

        break;

      case VCP_TAG_VENDOR_EXT:
        for (Index = 0; Index < Length; Index++) {
          volatile VCP_UINT8  B;
          B = Buffer[PayloadStart + Index];
          (void)B;
        }

        Result->VendorExtBytesTotal += Length;
        break;

      case VCP_TAG_PADDING:
        Result->PaddingBytesSkipped += Length;
        break;

      case VCP_TAG_LEGACY_BLOB:
        for (Index = 0; Index < Length; Index++) {
          volatile VCP_UINT8  B;
          B = Buffer[PayloadStart + Index];
          (void)B;
        }

        Result->LegacyBlobBytesTotal += Length;
        break;

      default:
        break;
    }

    Cursor = (VCP_UINT16)(PayloadStart + Length);
  }

  /* Fixed Defect 7: the original off-by-one let the TLV region
     consume the buffer's entire declared Size, leaving no room for a
     footer byte at all (Cursor == Size), and then read Buffer[Cursor]
     anyway. This is checked explicitly, before any footer byte is
     read, instead of folding the check into the summation loop. */
  if (Cursor != Size - 1) {
    Result->ParseStatus = VCP_STATUS_TRUNCATED_ENTRY;
    return Result->ParseStatus;
  }

  {
    VCP_UINT8   Computed;
    VCP_UINT16  Index;

    Computed = 0;
    for (Index = 0; Index < Cursor; Index++) {
      Computed ^= Buffer[Index];
    }

    Result->ChecksumValid = (Computed == Buffer[Cursor]) ? 1 : 0;
  }

  return Result->ParseStatus;
}
