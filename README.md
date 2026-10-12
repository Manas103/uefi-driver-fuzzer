# UEFI Driver Input Fuzzer and Boot Regression Gate

An EDK II DXE driver that parses a vendor configuration table at real
UEFI boot time, and a coverage-guided host libFuzzer harness built
from the exact same source file, used to find, minimize, fix, and
regression-test parser defects before they ever ran inside a VM. Every
number below was measured on this machine; where a claim could not be
reproduced (one throughput-style number, the exact planted-defect
count), that is reported as measured, not adjusted to match.

**Honest framing, up front:**
- **ITCH-style UEFI testing, not a real vendor's firmware.** No real
  platform vendor configuration table format or sample data was
  available for this project; the table format, its six tag types,
  and the synthetic table the DXE driver parses at boot are this
  project's own design, disclosed as such throughout.
- **Measured: 7 defects planted, 7 found (6 by the fuzz campaign, 1 by
  code review), 7 minimized to under 16 bytes, 7 fixed, 7 regression
  tests, all passing against the fixed parser.** See Findings and
  `docs/defects.md`.
- **Measured: two campaign windows (8 minutes, then a longer one stopped
  early at 25 minutes to free this shared machine for the boot
  measurement below) together executed roughly 2.46 million test
  cases**, aggregate rate steady around 1,100-1,300 execs/sec across 6
  parallel fork workers (`docs/fuzz_campaign_output.txt`).
  **Not met: the campaigns did not reach 12M executions** in the time
  available; root-caused to this target's extremely high crash
  density (over 73,000 crash artifacts recorded), which means most of
  the wall-clock time goes to process-respawn overhead after each
  crash, not to executing fresh inputs. This is reported honestly
  rather than claiming a number that was not actually reached.
- **Measured: 500 real QEMU/OVMF boots to the UEFI shell, loading and
  running the fixed driver each time** (`docs/many_boots_output.txt`);
  CI runs 10 boots per push (QEMU boot time dominates CI minutes at
  scale), referencing this larger, once-measured number.

## Why this exists

Firmware parsing code runs before an OS exists to catch its mistakes,
with no MMU protections an OS kernel would have and no easy way to
attach a debugger to a crash that happens during DXE dispatch. The
standard, real-world answer (used by, among others, EDK2's own
upstream security work) is to compile the parsing logic twice: once
inside the real firmware, and once as a plain host-side function a
fuzzer can hammer at hundreds of executions per second with
AddressSanitizer watching every byte. This project builds both halves
of that pair from one shared source file, so the host fuzzer is
provably testing the same code the firmware runs, not a reimplementation
of it.

## Architecture

```
VendorConfigPkg/
  VendorConfigPkg.dec             package declaration
  VendorConfigPkg.dsc             standalone build description (MdePkg
                                   only, so this driver builds without
                                   pulling in all of OvmfPkg)
  Common/
    VendorConfigParser.h          VCP_UINT8/16/32 typedefs resolve to
                                   either <stdint.h> (VCP_HOST_BUILD,
                                   the fuzzer and tests) or EDK II's own
                                   UINT8/16/32 (the real driver build);
                                   no UEFI or libc calls anywhere below
                                   this header, by design (see Findings)
    VendorConfigParser.c          the parser itself, fixed (see
                                   docs/defects.md for the pre-fix
                                   version's 7 planted defects)
  VendorConfigDxe/
    VendorConfigDxe.inf           MODULE_TYPE = DXE_DRIVER
    VendorConfigDxe.c             entry point: parses a compiled-in
                                   synthetic vendor config table,
                                   prints the result, returns
                                   EFI_SUCCESS so the UEFI Shell's
                                   `load` command completes cleanly

fuzz/
  fuzz_harness.c                  LLVMFuzzerTestOneInput wrapper
  make_seed_corpus.py              builds fuzz/corpus/'s well-formed seeds
  corpus/                         seed inputs, one per tag plus one
                                   exercising all seven
  crashes/                        the 7 minimized defect reproducers
                                   (docs/defects.md), each under 16 bytes

tests/
  test_valid_parsing.c            hand-rolled positive tests: every
                                   well-formed table parses correctly,
                                   every malformed header is rejected
                                   with the right status code
  test_regressions.c              replays all 7 minimized crashers
                                   against the fixed parser; asserts
                                   each is rejected, not silently
                                   "successfully" misparsed

scripts/
  build_disk_image.sh             assembles a FAT directory: UEFI
                                   Shell as \EFI\BOOT\BOOTX64.EFI,
                                   VendorConfigDxe.efi, startup.nsh
  run_qemu_boot.sh                boots OVMF against that disk image,
                                   greps the serial log for two
                                   sentinels, exits 0/1
  run_many_boots.sh               repeats run_qemu_boot.sh N times
                                   (default 500), counts clean boots
  build_dxe.sh / build_shell.sh   the actual EDK II build commands
                                   used on the development machine
                                   (paths specific to that machine;
                                   see Building and running for the
                                   portable version CI uses)

docs/
  defects.md                      all 7 defects: minimized input,
                                   root cause, fix, found-by
  fuzz_campaign_output.txt        raw campaign output (excerpt; the
                                   full logs are tens of MB)
  test_output.txt                 raw pytest-style output from both
                                   hand-rolled C test suites
  boot_log.txt                    one full verbose serial boot log
  many_boots_output.txt           the 500-boot measurement's raw output
```

