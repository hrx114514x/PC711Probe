# PC711Probe

[English](README.md) | 简体中文

用于修复已测 SK hynix PC711 与 BC711 在旧版 macOS 中初始化时发生 `IONVMeFamily` Identify 超时 Kernel Panic 的自动 Lilu 兼容插件。

> **新发现：PC711 在 macOS 26 已经原生免驱。** 同一块实机 PC711 在 macOS 26.5.1（25F80 / Darwin 25.5.0）中可由 Apple `IONVMeFamily` 正常完成识别、读写和睡眠唤醒，不需要 PC711Probe 或 NVMeFix。PC711Probe 不在 macOS 26 加载。

> [!IMPORTANT]
> 推荐的 `PC711Probe.kext` 匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`。v1.8.0 同时提供可选的 `PC711ProbeForce.kext`：它忽略 Vendor/Device ID，对机器中每一个 NVMe class `01:08:02` 控制器应用 MSI-X 兼容路径。严禁同时加载两个版本。只在标准版无法匹配已知故障硬盘时使用 Force，并保留可回滚 EFI 和最新数据备份。

> [!NOTE]
> ## ❤️ 支持 PC711Probe
>
> 赞助会直接支持我继续开发 PC711Probe、进行硬件测试和后续 macOS 兼容性工作。
>
> **[查看赞助方式](SUPPORT.md)**

## 已验证结果

PC711Probe 目前已有一个 PC711（`SKHynix_HFS512GDE9X084N`）和两个 BC711（`HFM512GD3JX016N`、`HFM512GD3JX013N`）测试通过。PC711 完成了 macOS 11–15 全矩阵与下述 macOS 15.7.9 扩展验证；两个 512 GB BC711 也已通过功能测试。原先约 75 秒后的 Identify/命令超时 KP 不再出现。

![PC711 运行 macOS 15.6.1，并显示 TRIM、PCIe 链路和实测磁盘性能](docs/images/macos15-installed-performance.png)

| 项目 | 已验证值 |
|---|---|
| 控制器 | SK hynix `1C5C:174A`，NVMe class `01:08:02` |
| 型号 | `SKHynix_HFS512GDE9X084N`（PC711） |
| 固件 | `41010C22` |
| Recovery 启动验证 | macOS 11.6（20G165）、12.5.1（21G83）、13.4.1（22F82）、14.6.1（23G93） |
| 完整安装验证 | macOS 15.6.1，Build 24G90，Darwin 24.6.0 |
| 扩展验证 | macOS 15.7.9，Build 24G830：96 GiB 顺序写入/读取/哈希、双路 8 GiB 并行 I/O、2 万个小文件、3 次睡眠唤醒、3 次重启和 APFS 校验 |
| macOS 15 链路/状态 | PCIe 3.0 x4、8.0 GT/s、TRIM：是、S.M.A.R.T.：已验证 |
| macOS 15 实测成绩 | 写入 2766.1 MB/s、读取 3005.9 MB/s（Blackmagic Disk Speed Test） |
| 原生系统 | macOS 26.5.1，Build 25F80，Darwin 25.5.0 |
| 引导环境 | OpenCore 1.0.8，Lilu 1.7.3 |

新增成功 BC711 型号：`HFM512GD3JX016N` 和 `HFM512GD3JX013N`。两者的详细固件、平台与压力测试矩阵尚未记录。

## 两个并列版本

v1.8.0 会构建两个互斥 Kext：

| Kext | 匹配方式 | 用途 |
|---|---|---|
| `PC711Probe.kext` | PCI `1C5C:174A` 且 NVMe class `01:08:02` | 已知 PC711/BC711 174A 系统的推荐默认版 |
| `PC711ProbeForce.kext` | 任意 PCI Vendor/Device，只要 NVMe class 为 `01:08:02` | 故障硬盘 PCI ID 不同时的可选后备版 |

型号字符串必须等第一次 Identify 成功后才能读取，因此两个版本都无法按型号选择。Force 版同时从早期 IOKit personality 和运行时路由中移除 PCI Vendor/Device 限制，因此会影响机器中所有 NVMe 控制器，包括本来不需要补丁的设备。两个 Kext 故意共用同一 Bundle ID，严禁同时启用。

两个版本都不需要启用参数，只在 Darwin 20–24（macOS 11–15）加载。Darwin 25/macOS 26 不加载，实测 PC711 在该系统已原生免驱。

## 原理

没有补丁时，PC711 控制器已经 Ready（`CSTS=1`），但 Identify 或其他早期 NVMe 命令可能无法通过旧中断路径完成，约 75 秒后触发超时 KP。

对比旧版与 macOS 26 的 Apple `IONVMeFamily` 后发现，新系统会在创建中断源前请求一个 MSI-X 向量，并移除了旧 MSI-X 特殊路径。PC711Probe 对匹配的 PC711：

1. 在 macOS 11–15 的 PCI 匹配早期请求 MSI-X，早于 Recovery 或 Installer 的敏感轮询命令；
2. macOS 11 使用 Big Sur 原始 PCI 消息中断分配器，macOS 12–15 使用 `IOPCIDevice::configureInterrupts`；
3. 保留中断源兼容路由作为后备，并在 macOS 14–15 清除旧路径选择位；
4. 随后主动放弃设备绑定，Identify、队列、namespace 和全部存储 I/O 仍由 Apple `IONVMeFamily` 完成。

[查看简明开发过程](docs/DEVELOPMENT.zh-CN.md)

## 安装

1. 备份现有 EFI，并先使用可回滚的测试 USB。
2. 二选一：通常使用 `PC711Probe.kext`；只在标准 PCI 匹配无法命中故障硬盘时使用 `PC711ProbeForce.kext`。
3. 确保 Lilu 先于选中的 PC711Probe 版本加载。
4. 将选中的 Kext 放入 `EFI/OC/Kexts` 并添加到 `Kernel -> Add`：
   - `BundlePath`: `PC711Probe.kext` 或 `PC711ProbeForce.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `20.0.0`
   - `MaxKernel`: `24.99.99`
