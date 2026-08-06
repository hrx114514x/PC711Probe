# Changelog

## 0.6.0 — 2026-08-06

- Added Darwin 24.6 PC711 interrupt compatibility mode via `-pc711pcompat`.
- Supports normal, installer, and recovery boot environments.
- Requested one MSI-X vector before Apple's interrupt-source enumeration.
- Selected Darwin 25-style normal event-source handling for the exact target.
- Hardware-validated macOS 15.6.1 Recovery initialization and partition enumeration.
- 保持对其他 PCI 设备及非 Darwin 24.6 系统的兼容路由不活动。
