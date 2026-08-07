# PC711Probe

English | [简体中文](README.md)

An automatic Lilu compatibility plugin that fixes an `IONVMeFamily` Identify-timeout kernel panic affecting the SK hynix PC711 on older macOS releases.

> **New finding: PC711 works natively on macOS 26.** The same physical PC711 was identified by Apple `IONVMeFamily` on macOS 26.5.1 (25F80 / Darwin 25.5.0), with working I/O and sleep/wake. Neither PC711Probe nor NVMeFix was required. PC711Probe does not load on macOS 26.

## Verified result

The interrupt compatibility patch was hardware-tested with macOS 15.6.1 Recovery (24G90 / Darwin 24.6.0). Disk Utility opened, the PC711 model and all five existing partitions were enumerated, and the former first-Identify timeout panic after roughly 75 seconds did not recur.

![PC711 enumerated in macOS 15.6.1 Recovery](docs/images/recovery-success.jpg)

| Item | Verified value |
|---|---|
| Controller | SK hynix `1C5C:174A`, NVMe class `01:08:02` |
| Model | `SKHynix_HFS512GDE9X084N` (PC711) |
| Firmware | `41010C22` |
| Failing OS | macOS 15.6.1, build 24G90, Darwin 24.6.0 |
| Native OS | macOS 26.5.1, build 25F80, Darwin 25.5.0 |
| Boot environment | OpenCore 1.0.8, Lilu 1.7.3 |

## Automatic matching

Version 1.0.0 requires no `-pc711pcompat` or other activation argument. Once enabled in OpenCore, it automatically patches only controllers matching:

- PCI Vendor/Device: `1C5C:174A`; and
- NVMe class: `01:08:02`.

The PC711 model string is not available until the first Identify succeeds, so the plugin uses its known PCI controller identity before that command. Different capacities and OEM model strings do not affect matching. NVMe controllers with other PCI IDs retain Apple's original behavior.

The declared automatic range is Darwin 8–24 (macOS 10.4–15); the route is installed when the corresponding `IONVMeFamily` symbol exists. The plugin does not load on Darwin 25/macOS 26.

## How it works

On macOS 15.6.1, the PC711 controller reaches Ready state (`CSTS=1`), but the first Identify Controller command never returns through the older interrupt completion path and eventually panics.

Comparison of Apple `IONVMeFamily` between macOS 15 and macOS 26 showed that the newer OS requests one MSI-X vector before creating the interrupt source and removes an older MSI-X-specific path. For the matched PC711, PC711Probe:

1. calls `IOPCIDevice::configureInterrupts(0x20000, 1, 1, 0)`;
2. calls Apple's original `CreateDeviceInterrupt`;
3. clears the old interrupt-path selector; and
4. leaves Identify, queues, namespaces, and storage I/O to Apple's driver.

[Read the concise development process](docs/DEVELOPMENT.en.md)

## Installation

1. Back up the current EFI and start with a rollback-capable test USB.
2. Ensure Lilu loads before PC711Probe.
3. Copy `PC711Probe.kext` to `EFI/OC/Kexts` and add it under `Kernel -> Add`:
   - `BundlePath`: `PC711Probe.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `8.0.0`
   - `MaxKernel`: `24.99.99`
4. Disable AML/SSDT code that hides the PC711 through `_STA=0` or spoofed class/vendor/device values.
5. Do not add a PC711Probe activation boot argument.

Use `-pc711poff` only as an emergency disable switch. Use `-pc711pdbg` for debug logging.

[Full English installation and rollback guide](docs/INSTALL.en.md)

## Build

```bash
git clone --recurse-submodules https://github.com/hrx114514x/PC711Probe.git
cd PC711Probe
./Scripts/verify.sh
```

Output: `build/Debug/PC711Probe.kext`

## Current validation boundary

Controller initialization, Identify, namespace discovery, and partition publication are verified. A full macOS 15 installation, sustained I/O, TRIM, sleep/wake, other firmware, and other platforms have not yet completed hardware validation. Keep a rollback EFI and data backup for the first test.

This project is licensed under [BSD 3-Clause](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependencies. No Apple Kernel Collection or `IONVMeFamily` binary is redistributed.