5. 停用通过 `_STA=0`、伪造 class/vendor/device 等方式隐藏目标硬盘的 AML/SSDT。
6. 不需要添加任何 PC711Probe 启动参数，严禁同时启用两个版本。

紧急关闭可使用 `-pc711poff`。调试日志可使用 `-pc711pdbg`。

[完整中文安装与回滚指南](docs/INSTALL.zh-CN.md)

## 构建

```bash
git clone --recurse-submodules https://github.com/hrx114514x/PC711Probe.git
cd PC711Probe
./Scripts/verify.sh
```

输出：`build/Debug/PC711Probe.kext` 和 `build/Debug/PC711ProbeForce.kext`

## 当前验证边界

macOS 11–14 Recovery 已通过启动验证；macOS 15.6.1 已完成完整安装、正常进入系统、namespace/分区发布、PCIe 3.0 x4 链路、TRIM 支持与 S.M.A.R.T. 状态确认，并实测写入 2766.1 MB/s、读取 3005.9 MB/s。在 macOS 15.7.9 中，同一块硬盘又通过了 96 GiB 顺序写入/读取/两次 SHA-256 校验、双路 8 GiB 并行 I/O、2 万个小文件、3 次睡眠唤醒、3 次重启和 APFS 校验，未发现 KP、NVMe 超时、I/O 错误、掉盘或哈希不一致。

最深入的矩阵仍只覆盖一块 PC711（`SKHynix_HFS512GDE9X084N`）、固件 `41010C22` 和一个 AMD 平台。两个 BC711 型号 `HFM512GD3JX016N` 与 `HFM512GD3JX013N` 也已通过功能测试，但其详细固件/平台矩阵与同等扩展负载尚未记录。Force 版已通过编译与静态检查，但尚未单独完成实机验证。首次使用请保留回滚 EFI 和数据备份，并通过 [GitHub Issues](https://github.com/hrx114514x/PC711Probe/issues) 提交独立测试结果。

## 许可

Copyright © 2026 hrx114514x。

PC711Probe v1.2.0 及后续版本以源码公开形式按照 [PolyForm Noncommercial License 1.0.0](LICENSE) 提供。个人学习、研究、实验、业余项目及其他非商业用途可以使用、修改和分发。

未经版权持有人单独书面授权，不允许任何商业用途，包括但不限于：

- 销售 PC711Probe 或修改版；
- 捆绑进收费 EFI 或其他付费软件包；
- 用于收费黑苹果安装、维修或技术服务；
- 以其他方式进行商业分发或商业利用。

此前已经按照 BSD 3-Clause 发布的 v0.6.0 和 v1.0.0 继续适用其原许可证；本次变更不追溯撤销已经授予的权利。

第三方依赖继续适用各自许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。仓库不包含 Apple Kernel Collection 或 `IONVMeFamily` 二进制。