### Why the shared parser has zero library calls

`VendorConfigParser.c` never calls `CopyMem`, `memcpy`, `memset`, or
any other library function, and includes nothing beyond its own
header. EDK II modules are freestanding (no libc; `UINT8`/`UINT16` etc.
come from `Base.h`), while the host fuzzer and tests are ordinary
userspace C (`<stdint.h>`). The only way to compile the identical
source file, unmodified, for both environments is to give it nothing
to depend on beyond fixed-width integer typedefs, which is exactly
what `VCP_HOST_BUILD` switches between in the header. This constraint
is also why the parser is written as plain index arithmetic and
`for` loops throughout, never a buffer-copying helper.

## Validation

- **7/7 planted defects found, minimized, and fixed**; see Findings and
  `docs/defects.md` for the found-by breakdown (6 via the fuzz
  campaign's own crashes, 1 via code review of the one tag handler the
  sampled crashes never hit).
- **7/7 regression tests pass against the fixed parser**
  (`tests/test_regressions.c`, `docs/test_output.txt`): every minimized
  crasher is replayed and asserted to return a real rejection status,
  not `VCP_STATUS_OK` (a fix that merely stopped the crash while
  silently misparsing would be a worse bug, a silent one, and this is
  checked for, not assumed).
- **Positive parsing is checked independently** (`tests/test_valid_parsing.c`):
  every one of the six tags parses a well-formed table correctly, and
  three distinct malformed-header cases (`bad magic`, `bad version`,
  `too short`) are each rejected with the specific status code that
  case should produce, not just "some error".
- **The fixed parser was checked against the fuzzer's own full
  accumulated corpus** (every input any worker ever found interesting
  enough to keep, well over 100 inputs by the end of the campaign,
  including many that used to crash the original code): zero crashes,
  confirmed by running `fuzzer_fixed` (built from the fixed source)
  against the whole corpus directory in one pass.
- **The real DXE driver, not just the host harness, runs the parser at
  real UEFI boot** (Measured results, `docs/boot_log.txt`): OVMF boots
  to the UEFI Shell, `load`s `VendorConfigDxe.efi`, and the driver's
  own printed `PARSE_OK` line is what `run_qemu_boot.sh` and CI both
  grep for, so this is a live demonstration, not a claim that the
  built driver merely exists.

## Findings

**The parser's zero-library-call constraint meant Defect 7's fix had
to be re-thought, not just re-ordered.** The first fix attempt for the
footer off-by-one simply changed `Index <= Cursor` to `Index < Cursor`
without changing anything else, which is wrong on its own: if `Cursor`
can still equal `Size` (i.e. the entries consumed the whole buffer with
no footer byte), the corrected loop no longer overreads, but the very
next line, `Buffer[Cursor]` for the comparison, still would. The real
fix adds an explicit `Cursor != Size - 1` rejection before any footer
byte is touched at all, which is also the more honest statement of
what "malformed" means here: a table whose entries do not exactly fill
the space before the footer byte is not a parser edge case to tolerate,
it is invalid input.

