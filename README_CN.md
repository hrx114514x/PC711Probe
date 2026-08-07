# PC711Probe

[English](README.md) | 简体中文

用于修复 SK hynix PC711 在旧版 macOS 中初始化时发生 `IONVMeFamily` Identify 超时 Kernel Panic 的自动 Lilu 兼容插件。

> **新发现：PC711 在 macOS 26 已经原生免驱。** 同一块实机 PC711 在 macOS 26.5.1（25F80 / Darwin 25.5.0）中可由 Apple `IONVMeFamily` 正常完成识别、读写和睡眠唤醒，不需要 PC711Probe 或 NVMeFix。PC711Probe 不在 macOS 26 加载。

> [!IMPORTANT]
> PC711Probe 必须在 NVMe Identify 取得型号字符串之前匹配控制器，因此实际匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`。SK hynix Gold P31 和 BC711 也有使用这一 PCI 身份的记录。目前只实机验证了固件 `41010C22` 的 `SKHynix_HFS512GDE9X084N`；其他 `1C5C:174A` 设备均属未验证范围，首次测试必须使用可回滚 EFI 并备份数据。

> [!NOTE]
> ## ❤️ 支持 PC711Probe
>
> 赞助会直接支持我继续开发 PC711Probe、进行硬件测试和后续 macOS 兼容性工作。
>
> **[查看赞助方式](SUPPORT.md)**

## 已验证结果

PC711Probe 1.7.0 已让同一块实机 PC711 在 macOS 11–15 全部成功启动：macOS 11–14 完成 Recovery 验证，macOS 15.6.1 完成完整安装并从 PC711 进入系统。安装后的系统升级到 macOS 15.7.9 后，又通过了下述存储压力、睡眠唤醒与重启测试。原先约 75 秒后的 Identify/命令超时 KP 不再出现。

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

## 自动匹配范围

1.7.0 不需要任何启用参数。Kext 加入 OpenCore 后自动运行，对以下控制器应用补丁：

- PCI Vendor/Device：`1C5C:174A`；
- NVMe class：`01:08:02`。

PC711 的型号字符串必须等第一次 Identify 成功后才能读取，因此插件使用 PCI 控制器身份进行预先匹配。这使不同容量和 OEM 型号成为可能，但也意味着插件无法区分已验证的 PC711 与使用同一 PCI 身份的未验证 P31/BC711。其他 PCI ID 的 NVMe 保持 Apple 原始行为。

插件声明的自动运行范围为 Darwin 20–24（macOS 11–15）。Darwin 25/macOS 26 不加载插件，实测 PC711 在该系统已原生免驱。

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
2. 确保 Lilu 先于 PC711Probe 加载。
3. 将 `PC711Probe.kext` 放入 `EFI/OC/Kexts` 并添加到 `Kernel -> Add`：
   - `BundlePath`: `PC711Probe.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `20.0.0`
   - `MaxKernel`: `24.99.99`
4. 停用通过 `_STA=0`、伪造 class/vendor/device 等方式隐藏 PC711 的 AML/SSDT。
5. 不需要添加任何 PC711Probe 启动参数。

紧急关闭可使用 `-pc711poff`。调试日志可使用 `-pc711pdbg`。

[完整中文安装与回滚指南](docs/INSTALL.zh-CN.md)

## 构建

```bash
git clone --recurse-submodules https://github.com/hrx114514x/PC711Probe.git
cd PC711Probe
./Scripts/verify.sh
```

输出：`build/Debug/PC711Probe.kext`

## 当前验证边界

macOS 11–14 Recovery 已通过启动验证；macOS 15.6.1 已完成完整安装、正常进入系统、namespace/分区发布、PCIe 3.0 x4 链路、TRIM 支持与 S.M.A.R.T. 状态确认，并实测写入 2766.1 MB/s、读取 3005.9 MB/s。在 macOS 15.7.9 中，同一块硬盘又通过了 96 GiB 顺序写入/读取/两次 SHA-256 校验、双路 8 GiB 并行 I/O、2 万个小文件、3 次睡眠唤醒、3 次重启和 APFS 校验，未发现 KP、NVMe 超时、I/O 错误、掉盘或哈希不一致。

上述结果只覆盖一块 `SKHynix_HFS512GDE9X084N`、固件 `41010C22` 和一个 AMD 平台。其他 PC711 容量/固件、Intel 平台及其他 `1C5C:174A` 产品仍未验证。首次使用请保留回滚 EFI 和数据备份，并通过 [GitHub Issues](https://github.com/hrx114514x/PC711Probe/issues) 提交独立测试结果。

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
