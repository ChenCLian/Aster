# 文档索引

[项目介绍](../README.md) · [快速开始](../QUICK_START.md) · [常见问题](../FAQ.md)

## 使用 Aster

| 内容 | 入口 |
| --- | --- |
| 完整操作流程、数据库、绘图与帮助 | [使用手册](USER_GUIDE.md)、[English](USER_GUIDE_EN.md) |
| 各任务的参数含义与默认值 | [中文参数参考](reference/task_parameters.md) · [English](reference/task_parameters_en.md) |
| 安装、第一次分析、导入和导出 | [快速开始](../QUICK_START.md) |
| 启动、中文字体、设置与结果疑问 | [FAQ](../FAQ.md) |
| 本版功能与平台要求 | [发布说明](../RELEASE_NOTES.md) |
| 预览、绘图交互与显示范围 | [即时预览](ui/live_preview.md)、[绘图说明](ui/plotting.md) |
| 科学方法、参数和适用条件 | [方法目录](methods/) |
| 检验输出的解释范围 | [统计说明](methods/statistical_limits.md)、[AR1 全局检验](methods/ar1_global.md) |
| 作者、版权、数据来源与第三方许可 | [作者](../AUTHORS.md)、[许可说明](LICENSE_GUIDE_ZH.md)、[第三方说明](third_party.md) |

发行程序包附带 README、快速开始、FAQ、发布说明、作者信息、LICENSE、COPYRIGHT.md、SOURCE_CODE.md 和依赖源码索引 SOURCE_DEPENDENCIES.md。使用手册、参数参考及科学方法文档内置于程序，可通过 F1、设置中的“使用手册”或参数问号离线查看；缺失的本地章节转至 GitHub。开发与维护文档位于源码仓库的 `docs/` 中。

## 开发与维护

| 内容 | 入口 |
| --- | --- |
| 编译、依赖配置与工程入口 | [源码构建](building.md) |
| 打包、GitHub Actions 和版本发布 | [发布维护](release_maintenance_zh.md) |
| 依赖源码、许可证自动收集与 Release 附件 | [依赖材料](dependency_materials_zh.md) |
| 参数参考生成、语言回退与同步校验 | [帮助维护](../tools/help_reference/README.md) |
| 模块、工作流与数据传输 | [架构](architecture.md) |
| 添加任务与组织分类 | [功能扩展清单](Aster_development_checklist.md)、[任务接入](add_task.md)、[任务分类](task_catalogue.md) |
| 数值后端与错误诊断 | [数值后端](numeric_backends.md)、[错误诊断](error_diagnostics.md) |
| 验证记录 | [验证说明](validation.md)、[结构化记录](validation.json) |
| 科学验证与来源摘要 | [科学证据](reference/scientific_evidence.json) |

验证入口统一为 `validation.json`，`validation.md` 提供摘要，`validation_results/release/` 保存对应日志。当前检查与保留的构建、科学和界面证据分别列出；每项记录注明来源及适用范围，不将历史通过数量累计为当前检查结果。发行包的原生构建和桌面验收另行记录。

## 文档维护约定

- README 保留产品介绍、平台下载表和文档入口。
- 快速开始提供一条可复现的使用流程，详细方法留在任务帮助与方法文档。
- FAQ 按用户看到的症状组织，给出适用平台、排查方法及下一步。
- 用户文档直接说明当前行为、操作步骤及必要限制，不记录讨论过程、被弃用的设计建议或个人工作环境。
- 发布说明列出用户可见的变更；构建和实现细节写入开发文档，验证结果及适用范围写入验证记录。
- 说明使用现在时，步骤使用明确动作；同一功能在界面、手册和发布说明中使用一致名称。
- 更新功能时同步相关使用说明，调整运行要求时同时更新下载表和打包脚本生成的 `START_HERE.txt`。
