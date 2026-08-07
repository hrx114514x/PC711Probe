# PC711Probe 1.7.0

Complete macOS 11–15 PC711 compatibility update. / 完整覆盖 macOS 11–15 的 PC711 兼容性更新。

## Changes / 变化

- Keeps one automatic `PC711Probe.kext`, restricted to SK hynix `1C5C:174A` with NVMe class `01:08:02`. / 保持单个自动运行的 `PC711Probe.kext`，仅匹配 SK hynix `1C5C:174A` 与 NVMe class `01:08:02`。
- Switches Big Sur's existing PC711 MSI allocation to MSI-X before Apple `IONVMeFamily` attaches. / 在 Apple `IONVMeFamily` 接管前，把 Big Sur 为 PC711 分配的 MSI 切换为 MSI-X。
- Requests MSI-X during early PCI matching on macOS 12–15, covering Recovery and second-stage Installer polled commands. / 在 macOS 12–15 的 PCI 匹配早期请求 MSI-X，覆盖 Recovery 与安装第二阶段的轮询命令。
- Retains the later macOS 14–15 interrupt-source route as a fallback. / 保留 macOS 14–15 的中断源兼容路由作为后备。
- Does not load on macOS 26, where the tested PC711 works natively. / macOS 26 不加载本插件，实测 PC711 已原生免驱。

## Hardware results / 实机结果

- macOS 11.6, 12.5.1, 13.4.1, and 14.6.1 Recovery: booted successfully. / Recovery 启动成功。
- macOS 15.6.1: completed installation, passed the second-stage `macOS Installer` boot, and entered the installed system from the PC711. / 完成安装、通过第二阶段 `macOS Installer` 启动，并从 PC711 进入系统。
- Installed-system status: PCIe x4 at 8.0 GT/s, TRIM Yes, S.M.A.R.T. Verified. / 安装后状态：PCIe x4、8.0 GT/s、TRIM 是、S.M.A.R.T. 已验证。
- Blackmagic Disk Speed Test: 2766.1 MB/s write and 3005.9 MB/s read. / 实测写入 2766.1 MB/s、读取 3005.9 MB/s。

Back up EFI and data and keep a rollback-capable EFI for the first boot on other firmware or hardware. / 其他固件或硬件首次启动前，请备份 EFI 与数据并保留可回滚 EFI。
