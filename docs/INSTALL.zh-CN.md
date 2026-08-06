# 安装与回滚

[English](INSTALL.en.md) | 简体中文

## 准备

- 备份重要数据和现有 EFI。
- 首次验证使用独立 USB EFI，并保留另一个可启动 EFI。
- 确保 Lilu 已安装且排在 PC711Probe 前面。
- macOS 26 已可原生驱动 PC711，不需要加载本插件。

## OpenCore 配置

1. 把 `PC711Probe.kext` 复制到 `EFI/OC/Kexts`。
2. 在 `Kernel -> Add` 中添加并启用：

   | 字段 | 值 |
   |---|---|
   | BundlePath | `PC711Probe.kext` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `8.0.0` |
   | MaxKernel | `24.99.99` |

3. 不要添加 `-pc711pcompat`；1.0.0 会自动匹配 `1C5C:174A` PC711。
4. 停用隐藏 PC711 的 AML/SSDT，包括 `_STA=0` 或伪造 class/vendor/device 的规则。
5. 首次验证时暂时停用 NVMeFix，避免混淆结果。
6. 使用与 OpenCore 版本匹配的 `ocvalidate` 检查配置。

## 首次启动

1. 从测试 USB 启动旧版 macOS 或 Recovery。
2. 确认系统能够进入桌面或磁盘工具。
3. 只检查 PC711 型号和既有分区是否出现，不要抹盘或写入。
4. 确认同机其他 NVMe 仍正常。

## 回滚

如果 KP、卡住或设备异常：

1. 关机并从已验证 EFI 启动。
2. 在测试 EFI 中禁用 PC711Probe，或临时添加 `-pc711poff`。
3. 如需恢复原行为，重新启用之前隐藏 PC711 的 SSDT。

调试时可添加 `-pc711pdbg`。除关闭和调试外，正常使用不需要任何 PC711Probe 启动参数。
