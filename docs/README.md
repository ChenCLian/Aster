# 文档索引

[项目介绍](../README.md) · [快速开始](../QUICK_START.md) · [常见问题](../FAQ.md)

## 使用 Aster

| 内容 | 入口 |
| --- | --- |
| 安装、第一次分析、导入和导出 | [快速开始](../QUICK_START.md) |
| 启动、中文字体、设置与结果疑问 | [FAQ](../FAQ.md) |
| 本版功能与平台要求 | [发布说明](../RELEASE_NOTES.md) |
| 预览、绘图交互与显示范围 | [即时预览](ui/live_preview.md)、[绘图说明](ui/plotting.md) |
| 科学方法、参数和适用条件 | [方法目录](methods/) |
| 检验输出的解释范围 | [统计说明](methods/statistical_limits.md)、[AR1 全局检验](methods/ar1_global.md) |
| 作者、数据来源与第三方许可 | [作者](../AUTHORS.md)、[第三方说明](third_party.md) |

发行程序包附带 README、快速开始、FAQ、发布说明和作者信息。完整方法及开发文档保留在源码仓库的 `docs/` 中。

## 开发与维护

| 内容 | 入口 |
| --- | --- |
| 编译、依赖配置与工程入口 | [源码构建](building.md) |
| 打包、GitHub Actions 和版本发布 | [发布维护](release_maintenance_zh.md) |
| 模块、工作流与数据传输 | [架构](architecture.md) |
| 添加任务与组织分类 | [功能扩展清单](Aster_development_checklist.md)、[任务接入](add_task.md)、[任务分类](task_catalogue.md) |
| 数值后端与错误诊断 | [数值后端](numeric_backends.md)、[错误诊断](error_diagnostics.md) |
| 验证记录 | [验证说明](validation.md)、[结构化记录](validation.json) |
| 科学验证与来源摘要 | [科学证据](reference/scientific_evidence.json) |

验证记录对应其记录的构建和环境。发布包的自动化检查与桌面实际验收应分别确认。

## 文档维护约定

- README 保留产品介绍、平台下载表和文档入口。
- 快速开始提供一条可复现的使用流程，详细方法留在任务帮助与方法文档。
- FAQ 按用户看到的症状组织，给出适用平台、排查方法及下一步。
- 发布说明面向该版本用户，构建与内部实现细节写入开发文档。
- 更新功能时同步相关使用说明，调整运行要求时同时更新下载表和打包脚本生成的 `START_HERE.txt`。
