# PC711Probe 1.0.0

Automatic PC711 compatibility release. / PC711 自动兼容正式版。

## Changes / 变化

- No `-pc711pcompat` boot argument is required. / 不再需要 `-pc711pcompat`。
- Automatically matches SK hynix `1C5C:174A` with NVMe class `01:08:02`. / 自动匹配该 PC711 控制器身份。
- Other PCI IDs retain Apple's original behavior. / 其他 PCI ID 保持 Apple 原始行为。
- Darwin 25/macOS 26 is excluded because the tested PC711 works natively. / macOS 26 已实测原生免驱，因此不加载本插件。

## Verified / 已验证

- macOS 15.6.1 Recovery, build 24G90, Darwin 24.6.0
- PC711 initialization, Identify, namespace, and existing partition enumeration
- OpenCore 1.0.8 + Lilu 1.7.3

Back up the EFI and data, then perform the first boot from a rollback-capable USB EFI. / 请备份 EFI 和数据，并从可回滚的测试 USB EFI 首次启动。
