# NX2 Port Studio

[English](README.md) · [Español](README.es.md) · [Français](README.fr.md) · [日本語](README.ja.md) · **简体中文**

我正在开发面向 Apple Silicon 的原生 Switch 2 工具。**0.3.0** 新增了基于固定 nxnative 提交版本的 Mac 应用导出功能，与我的实验性 NVN2 后端分开。

我会检查游戏 ID、更新版本和完整模块构建 ID，目前只接受 **Donkey Kong Bananza（458752）**及 **Splatoon Raiders（65536）**对应的构建。我不会把上游项目的游戏测试结果当作 NX2 自身的验证。Mario Kart World 仍在计划中；目前不生成 iPhone/iPad IPA。

选择包含 `base/` 和 `updates/` 的已解密游戏目录，然后依次使用 **Inspect dump → Prepare Mac export tools → Create Mac app**。更新的 RomFS 必须完整，不能只是补丁。默认保存到 Applications；我会保留已有应用。首次编译可能需要一小时。操作成功或失败后，我会清理该操作的临时文件，仅保留私有日志及成功记录。生成已签名应用并不证明游戏能够正常游玩。

我保留输入检查、开发工作区和自己的 NVN2 研究。请参阅[导出指南](docs/nx2/NATIVE_EXPORT.md)、[完整英文说明](README.md)、[固定来源](upstreams.lock.json)及[致谢](CREDITS.md)。我保留原始许可证，不在仓库或 Studio 发布包中提供游戏、Nintendo 素材、固件、密钥、账户或存档。
