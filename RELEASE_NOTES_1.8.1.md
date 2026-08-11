# PC711Probe 1.8.1

Compatibility metadata correction; the NVMe patch itself is unchanged. / 兼容性元数据修正；NVMe 补丁逻辑未改变。

## Changes / 变更

- The declared minimum Lilu version is now 1.6.1 instead of 1.7.2. PC711Probe's newest imported Lilu API is available with the same ABI from Lilu 1.6.1 onward. The latest Lilu release remains recommended. / 声明的 Lilu 最低版本由 1.7.2 降至 1.6.1。PC711Probe 使用的最新 Lilu API 从 1.6.1 起即已提供且 ABI 未改变，日常仍推荐使用最新版 Lilu。
- Both standard and Force builds now verify that dependency floor during the release checks. / 标准版与 Force 版均在发布验证中检查该最低依赖。
- Safe Mode remains unvalidated and disabled; normal, Installer, and Recovery environments remain supported. / 安全模式仍未验证且不启用；普通、Installer 与 Recovery 环境继续受支持。

## Downloads / 下载选择

- `PC711Probe-1.8.1.zip` (recommended): PCI `1C5C:174A` and NVMe class `01:08:02`. / 推荐标准版：匹配 PCI `1C5C:174A` 与 NVMe class `01:08:02`。
- `PC711ProbeForce-1.8.1.zip` (opt-in): any Vendor/Device with NVMe class `01:08:02`. / 可选 Force 版：忽略 Vendor/Device，只匹配 NVMe class `01:08:02`。

Never load both variants. Device matching, macOS 11–15 support, and the MSI-X compatibility behavior are identical to v1.8.0. macOS 26 continues to use native Apple support. / 严禁同时加载两个版本。设备匹配、macOS 11–15 支持范围及 MSI-X 兼容行为均与 v1.8.0 相同；macOS 26 继续使用 Apple 原生支持。
