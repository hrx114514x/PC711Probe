# Installation and rollback

English | [简体中文](INSTALL.zh-CN.md)

## Preparation

- Back up important data and the current EFI.
- Use an independent USB EFI for the first test and retain another bootable EFI.
- Ensure Lilu is installed and ordered before PC711Probe.
- macOS 26 drives the PC711 natively and does not need this plugin.

## OpenCore configuration

1. Copy `PC711Probe.kext` to `EFI/OC/Kexts`.
2. Add and enable the following under `Kernel -> Add`:

   | Field | Value |
   |---|---|
   | BundlePath | `PC711Probe.kext` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `8.0.0` |
   | MaxKernel | `24.99.99` |

3. Do not add `-pc711pcompat`; version 1.0.0 automatically matches the `1C5C:174A` PC711.
4. Disable AML/SSDT code that hides the PC711 through `_STA=0` or spoofed class/vendor/device values.
5. Temporarily disable NVMeFix for the first test so results are not mixed.
6. Validate the configuration with the `ocvalidate` matching the OpenCore version.

## First boot

1. Boot the older macOS release or Recovery through the test USB.
2. Confirm that the desktop or Disk Utility opens.
3. Check only that the PC711 model and existing partitions appear; do not erase or write to the drive.
4. Confirm that other NVMe devices remain functional.

## Rollback

If the machine panics, stalls, or shows unexpected devices:

1. Power off and boot through the known-good EFI.
2. Disable PC711Probe in the test EFI, or temporarily add `-pc711poff`.
3. Re-enable the SSDT that previously hid the PC711 if the original behavior is required.

Use `-pc711pdbg` for diagnostics. Normal operation requires no PC711Probe boot argument other than the optional disable or debug switches.
