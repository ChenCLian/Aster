# Aster 1.1.1 历史验证记录

本页保留 1.1.1 的环境、范围与计数；当前开发版本见[验证入口](README.md)。

[结构化记录](validation.json) · [发布说明](../RELEASE_NOTES.md)

各项结果只适用于记录中的源码、环境及检查范围。QtTest 计数包含初始化和清理，不将不同批次或不同缩放比例的结果相加为一次完整回归。历史参照版本与来源摘要按原值保留。

## 算法与界面回归

日期：2026-11-06。环境：Debian 13 x86_64、Qt 6.8.2、GCC 14.2.0、Release、offscreen；数值后端为 OpenBLAS 0.3.29 / LP64 LAPACK、PocketFFT、GSL 2.8。

| 检查 | 结果与记录 |
| --- | --- |
| 分析测试 | 21 组，694 通过、0 失败、0 跳过；[日志](validation_results/release/algorithm_regression.txt) |
| 完整 UI 测试 | 761 通过、0 失败、0 跳过；[日志](validation_results/release/parameter_ui_regression.txt) |
| 参数一致性 | 53 个注册任务、228 个参数声明；通用编辑器的 223 个参数覆盖 667 个读回用例，另有文件导入专用编辑器测试；计数已包含在 UI 总数中 |

覆盖 COCO 100 亿工作单位与 20000 次模拟边界、复合深度单位匹配、多输出数据源预览、频率上限提交与旧比例兼容、科学计数法及微小数值读回，并验证参数从编辑器进入预览和正式运行后的结果一致性。

这些日志来自数值编辑修复后的完整测试。文档同步不改变算法、参数定义、语言资源或测试实现；结构化记录保存相应实现指纹。离线帮助资源更新后的聚焦验证另列于下节，未将完整算法/UI 回归重复计数。

自动回归和固定参照不能证明所有输入、所有算法的统计推断或所有平台均无缺陷。独立 MATLAB 对照只覆盖历史证据注明的方法与样例；其他方法采用各自的解析解、独立数值参照或边界测试。详见 [科学证据](reference/scientific_evidence.json) 与 [统计解释范围](methods/statistical_limits.md)。

## 文档与离线帮助

文档检查覆盖第一方 Markdown 的本地链接与锚点、源码仓库路径链接、内置资源、个人路径及发布措辞；参数参考与当前任务描述和语言资源校验。生成页不手工维护。数量、工具版本、结果及日志见 `validation.json` 的 `current_checks`。

| 检查 | 结果与记录 |
| --- | --- |
| 文档链接、锚点与资源 | [检查记录](validation_results/release/documentation_sync_checks.json)；未发现断链或个人路径 |
| 参数参考 | 53 个任务、228 个参数，中英文均与当前描述一致；生成工具 6 项测试通过；[日志](validation_results/release/parameter_reference_sync.txt) |
| 内置帮助 | 重建帮助资源后，8 个聚焦测试函数在 100% 和 150% 缩放下分别通过；各含初始化和清理，共 10 通过、0 失败；[100%](validation_results/release/help_documentation.txt)、[150%](validation_results/release/help_documentation_150.txt) |
| 随包文档 | 7 份根目录 Markdown 的 33 个本地文件链接有效；采用模拟 Qt 目录进行文档部署，未执行程序打包 |

README、FAQ、操作手册、方法说明和发布说明的使用语义与当前实现核对。`VERSION` 保持 1.1.1；同版本号不表示文件内容相同，发布时以源码提交及包内容为准。

## 保留的验证证据

`validation.json` 的 `retained_evidence` 分别记录历史来源、环境、范围和日志，不将这些计数视为新的验收结果。

| 范围 | 保留内容 |
| --- | --- |
| 发布工具 | 74 项工具回归、12 项受控 PowerShell 进程测试、Homebrew 补丁查询夹具、工作流及源码导出检查；均不代表原生程序部署 |
| macOS 原生回归 | 两种架构在提交 `7b425c5` 上七组应用测试通过；该构建的依赖收集失败，不能据此宣布当前包可发布 |
| 依赖材料 | 指定 Qt 源包的下载、摘要、许可提取，以及依赖来源和失败处理检查 |
| 帮助与设置 | 描述导出工具构建、帮助路由、许可证和设置测试，含 100% 与 150% 缩放 |
| 更新、绘图与存储 | 更新检查、样式导出、高 DPI 交互、核心、工作区及集成回归 |
| 科学参照 | [科学证据](reference/scientific_evidence.json) 中的原始版本、来源摘要及适用边界 |

## 发行验收边界

- Windows、macOS 和 Linux 的最终程序包仍需完成对应原生构建、材料收集及桌面验收。Linux offscreen 测试不代替 Windows/MSVC 或 macOS 验收。
- vcpkg、Debian/Ubuntu 和 Homebrew 受控测试不代替对实际依赖版本、配方和源包可获取性的核对。
- 本记录不证明远端 Release 附件可下载，也不构成新的统计误报率校准。
- 依赖源码作为单独 Release 附件，不收进项目源码导出目录。La2004/La2010 再分发依据仍待确认。

`resources/help.qrc` 列出的文档内置于程序。修改后须重新构建并打包，才能更新软件中的离线副本；只替换包外 Markdown 不会更新 F1 手册。
