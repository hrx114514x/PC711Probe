# PC711Probe

English | [简体中文](README.md)

An automatic Lilu compatibility plugin that fixes an `IONVMeFamily` Identify-timeout kernel panic affecting the SK hynix PC711 on older macOS releases.

> **New finding: PC711 works natively on macOS 26.** The same physical PC711 was identified by Apple `IONVMeFamily` on macOS 26.5.1 (25F80 / Darwin 25.5.0), with working I/O and sleep/wake. Neither PC711Probe nor NVMeFix was required. PC711Probe does not load on macOS 26.

> [!NOTE]
> ## ❤️ Support PC711Probe
>
> PC711Probe is free for personal and noncommercial use. If it fixed the PC711 kernel panic on your machine, consider supporting continued development and hardware testing.
>
> **[Support the project](SUPPORT.md)**

## Verified result

PC711Probe has passed hardware boot tests on the same PC711 with macOS 13.4.1 and macOS 15.6.1 Recovery. Disk Utility opened, the model and all five existing partitions were enumerated, and the former NVMe command-timeout panic after roughly 75 seconds did not recur. macOS 11.6 still panics and is not currently supported.

![PC711 enumerated in macOS 15.6.1 Recovery](docs/images/recovery-success.jpg)

| Item | Verified value |
|---|---|
| Controller | SK hynix `1C5C:174A`, NVMe class `01:08:02` |
| Model | `SKHynix_HFS512GDE9X084N` (PC711) |
| Firmware | `41010C22` |
| v1.2.0 verified | macOS 13.4.1, build 22F82, Darwin 22.5.0 |
| Previously verified | macOS 15.6.1, build 24G90, Darwin 24.6.0 |
| Booted normally here | macOS 12.5.1 (21G83) and macOS 14.6.1 (23G93) Recovery |
| Currently unsupported | macOS 11.6 (20G165), original NVMe timeout panic remains |
| Native OS | macOS 26.5.1, build 25F80, Darwin 25.5.0 |
| Boot environment | OpenCore 1.0.8, Lilu 1.7.3 |

## Automatic matching

Version 1.2.0 requires no activation argument. Once enabled in OpenCore, it automatically patches only controllers matching:

- PCI Vendor/Device: `1C5C:174A`; and
- NVMe class: `01:08:02`.

The PC711 model string is not available until the first Identify succeeds, so the plugin uses its known PCI controller identity before that command. Different capacities and OEM model strings do not affect matching. NVMe controllers with other PCI IDs retain Apple's original behavior.

The declared automatic range is Darwin 20–24 (macOS 11–15). The plugin does not load on Darwin 25/macOS 26. macOS 11 is within the load range but still panics on the tested machine.

## How it works

On macOS 15.6.1, the PC711 controller reaches Ready state (`CSTS=1`), but the first Identify Controller command never returns through the older interrupt completion path and eventually panics.

Comparison of older Apple `IONVMeFamily` builds with macOS 26 showed that the newer OS requests one MSI-X vector before creating the interrupt source and removes an older MSI-X-specific path. For the matched PC711, PC711Probe:

1. requests one MSI-X vector during early PCI matching on macOS 11–13, then declines attachment;
2. leaves Apple `IONVMeFamily` as the actual NVMe driver;
3. requests MSI-X and clears the old interrupt-path selector during interrupt-source creation on macOS 14–15; and
4. leaves Identify, queues, namespaces, and storage I/O to Apple's driver.

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

Controller initialization, Identify, namespace discovery, and partition publication are verified in macOS 13/15 Recovery; macOS 12/14 Recovery booted normally here. macOS 11 remains unresolved. Full installation, sustained I/O, TRIM, sleep/wake, other firmware, and other platforms have not completed hardware validation. Keep a rollback EFI and data backup for the first test.

## License

PC711Probe v1.2.0 and later are source-available under the [PolyForm Noncommercial License 1.0.0](LICENSE). Use, modification, and distribution are permitted for personal study, research, experimentation, hobby projects, and other noncommercial purposes.

Commercial use is prohibited without separate written permission from the copyright holder, including but not limited to:

- selling PC711Probe or modified builds;
- bundling it with paid EFI or other commercial packages;
- using it in paid Hackintosh installation, repair, or support services; and
- any other commercial distribution or exploitation.

Previously published v0.6.0 and v1.0.0 releases remain available under the BSD 3-Clause license that accompanied them. This change does not retroactively withdraw rights already granted.

Third-party dependencies remain under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No Apple Kernel Collection or `IONVMeFamily` binary is redistributed.
