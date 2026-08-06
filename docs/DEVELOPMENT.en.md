# Development process

English | [简体中文](DEVELOPMENT.zh-CN.md)

PC711Probe 0.6.0 followed a direct path: reproduce the fault, use the newer OS as a reference, locate the relevant change, implement the smallest patch, and validate it on hardware.

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

## 3. Implement the minimal compatibility patch

PC711Probe uses Lilu to route only `CreateDeviceInterrupt` and applies strict activation conditions:

1. the OS is Darwin 24.6.0;
2. `-pc711pcompat` is present in boot arguments;
3. the PCI identity is `1C5C:174A` with NVMe class `01:08:02`;
4. one MSI-X vector is requested;
5. Apple's original implementation creates the event source; and
6. the old MSI-X path-selector bit `0x10` is cleared.

Identify, queues, namespaces, and storage I/O remain handled by Apple `IONVMeFamily`. The compatibility route stays inactive for other devices and Darwin versions.

## 4. Build and validate

The project was built with pinned Lilu and MacKernelSDK revisions, followed by:

- static, architecture, and `Info.plist` checks;
- boot testing from an independent USB EFI;
- controller, model, namespace, and five existing-partition enumeration on macOS 15.6.1; and
- a return to macOS 26 to confirm PCIe x4 / 8.0 GT/s, verified SMART status, and no regression on the other NVMe drive.

No existing PC711 partition was erased or modified during validation, and the repository redistributes no Apple binaries.

## 5. Current conclusion

The combined 0.6.0 patch removes the first-Identify timeout and publishes the controller, namespace, and partitions in the verified hardware and OS environment.

Other macOS 15 builds, firmware revisions, and platforms remain untested, as do a full macOS 15 installation, sustained I/O, TRIM, and sleep/wake. PC711Probe is therefore a narrowly scoped, hardware-verified compatibility patch rather than a generic PC711 driver.
