# PM9C1aReadOnly 0.3.0 runtime experiment

The intended end result for Issue 9 is a PM9C1a-specific compatibility fix supporting physical-disk detection, installation, boot and normal storage operation. This diagnostic is the next measurement used to choose that fix; it does not bypass InitializePCI failures or claim the disk is working.

## Hardware result and the next measurement

The [reporter's 0.2.0 test](https://github.com/hrx114514x/PC711Probe/issues/9#issuecomment-6251999746) on Sequoia 15.6 / Darwin 24.6.0 records InitializePCI `Result=0`, `ObservedExitMask=2`, `Tracked=1`, `InstalledRoutes=127`, Call 3, ControllerID 4294968412 and ProviderID 4294968319. The screenshots agree on this complete observation: the original map-unavailable helper was reached. This does not yet distinguish a missing BAR0 descriptor, a failed new map, or an unavailable saved map. The comment transcribes the interrupt result as 0, whereas the attached photos show 1; do not use that discrepancy as evidence of interrupt failure. Even a non-null event source does not prove interrupt delivery.

Version 0.3.0 keeps the same seven routes and code fingerprints, adding passive snapshots before InitializePCI and at its already-observed map exit. It reads the existing registry property table, descriptor tags/lengths, raw PCI configuration, provider lifecycle state and the nearest upstream PCI bridge window. It does not request a new mapping, interrupt resolution, resource construction or PCI configuration change. The new version has not yet been tested on the reporter's hardware.

`PM9C1aROResBefore` and `PM9C1aROResAtMapExit` contain:

- `Schema=3`, `Call`, `ControllerID`, `ProviderID`: match these to the completed InitializePCI observation from the same boot.
- `SnapshotReady`: 1 means the existing property-table copy succeeded. `MemoryKind`: 0 = absent, 1 = array, 2 = unexpected type.
- `MemoryCount`, `Scanned`, `CountStable`, `InvalidObjects`, `Truncated`: qualify the bounded scan, at most 64 descriptors. An incomplete or malformed scan is inconclusive.
- `BAR0Matches`, `BAR0Tag`, `BAR0Length`: descriptors whose tag's low byte is 0x10. Length is from the descriptor, not a destructive PCI BAR sizing probe. Duplicate matches are ambiguous.
- `ConfigIdentity`, `PCICommand`, `BAR0Low`, `BAR0High`, `BARBase`, `BARValid`, `BDF`: current PCI reads. `BARValid` validates identity and memory-BAR encoding, not usable allocation or reachability. A zero base is not accepted for window assessment.
- `IRQControllersPresent`/`IRQControllers`, `IRQSpecifiersPresent`/`IRQSpecifiers`: counts of **already-cached** arrays. These do not prove interrupt allocation or delivery. The property table is copied directly because IOPCIDevice's interrupt property getter can trigger resolution.
- `ProviderInactive`: a lifecycle-state sample, not a record of the original selector's return. `InactiveSelectorVerified=1` means both direct calls at InitializePCI +0x41/+0x77 resolved to the running kernel's `IOService::isInactive` symbol. Missing symbol/mismatch leaves 0 without adding any hook or disabling the existing observation.

The [Intel Tahoe reference](https://github.com/hrx114514x/PC711Probe/actions/runs/39178373866) identifies its corresponding selector as `IOService::isInactive`. In both captured control-flow sequences, a true second selector uses the saved map and a false selector calls the mapping slot. Sequoia's symbol identity remains conditional on the runtime check above. A before/exit sample cannot alone establish the exact state at an intervening call.

`PM9C1aROLastBridge` describes the nearest upstream PCI node (up to eight parent steps, rejecting ambiguous multiple parents). `ParentSearch`: 1 = found, 2 = no parent/iterator unavailable, 3 = ambiguous, 4 = depth limit. `IsPCIBridge=1` qualifies the non-prefetchable memory window. `BAR0WindowFit`: 0 = not assessed, 1 = descriptor-sized BAR range inside that window, 2 = outside. Assessment requires a unique complete descriptor scan, matching identity, valid non-prefetchable BAR encoding, nonzero base/length, valid bridge encoding and no arithmetic overflow. This compares raw configuration with descriptor length; it does not prove descriptor physical backing, upstream routing through every bridge, or successful mapping. Bridge observations overwrite on each snapshot; match `Call`/`ProviderID`.

These are separately read observations, not an atomic PCI/topology transaction. Allocation failure can omit a dictionary; stale dictionaries must not be mixed across calls. A complete scan with no BAR0 descriptor directs investigation to resource publication. A unique nonzero descriptor moves attention to mapping and lifecycle selection, but does not itself prove that the original mapping call ran and failed. An outside bridge window is a resource-consistency lead, not permission to write addresses copied from Linux.

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

Use the separate test EFI with Lilu 1.7.3 or newer. Keep the Golden EFI available unchanged. Disable PC711Probe, Force, NVMeFix and the old PM9C1aDiag in the test configuration. Replace any previous PM9C1aReadOnly version with **0.3.0**; do not load both. Preserve the activation that already worked; another NVRAM reset is not part of this experiment.

| OpenCore field | Value |
| --- | --- |
| BundlePath | `PM9C1aReadOnly.kext` |
| ExecutablePath | `Contents/MacOS/PM9C1aReadOnly` |
| PlistPath | `Contents/Info.plist` |
| Enabled | Boolean true |
| MinKernel and MaxKernel | `24.6.0` |
| Explicit activation | `-pm9c1adiag` |
| Disable argument | `-pm9c1adiagoff` takes precedence |

Use the CI artifact's `PM9C1aReadOnly-0.3.0-REVIEW_ONLY.tar.gz` to preserve Unix permissions, or build with `sh Diagnostics/verify-macos.sh`. No installation is performed by the build scripts. The earlier 0.2.0 hardware observation does not validate the added 0.3.0 snapshot implementation.

After booting Recovery, run `sh Diagnostics/capture-recovery.sh`. It records loaded kext versions, IORegistry, unified-log output, a best-effort dmesg snapshot and physical-disk listing under `/tmp`. The existing Apple expression strings may already identify the failure in baseline logs; the new dictionary is intended to retain the branch observation if boot logs are unavailable. Copy evidence to separate media before reboot and review serial/SMBIOS information before sharing.

When USB storage is unavailable, photographs are sufficient for this stage. Run each command separately and photograph its entire output (split long output across photos). `tr` places one dictionary field per line; it does not alter the evidence.

```sh
sysctl kern.bootargs
kmutil showloaded | grep PM9C1aReadOnly
ioreg -lw0 | grep 'PM9C1aROLastInitializePCI' | tr ',' '\n'
ioreg -lw0 | grep 'PM9C1aROResAtMapExit' | tr ',' '\n'
ioreg -lw0 | grep 'PM9C1aROLastBridge' | tr ',' '\n'
```

If a resource inconsistency is visible, also photograph `PM9C1aROResBefore` using the same command pattern. Retain the `Call`, IDs and qualification fields; a cropped BAR0Matches value alone is insufficient.

If boot or input regresses, return to the Golden EFI and disable the experimental entry; do not hot-unload it. Fingerprint mismatch, missing properties or incomplete hooks should be reported with the capture rather than worked around by relaxing checks. No additional VMD, BAR, IOMMU or disk-writing experiment is part of this measurement.

## Moving from diagnosis to a fix

A map-unavailable result directs the next work to the macOS PCI resource descriptor and mapping path; a zero-address result directs it to the returned memory map; a provider exit directs it to actual binding/provider identity. An observed mapper-false exit would require checking the dynamic override or other instrumentation because the supplied base method returns true. None warrants converting failure to success. A candidate correction can then be evaluated for physical detection first, followed by controlled I/O, installation/boot and restart/sleep checks; the present evidence does not establish 100% reliability.
