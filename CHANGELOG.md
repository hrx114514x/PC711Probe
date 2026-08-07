# Changelog

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
