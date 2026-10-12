/** Vendor configuration table parser: shared, verbatim, between the real
    EDK II DXE driver (VendorConfigDxe.c) and the host-side libFuzzer
    harness (fuzz/fuzz_harness.c). The exact same object code (same .c
    file, compiled twice for two targets) is what both the firmware and
    the fuzzer run; this header's only job is making the same source
    compile under both EDK II's Base.h types and a freestanding host
    build without pulling in any UEFI or libc headers that would make
    the two builds diverge.

    No library calls (no CopyMem, no memcpy) anywhere in this parser,
    on purpose: a parser with zero dependencies outside its own loops
    and pointer arithmetic is the easiest kind to keep byte-for-byte
    identical between a freestanding UEFI build and a host build.
**/
#ifndef VENDOR_CONFIG_PARSER_H_
#define VENDOR_CONFIG_PARSER_H_

#ifdef VCP_HOST_BUILD
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  VCP_UINT8;
typedef uint16_t VCP_UINT16;
typedef uint32_t VCP_UINT32;
#else
/* Built inside EDK II: the driver's own includes (Uefi.h / Base.h)
   already define UINT8/UINT16/UINT32 before this header is included. */
typedef UINT8  VCP_UINT8;
typedef UINT16 VCP_UINT16;
typedef UINT32 VCP_UINT32;
#endif

/* Parse status codes. */
#define VCP_STATUS_OK                   0
#define VCP_STATUS_BAD_MAGIC            1
#define VCP_STATUS_BAD_VERSION          2
#define VCP_STATUS_TRUNCATED_HEADER     3
#define VCP_STATUS_TRUNCATED_ENTRY      4
#define VCP_STATUS_BAD_CHECKSUM         5

/* Entry tags. */
#define VCP_TAG_DEVICE_LIST   1  /* array of UINT32 device IDs */
#define VCP_TAG_STRING        2  /* raw bytes, not null-terminated */
#define VCP_TAG_CHECKSUM      3  /* fixed 4-byte field, unused by the footer checksum */
#define VCP_TAG_NESTED        4  /* a nested sub-table: 1 count byte + mini (tag,len,payload) entries */
#define VCP_TAG_VENDOR_EXT    5  /* opaque vendor-specific blob */
#define VCP_TAG_PADDING       6  /* Length bytes to skip, no payload semantics */
#define VCP_TAG_LEGACY_BLOB   7  /* opaque blob, with its own (buggy) explicit bounds check */

typedef struct {
  VCP_UINT32 DeviceCount;
  VCP_UINT32 StringBytesTotal;
  VCP_UINT32 NestedBlocksSeen;
  VCP_UINT32 VendorExtBytesTotal;
  VCP_UINT32 PaddingBytesSkipped;
  VCP_UINT32 LegacyBlobBytesTotal;
  VCP_UINT8  ChecksumValid;
  VCP_UINT8  ParseStatus;
} VENDOR_CONFIG_PARSE_RESULT;

/* Parses Buffer[0..Size-1] (the last byte is the footer checksum, not
   part of the TLV region) and fills Result. Returns Result->ParseStatus
   for convenience. Never allocates; never calls into any library. */
VCP_UINT8
ParseVendorConfigTable (
  const VCP_UINT8             *Buffer,
  VCP_UINT16                  Size,
  VENDOR_CONFIG_PARSE_RESULT  *Result
  );

#endif /* VENDOR_CONFIG_PARSER_H_ */
