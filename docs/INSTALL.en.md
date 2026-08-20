# Installation and rollback

English | [简体中文](INSTALL.zh-CN.md)

## Preparation

- Back up important data and the current EFI.
- Use an independent USB EFI for the first test and retain another bootable EFI.
- Install Lilu 1.6.1 or newer and order it before PC711Probe. The latest Lilu release is recommended.
- macOS 26 drives the PC711 natively and does not need this plugin.
- The standard build matches PCI `1C5C:174A` with NVMe class `01:08:02`. The Force build ignores Vendor/Device ID and affects every NVMe controller in the machine.

## OpenCore configuration

1. Choose and copy exactly one Kext to `EFI/OC/Kexts`:
   - `PC711Probe.kext` (recommended): restricted to PCI `1C5C:174A` and NVMe class `01:08:02`;
   - `PC711ProbeForce.kext` (opt-in): matches any Vendor/Device with NVMe class `01:08:02`.
2. Add and enable the following under `Kernel -> Add`:

   | Field | Value |
   |---|---|
   | BundlePath | `PC711Probe.kext` or `PC711ProbeForce.kext` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `20.0.0` |
   | MaxKernel | `24.99.99` |

3. Do not add an activation argument and never enable both variants. They intentionally share one bundle identifier.
4. Disable AML/SSDT code that hides the PC711 through `_STA=0` or spoofed class/vendor/device values.
5. Temporarily disable NVMeFix for the first test so results are not mixed.
6. Validate the configuration with the `ocvalidate` matching the OpenCore version.

> PC711 `SKHynix_HFS512GDE9X084N` and BC711 `HFM512GD3JX016N` / `HFM512GD3JX013N` have reported successful results with the standard build. BC511 and Samsung PM991 have passed functional testing with the Force build. The deepest matrix still belongs to the PC711; detailed identifiers, firmware, platforms, and extended workloads have not been recorded for every additional drive.

## First boot

1. Boot the older macOS release or Recovery through the test USB.
2. Confirm that the desktop or Disk Utility opens.
3. For a read-only first test, check that the PC711 model and existing partitions appear before installing or writing data.
4. Confirm that other NVMe devices remain functional.

## Rollback

If the machine panics, stalls, or shows unexpected devices:

1. Power off and boot through the known-good EFI.
2. Disable PC711Probe in the test EFI, or temporarily add `-pc711poff`.
3. Re-enable the SSDT that previously hid the PC711 if the original behavior is required.

Use `-pc711pdbg` for diagnostics. Normal operation requires no PC711Probe boot argument other than the optional disable or debug switches.

Safe Mode is not currently validated or enabled. Use a normal boot or Recovery for testing and rollback.
