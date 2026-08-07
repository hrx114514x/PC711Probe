# Development process

English | [简体中文](DEVELOPMENT.zh-CN.md)

PC711Probe followed a direct path: reproduce the fault, use the newer OS as a reference, locate the relevant change, implement the smallest patch, and validate it on hardware.

## 1. Reproduce and bound the fault

The target is an SK hynix PC711 (`1C5C:174A`). On macOS 15.6.1 (24G90 / Darwin 24.6.0), the controller reached Ready state (`CSTS=1`), but the first Identify Controller command timed out after about 75 seconds and caused a kernel panic.

The same drive initialized natively through Apple `IONVMeFamily` on macOS 26.5.1 (25F80 / Darwin 25.5.0), including normal I/O and sleep/wake. A replacement NVMe driver was therefore unnecessary; the useful target was the initialization and interrupt-handling difference between the two OS versions.

## 2. Compare against macOS 26

The investigation compared symbols, call relationships, and limited instruction semantics in the Darwin 24 and Darwin 25 `BootKernelExtensions.kc` files from legally installed local systems. The main functions examined were:

- `IONVMeController::IssueIdentifyCommand`
- `IONVMeController::CreateDeviceInterrupt`
- `FilterInterruptRequest`
- `HandleInterruptRequest`

The Identify submission and wait paths were largely unchanged. The important difference appeared in interrupt creation: Darwin 25 calls the following before enumerating interrupt sources:

```cpp
IOPCIDevice::configureInterrupts(0x20000, 1, 1, 0);
```

`0x20000` requests MSI-X. Darwin 25 also removed the older MSI-X-specific path selected by bit `0x10` at controller offset `0x191`, consistently using the standard event-source path instead.

Further comparison of Darwin 20–22 showed that `CreateDeviceInterrupt` is not exported in those kernel collections, and requesting MSI-X only when that function runs is already too late on Ventura. Older `IOPCIFamily` may have resolved a different interrupt allocation and refuse a second configuration request.

## 3. Implement the minimal compatibility patch

PC711Probe keeps two version-bounded compatibility entry points:

1. PCI identity `1C5C:174A` with NVMe class `01:08:02` is matched automatically;
2. on Darwin 20–22, a high-score PCI probe requests one MSI-X vector early and returns null without claiming the device;
3. on Darwin 23–24, `CreateDeviceInterrupt` is routed to request MSI-X and clear the old path-selector bit `0x10`; and
4. Apple `IONVMeFamily` remains responsible for the actual device attachment and storage I/O.

Identify, queues, namespaces, and storage I/O remain handled by Apple `IONVMeFamily`. Other PCI IDs retain Apple's original behavior. The tested PC711 works natively on macOS 26, so the plugin loads only through Darwin 24.

## 4. Build and validate

The project was built with pinned Lilu and MacKernelSDK revisions, followed by:

- static, architecture, and `Info.plist` checks;
- boot testing from an independent USB EFI;
- controller, model, namespace, and five existing-partition enumeration on macOS 13.4.1 and 15.6.1;
- normal Recovery boots on macOS 12.5.1 and 14.6.1;
- an explicit unsupported result for macOS 11.6, where the original timeout panic remains; and
- a return to macOS 26 to confirm PCIe x4 / 8.0 GT/s, verified SMART status, and no regression on the other NVMe drive.

No existing PC711 partition was erased or modified during validation, and the repository redistributes no Apple binaries.

## 5. Current conclusion

The combined patch removes the timeout and publishes the controller, namespace, and partitions in the verified macOS 13.4.1 and 15.6.1 hardware tests.

macOS 11 remains unresolved. Other builds, firmware revisions, platforms, full installation, sustained I/O, TRIM, and sleep/wake also remain untested. PC711Probe is therefore a narrowly scoped, hardware-verified compatibility patch rather than a generic PC711 driver.
