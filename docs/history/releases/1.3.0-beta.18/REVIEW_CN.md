# Aster 1.3.0-beta.18 更新说明

第三阶段已完成本地实现：四平台构建/回归/最终包检查后准备 GitHub 草稿，发布后可启用 Gitee 镜像。所有 dependency-materials 仅保留在 GitHub，Gitee 只接收符合上限的原始程序包、更新信息和干净源码快照；程序包内部许可文件不删。

文档正文统一为相对链接，内置手册读取同一份 Markdown；Release 公告按平台和标签生成网址，Gitee 沿用 GitHub 发布后的公告。本机说明及打包清单不需要维护四套。

本地：更新 75、下载 61、帮助交互 14 项通过；发布工具 163 通过、1 Ruby 环境跳过；文档生成 8 通过。原生 Windows/macOS、实际 Actions 和 Gitee 上传尚待首次验收。

下一步：提交 source/ 内容到 GitHub，运行 Build desktop packages；四个平台通过后检查草稿、日志和截图再发布。启用 Gitee 时，在 GitHub Actions 中设置 GITEE_TOKEN Secret 和 ENABLE_GITEE_MIRROR=true Variable，不要在聊天中提供令牌。

[发布维护与操作步骤](../../../release_maintenance_zh.md) · [验证范围](../../../validation_current.md) · [发布说明](RELEASE_NOTES.md)
