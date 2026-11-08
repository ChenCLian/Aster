# 文档索引

适用版本：Aster 1.2.0-beta.5。

[项目介绍](../README.md) · [快速开始](../QUICK_START.md) · [常见问题](../FAQ.md)

## 使用 Aster

| 内容 | 入口 |
| --- | --- |
| 完整操作流程、数据库、绘图与帮助 | [使用手册](USER_GUIDE.md)、[English](USER_GUIDE_EN.md) |
| 各任务的参数含义与默认值 | [中文参数参考](reference/task_parameters.md) · [English](reference/task_parameters_en.md) |
| 下载与启动、第一次分析、导入和导出 | [快速开始](../QUICK_START.md) |
| 启动、中文字体、设置与结果疑问 | [FAQ](../FAQ.md) |
| 本版功能与平台要求 | [发布说明](../RELEASE_NOTES.md) |
| 预览、绘图交互与显示范围 | [即时预览](ui/live_preview.md)、[绘图说明](ui/plotting.md) |
| A/B 配对与交叉谱 | [操作流程](USER_GUIDE.md#cross-spectrum)、[方法与输出](methods/cross_spectrum.md) |
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
| 模块、工作流与数据传输 | [架构](architecture.md)、[预览接口](development/workflow_preview.md)、[绘图接口](development/plotting.md) |
| 添加任务与组织分类 | [功能扩展清单](Aster_development_checklist.md)、[任务接入](add_task.md)、[任务分类](task_catalogue.md) |
| 数值后端与错误诊断 | [数值后端](numeric_backends.md)、[错误诊断](error_diagnostics.md) |
| 当前版本验证 | [当前验证入口](validation_current.md)：完整回归、数值参照、性能、界面及文档检查的范围与记录 |
| 科学验证与来源摘要 | [科学证据](reference/scientific_evidence.json) |

当前版本的验证入口由根目录 `PACKAGE_STATUS.json` 的 `validation` 字段指定。程序包的原生构建、依赖检查和发布状态按平台记录，详见[验证范围](validation_current.md)。

## 历史记录

历史报告和日志保留原版本、环境及测试范围，不作为当前版本新运行的测试结果。

- [1.2.0-alpha.20 交互与压力验收](history/acceptance_1.2.0_alpha20.md)
- [1.2.0-alpha.10 发布前检查](history/release_audit_1.2.0_alpha10.md)
- [开发变更记录](development/CHANGELOG_1.2.0.md)
- [各版本原始验证材料](validation_results/)
- [1.1.1 验证说明](validation.md)及[结构化记录](validation.json)

文档编写、版本同步及归档要求见[文档维护](development/documentation.md)。
