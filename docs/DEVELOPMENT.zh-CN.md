# 开发过程

[English](DEVELOPMENT.en.md) | 简体中文

PC711Probe 按照“复现故障 → 参照新系统 → 定位差异 → 最小补丁 → 硬件验证”的路线完成。

## 1. 复现并确定故障边界

目标设备为 SK hynix PC711（`1C5C:174A`）。在 macOS 15.6.1（24G90 / Darwin 24.6.0）中，控制器已经进入 Ready 状态（`CSTS=1`），但第一条 Identify Controller 命令约 75 秒后超时并触发 Kernel Panic。

同一块硬盘在 macOS 26.5.1（25F80 / Darwin 25.5.0）中可被 Apple `IONVMeFamily` 原生初始化，读写与睡眠唤醒正常。因此不需要重写 NVMe 驱动，重点是查找两个系统版本在初始化和中断处理上的差异。

## 2. 以 macOS 26 为参照进行对比

对本机合法安装的 Darwin 24 与 Darwin 25 `BootKernelExtensions.kc` 进行符号、调用关系和少量指令语义对比，重点检查：

- `IONVMeController::IssueIdentifyCommand`
- `IONVMeController::CreateDeviceInterrupt`
- `FilterInterruptRequest`
- `HandleInterruptRequest`

Identify 的提交和等待流程基本一致，关键变化位于中断创建：Darwin 25 在枚举中断源前调用：

```cpp
IOPCIDevice::configureInterrupts(0x20000, 1, 1, 0);
```

其中 `0x20000` 请求 MSI-X。Darwin 25 同时删除了 Darwin 24 中由控制器偏移 `0x191` 的 bit `0x10` 选择的旧 MSI-X 特殊路径，统一使用标准事件源路径。

随后对 Darwin 20–22 继续对比发现：`CreateDeviceInterrupt` 符号在旧 Kernel Collection 中未导出，而且到该函数执行时再申请 MSI-X 对 Ventura 已经太晚。旧版 `IOPCIFamily` 可能已解析其他中断分配，并拒绝第二次配置。

## 3. 实现最小兼容补丁

PC711Probe 保留两个受版本限制的兼容入口：

1. 自动匹配 PCI 身份 `1C5C:174A` 和 NVMe class `01:08:02`；
2. Darwin 20–22 通过高优先级 PCI probe 提前申请一个 MSI-X 向量，然后返回空值，不占用设备；
3. Darwin 23–24 路由 `CreateDeviceInterrupt`，申请 MSI-X 并清除旧路径选择位 `0x10`；
4. Apple 原始 `IONVMeFamily` 始终负责真正的设备绑定与存储 I/O。

Identify、队列、namespace 和存储 I/O 仍由 Apple `IONVMeFamily` 完成。其他 PCI ID 保持 Apple 原始行为。macOS 26 已原生支持实测 PC711，因此插件最高只加载到 Darwin 24。

## 4. 构建与验证

使用固定版本的 Lilu 和 MacKernelSDK 构建，并依次完成：

- 静态检查、架构检查和 `Info.plist` 校验；
- 独立 USB EFI 启动验证；
- macOS 13.4.1 与 15.6.1 中控制器、型号、namespace 与五个既有分区枚举；
- macOS 12.5.1 与 14.6.1 Recovery 启动正常；
- macOS 11.6 仍复现原始超时 KP，明确标记为未支持；
- 重启至 macOS 26，确认 PC711 仍为 PCIe x4 / 8.0 GT/s、SMART Verified，且另一块 NVMe 无回归。

验证中没有抹除或修改 PC711 的现有分区，仓库也不分发任何 Apple 二进制。

## 5. 当前结论

已证明该组合补丁可在 macOS 13.4.1 与 15.6.1 的上述实机环境中消除超时，并发布控制器、namespace 和分区。

macOS 11 尚未修复；其他 build、固件或平台，以及完整安装、持续读写、TRIM 和睡眠唤醒也未覆盖。因此它是一个经过硬件验证的窄范围兼容补丁，不是通用 PC711 驱动。
