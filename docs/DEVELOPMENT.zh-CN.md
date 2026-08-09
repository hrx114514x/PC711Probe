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

继续对比发现：部分 Recovery 与 Installer 路径在执行到 `CreateDeviceInterrupt` 之前就会发出敏感命令，此时再请求 MSI-X 已经太晚。Big Sur 的 `IOPCIFamily` 还早于 `IOPCIDevice::configureInterrupts`，当 PC711 同时提供 MSI 与 MSI-X 时会先选择 MSI。

## 3. 实现最小兼容补丁

PC711Probe 保留两个受版本限制的兼容入口，并构建两个匹配版本：

1. 标准 Kext 匹配 PCI 身份 `1C5C:174A` 和 NVMe class `01:08:02`，可选 Force Kext 只匹配 NVMe class，不检查 Vendor/Device；
2. Darwin 20 通过高优先级 PCI probe，调用 Big Sur 导出的消息中断分配器，把 PC711 已有的 MSI 分配切换为 MSI-X；
3. Darwin 21–24 由同一个早期 probe 通过 `IOPCIDevice::configureInterrupts` 申请一个 MSI-X 向量；
4. Darwin 23–24 继续路由 `CreateDeviceInterrupt` 作为后备，申请 MSI-X 并清除旧路径选择位 `0x10`；
5. Apple 原始 `IONVMeFamily` 始终负责真正的设备绑定与存储 I/O。

Identify、队列、namespace 和存储 I/O 仍由 Apple `IONVMeFamily` 完成。只有标准版会让其他 PCI ID 保持 Apple 原始行为；Force 版会故意对每一个 NVMe class 控制器应用兼容路径。实测 PC711 在 macOS 26 已原生免驱，因此两个版本最高都只加载到 Darwin 24。

## 4. 构建与验证

使用固定版本的 Lilu 和 MacKernelSDK 构建，并依次完成：

- 静态检查、架构检查和 `Info.plist` 校验；
- 独立 USB EFI 启动验证；
- macOS 11.6、12.5.1、13.4.1 与 14.6.1 Recovery 启动正常；
- macOS 15.6.1 完成完整安装，包括第二阶段 `macOS Installer` 启动；
- 安装后的 macOS 15 正常发布控制器、型号、namespace 和分区；
- 确认 PCIe x4 / 8.0 GT/s、TRIM 支持为“是”、S.M.A.R.T. 已验证，并实测写入 2766.1 MB/s、读取 3005.9 MB/s；
- 重启至 macOS 26，确认 PC711 原生工作且另一块 NVMe 无回归。

验证中没有抹除或修改 PC711 的现有分区，仓库也不分发任何 Apple 二进制。

## 5. 当前结论

已证明该组合补丁可覆盖实测的 macOS 11–15 Recovery 与 Installer 路径。macOS 15.6.1 已完整安装并从 PC711 进入系统，存储发布正常，顺序性能接近接口上限。

发布后又在 macOS 15.7.9 完成了 3 轮 32 GiB 写入/读取/两次 SHA-256 校验、双路 8 GiB 并行 I/O、2 万个小文件、3 次睡眠唤醒、3 次重启和 APFS 校验，未出现 KP、NVMe 超时、I/O 错误、掉盘或哈希不一致。

两个 BC711 型号 `HFM512GD3JX016N` 和 `HFM512GD3JX013N` 后续也已通过功能测试，证明该兼容路径不只适用于原 PC711 型号。两者的详细固件/平台矩阵与扩展负载结果尚未记录。Force 版已通过编译与静态检查，但尚未单独完成实机验证。PC711Probe 仍然是兼容补丁，不是替代 NVMe 驱动。
