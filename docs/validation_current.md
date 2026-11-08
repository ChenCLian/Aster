# Aster 1.2.0-beta.7 验证范围

[文档索引](README.md) · [版本状态](../PACKAGE_STATUS.json) · [发布维护](release_maintenance_zh.md)

beta.7 移除参数及方法帮助的悬停弹框，保留点击问号定位帮助、键盘操作和辅助阅读说明。计算行为与默认设置不变。

## beta.7 检查

[帮助交互检查](validation_results/1.2.0-beta.7/interaction_checks.json)与[文档检查](validation_results/1.2.0-beta.7/documentation_checks.json)记录本次小范围修正的验证。后续章节保留此前版本的实际证据。

## beta.6 检查记录

beta.6 统一演化 FFT、演化 MTM 与 MTM 的频率上限输入，新增默认关闭的逐窗归一化，并更新参数帮助。默认物理功率谱密度、热图显示、数据格式及资源限额保持不变。

## beta.6 检查

[演化频谱检查](validation_results/1.2.0-beta.6/evolution_checks.json)记录数值参照、旧比例兼容、绝对频率裁剪、逐窗归一化、数据库往返和参数交互。705 点样例同时用于核对本版与 beta.5 的默认输出。检查使用小规模数据，不重复大规模压力测试。

[文档检查](validation_results/1.2.0-beta.6/documentation_checks.json)记录当前版本标识、双语参数参考、内置帮助与源码导出的一致性。当前构建环境为 Linux/Qt 6.8.2；Windows 与 macOS 最终程序包仍需分别重新构建和原生验收。之前的验收记录保留原版本和范围。

## beta.5 检查记录

beta.5 修正结果布局、联合预览、自动坐标范围与显示文案。数值算法、计算默认参数、限额和数据格式保持不变。前序版本的本地验收报告不作为本版的新验收，也不替代各平台最终程序包检查。

### beta.5 交互与发布工具

[交互修正记录](validation_results/1.2.0-beta.5/interaction_checks.json)记录坐标范围、输出区高度与折叠、联合预览、结果文案和导出标题的检查。原生界面使用 Linux/X11 小数据操作；QtTest 使用 offscreen。内置帮助随程序重新构建；单独覆盖外部 Markdown 不会更新 F1 手册。

本版使用小规模数据验证绘图状态与交互，不重复数值算法和大规模压力测试。以下证据保留其实际运行版本、环境与范围；不同批次通过数量不相加，也不作为所有平台均无缺陷的证明。

[版本与文档专项记录](validation_results/1.2.0-beta.5/version_documentation_audit_20261108.json)记录增量构建版本残留的复现、生成头文件修正、Windows 完整版本检查及当前文档复核。该记录不代替维护者 Windows 程序包的重新构建与验收；包名、源码导出成功和依赖材料齐全均不能证明 EXE 已更新。

[macOS CI 自检专项记录](validation_results/1.2.0-beta.5/macos_ci_checks_20261108.json)记录 arm64 与 x86_64 日志中的两项发布工具测试失败，以及本地路径别名和整秒时间戳复现。修正后通过相应回归与反向验证；含空格临时目录的 APT 测试夹具同时修正。应用代码、版本生成规则和版本号不变，macOS 原生构建与打包仍需在修复提交上重新执行。

[Linux CI 源码完整性记录](validation_results/1.2.0-beta.5/linux_ci_checks_20261108.json)记录共享测试头文件被 Git 规则忽略的复现与修正。干净检出后两个受影响测试程序重新编译并运行；旧搜索断言已按现行规则更新。发布工具测试和 Git 完整性检查通过，完整的原生 CI 打包仍需在修复提交上运行。

[共享测试头文件交付检查](validation_results/1.2.0-beta.5/shared_test_header_checks_20261108.json)记录两种 macOS 架构在提交 `927ee763` 上仍缺少辅助头文件的情况。辅助代码已移入 `tests/support`，不再依赖 Qt 生成文件命名的 Git 例外；新增相对包含路径检查。Linux 干净检出后两项测试程序编译成功，37 项与 53 项回归通过；发布工具 113 项测试无失败、1 项跳过。此结果不代替 macOS 原生运行。

