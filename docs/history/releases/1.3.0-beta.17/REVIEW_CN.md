# Aster 1.3.0-beta.17 更新说明

第二阶段完成：有有效发布包信息时，可在更新窗口下载完整包、核对 SHA-256 并解压到新目录；旧发布没有信息时保留浏览器下载入口。自动换源不拼接片段，手动指定来源不自动切换，旧目录与数据不被覆盖。

源码构建新增 libarchive：Windows 使用 vcpkg 的 `libarchive:x64-windows`；Linux 安装 `libarchive-dev`；macOS 使用 Homebrew libarchive 并设置 ARCHIVE_PREFIX。Qt Creator 需重新运行 qmake。详见 [构建说明](../../../building.md)。

本地验证：下载测试 60 通过、设置/界面测试 67 通过；发布工具 143 通过、2 环境跳过。Windows/macOS 原生与真实线上更新尚未验收。

下一阶段完成 Actions 草稿发布、镜像原包核验和独立通道索引。暂不需要创建测试 Release，也不需要在聊天中提供凭据。

[实施计划](../../../development/updates.md) · [验证记录](../../../validation_current.md)
