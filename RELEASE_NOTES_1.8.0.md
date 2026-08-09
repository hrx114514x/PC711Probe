# PC711Probe 1.8.0

Adds a parallel Force build and two successful BC711 model reports. / 新增并列 Force 版本与两个测试通过的 BC711 型号。

## Downloads / 下载选择

- `PC711Probe-1.8.0.zip` (recommended): contains `PC711Probe.kext`, matching PCI `1C5C:174A` and NVMe class `01:08:02`. / 推荐标准包：内含 `PC711Probe.kext`，匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`。
- `PC711ProbeForce-1.8.0.zip` (opt-in): contains `PC711ProbeForce.kext`, ignoring PCI Vendor/Device and matching every NVMe class `01:08:02` controller. / 可选 Force 包：内含 `PC711ProbeForce.kext`，忽略 PCI Vendor/Device，匹配每一个 NVMe class `01:08:02` 控制器。

Never load both variants. They deliberately share `com.stationk9.driver.PC711Probe`. The Force build affects all NVMe controllers in the machine and should be used only when the standard build cannot match a known affected drive. / 严禁同时加载两个版本；它们故意共用 `com.stationk9.driver.PC711Probe`。Force 会影响机器中所有 NVMe 控制器，只应在标准版无法匹配已知故障硬盘时使用。

## Hardware results / 实机结果

- `SKHynix_HFS512GDE9X084N`, firmware `41010C22`: full macOS 11–15 and extended macOS 15.7.9 validation remains passed. / 完整 macOS 11–15 与 macOS 15.7.9 扩展验证仍通过。
- BC711 `HFM512GD3JX016N`: functional test passed. / BC711 功能测试通过。
- BC711 `HFM512GD3JX013N`: functional test passed. / BC711 功能测试通过。

The standard v1.8.0 build preserves v1.7.0 targeting behavior. The Force build has passed compilation and static verification but has not yet been separately hardware-validated. Both builds remain limited to macOS 11–15; macOS 26 uses native Apple support. / 标准 v1.8.0 保持 v1.7.0 的匹配行为。Force 已通过编译与静态检查，但尚未单独完成实机验证。两个版本都只用于 macOS 11–15；macOS 26 使用 Apple 原生支持。
