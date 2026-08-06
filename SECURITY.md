# Safety and security

PC711Probe changes kernel behavior in the storage initialization path. Treat every unverified configuration as capable of causing a kernel panic, boot failure, filesystem damage, or data loss.

PC711Probe 会修改存储初始化阶段的内核行为。任何未经验证的配置都可能导致 KP、无法启动、文件系统损坏或数据丢失。

For private vulnerability reports, use GitHub's private vulnerability reporting feature when available. For hardware compatibility failures without sensitive data, open a normal issue after removing serial numbers and EFI identity data.

安全问题可优先使用 GitHub 私密漏洞报告；普通硬件兼容问题请在移除序列号和 EFI 身份信息后提交 Issue。
