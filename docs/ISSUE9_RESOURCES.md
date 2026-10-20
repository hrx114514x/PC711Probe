# Issue 9: missing published memory resource and closed upstream window

## Observed on the reporter hardware

The [0.3.0 report and two photos](https://github.com/hrx114514x/PC711Probe/issues/9#issuecomment-6273261459) are from Sequoia 15.6 / Darwin 24.6.0. The photos show schema 3 and the complete observation for Call 3, ControllerID 4294968490 and ProviderID 4294968326. InitializePCI returns 0 at exit sequence 5 with mask 2, Tracked 1 and InstalledRoutes 127. These IDs belong to this boot and must not be mixed with the earlier 0.2.0 run.

| Observation | Meaning and limit |
| --- | --- |
| SnapshotReady 1, MemoryKind 0, MemoryCount/Scanned 0, CountStable 1, InvalidObjects/Truncated 0 | The retained property-table snapshot has **no IODeviceMemory property**, rather than merely an empty array or an incomplete scan. |
| ConfigIdentity 2819429453 = 0xA80D144D, BDF 256 = 01:00.0 | Correct Samsung endpoint. |
| BAR0Low 2349858820 = 0x8C100004, BAR0High 0 | 64-bit, non-prefetchable memory BAR encoding; base 0x8C100000. BARValid 1 validates the encoding/identity, not an OS allocation. The comment calls this BAR0Valid; the photo's actual field is BARValid. |
| BAR0Matches 0, BAR0Length 0 | No descriptor was available to supply a length. This is **not** a hardware BAR-size measurement of zero. |
| PCICommand 1030 = 0x406 | Endpoint memory-space and bus-master bits are set. That does not create a descriptor or open an upstream window. |
| ProviderInactive 0, InactiveSelectorVerified 1 | The provider is active at the snapshot and both selector call targets matched the running kernel's isInactive symbol. The snapshot is not a recording of the exact intervening selector return. |
| IRQControllers/IRQSpecifiers 2 | Cached arrays exist; no interrupt-delivery conclusion follows. |
| ParentFound 1, ParentSearch 1, Identity 2127200390 = 0x7ECA8086, BDF 48 = 00:06.0 | Nearest upstream PCI node matches the Intel root port identified in the Linux capture. |
| ClassRevision 100925456 = 0x06040010, IsPCIBridge 1, PCICommand 263 = 0x107 | PCI bridge class, revision 0x10, memory-space bit enabled. |
| MemoryBaseRaw 65520 = 0xFFF0, MemoryLimitRaw 0 | Conventional non-prefetchable memory window is closed: decoded base 0xFFF00000 exceeds limit 0x000FFFFF. |
| BAR0WindowFit 0 | Deliberately **not assessed**, because a valid descriptor/window is absent. Do not reinterpret 0 as the diagnostic's outside-window result (2). |

The [earlier Linux capture](https://github.com/hrx114514x/PC711Probe/issues/9#issuecomment-6223345532) has the same endpoint/root-port IDs, BAR base 0x8C100000 and a 16-KiB BAR. Linux's bridge window is 0x8C100000–0x8C1FFFFF. This is a real cross-OS difference in observed resource state, not proof of the firmware's original state or proof that macOS may safely reuse Linux's allocation.

## Why an InitializePCI mapping replacement is premature

The current evidence moves the investigation to native PCI resource discovery, allocation and publication. It does not demonstrate an NVMe command-set incompatibility or an interrupt failure. A missing descriptor and closed upstream window can explain a map being unavailable even though PCI configuration reads and the PCI nub remain available.

Apple's public IOPCIFamily source, pinned at [4822b27a36e2de70e231ecf2bf3021384fab6ec2](https://github.com/apple-oss-distributions/IOPCIFamily/tree/4822b27a36e2de70e231ecf2bf3021384fab6ec2), provides a concrete reference chain:

1. [BAR probing](https://github.com/apple-oss-distributions/IOPCIFamily/blob/4822b27a36e2de70e231ecf2bf3021384fab6ec2/IOPCIConfigurator.cpp#L2617) removes a range whose proposed size is zero.
2. [Property construction](https://github.com/apple-oss-distributions/IOPCIFamily/blob/4822b27a36e2de70e231ecf2bf3021384fab6ec2/IOPCIConfigurator.cpp#L718) emits requested BAR length in `reg`; it emits `assigned-addresses` for BAR ranges with a nonzero assigned start.
3. [Nub addressing](https://github.com/apple-oss-distributions/IOPCIFamily/blob/4822b27a36e2de70e231ecf2bf3021384fab6ec2/IOPCIBridge.cpp#L4299) constructs IODeviceMemory descriptors from `assigned-addresses`. Its no-property path can return success without adding descriptors; IOPCIResourced alone is therefore not proof of a memory resource.
4. [Bridge programming](https://github.com/apple-oss-distributions/IOPCIFamily/blob/4822b27a36e2de70e231ecf2bf3021384fab6ec2/IOPCIConfigurator.cpp#L4353) starts with the exact observed dword, `0x0000FFF0`. It substitutes an open window only for a nonempty assigned range without the NoLink state.

This public revision is a source reference, not an independently verified disassembly of the reporter's Sequoia IOPCIFamily. The matching closed-window value does not identify who wrote it or prove whether missing child resources, allocation constraints or a link-state decision came first. The closed window may be a consequence of the missing resource, not a separate value that should simply be overwritten.

An ad hoc physical map or injected IODeviceMemory object would not repair native allocation ownership or the bridge window. Writing Linux's base/limit into the bridge would additionally require checking macOS's current root apertures and competing assignments. The present snapshots do not supply that authority.

## One next record to choose the first correction

Obtain the Samsung endpoint's native **resource publication record: `reg` and `assigned-addresses`**, with its node header, pcidebug, IOPCIResourced and same-boot InitializePCI dictionary. This requires no new kext, install, NVRAM reset, PCI probe, PCI write or SSD write:

```sh
ioreg -r -n pci144d,a80d -lw0 | grep -E 'reg|assigned|pcidebug|Resourced|LastInitializePCI'
```

Photograph the complete output. A visible target header plus a missing property is useful evidence. Entirely empty output is a selector failure, not evidence of a missing property; in that case retain the actual Samsung node name from the existing IORegistry view. The `reg` record may wrap onto multiple screen lines and must not be cropped.

| Native publication record | First correction layer to investigate |
| --- | --- |
| No nonzero BAR0 request in `reg` | BAR discovery/sizing before allocation. Determine why native probing did not retain the expected 16-KiB request; a later physical-map substitute would hide this. |
| Nonzero BAR0 request, no assigned BAR0 record | PCI allocation/root-bridge resource constraints. Select the relevant EFI/ACPI or allocator correction after examining that path; changing NVMe return values cannot repair it. |
| Assigned BAR0 base/length present, IODeviceMemory absent | Descriptor publication/resource lifecycle, while also reconciling the independently closed bridge window before MMIO is attempted. |

This record chooses the correction layer; it cannot promise that no further layer-specific evidence will be needed to validate an implementation. No 0.4.0 diagnostic or speculative repair kext is issued for this result. The immediate acceptance criterion remains physical detection of the Samsung SSD, followed by separate I/O, installation, boot and stability validation. Draft PR #10 remains a research change.
