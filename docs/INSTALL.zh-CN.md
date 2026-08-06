# 安装、验证与回滚

[English](INSTALL.en.md) | 简体中文

## 前提

- 已备份 PC711 上的重要数据。
- 有一个可启动且已验证的主 EFI。
- 准备一个独立 USB 测试 EFI；首次测试不要修改主 EFI。
- OpenCore 与 Lilu 可正常启动目标 macOS。

已验证组合为 OpenCore 1.0.8、Lilu 1.7.3 和 macOS 15.6.1 24G90。

## 安装到测试 EFI

1. 将 `PC711Probe.kext` 复制到 `EFI/OC/Kexts/`。
2. 确保 `Kernel -> Add` 中 Lilu 位于 PC711Probe 之前。
3. 添加 PC711Probe 条目：

   | 字段 | 值 |
   |---|---|
   | Arch | `Any` |
   | BundlePath | `PC711Probe.kext` |
   | Enabled | `True` |
   | ExecutablePath | `Contents/MacOS/PC711Probe` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `24.0.0` |
   | MaxKernel | `24.99.99` |

4. 在 `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args` 添加：

   ```text
   -pc711pcompat
   ```

   需要调试日志时再加 `-pc711pdbg`。

5. 停用任何隐藏目标 NVMe 的 AML/SSDT，例如对设备返回 `_STA=0` 的规则。
6. 首次隔离验证时禁用 NVMeFix。当前结果只证明“PC711Probe 单独启用、NVMeFix 关闭”的组合；这不等于二者必然冲突。
7. 使用与你的 OpenCore 版本匹配的 `ocvalidate` 检查 `config.plist`。
8. 对测试 EFI 做完整备份和读回校验。

## 首次启动

1. 从 USB OpenCore 启动 macOS 15.6.1 Recovery。
2. 等待超过原故障窗口（约 75 秒）。
3. 在磁盘工具选择“显示所有设备”。
4. 只确认 PC711 型号与现有分区是否出现，不要抹盘、分区或写入。

成功标准：

- 没有 `Command timeout. Identify` KP；
- 系统进入 Recovery；
- PC711 设备、namespace 和现有分区可见；
- 另一块系统 NVMe 仍可见。

## 回滚

如果 KP、卡住或设备异常：

1. 关机。
2. 从已验证的主 EFI 或回滚 USB 启动。
3. 在测试 EFI 中禁用 PC711Probe，或删除 `-pc711pcompat`。
4. 如需恢复原行为，重新启用之前隐藏 PC711 的 SSDT。

插件在缺少 `-pc711pcompat` 时不会启用 Darwin 24 兼容路由。

## 目前不要做的测试

在进一步验证前，不建议：

- 将 macOS 15 安装到 PC711；
- 对 PC711 执行格式化、TRIM 或压力写入；
- 把测试配置直接复制到其他同 PCI ID 设备；
- 在未重新分析的 macOS 更新上继续使用；
- 首次就覆盖唯一可启动的主 EFI。