[绘图视区与 CI 回归记录](validation_results/1.2.0-beta.5/viewport_ci_checks_20261108.json)记录三个平台完成编译后出现的回归失败及修正：延迟样式提交保留最新缩放范围，精度状态栏尺寸不随文字内容改变，读数浮窗测试按既有设计上限检查数值可读性。记录区分中断前的回归结果与恢复后的复测。应用界面代码已改变，需要重新编译；数值算法、限额与数据格式未变。原生 Actions 与最终程序包仍须在更新提交上验收。

[Homebrew 材料收集记录](validation_results/1.2.0-beta.5/homebrew_materials_checks_20261108.json)记录提交 `95d467f3` 的 macOS arm64 构建及 2079 项 Qt 回归通过（含初始化与清理），以及后续 GCC 本地补丁恢复失败。收集器已补充缺失提交号时的受校验恢复路径，并验证拒绝不匹配材料的行为。Linux 完整流程通过由维护者报告；本轮修复后的 macOS 原生材料收集与最终归档仍待 Actions 验证。应用代码和版本号不变。

[Homebrew 实际二进制包核验](validation_results/1.2.0-beta.5/homebrew_bottle_checks_20261108.json)记录提交 `8578d032` 仍在 GCC 配方比较处停止的原因：二进制包保存的 GNU 源码 URL 含 `/gnu/`，当前配方已去掉该前缀，其余内容一致。修正仅接受这一特定下载入口差异，并保留全部源码与补丁校验；同时修正镜像地址中重复的 `/gnu/`。本轮使用真实二进制包配方核验，具体下载、许可证提取结果与原生执行边界见记录。

## 按原版本保留的验证证据

| 范围 | 证据及边界 |
| --- | --- |
| beta.4 空位复用 | [检查记录](validation_results/1.2.0-beta.4/clear_subplot_checks.json)：显式清空、原位填入、取消和配方验证 |
| beta.3 文字整理 | [检查记录](validation_results/1.2.0-beta.3/documentation_checks.json)：界面文字、帮助和发布说明；数值实现与交互逻辑不变 |
| beta.2 名称与帮助 | [检查记录](validation_results/1.2.0-beta.2/documentation_checks.json)：启动说明、小波选项名称、双语参数参考及内置帮助；数值实现不变 |
| beta.1 文档整理 | [检查记录](validation_results/1.2.0-beta.1/documentation_checks.json)：发布材料、帮助章节、版本一致性与源码导出；历史报告及算法不变 |
| alpha.21 坐标读数浮窗 | [专项记录](validation_results/1.2.0-alpha.21/probe_checks.json)：紧凑布局、提示互斥、曲线标识、复制精度及既有读数行为；原生平台仍需验收 |
| alpha.20 交互与适度压力验收 | [验收记录](history/acceptance_1.2.0_alpha20.md)：连续绘图、窄侧栏、搜索、短序列诊断和地址/未定义行为检测；复用范围与原生平台边界单独列出 |
| alpha.19 子图布局与导出 | [专项记录](validation_results/1.2.0-alpha.19/plot_layout_checks.json)：分隔条、三图通栏、拖放/取消/失效引用、异步目标、图页恢复及 PNG/PDF 布局快照；原生拖放仍需验收 |
| alpha.18 默认投图与重建 | [专项记录](validation_results/1.2.0-alpha.18/plot_navigation_checks.json)：默认图页设置、输出选择隔离、小波/MTM 全选分组及删除重建；原生平台仍需验收 |
| alpha.17 连续绘图与精度状态 | [专项记录](validation_results/1.2.0-alpha.17/plot_interaction_checks.json)：连续操作刷新、恢复默认重读、窄窗口状态稳定性及累计绘图回归；原生平台仍需验收 |
| alpha.16 热图与组合投图 | [专项记录](validation_results/1.2.0-alpha.16/plot_refinement_checks.json)：对数周期坐标、首次及视口细化、真实精度状态、组合投图、色阶与导出一致性；完整回归与大矩阵基准，范围及限制见记录 |
| alpha.15 结果投图与布局 | [专项记录](validation_results/1.2.0-alpha.15/plot_navigation_checks.json)：默认图选择与复用、单选步骤、异步叠加目标、单位不兼容诊断、按图族限额及相关桌面回归；截图问题与已复现问题分别记录 |
| alpha.14 发行验收工具 | [专项记录](validation_results/1.2.0-alpha.14/release_readiness_checks.json)：统一回归入口的日志保留、故障状态、版本一致性、发布脚本与源码导出；新运行结果和既有桌面证据分别记录 |
| alpha.13 图页状态与性能 | [专项记录](validation_results/1.2.0-alpha.13/plot_lifecycle_checks.json)：后台图页关闭、待提交引用删除、子图取消、勾选清单失效与批量清空、导出快照及相关桌面回归；环境、计数与性能范围以记录为准 |
| alpha.12 输出浏览与投图 | [专项记录](validation_results/1.2.0-alpha.12/output_browser_checks.json)：浏览/投图分离、勾选身份、批量子图、叠加失败回退、图页容量、样式保持及相关桌面回归；环境和计数以记录为准 |
| alpha.11 预览与热图 | [专项记录](validation_results/1.2.0-alpha.11/preview_heatmap_checks.json)：主窗口源/分支往返选择、CWT 默认方向与配色、首次异步载入和换页的热图设置入口、手动样式保持及 PNG/PDF/CSV；具体计数和环境以该记录为准 |
| alpha.10 发布前检查 | [检查报告](history/release_audit_1.2.0_alpha10.md)、[结构化记录](validation_results/1.2.0-alpha.10/release_audit/checks.json)：12 个测试程序，1993 通过、0 失败、0 跳过；新增批次总量边界、大规模配对和随机跨节点执行及来源索引扩容用例。计数包含 QtTest 初始化和清理 |
| alpha.10 初始桌面回归 | [保留记录](validation_results/1.2.0-alpha.10/linked_input_ui_checks.json)：1976 通过；时间与范围以原记录为准 |
| 来源选择、配对与画布 | 同一记录含节点/数据/端口选择、取消、原子提交、撤销/重做、失效引用及 100%/125%/200% 缩放；Linux/Qt offscreen |
| 跨节点执行与存储 | [alpha.9 记录](validation_results/1.2.0-alpha.9/linked_input_checks.json)：不同普通支路及源根的真实 A/B、共享生产者、预览与保存 |
| 结构编辑与选择窗口 | [alpha.8](validation_results/1.2.0-alpha.8/rewrite_checks.json)、[alpha.7](validation_results/1.2.0-alpha.7/edit_checks.json)、[alpha.6](validation_results/1.2.0-alpha.6/popup_checks.json)：插入/替换、统一编辑、输出选择 |
| 交叉谱独立数值参照 | [alpha.2](validation_results/1.2.0-alpha.2/cross_spectrum_checks.json)、[alpha.4](validation_results/1.2.0-alpha.4/integration_checks.json)：固定 SciPy/直接 DFT 参照、公开 ODP 数据及接口核对；方法版本和零频点规则以各记录为准 |
| 性能与取消 | [alpha.10 检查报告](history/release_audit_1.2.0_alpha10.md)：更新后的非阻塞配对窗口、百万点输入、跨节点并行、主窗口事件循环和取消。保留 [alpha.5 记录](validation_results/1.2.0-alpha.5/performance_checks.json)；不同基准的计时口径不能直接混用 |
| 既有科学方法 | [科学证据](reference/scientific_evidence.json)及 [1.1.1 历史记录](validation.md)；数值对照与统计误报率校准分开 |


