# Aster 1.1.1 验证记录

[结构化记录](validation.json) · [发布说明](../RELEASE_NOTES.md)

本页区分当前检查与保留的验证证据。各项结果只适用于记录中的源码、环境及检查范围；不同批次的通过数量不累计为一次完整回归。

## 当前检查

日期：2026-11-06。范围为 Windows 打包的 Python 预检、收集器进程诊断、发布工具、配套文档与源码归档。

| 检查 | 结果与记录 |
| --- | --- |
| 发布工具回归 | 27 通过、0 失败；[日志](validation_results/release/dependency_materials_tests.txt) |
| Windows 打包进程诊断 | 12 通过、0 失败；PowerShell 7.4.6 / Linux；[日志](validation_results/release/windows_packaging_process_tests.txt) |
| 文档链接、锚点和内置资源 | 检查范围与数量见 `validation.json` 的 `current_checks.documentation` |
| 源码与归档完整性 | 文件摘要、ZIP 完整性及本机配置排除结果见 `current_checks.source_packaging` |

Windows 进程测试覆盖解释器检查、标准输出与错误输出、异常退出、缺失或损坏报告、材料缺项报告保留、旧报告隔离及输出目录保护。测试使用受控子进程，不代表已在 Windows PowerShell 5.1、实际 Qt/vcpkg 环境完成打包。C++ 实现、科学数据、参数默认值、数据库 schema、本机配置及内置帮助文件未改变。

## 保留的验证证据

`validation.json` 的 `retained_evidence` 按范围平列记录来源、环境和日志，不代表这些检查已在本次重新执行。

| 范围 | 保留内容 |
| --- | --- |
| 文档与帮助 | 参数参考生成器 6 项通过；53 个任务、228 个参数的中英文参考同步；文档部署检查，见 `retained_evidence` |
| 依赖材料 | Qt 6.8.3 四个官方源包的下载、摘要与许可提取；依赖来源及失败处理检查 |
| 参数目录构建 | Release 描述导出工具及所需模块的构建日志 |
| 帮助、许可证和设置 | 聚焦的界面测试，含 100% 与 150% 缩放 |
| 主界面与更新 | 已有完整 UI 与更新测试；帮助目录行为另由聚焦测试覆盖 |
| 绘图与导出 | 样式导出和高 DPI 交互测试 |
| 核心与科学计算 | 核心、分析、数据库工作区及集成回归 |

历史科学验证独立保存在 [科学证据](reference/scientific_evidence.json)。其中的旧版本号、参考实现版本、来源摘要和统计限制是证据的一部分，不改写为 1.1.1 的新验收结果。被后续检查覆盖的重复帮助与工具日志不再单独保留。

## 发行验收边界

- 最终 Windows、macOS 和 Linux 程序包仍需在对应构建机完成原生打包、依赖材料收集和桌面验收。
- vcpkg、Debian/Ubuntu 和 Homebrew 适配器的受控测试不代替对实际发行依赖版本、配方和源包可获取性的核对。
- 本记录未验证远端 Release 附件及下载入口，也未重新执行科学统计校准。
- 依赖源码由维护者本机或 CI 收集，单独作为 Release 附件；不收进项目工程 ZIP。
- La2004/La2010 再分发依据仍待确认，未计入依赖自动收集的完成状态。

README、FAQ、使用手册及其他 `resources/help.qrc` 列出的文件内置于程序。修改这些文档后须重新构建并打包，才能更新软件中的离线副本。
