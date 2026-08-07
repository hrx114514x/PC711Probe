# Contributing / 参与贡献

Issues and pull requests are welcome, but this project deals with storage initialization and private kernel layouts. Evidence and narrow scope are required.

欢迎提交 Issue 和 Pull Request，但本项目涉及存储初始化和 Apple 私有内核布局，必须提供明确证据并保持修改范围最小。

When reporting hardware results, include / 报告硬件结果时请提供：

- exact macOS version, build, and Darwin version / macOS 版本、Build 与 Darwin 版本；
- PCI vendor/device/class and the public model/firmware string / PCI 身份及公开型号、固件；
- OpenCore, Lilu, and PC711Probe versions / OpenCore、Lilu、PC711Probe 版本；
- whether NVMeFix or an SSDT hide rule was active / 是否启用 NVMeFix 或隐藏设备的 SSDT；
- a sanitized panic excerpt or success criteria / 脱敏后的 panic 摘要或成功标准。

Do not publish serial numbers, complete EFI folders, SMBIOS data, NVRAM dumps, credentials, or Apple proprietary binaries.

不要上传序列号、完整 EFI、SMBIOS 数据、NVRAM 转储、凭据或 Apple 专有二进制。

Before opening a pull request / 提交 PR 前：

```bash
git submodule update --init --recursive
./Scripts/verify.sh
```

Unless explicitly agreed otherwise in writing, contributions accepted into the v1.2.0-and-later development line are provided under the project's [PolyForm Noncommercial License 1.0.0](LICENSE).

除非另有明确书面约定，合入 v1.2.0 及后续开发分支的贡献均按本项目的 [PolyForm Noncommercial License 1.0.0](LICENSE) 提供。
