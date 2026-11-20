# 自动参数与按数据覆盖的约定

适用版本：1.3.3-beta.1

本约定覆盖新建节点、未访问的参数面板、按数据编辑、批量应用和恢复默认。用户说明见[参数作用范围](../USER_GUIDE.md#parameter-scope)。

## 参数来源

- TaskDescriptor 是新建任务及“恢复默认参数”的唯一默认值来源。
- 节点保存基础参数；parameterOverrides 只保存某条数据或配对相对于基础参数的差异。有效参数由 effectiveParameters 合并。
- 是否打开过编辑器不构成参数状态。showNode 仅规范化并显示已有设置，不修改工作流。
- 参数面板重建前停用待提交定时器，断开旧字段到编辑器的连接，避免销毁中发出的编辑信号污染新页面。切换数据前提交旧页面由 MainWindow 的现有流程负责。

## 自动规则与实际值

自动设置应保存为可序列化的模式、百分比、目标数量、倍率或明确约定的 0 值。不要把预览报告中的实际点数写回参数。预览和正式运行都通过注册任务解析规则，依据本条数据的实际上游输出重新计算。

演化 FFT 的默认值是 20% 输入跨度、目标 500 窗口、4 倍 FFT、线性去趋势、矩形窗、逐窗归一化、功率平方根显示和自动频率上限。窗口与资源限制仍由算法检查；失败时返回具体诊断，不静默夹取或替换为别的配置。

TaskDescriptor.prepareParameters 是可选的纯参数兼容转换，在验证与补默认值前执行。它只能扩展缺少的兼容字段，不得读取数据、界面、文件或可变全局状态，不得覆盖明确设置，并应保持规范化幂等。旧演化 FFT 参数缺少模式字段时，转换器根据已有的固定点数还原对应模式。

## 批量与恢复

- setScopedParameters(..., all=true) 统一当前流程中该节点的基础设置并清除相关个别覆盖；未来加入的数据继承基础设置。
- 自动规则随参数复制，手动固定值按明确的固定值复制。不能因为某个固定值“看起来像自动生成”而改写用户意图。
- perDatasetOnly 字段不广播。批量操作在清空基础字段前，保存每个现有数据／配对的有效值，包括恢复的节点中从基础参数继承的值；不能只遍历覆盖表。未来加入的数据保持该字段默认值。恢复默认也保留它们；例如异常点的逐点决定只能用于对应输入。
- 单条和批量编辑均要求来源 ID 仍属于当前节点。已删除、空或其他流程的来源一律拒绝；不修改工作流或添加撤销记录。
- 恢复默认走普通编辑、提交、撤销流程，只作用于当前数据或配对。用户随后点击批量按钮才扩展范围。
- 打开旧流程保留明确参数；已保存的数值结果不会因默认值改变而重算。

## 验证入口

- analysis_tests/evolution_audit.cpp：旧参数数值对照、规范化幂等、按实际输入换算、容量边界和输出单位。
- ui_tests/ui_tests.cpp：automaticEvolutionScopes、automaticEvolutionBatchButtons、evolutionPresetIsEditable、parameterRoundTrip、parameterLayoutDefaultsAndVisibility。
- 上述检查涵盖未访问数据、不同输入长度与间隔、输入替换、独立编辑、批量、撤销／重做、键盘输入后立即批量、恢复默认后立即批量，以及逐点决定保留。
- 预览提示相关检查：previewMessagesRemainReadable、evolutionResolvedPreviewFollowsInput；短提示与长提示均使用可读字号，长内容换行并滚动。

## 批量设置测试

`batchPreservesInheritedDecisions` 检查继承的逐点决定、显式清空、后加入数据及保存重开。`batchRejectsStaleScope` 检查失效／跨流程来源与执行锁。`automaticScopeReplayIsolation` 检查同名数据重排、自动与固定值混用、分支隔离及冻结请求。`automaticBatchFailureIsolation` 检查短输入失败与其他输入正常计算并保存。`joint_ui_tests/joint_ui_tests.cpp` 中的 `pairBatchIsolationAndReplay` 检查配对批量、未来配对、撤销重做和序列化后执行。

键鼠用例检查参数仍在等待提交时切换数据、立即批量应用和撤销／重做。5000 条输入的元数据用例测量批量操作耗时，并检查数值缓冲区是否共享。

## 输入列表与参数草稿

参数面板延时提交期间，删除输入或打开配对管理之前，由 MainWindow 的提交回调先将草稿保存到原输入／配对。提交失败时中止后续操作。删除后的撤销恢复已提交的参数；不能把旧草稿迁移到新的当前输入。配对窗口必须在提交完成后取得工作流版本，避免刚打开就被旧定时器提交失效。

内置／复用输入的显示名称不保证唯一。SourcePanel 刷新时使用数据 ID 恢复当前选择，不能用名称匹配。对应回归为 removeInputFlushesPendingParameters、pairManagerFlushesPendingParameters、duplicateSourceSelectionUsesIdentity。
