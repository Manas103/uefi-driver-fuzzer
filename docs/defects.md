# Defects: found, minimized, fixed

Seven defects were planted in the first version of
`VendorConfigPkg/Common/VendorConfigParser.c` (see the git history for
that version; the version in this repo is already fixed). Five share
one root cause (a missing bound check on an attacker-controlled length
field before reading that many bytes); the other two are a 16-bit
integer-overflow that bypassed the one explicit bound check the
original code did have, and an off-by-one in the final checksum loop.
Each is reproduced below with its minimized input (every one under 16
bytes), the exact fix, and the regression test that replays it against
the fixed parser.

Method: the fuzz campaign (`docs/fuzz_campaign_output.txt`) found tens
of thousands of crashing inputs; triaging a 300-file sample by the
source line each crash's ASan report named (not by file content, since
almost none of them were byte-identical) found 6 of these 7 defects
represented in that sample alone (Defects 1, 2, 4, 5, 6, 7; see the
line numbers below). Defect 3 (the one nested nested check) was not
observed in that sample, most likely because a mutation has to build a
structurally plausible nested sub-table before it can trigger this one,
which is a narrower target for random mutation than "any large length
field"; it was found by code review of the one tag handler the triage
sample never hit, then confirmed to genuinely crash the original
parser the same way the other six do (see Validation below). For every
one of the 7, the minimal (<16-byte) input below is a small,
hand-built, understood-byte-for-byte reproducer of that exact defect
(not a mechanically shrunk fuzzer finding), each independently
confirmed against the original (pre-fix) parser with a tightly-sized
heap allocation (so AddressSanitizer's redzone sits exactly at the
buffer's real end, the same guarantee the fuzz harness itself relies
on) before being adopted as that defect's regression input.

## Defect 1: VCP_TAG_DEVICE_LIST, missing bound check

- **Minimized input (8 bytes):** `fuzz/crashes/defect1_device_list`
- **Found by:** the fuzz campaign (original line 91, in the 300-file triage sample).
- **Root cause:** the handler read `Length` bytes from `PayloadStart`
  with no check that `PayloadStart + Length` stayed inside `Size`.
- **Fix:** a single bound check, in 32-bit arithmetic, added once
  before the `switch` so it covers every tag that consumes a payload
  (Defects 1, 2, 4, 5, 6 together), not five separate ad hoc checks.
- **Regression test:** `tests/test_regressions.c`

## Defect 2: VCP_TAG_STRING, missing bound check

- **Minimized input (8 bytes):** `fuzz/crashes/defect2_string`
- **Found by:** the fuzz campaign (original line 104, in the 300-file triage sample).
- **Root cause and fix:** same as Defect 1.

## Defect 3: VCP_TAG_NESTED, missing bound check on the nested region

- **Minimized input (9 bytes):** `fuzz/crashes/defect3_nested`
- **Found by:** code review (original line 134; not observed in the
  300-file triage sample, see Method above), then confirmed by
  reproducing the crash directly against the original parser with a
  tightly-sized heap buffer.
- **Root cause:** the outer per-entry bound check (once added, see
  Defect 1) guarantees the nested blob itself is inside the buffer,
  but the original code never checked that the nested mini-entries
  *inside* that blob stayed inside it either; a large nested count or
  length could still walk past the blob's own end (which, for a
  small buffer, is also past the whole buffer's end).
- **Fix:** every nested mini-entry is now bound-checked against the
  nested region's own end (`PayloadStart + Length`), not just the
  outer buffer's end.

## Defect 4: VCP_TAG_VENDOR_EXT, missing bound check

- **Minimized input (8 bytes):** `fuzz/crashes/defect4_vendor_ext`
- **Found by:** the fuzz campaign (original line 147, in the 300-file triage sample).
- **Root cause and fix:** same as Defect 1.

## Defect 5: VCP_TAG_PADDING, missing bound check

- **Minimized input (8 bytes):** `fuzz/crashes/defect5_padding`
- **Found by:** the fuzz campaign (original line 213, in the 300-file
  triage sample; see the note on Defect 7 below about why both land on
  the same line).
- **Root cause:** unlike Defects 1/2/4, this handler never reads
  anything itself; the bug was letting `Cursor` advance by an
  unchecked `Length` with no read at all in this handler, so the
  damage showed up later, either on the next entry's header check or
  in the final checksum loop, far from the line that caused it.
- **Fix:** same single bound check as Defect 1, which also catches
  this one before `Cursor` is ever allowed to leave the buffer.

## Defect 6: VCP_TAG_LEGACY_BLOB, 16-bit integer overflow in its own bound check

- **Minimized input (8 bytes):** `fuzz/crashes/defect6_legacy_blob_overflow`
- **Found by:** the fuzz campaign (original line 183, in the 300-file triage sample).
- **Root cause:** this was the one handler that *did* have an explicit
  bound check in the original code, computed as
  `(VCP_UINT16)(PayloadStart + Length) > Size - 1`. When
  `PayloadStart + Length` exceeds 65535, the 16-bit sum wraps to a
  small value, which can pass the check even though the real,
  un-wrapped `Length` (read and used directly afterward) is far larger
  than anything that should have been allowed through.
- **Fix:** removed the handler's own 16-bit check entirely; it is now
  covered by the same 32-bit outer check as every other tag, which
  cannot wrap for any `Length` value up to 65535.

## Defect 7: footer checksum, off-by-one

- **Minimized input (8 bytes):** `fuzz/crashes/defect7_checksum_off_by_one`
- **Found by:** the fuzz campaign (original line 213, in the 300-file
  triage sample; this is the single most common crash site, 61 of 300
  sampled crashes, since both this defect and Defect 5 above both end
  up reading past the end here, from an off-by-one of exactly one byte
  for this defect, versus the tens of thousands of bytes Defect 5's
  unchecked `Length` can produce; the minimized inputs for the two are
  constructed to land on opposite ends of that range on purpose, so
  they are clearly two different bugs sharing one crash site, not one
  bug counted twice).
- **Root cause:** the checksum loop ran `for (Index = 0; Index <=
  Cursor; Index++)`, folding the footer byte's own read into the same
  loop that sums the preceding bytes. When a crafted set of entries
  made the TLV region consume the buffer's entire declared `Size`
  (leaving `Cursor == Size`, no room for a footer byte at all),
  `Buffer[Cursor]` read exactly one byte past the end of the buffer.
- **Fix:** `Cursor != Size - 1` is checked explicitly and rejected
  *before* any footer byte is touched; the checksum loop itself now
  runs strictly `Index < Cursor`, with the footer byte at
  `Buffer[Cursor]` read once, separately, only after that check passes.