alpha.10 的 1993 项完整回归结果及一次关闭确认超时、后续复测均保存在其[历史报告](history/release_audit_1.2.0_alpha10.md)中。alpha.19 的完整回归、alpha.20 的交互与适度压力检查、alpha.21 的坐标读数专项各有独立记录；不能将其中某次完整回归的数量视为 beta.5 新运行的数量。测试程序的计数可能包含 QtTest 初始化和清理。

科学方法的独立数值对照与统计误报率校准分别记录；数值吻合不代表所有统计解释均已校准。历次文档检查保留在各版本目录的 `documentation_checks.json`，当前版本采用本页链接的 beta.7 专项记录。

## 最终程序包检查

维护者本地使用验收与最终分发包的验收分别记录。发布某个平台前，仍需对该平台的实际构建与附件核对：

- Windows/MSVC、macOS 对应架构或 Linux 原生构建与运行；offscreen 检查不替代窗口管理器、鼠标拖放、多显示器及系统缩放验证。
- 干净主机启动、字体、数据库、双输入工作流、绘图导出，以及签名或系统安全提示。
- 构建提交、标签、项目源码、平台依赖材料与下载入口的对应关系；本记录不声明远端 Release 已发布。
- La2004/La2010 数据再分发依据，见[第三方说明](third_party.md#轨道表再分发核验)。

联合输出再次配对、任意 DAG 连线、直接改写联合节点结构不属于 1.2.0 的开放功能范围。
