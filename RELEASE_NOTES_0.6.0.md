# PC711Probe 0.6.0

First hardware-verified Darwin 24.6 compatibility build for the tested SK hynix PC711 (`1C5C:174A`).

首个在目标 SK hynix PC711（`1C5C:174A`）上完成 Darwin 24.6 硬件启动验证的兼容版本。

## Verified / 已验证

- macOS 15.6.1 Recovery, build 24G90, Darwin 24.6.0
- PC711 initialization, Identify, namespace, and existing partition enumeration
- OpenCore 1.0.8 + Lilu 1.7.3
- No regression after returning to macOS 26.5.1 on the tested machine

## Important / 重要

- Use `-pc711pcompat`.
- Test from a rollback-capable USB EFI first.
- Disable SSDT device hiding and isolate the first test from NVMeFix.
- Full installation, sustained I/O, TRIM, and sleep/wake on macOS 15 are not yet verified.
- 不是通用 NVMe 驱动；其他型号、固件、平台和系统版本均未验证。

Read `README.md` / `README_CN.md` and the installation guide before use.
