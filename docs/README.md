# 文档索引

适用版本：Aster 1.3.0-beta.20。[English](README_EN.md) · [使用手册](USER_GUIDE.md) · [参数参考](reference/task_parameters.md)

按使用目的浏览下列主题。内置帮助左侧提供相同分类的可折叠目录与全文搜索，正文区另有独立的页内查找。

<a id="start"></a>
## 1. 快速入门

- [下载、启动与第一次分析](../QUICK_START.md)。
- [工作区与基本流程](USER_GUIDE.md#overview)。
- [帮助导航、全文搜索与页内查找](USER_GUIDE.md#help)。

<a id="使用-aster"></a>
<a id="features"></a>
## 2. 软件功能

### 输入与流程

- [文件、数据库复用、ODP 示例与测试信号](methods/data_sources.md) · [English](methods/data_sources_en.md)。
- [导入、多选及作用范围](USER_GUIDE.md#input-selection)。
- [添加、连接、插入、替换与撤销](USER_GUIDE.md#workflow)。
- [每条数据的参数与节点批量设置](USER_GUIDE.md#parameter-scope)。
- [A/B 配对与联合输入](USER_GUIDE.md#cross-spectrum)。

### 数据处理

- [采样检查、排序去重与重采样](methods/preprocessing.md) · [English](methods/preprocessing_en.md)。
- [选段与序列摘要](methods/selection_summary.md) · [English](methods/selection_summary_en.md)。
- [平滑与去趋势](methods/smoothing_trends.md) · [English](methods/smoothing_trends_en.md)。
- [异常值与数值变换](methods/outliers_and_transforms.md) · [English](methods/outliers_and_transforms_en.md)。
- [滤波与振幅包络](methods/filter_usage.md) · [English](methods/filter_usage_en.md)。

### 频谱、时频分析与检验

- [FFT、Welch 与演化功率谱](methods/power_spectra.md) · [English](methods/power_spectra_en.md)。
- [MTM、Lomb–Scargle、交叉谱、小波与 AR1 检验](methods/spectral_inference.md) · [English](methods/spectral_inference_en.md)。
- [COCO 扫描、蒙特卡洛与滑窗图](methods/coco_usage.md) · [English](methods/coco_usage_en.md)。

### 年代模型与天文数据

- [速率积分、年龄模型、逐周期赋龄与目标周期](methods/chronology_usage.md) · [English](methods/chronology_usage_en.md)。
- [轨道曲线、ETP 与日照](methods/astronomy_usage.md) · [English](methods/astronomy_usage_en.md)。

### 运行、结果与绘图

- [即时预览](USER_GUIDE.md#preview)及[显示范围](ui/live_preview.md#display-limits)。
- [完整运行与结果浏览](USER_GUIDE.md#run)。
- [绘图、读数与导出](USER_GUIDE.md#plots)及[绘图详细说明](ui/plotting.md)。
- [数据库备份、合并、筛选与批量管理](USER_GUIDE.md#database)。
- [复用已有结果及来源保留要求](methods/data_sources.md#database)。

<a id="shared"></a>
## 3. 通用概念与参数

多个任务引用同一公共主题，任务指南只补充各自差异。默认值与数值约束由程序定义生成，不是对所有数据的推荐设置。

| 公共主题 | 入口 |
| --- | --- |
| 采样、重复坐标、间隔与插值 | [中文](concepts/sampling.md) · [English](concepts/sampling_en.md) |
| 窗口、重叠、补零、频谱单位与显示 | [中文](concepts/spectral_parameters.md) · [English](concepts/spectral_parameters_en.md) |
| 数据处理、中心化、标准化及单位 | [中文](concepts/data_processing.md) · [English](concepts/data_processing_en.md) |
| 局部阈值、FDR、全局检验、不可用值与 COI | [中文](concepts/statistical_interpretation.md) · [English](concepts/statistical_interpretation_en.md) |
| 深度、年龄、速率、年代基准与覆盖 | [中文](concepts/chronology_coordinates.md) · [English](concepts/chronology_coordinates_en.md) |
| 全部任务参数、默认值与范围 | [中文](reference/task_parameters.md) · [English](reference/task_parameters_en.md) |

<a id="troubleshooting"></a>
## 4. 常见问题与排查

- [启动、字体、设置及结果疑问](../FAQ.md)。
- [检查输入、预览和最早失败步骤](USER_GUIDE.md#troubleshooting)。
- [错误诊断](error_diagnostics.md)。
- [当前验证范围](validation_current.md)：区分当前版本检查、历史记录及未测平台。

<a id="about"></a>
## 5. 关于与引用

- [作者与联系](../AUTHORS.md)。
- [许可说明](LICENSE_GUIDE_ZH.md) · [英文许可原文](../LICENSE)、[第三方数据与依赖](third_party.md)。
- [发布说明](../RELEASE_NOTES.md)。
- [方法公式、来源和参考实现](methods/README.md)。

上述修订的任务指南和公共概念提供中英文；部分方法技术定义、开发及平台排错页仍以中文提供，英文索引明确标注。内置帮助可离线阅读；外部资料和缺失章节通过在线链接访问。

## 开发与维护

| 内容 | 入口 |
| --- | --- |
| 编译、依赖配置与工程入口 | [源码构建](building.md) |
| 打包、GitHub Actions 和版本发布 | [发布维护](release_maintenance_zh.md) |
| 更新下载、校验与镜像契约 | [更新模块](development/updates.md) |
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
