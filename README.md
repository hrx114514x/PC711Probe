# PC711Probe

English | [简体中文](README_CN.md)

An automatic Lilu compatibility plugin that fixes an `IONVMeFamily` Identify-timeout kernel panic affecting the SK hynix PC711 on older macOS releases.

> **New finding: PC711 works natively on macOS 26.** The same physical PC711 was identified by Apple `IONVMeFamily` on macOS 26.5.1 (25F80 / Darwin 25.5.0), with working I/O and sleep/wake. Neither PC711Probe nor NVMeFix was required. PC711Probe does not load on macOS 26.

> [!NOTE]
> ## ❤️ Support PC711Probe
>
> Donations directly support my continued development of PC711Probe, hardware testing, and future macOS compatibility work.
>
> **[Support the project](SUPPORT.md)**

## Verified result

PC711Probe 1.7.0 has booted the same physical PC711 across macOS 11–15. macOS 11–14 were verified in Recovery, while macOS 15.6.1 completed a full installation and booted from the PC711. The former Identify/command timeout panic after roughly 75 seconds did not recur.

![PC711 running macOS 15.6.1 with TRIM, PCIe link details, and measured disk performance](docs/images/macos15-installed-performance.png)

| Item | Verified value |
|---|---|
| Controller | SK hynix `1C5C:174A`, NVMe class `01:08:02` |
| Model | `SKHynix_HFS512GDE9X084N` (PC711) |
| Firmware | `41010C22` |
| Recovery boot verified | macOS 11.6 (20G165), 12.5.1 (21G83), 13.4.1 (22F82), 14.6.1 (23G93) |
| Full installation verified | macOS 15.6.1, build 24G90, Darwin 24.6.0 |
| macOS 15 link/status | PCIe 3.0 x4, 8.0 GT/s, TRIM: Yes, S.M.A.R.T.: Verified |
| macOS 15 measured result | 2766.1 MB/s write, 3005.9 MB/s read (Blackmagic Disk Speed Test) |
| Native OS | macOS 26.5.1, build 25F80, Darwin 25.5.0 |
| Boot environment | OpenCore 1.0.8, Lilu 1.7.3 |

## Automatic matching

Version 1.7.0 requires no activation argument. Once enabled in OpenCore, it automatically patches only controllers matching:

- PCI Vendor/Device: `1C5C:174A`; and
- NVMe class: `01:08:02`.

The PC711 model string is not available until the first Identify succeeds, so the plugin uses its known PCI controller identity before that command. Different capacities and OEM model strings do not affect matching. NVMe controllers with other PCI IDs retain Apple's original behavior.

The declared automatic range is Darwin 20–24 (macOS 11–15). The plugin does not load on Darwin 25/macOS 26, where the tested PC711 works natively.

## How it works

Without the patch, the PC711 controller reaches Ready state (`CSTS=1`), but Identify or another early NVMe command may never complete through the older interrupt path and eventually panics after roughly 75 seconds.

Comparison of older Apple `IONVMeFamily` builds with macOS 26 showed that the newer OS requests one MSI-X vector before creating the interrupt source and removes an older MSI-X-specific path. For the matched PC711, PC711Probe:

1. requests MSI-X during early PCI matching on macOS 11–15, before Recovery or Installer can issue sensitive polled commands;
2. uses Big Sur's original PCI message-interrupt allocator on macOS 11 and `IOPCIDevice::configureInterrupts` on macOS 12–15;
3. keeps the interrupt-source route as a fallback and clears the old selector on macOS 14–15; and
4. declines attachment, leaving Identify, queues, namespaces, and all storage I/O to Apple `IONVMeFamily`.

[Read the concise development process](docs/DEVELOPMENT.en.md)

## Installation

1. Back up the current EFI and start with a rollback-capable test USB.
2. Ensure Lilu loads before PC711Probe.
3. Copy `PC711Probe.kext` to `EFI/OC/Kexts` and add it under `Kernel -> Add`:
   - `BundlePath`: `PC711Probe.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `20.0.0`
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

Recovery boot is verified on macOS 11–14. A complete macOS 15.6.1 installation, normal system boot, namespace/partition publication, PCIe 3.0 x4 link, reported TRIM support, S.M.A.R.T. status, and a 2766.1/3005.9 MB/s write/read benchmark are verified on the tested PC711. Sleep/wake on macOS 11–15, long-duration stress, other firmware revisions, and other platforms remain outside the current validation boundary. Keep a rollback EFI and data backup for the first test.

## License

Copyright © 2026 hrx114514x.

PC711Probe v1.2.0 and later are source-available under the [PolyForm Noncommercial License 1.0.0](LICENSE). Use, modification, and distribution are permitted for personal study, research, experimentation, hobby projects, and other noncommercial purposes.

Commercial use is prohibited without separate written permission from the copyright holder, including but not limited to:

- selling PC711Probe or modified builds;
- bundling it with paid EFI or other commercial packages;
- using it in paid Hackintosh installation, repair, or support services; and
- any other commercial distribution or exploitation.

Previously published v0.6.0 and v1.0.0 releases remain available under the BSD 3-Clause license that accompanied them. This change does not retroactively withdraw rights already granted.

Third-party dependencies remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No Apple Kernel Collection or `IONVMeFamily` binary is redistributed.
