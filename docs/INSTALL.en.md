# Installation, validation, and rollback

English | [简体中文](INSTALL.zh-CN.md)

## Prerequisites

- Back up important data on the PC711.
- Keep a known-good primary EFI.
- Prepare a separate USB test EFI; do not modify the primary EFI for the first test.
- Confirm that OpenCore and Lilu can already boot the target macOS environment.

The verified combination is OpenCore 1.0.8, Lilu 1.7.3, and macOS 15.6.1 build 24G90.

## Install to the test EFI

1. Copy `PC711Probe.kext` to `EFI/OC/Kexts/`.
2. Make sure Lilu precedes PC711Probe under `Kernel -> Add`.
3. Add the PC711Probe entry:

   | Field | Value |
   |---|---|
   | Arch | `Any` |
   | BundlePath | `PC711Probe.kext` |
   | Enabled | `True` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `24.0.0` |
   | MaxKernel | `24.99.99` |

4. Add this to `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args`:

   ```text
   -pc711pcompat
   ```

   Add `-pc711pdbg` only when debug logging is required.

5. Disable AML/SSDT code that hides the target NVMe, including rules that return `_STA=0` for the device.
6. Disable NVMeFix for the first isolated test. The verified result covers PC711Probe alone with NVMeFix disabled; it does not prove that the two are inherently incompatible.
7. Validate `config.plist` with the `ocvalidate` version matching your OpenCore release.
8. Create a complete backup and readback verification of the test EFI.

## First boot

1. Boot macOS 15.6.1 Recovery through the USB OpenCore.
2. Wait beyond the former failure window of about 75 seconds.
3. Select “Show All Devices” in Disk Utility.
4. Confirm only that the PC711 model and existing partitions appear. Do not erase, repartition, or write to the drive.

Success criteria:

- no `Command timeout. Identify` panic;
- Recovery opens;
- the PC711 device, namespace, and existing partitions are visible; and
- the other system NVMe remains visible.

## Rollback

If the machine panics, stalls, or shows unexpected devices:

1. Power it off.
2. Boot from the known-good primary EFI or rollback USB.
3. Disable PC711Probe in the test EFI, or remove `-pc711pcompat`.
4. Re-enable the SSDT that previously hid the PC711 if the original behavior is required.

Without `-pc711pcompat`, the Darwin 24 compatibility route does not activate.

## Tests not recommended yet

Until the validation boundary is expanded, do not:

- install macOS 15 onto the PC711;
- format, TRIM, or stress-write the PC711;
- copy the setup to a different device merely because its PCI ID matches;
- keep using it after a macOS update without new analysis; or
- overwrite your only bootable EFI during the first test.
