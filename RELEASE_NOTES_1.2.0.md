# PC711Probe 1.2.0

Legacy macOS interrupt-timing update. / 旧版 macOS 中断时机更新。

## Changes / 变化

- Requests one MSI-X vector during early PCI matching on Darwin 20–22, then leaves Apple `IONVMeFamily` in control. / 在 Darwin 20–22 的 PCI 匹配早期申请一个 MSI-X 向量，随后仍由 Apple 驱动接管。
- Removes the direct dependency on the legacy unexported `IOPCIDevice::configureInterrupts` symbol. / 不再直接依赖旧系统未导出的符号。
- Keeps matching restricted to SK hynix `1C5C:174A` with NVMe class `01:08:02`. / 仍只匹配该 PC711 控制器身份。
- Changes the project-authored v1.2.0 code to PolyForm Noncommercial 1.0.0. / v1.2.0 项目代码改用 PolyForm 非商业许可。

## Hardware results / 实机结果

- macOS 13.4.1 Recovery (22F82): boots and enumerates the PC711 and its existing partitions. / 启动成功并识别 PC711 及既有分区。
- macOS 15.6.1 Recovery (24G90): previously verified and retained. / 既有验证保持正常。
- macOS 12.5.1 and 14.6.1 Recovery: booted normally in the multi-version test. / 多版本测试中启动正常。
- macOS 11.6 Recovery (20G165): still hits the original NVMe timeout panic and is not supported. / 仍发生原始 NVMe 超时 KP，目前不支持。

Back up EFI and data and use a rollback-capable USB EFI for the first boot. / 首次启动请备份 EFI 与数据，并使用可回滚的 U 盘 EFI。