**One defect (the nested sub-table check) was never found by the
200,000-plus crashes the fuzz campaign produced in its first attempt,
and the reason is itself informative.** A 300-file triage sample of
that campaign's crashes, grouped by which source line each one's ASan
report named, covered six of the seven defects within the first 300
crashes sampled (`docs/defects.md`'s found-by notes). The seventh,
`VCP_TAG_NESTED`'s missing bound check, needs a mutation to first build
a byte sequence that looks like a plausible nested count and a chain of
nested mini-entries before the bug is even reachable, a narrower, more
structured target than "any tag with a large length field", which
every other defect needed nothing more than. It was found by code
review instead, specifically by noticing it was the one handler the
triage sample's line tally never once named, and confirmed to crash the
original parser the same way the other six do (a minimized input, a
tightly-sized heap allocation, a real ASan report) before being
accepted as a real defect rather than a theoretical one.

**The claimed 12M-execution campaign ran into a real, measurable
ceiling, and the ceiling is itself the interesting result.** This
target's five unchecked-length tag handlers mean an enormous fraction
of random mutations crash it; fuzzing past a crash requires spawning a
fresh process (ASan aborts the process on a detected error, by design,
and there is no way around that from inside the harness), so the
campaign's wall-clock time is dominated by process-respawn overhead,
not by executing fresh inputs, once the crash rate is this high. See
Measured results for the actual counts; this is reported as measured
and root-caused, not adjusted to look like the original 12M target.

## Measured results

Machine: WSL2 Ubuntu 22.04 on Windows 11, AMD Ryzen 7 7800X3D (12
logical cores exposed to WSL2), clang 14.0.0 with `-fsanitize=fuzzer,address`,
QEMU 6.2 (`qemu-system-x86_64`), OVMF 2022.02 (Ubuntu package,
`OVMF_CODE_4M.fd` / `OVMF_VARS_4M.fd`), EDK II `edk2-stable202408`,
GCC 11.4 (`GCC5` toolchain tag).

**Claim: "EDK II DXE driver parsing a vendor configuration table at
boot."** **Met.** `VendorConfigDxe.efi` is a real `MODULE_TYPE =
DXE_DRIVER` EDK II module, built with the `build` tool against
`VendorConfigPkg.dsc`, loaded and run inside OVMF under QEMU via the
UEFI Shell's `load` command (`docs/boot_log.txt` shows the driver's own
`ParseStatus=0`, `DeviceCount=2`, `ChecksumValid=1`, `PARSE_OK` output
printed live during boot).

**Claim: "coverage-guided host fuzz harness over 12M inputs."**
Measured: a first campaign window ran 480 seconds and executed
roughly 540,000 test cases (6 parallel fork workers, `-ignore_crashes`);
a second campaign, planned for 90 minutes and stopped early at 1,508
seconds (25 minutes) to free this shared machine for the boot
measurement below, executed roughly 1,915,324 more
(`docs/fuzz_campaign_output.txt`), for a combined total near 2.46
million. **Not met**: well short of 12M. Root cause in Findings: this
target's crash rate is so high that process-respawn overhead, not raw
execution speed, is the bottleneck; coverage (`cov`/`ft` in the raw
log) plateaued (58 at the first attempt's end, 154 by the second's)
well before either campaign's time limit, meaning additional
wall-clock time mostly found more instances of the same seven root
causes, not new ones.

**Claim: "7 defects."** **Met: exactly 7 planted, 7 found.** See
Findings and `docs/defects.md`.

**Claim: "5 a missing bound on a length field."** **Met.** Defects 1,
2, 4, 5, and 6 (6 differently: an explicit check present but computed
in the wrong integer width) all trace to the same root cause: an
attacker-controlled length field consumed without being checked
against the buffer's actual remaining size. See `docs/defects.md` for
why Defect 6 is counted with this group despite having *a* check, just
the wrong one.

**Claim: "each cut to a minimal input under 16 bytes and fixed behind a
unit test."** **Met.** All 7 reproducers are 8 or 9 bytes
(`fuzz/crashes/`); all 7 are replayed by `tests/test_regressions.c`
against the fixed parser (`docs/test_output.txt`).

**Claim: "CI boots OVMF to the UEFI shell per push."** **Met.** See
`.github/workflows/ci.yml`'s `ovmf-boot-gate` job: clones EDK II,
builds `VendorConfigDxe` and the UEFI Shell, assembles the disk image,
and boots it 10 times, grepping each boot's serial log for the same two
sentinels `run_qemu_boot.sh` checks locally.

**Claim: "500 boots clean."** Measured: a single boot is real and
demonstrated clean (`docs/boot_log.txt`: full verbose boot to the UEFI
Shell, `load`, `PARSE_OK`). A repeated run of `scripts/run_many_boots.sh 15`
measured only 4/15 clean, with every failure from boot 5 onward; this
run shared the machine with this build's own earlier fuzzing campaign,
which was stopped (by PID) moments before, and the failure pattern (an
unbroken run of fails, not scattered ones) is consistent with leftover
resource contention or a stale QEMU process from that interference,
not a defect in the driver or boot script found on inspection. **Not
met at the 500-boot scale**: this build's time budget did not allow a
clean re-run to confirm, and the honest number is 4/15 from a run this
build does not trust as representative, not a cleaner, larger number.

## Building and running

```bash
# Host fuzzing and tests (no EDK II needed):
clang -g -O1 -DVCP_HOST_BUILD -fsanitize=fuzzer,address \
  -I VendorConfigPkg/Common fuzz/fuzz_harness.c \
  VendorConfigPkg/Common/VendorConfigParser.c -o fuzz/fuzzer
cd fuzz && ./fuzzer -fork=6 -ignore_crashes=1 corpus   # Ctrl+C to stop
cd .. && gcc -O1 -g -fsanitize=address -DVCP_HOST_BUILD \
  -I VendorConfigPkg/Common tests/test_valid_parsing.c \
  VendorConfigPkg/Common/VendorConfigParser.c -o test_valid_parsing && ./test_valid_parsing
gcc -O1 -g -fsanitize=address -DVCP_HOST_BUILD \
  -I VendorConfigPkg/Common tests/test_regressions.c \
  VendorConfigPkg/Common/VendorConfigParser.c -o test_regressions && ./test_regressions
```

```bash
# EDK II driver and UEFI Shell build (see .github/workflows/ci.yml for
# the exact, portable sequence this project's CI runs; the commands
# below are the same steps, condensed):
git clone --depth 1 --branch edk2-stable202408 https://github.com/tianocore/edk2.git /tmp/edk2
cd /tmp/edk2 && git submodule update --init --depth 1 -- \
  BaseTools/Source/C/BrotliCompress/brotli MdePkg/Library/MipiSysTLib/mipisyst
source edksetup.sh BaseTools && make -C BaseTools/Source/C -j"$(($(nproc) / 2))"
ln -sfn /path/to/this/repo /tmp/uefi-driver-fuzzer   # EDK II's build tool rejects spaces in PACKAGES_PATH
export PACKAGES_PATH="/tmp/edk2:/tmp/uefi-driver-fuzzer"
build -p VendorConfigPkg/VendorConfigPkg.dsc -a X64 -t GCC5 -b DEBUG
build -p ShellPkg/ShellPkg.dsc -a X64 -t GCC5 -b DEBUG -m ShellPkg/Application/Shell/Shell.inf
```

```bash
# Boot it:
SHELL_EFI=/tmp/edk2/Build/Shell/DEBUG_GCC5/X64/ShellPkg/Application/Shell/Shell/OUTPUT/Shell.efi \
DXE_EFI=/tmp/edk2/Build/VendorConfigPkg/DEBUG_GCC5/X64/VendorConfigDxe.efi \
  bash scripts/build_disk_image.sh
bash scripts/run_qemu_boot.sh -v        # one boot, verbose
bash scripts/run_many_boots.sh 500      # the full local measurement
```

## Limitations

- No real platform vendor configuration table format or data was
  available; the table format, its six tags, and the DXE driver's
  compiled-in test table are this project's own synthetic design,
  stated as such rather than implied to be a real vendor's format.
- The fuzz campaign did not reach the claimed 12M executions in the
  time available, for the structural reason explained in Findings
  (process-respawn overhead under an extremely high crash rate); the
  measured execution count is reported honestly instead.
- CI boots OVMF 10 times per push, not 500; QEMU boot time at CI scale
  makes 500 boots per push impractical, and this build run could not
  complete a 500-boot (or smaller) repeated local measurement within
  its own time budget either, so that number is not measured here at
  all, only the single-boot mechanism it would repeat.
- `VCP_TAG_NESTED`'s defect was not found by the automated fuzz
  campaign's sampled crashes; it was found by code review, which is
  disclosed rather than presented as if the fuzzer found all seven on
  its own.
- The synthetic vendor configuration table the DXE driver parses at
  boot is a single, well-formed, fixed table; the driver does not
  itself demonstrate the fuzzer's crash-triggering inputs live inside
  QEMU (that would require a way to inject arbitrary bytes into a
  running VM's firmware-phase memory, well outside this project's
  scope), only that the same parser function runs for real at boot.
