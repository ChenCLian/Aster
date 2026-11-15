# Aster 1.3.0-alpha.7 更新说明

新增 `tools/release/update_version.py` 与集中维护范围的 `version_policy.json`，程序版本仍唯一来自根目录 `VERSION`。

小更新只输入一次版本号和一句发布说明，自动同步当前文档标识、打包示例、元数据并归档旧发布说明。README、FAQ、使用手册正文保持原样；大更新再按实际变化手动补充。支持 `--dry-run`、`--check`、`--notes-file`，普通写入失败时回滚，并拒绝过期的修改计划和历史文件覆盖。

本轮已实际使用脚本换号。13 项版本/同步测试、发布工具检查、真实 qmake 增量版本构建、51 项应用更新测试和 7 项帮助生成器测试通过；Homebrew Ruby 查询测试因本机缺少 Ruby 跳过。Release 构建成功，程序报告版本正确，双语帮助与代码同步。

应用 C++ 源码未改动，未重新执行上一版的完整科学、界面及 sanitizer 回归；旧记录保留原版本。Windows/macOS 原生验收未执行。

日常命令与大更新流程见 [发布维护](../../../release_maintenance_zh.md)，详细验证见 [本版记录](../../../validation_results/1.3.0-alpha.7/checks.json)。
