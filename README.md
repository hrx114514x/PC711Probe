# PC711Probe

[English](README_EN.md) | 简体中文

用于解决特定黑苹果平台上 SK hynix PC711 在 macOS 15.6.1 初始化时发生 `IONVMeFamily` Identify 超时 Kernel Panic 的实验性 Lilu 插件。

> 这不是通用 NVMe 驱动。它包含针对 Darwin 24.6 内部实现和控制器私有偏移的窄范围兼容补丁。请先备份数据，并只在可回滚的测试 EFI 上验证。

## 已验证结果

`PC711Probe 0.6.0` 已在 macOS 15.6.1 Recovery（24G90 / Darwin 24.6.0）上完成一次硬件启动验证：系统进入磁盘工具，PC711 型号及五个既有分区全部被正确枚举，原先约 75 秒后的第一条 Identify 超时 KP 不再出现。

![macOS 15.6.1 Recovery 中识别 PC711](docs/images/recovery-success.jpg)

验证环境：

| 项目 | 已验证值 |
|---|---|
| 控制器 | SK hynix `1C5C:174A`，NVMe class `01:08:02` |
| 设备型号 | `SKHynix_HFS512GDE9X084N`（PC711） |
| 固件 | `41010C22` |
| PCI 路径 | AMD 平台 `GPP3/NVME` |
| 故障系统 | macOS 15.6.1，Build 24G90，Darwin 24.6.0 |
| 参考系统 | macOS 26.5.1，Build 25F80，Darwin 25.5.0 |
| 引导环境 | OpenCore 1.0.8，Lilu 1.7.3 |

目前证明的范围是：控制器初始化、Identify、namespace 和 IOMedia/分区发布。尚未验证 macOS 15 的完整安装、持续读写、TRIM、睡眠唤醒或其他固件/平台。

## 故障表现

未修复的 macOS 15.6.1 在第一次 Identify Controller 后约 75 秒发生 KP：

```text
nvme: Command timeout. Identify.
MODEL=Model string not available
FW=FW Revision not available
CSTS=0x1 VID=0x1c5c DID=0x174a
```

同一硬件、同一 OpenCore 配置在 macOS 26.5.1 可由 Apple `IONVMeFamily` 原生初始化，读写和睡眠唤醒正常，因此问题被缩小到两个系统版本之间的 NVMe 初始化/中断实现差异。

## 修复原理

对两套真实 `BootKernelExtensions.kc` 的符号与机器码进行对比后发现：

1. Darwin 25 的 `IONVMeController::CreateDeviceInterrupt` 在枚举中断源前新增：

   ```cpp
   IOPCIDevice::configureInterrupts(0x20000, 1, 1, 0);
   ```

   `0x20000` 表示请求 MSI-X。

2. Darwin 24 的 `FilterInterruptRequest` / `HandleInterruptRequest` 仍有一条旧 MSI-X 特殊分支；Darwin 25 已删除它并统一使用标准事件源路径。

`PC711Probe 0.6.0` 只在 Darwin 24.6.0、启动参数 `-pc711pcompat` 存在且 PCI 身份精确匹配时执行：

- 路由 Apple 的 `CreateDeviceInterrupt`；
- 请求一个 MSI-X 向量；
- 调用原始 Apple 函数创建事件源；
- 清除 Darwin 24 控制器偏移 `0x191` 的旧 MSI-X 路径选择位 `0x10`；
- 其余 Identify、队列、完成处理、namespace 和存储 I/O 仍由 Apple 代码执行。

完整开发过程见 [docs/DEVELOPMENT.zh-CN.md](docs/DEVELOPMENT.zh-CN.md)。

## 适用范围

插件内部同时要求：

- Darwin `24.6.0`；
- 启动参数 `-pc711pcompat`；
- PCI Vendor/Device `1C5C:174A`；
- NVMe class/revision 掩码结果 `01:08:02`。

在其他 Darwin 版本或不匹配设备上兼容路由保持不活动。

同一 PCI ID 可能被多个 SK hynix OEM 型号复用。除上表环境外均应视为“未验证”，不能因为 ID 相同就直接安装到主 EFI。

## 安装

请先阅读 [中文安装与回滚指南](docs/INSTALL.zh-CN.md)。最小 OpenCore 配置为：

1. 确保 Lilu 先于 PC711Probe 加载。
2. 将 `PC711Probe.kext` 添加到 `EFI/OC/Kexts`。
3. 在 `Kernel -> Add` 中启用：
   - `BundlePath`: `PC711Probe.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `24.0.0`
   - `MaxKernel`: `24.99.99`
4. 在 `boot-args` 添加 `-pc711pcompat`。
5. 首次验证时关闭 NVMeFix，并停用任何通过 `_STA=0` 等方式隐藏目标 NVMe 的 SSDT。

调试日志可额外添加 `-pc711pdbg`，日常使用不需要。

## 构建

需要 macOS、Apple Command Line Tools 和 Git：

```bash
git clone --recurse-submodules https://github.com/hrx114514x/PC711Probe.git
cd PC711Probe
./Scripts/verify.sh
```

输出位于 `build/Debug/PC711Probe.kext`。依赖固定为仓库中的 Lilu 与 MacKernelSDK 子模块提交。

## 安全边界

- 首次测试必须使用独立 USB EFI，并保留可启动的回滚 EFI。
- 不要在 Recovery 中抹除或修改现有分区来“验证”驱动。
- 不要与隐藏 PC711 的 AML/SSDT 同时使用，否则插件无法匹配设备。
- 这是使用 Apple 私有对象布局的版本专用补丁；系统更新后默认不应假设仍然兼容。
- 使用者自行承担数据丢失、无法启动和 Kernel Panic 风险。

## 许可证与致谢

本项目代码使用 [BSD 3-Clause](LICENSE) 许可证。

- [Lilu](https://github.com/acidanthera/Lilu) — 路由与插件框架
- [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) — 内核扩展构建 SDK

第三方依赖保留各自许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。仓库不包含 Apple Kernel Collection 或 `IONVMeFamily` 二进制。
