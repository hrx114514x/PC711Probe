# PM9C1aReadOnly 0.2.0 runtime experiment

The intended end result for Issue 9 is a PM9C1a-specific compatibility fix supporting physical-disk detection, installation, boot and normal storage operation. This diagnostic is the next measurement used to choose that fix; it does not bypass InitializePCI failures or claim the disk is working.

## New findings from the detailed attachment

The [detailed evidence](https://github.com/hrx114514x/PC711Probe/issues/9#issuecomment-6244363004) contains the actual strings and additional code. All ten code/data window hashes, overlapping ranges and nine string byte/text pairs were checked. The embedded InitializePCI bytes in the start window also match the original 580-byte fixture.

| Exit | Expression | Cleanup label | Source-location argument |
| --- | --- | --- | --- |
| Provider | `( 0 != fPCIProvider )` | `ErrorExit` | 1007 |
| Map | `fIOMapRegs` | `ErrorExit` | 1027 |
| Virtual address | `( 0 != fBaseAddress )` | `ReleaseMemoryMap` | 1031 |
| Mapper result | `( result == true )` | `ReleaseMSIXMemoryMap` | 1081 |

The common routine passes the component name (`AppleNVMe`), expressions, labels and `IONVMeController.cpp` path to a common external output-looking function. Its format strings and that external symbol identity are not included, so this does not establish an exact rendered log format or Recovery log retention. The source path plus these arguments support treating the four numbers as source-location metadata.

The captured **base** GetIOMappers implementation stores its callee's pointer result at `+0x298`, then executes `mov al,1` and returns. It has no null test or false-return branch. Thus this base implementation cannot normally produce the observed InitializePCI false result, even if that pointer is null. This narrows the priority to provider/map/address acquisition, particularly BAR0 map/address. It does not prove all IOMMU behavior is correct, nor exclude subclass overrides or other runtime hooks; the diagnostic records the actual controller class and whether the base method was observed.

The supplied base start path calls the helper at `0xffffff80025f55b6` before its InitializePCI dispatch. That helper dispatches CreateDeviceInterrupt first. The controller vtable's encoded target fields agree with the CreateDeviceInterrupt, InitializePCI and GetIOMappers symbol addresses at address-point slots `0xa38`, `0xa50` and `0xa68`. These are kernel-cache chained-pointer fields, not directly usable live pointers; their interpretation follows [Apple's format definition](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h), with the image base inferred consistently from the supplied addresses. Complete fixup metadata and the hardware's dynamic vtable are not present.

Consequently, **EventSourceCreated=true followed by InitializePCI=false is consistent with a single base-start attempt**. It need not imply stale flags or separate attempts, although the old flags alone still cannot correlate attempts. This also means the existing CreateDeviceInterrupt wrapper's flag clear can occur before InitializePCI's own MSI-X-path flag set; it must not be described as unconditionally running after initialization. That path still depends on MSI capability absence, and is not a demonstrated cause of this PM9C1a failure.

## What version 0.2.0 observes

The new observer adds the four original error-helper entries to its three method observations. All seven function bodies are compared against generated exact byte fingerprints before routing. Helper addresses are derived from verified relative calls, not hardcoded runtime addresses. No route is installed on a mismatch, missing symbol or out-of-range helper jump.

Lilu's pinned trampoline implementation copies a prologue without relocating RIP-relative instructions. The short error helpers contain such LEAs at +12/+13, so they use preflighted five-byte short routes, whose copied prefixes are position independent. A long-jump fallback is prohibited. Do not combine other instrumentation on these methods/helpers: short routes are not designed for multi-route chaining.

Each InitializePCI call creates a thread-scoped frame, including off-target calls so that nested calls cannot be attributed to the wrong controller. A short-held lock protects only frame bookkeeping; the original routine, logging, allocations and registry writes execute outside it. The original helper is called exactly once, including the mapper cleanup. The original InitializePCI result is returned unchanged.

`PM9C1aROLastInitializePCI` on the PCI provider contains one complete dictionary:

- `Call`, `ExitSequence`, `ControllerID`, `ProviderID` and `ControllerClass` identify the completed call.
- `Result`: 0 means the original InitializePCI returned false; 1 means true.
- `ObservedExitMask`: 1 = provider exit, 2 = map unavailable (new or cached map), 4 = zero virtual address, 8 = mapper-false exit.
- `Tracked`: 1 means the call obtained a tracking frame.
- `InstalledRoutes`: 127 means all seven routes were reported installed with a non-null original trampoline and no patcher error.

Use a single nonzero exit bit with `Result=0`, valid IDs, `Tracked=1` and full installation to identify an observed exit. Zero means unobserved/inconclusive, not success or a guessed BAR0 failure. Multiple bits or inconsistent results require log review. Allocation failure may prevent the dictionary from being published. A second dictionary, `PM9C1aROLastInterrupt`, records the last CreateDeviceInterrupt call and must be compared using IDs and sequence numbers, not merely its presence. These are last-completed-call snapshots, not a full history.

## Test and capture

Use the separate test EFI with Lilu 1.7.3 or newer. Keep the Golden EFI available unchanged. Disable PC711Probe, Force, NVMeFix and the old PM9C1aDiag in the test configuration. Replace any previous PM9C1aReadOnly version with **0.2.0**; do not load both.

| OpenCore field | Value |
| --- | --- |
| BundlePath | `PM9C1aReadOnly.kext` |
| ExecutablePath | `Contents/MacOS/PM9C1aReadOnly` |
| PlistPath | `Contents/Info.plist` |
| Enabled | Boolean true |
| MinKernel and MaxKernel | `24.6.0` |
| Explicit activation | `-pm9c1adiag` |
| Disable argument | `-pm9c1adiagoff` takes precedence |

Use the CI artifact's review-only tar archive to preserve Unix permissions, or build with `sh Diagnostics/verify-macos.sh`. No installation is performed by the build scripts. The 0.2.0 binary has been compiled and statically checked; no hardware test is implied.

After booting Recovery, run `sh Diagnostics/capture-recovery.sh`. It records loaded kext versions, IORegistry, unified-log output, a best-effort dmesg snapshot and physical-disk listing under `/tmp`. The existing Apple expression strings may already identify the failure in baseline logs; the new dictionary is intended to retain the branch observation if boot logs are unavailable. Copy evidence to separate media before reboot and review serial/SMBIOS information before sharing.

If boot or input regresses, return to the Golden EFI and disable the experimental entry; do not hot-unload it. Fingerprint mismatch, missing properties or incomplete hooks should be reported with the capture rather than worked around by relaxing checks. No additional VMD, BAR, IOMMU or disk-writing experiment is part of this measurement.

## Moving from diagnosis to a fix

A map-unavailable result directs the next work to the macOS PCI resource descriptor and mapping path; a zero-address result directs it to the returned memory map; a provider exit directs it to actual binding/provider identity. An observed mapper-false exit would require checking the dynamic override or other instrumentation because the supplied base method returns true. None warrants converting failure to success. A candidate correction can then be evaluated for physical detection first, followed by controlled I/O, installation/boot and restart/sleep checks; the present evidence does not establish 100% reliability.
