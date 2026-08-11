# 安装与回滚

[English](INSTALL.en.md) | 简体中文

## 准备

- 备份重要数据和现有 EFI。
- 首次验证使用独立 USB EFI，并保留另一个可启动 EFI。
- 安装 Lilu 1.6.1 或更高版本并排在 PC711Probe 前面，仍推荐使用最新版 Lilu。
- macOS 26 已可原生驱动 PC711，不需要加载本插件。
- 标准版匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`；Force 版忽略 Vendor/Device ID，会影响机器中每一个 NVMe 控制器。

## OpenCore 配置

1. 二选一并复制到 `EFI/OC/Kexts`：
   - `PC711Probe.kext`（推荐）：只匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`；
   - `PC711ProbeForce.kext`（可选）：匹配任意 Vendor/Device，只限 NVMe class `01:08:02`。
2. 在 `Kernel -> Add` 中添加并启用：

   | 字段 | 值 |
   |---|---|
   | BundlePath | `PC711Probe.kext` 或 `PC711ProbeForce.kext` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `20.0.0` |
   | MaxKernel | `24.99.99` |

3. 不要添加启用参数，严禁同时启用两个版本。它们故意共用同一 Bundle ID。
4. 停用隐藏 PC711 的 AML/SSDT，包括 `_STA=0` 或伪造 class/vendor/device 的规则。
5. 首次验证时暂时停用 NVMeFix，避免混淆结果。
6. 使用与 OpenCore 版本匹配的 `ocvalidate` 检查配置。

> PC711 `SKHynix_HFS512GDE9X084N` 与 BC711 `HFM512GD3JX016N` / `HFM512GD3JX013N` 均已报告成功，其中 PC711 的验证矩阵最完整。Force 版尚未单独完成实机验证。

## 首次启动

1. 从测试 USB 启动旧版 macOS 或 Recovery。
2. 确认系统能够进入桌面或磁盘工具。
3. 首次只读验证时，先确认 PC711 型号和既有分区正常出现，再进行安装或写入。
4. 确认同机其他 NVMe 仍正常。

## 回滚

如果 KP、卡住或设备异常：

1. 关机并从已验证 EFI 启动。
2. 在测试 EFI 中禁用 PC711Probe，或临时添加 `-pc711poff`。
3. 如需恢复原行为，重新启用之前隐藏 PC711 的 SSDT。

调试时可添加 `-pc711pdbg`。除关闭和调试外，正常使用不需要任何 PC711Probe 启动参数。

安全模式目前尚未验证，也不会启用本插件。测试与回滚请使用正常启动或 Recovery。
