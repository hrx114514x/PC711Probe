# Changelog

## 1.8.0 — 2026-08-09

- Added `PC711ProbeForce.kext`, an opt-in parallel build that removes the PCI Vendor/Device check while retaining the NVMe class and Darwin 20–24 boundaries.
- Kept the recommended `PC711Probe.kext` restricted to `1C5C:174A` and NVMe class `01:08:02`.
- Made both variants share one bundle identifier and documented that they must never be loaded together.
- Added successful BC711 reports for `HFM512GD3JX016N` and `HFM512GD3JX013N`, extending validation beyond the original PC711.
- Extended static verification to build, inspect, and distinguish both Kext variants.

## 1.7.0 — 2026-08-07

- Added a Big Sur-compatible direct MSI-to-MSI-X reallocation path while keeping a single Kext loadable on macOS 11–15.
- Moved the MSI-X request to early PCI matching on every supported release so Recovery and second-stage Installer polled commands are covered.
- Retained the macOS 14–15 interrupt-source route as a fallback.
- Hardware-validated macOS 11–14 Recovery and a complete macOS 15.6.1 installation.
- Verified PCIe x4 / 8.0 GT/s, TRIM support, S.M.A.R.T. status, and 2766.1/3005.9 MB/s measured sequential write/read performance on the tested PC711.

## 1.2.0 — 2026-08-07

- Added early, PC711-only MSI-X allocation for Darwin 20–22 without taking ownership away from Apple `IONVMeFamily`.
- Removed the direct dependency on the unexported legacy `IOPCIDevice::configureInterrupts` symbol.
- Added a legacy `CreateDeviceInterrupt` route for Darwin 20–22.
- Hardware-validated macOS 13.4.1 Recovery; macOS 11.6 remains unresolved.
- Changed the v1.2.0-and-later project license to PolyForm Noncommercial 1.0.0.
- Added bilingual voluntary cryptocurrency support information.

## 1.0.0 — 2026-08-07

- Enabled automatic operation without `-pc711pcompat`.
- Kept matching limited to SK hynix `1C5C:174A` with NVMe class `01:08:02`.
- Excluded Darwin 25/macOS 26, where the tested PC711 works natively.
- Removed the public diagnostic stages and read-only probe paths.
- Added concise Chinese and English installation guidance for the automatic build.

## 0.6.0 — 2026-08-06

- Added Darwin 24.6 PC711 interrupt compatibility mode via `-pc711pcompat`.
- Supports normal, installer, and recovery boot environments.
- Requested one MSI-X vector before Apple's interrupt-source enumeration.
- Selected Darwin 25-style normal event-source handling for the exact target.
- Hardware-validated macOS 15.6.1 Recovery initialization and partition enumeration.
- 保持对其他 PCI 设备及非 Darwin 24.6 系统的兼容路由不活动。
