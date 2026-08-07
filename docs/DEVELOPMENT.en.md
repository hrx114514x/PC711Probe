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

Further comparison showed that requesting MSI-X only from `CreateDeviceInterrupt` is too late for some Recovery and Installer paths. Big Sur's `IOPCIFamily` also predates `IOPCIDevice::configureInterrupts` and initially chooses MSI when the PC711 exposes both MSI and MSI-X.

## 3. Implement the minimal compatibility patch

PC711Probe keeps two version-bounded compatibility entry points:

1. PCI identity `1C5C:174A` with NVMe class `01:08:02` is matched automatically;
2. on Darwin 20, a high-score PCI probe switches the existing PC711 MSI allocation to MSI-X through Big Sur's exported message-interrupt allocator;
3. on Darwin 21–24, the same early probe requests one MSI-X vector through `IOPCIDevice::configureInterrupts`;
4. on Darwin 23–24, `CreateDeviceInterrupt` is also routed as a fallback to request MSI-X and clear the old path-selector bit `0x10`; and
5. Apple `IONVMeFamily` remains responsible for the actual device attachment and storage I/O.

Identify, queues, namespaces, and storage I/O remain handled by Apple `IONVMeFamily`. Other PCI IDs retain Apple's original behavior. The tested PC711 works natively on macOS 26, so the plugin loads only through Darwin 24.

## 4. Build and validate

The project was built with pinned Lilu and MacKernelSDK revisions, followed by:

- static, architecture, and `Info.plist` checks;
- boot testing from an independent USB EFI;
- normal Recovery boots on macOS 11.6, 12.5.1, 13.4.1, and 14.6.1;
- a complete macOS 15.6.1 installation, including its second-stage `macOS Installer` boot;
- controller, model, namespace, and partition publication in the installed macOS 15 system;
- PCIe x4 / 8.0 GT/s, TRIM support reported as Yes, verified S.M.A.R.T. status, and a 2766.1/3005.9 MB/s write/read benchmark; and
- a return to macOS 26 to confirm native PC711 operation and no regression on the other NVMe drive.

No existing PC711 partition was erased or modified during validation, and the repository redistributes no Apple binaries.

## 5. Current conclusion

The combined patch removes the timeout across the tested macOS 11–15 Recovery and Installer paths. macOS 15.6.1 completed installation and booted from the PC711 with normal storage publication and near-interface-limit sequential performance.

Other builds, firmware revisions, platforms, long-duration stress, and macOS 11–15 sleep/wake remain untested. PC711Probe is therefore a narrowly scoped, hardware-verified compatibility patch rather than a generic PC711 driver.
