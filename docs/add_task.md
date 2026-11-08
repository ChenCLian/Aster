# Aster：添加一种任务

适用版本：Aster 1.2.0-beta.5。

最短的实际样例是 `src/processing/affine_task.h/.cpp`。它展示了统一接口、独立参数、只读上游和具名输出。新增任务仍需要自己的计算实现；宏负责消除分散的注册代码。

## 1. 独立文件与统一接口

创建 `src/processing/my_task.h` 和 `my_task.cpp`，实现以下约定：

```cpp
class MyTask final : public aster::Task {
public:
    explicit MyTask(const QJsonObject& normalizedParameters);
    static aster::TaskDescriptor descriptor();
    aster::NodeResult execute(const aster::TaskInput& input,
                              const aster::ExecutionContext& context) override;
private:
    // 存放明确类型的实例参数，例如 double、int、枚举。
};
```

任务在执行器中通过注册表创建，传给构造函数的 JSON 已补齐默认值并通过字段类型／范围校验。没有参数的任务也接受 `const QJsonObject&`，以保持工厂入口一致。

不要使用 static 参数 setter。不要在任务内部读取控件、提交数据库或改变父节点的数据。传入旧的原地算法时，先准备任务自身的可写缓冲区。

## 2. 声明方法元数据

`descriptor()` 声明显示名称、分类路径、版本、参数和端口。`key` 由集中注册宏填入，避免重复填写导致不一致。

分类请统一引用 `processing/task_categories.h`，例如 `d.category = taskCategory(TaskCategory::Filtering);`。已有与新增方法的完整归类见 [任务分类](task_catalogue.md)。不要在两个界面入口单独填写目录；新增分类词须补齐中英文 `category.*` 资源。

普通任务设置 `inputKind`；数据源不设置。使用外部输入数据的数据源另设 `requiresSourceBinding=true`；内存源设置 `boundDataKind` 并从 `context.boundSource` 读取，文件源不设置 boundDataKind，从 `context.sourceMetadata` 取得路径。纯生成任务从参数生成数据，不能把外部文件选择隐式当成上游。

输出使用稳定的键，例如 `spectrum`、`peaks`、`power`。显示名称允许后续翻译，键不能跟随翻译改变。数据类型和算法要求分开：Series 类型验证长度和有限值，等间隔采样、正周期或单位兼容等科学约束仍由任务检查。

多个参数之间的关系、输入数据相关约束在 execute() 中检查并返回 `NodeResult::failure(...)`。需要常规校验时可使用 `validateData()`。运行器会校验输入以及成功返回的每个输出。

版本号用于识别计算和参数约定。改变参数含义、输出语义时提高版本；方法定义不自动迁移旧版本。

## 3. 集中注册

科学任务在 `scientific_registry.h` 的 `SCIENTIFIC_METHODS(X)` 中增加一行：

```cpp
X(my_task, MyTask)
```

在 `scientific_registry.cpp` 引入新类的 `.h`。宏会生成 `MethodKeys::my_task` 和工厂注册绑定。无需给执行器增加 switch/if，也无需修改工作流模型和主窗口。

在 `science/science.pro` 的 SOURCES 和 HEADERS 中加入新文件。不依赖外部科学库的已有基础任务仍使用原 `process_definitions.h` / `builtin_tasks.h` / `core.pro`。这里只使用显式构建清单，便于审查增量改动。

## 4. 输出与参数示例

输入数据通过 `dataAs<SeriesData>(input.data)` 检查类型。生成新序列后可以返回：

```cpp
return NodeResult::success(
    {{"series", makeData(std::move(result))}},
    {{"samples", sampleCount}}
);
```

其中输出键必须与描述一致。不要为了输出绘图曲线而把图例颜色、QPen、QWidget 或 NodePayload 放回计算类型。

## 5. 最少需要验证的行为

用有已知预期结果的小数据检查计算；验证参数变化只作用于该实例；验证输入没有被原地改变；验证失败输入返回清楚错误。若有长循环，按适当频率检查取消标记。

继续使用已有数学函数时，尽量保持 math 函数签名和数学行为；在任务包装层组织参数和结果。矩阵输出要显式对照 GridData 的 row-major 排列，不能依赖绘图端猜测维度。

## UI 开发者的接口

读取 `registry.descriptors()` 得到多级任务目录及参数声明。结构预检可用 `model.previewChild(...)` 或改写检查接口；实际添加、删除、插入、替换、源绑定和配对变更统一通过 `WorkflowDocument` / `WorkflowEditor` 提交。不要直接改桌面文档内部的模型，否则会绕过源绑定、配对、修订号和统一撤销历史。删除前检查影响范围，插入/替换前明确端口映射。

桌面运行使用 `WorkflowDocument::plan()` 冻结图、来源和配对，不把 WorkflowModel 本身交给 worker。普通路径通过 `prepareRuns()` 产生请求；含联合输入的关联根使用 `prepareLinkedBatch()` 生成批次计划。普通记录按数据保存，联合记录按配对保存，并保留实际双方来源。

科学算法只计算并返回具名输出，不在每个 Task 中重复写库。BatchRunner 与 ResultRepository 已统一承担保存；具体协议和 UI 对接见 [工作流契约](architecture.md)。

## UI 已自动接入的部分

`TaskCatalog` 从 descriptor.category 构造纵向多级目录和级联菜单；`ParameterEditor` 从参数声明生成数值、布尔或文本/枚举控件。新增常规任务不需要修改主窗口。显示文字通过 `presentation.cpp` / `LanguageCatalog` 从 `resources/i18n/zh.json` 和 `en.json` 获取；没有翻译的新增任务直接显示描述中的名称。

`PlotView` 已支持 Series、Spectrum、Grid、Table。沿用这些类型通常无需新增基础绘图控件；需要特殊曲线组合或领域专用展示时，只在绘图适配层扩展。若增加全新的 DataObject 类型，需要一起更新验证、编码、回读和绘图契约。

无输入的生成型任务自然属于数据源，可以独立成为根节点。需要新的外部数据绑定方式（例如按 ODP 数据库区间选取）时，另增加对应源选择面板与来源元信息。文件源、正弦生成及内置 ODP 数据源均通过任务接口提供。离线示例共用 `io/examples` 和 `ExampleSourceTask`：新增同类数据时在来源 catalog、资源清单、ODP_EXAMPLE_METHODS 与双语目录登记，保留原档案、引用/许可，并补充全量数值摘要测试。远程 ODP 查询并未实现。

详见 [开发清单](Aster_development_checklist.md) 的文件分工。保持算法不引用 QWidget、QCustomPlot 或 QSettings。

## 绘图和导出适配

常规 Series/Spectrum、Grid 与 Table 使用对应的绘图或表格适配。输出组合通过 `plot_recipe.h` 按稳定方法和端口标识声明，默认样式由 `plotPreset` 生成；无配方的输出不应仅因类型兼容而自动合并。新组合须核对自动投图、手动投图、预览及导出。颜色、可见性等显示设置不作为算法参数，也不写入数值结果。

四类 DataRef 均已支持后台 CSV。只有增加新的 DataObject 种类或改变数值形状契约时，才需要相应扩展 `io/exports/csv_export`。增加普通算法无需修改导出控制器。

## 来源、目录与翻译

- `TaskDescriptor::catalogVisible` 控制通用任务目录是否显示。默认 true；必须由专用选择器创建的输入设 false。不要依赖方法名或 `database_` 前缀判断是否显示。
- 新来源选择器可调用 `WorkflowDocument::setSources(rootId, QVector<SourceItem>)`。运行计划按 descriptor.requiresSourceBinding 自动收集；boundDataKind 存在时接口会检查内存输入类型。纯生成器不用绑定，不受文件选择影响。
- 多选文本参数的 `choices` 是稳定程序值，例如 `fixed`。资源中的 `choice.spacing_mode.fixed` 为显示文字；ParameterEditor 始终返回 itemData 中的稳定值。
- `method.<方法键>`、`parameter.<参数键>`、`category.<分类名>`、`port.<端口键>` 是现有翻译入口；缺失时回退到描述文本或稳定键。原始来源字段和科学数据列名不要为切换语言改名。
- 普通任务使用 `TaskInput.data`。双输入任务声明 `TaskDescriptor.pairedInput=true`，A/B 分别通过 `data` 和 `second` 接收，由 `prepareBatch`（同父节点）或 `prepareLinkedBatch`（显式跨节点）验证配对并交给 `BatchWorkflowExecutor` 调度。双输入任务不得交给原单数据执行入口，也不得自行读取其他节点的全局结果。桌面“管理配对”已支持同工作区普通支路和不同源根节点；不支持联合输出再次配对。接口与边界见[联合分析开发说明](development/batch_joint_analysis.md)。
- C++ 算法通过源码实现、注册和重新编译接入。常规扩展无需修改工作流执行器或主窗口。

## 结构化报错

优先使用 `NodeResult::failure(makeError(ErrorCode::..., context, technical))`，在发现问题的位置填写具体索引、数量、参数或底层返回码。不要把整段数值数组装入 context。

错误通过 `report.diagnostic` 自动转发和保存，`NodeResult::diagnostic()` 可回读；现有 `failure(QString)` 仍兼容。执行器补充 method/node_id/run_id/input_port，算法不需要依赖 UI 或数据库。跨行／合并后的序列索引用“输入点”表述，与文件物理行号区分。

内置错误用宏统一登记；独立新算法也可以使用带命名空间的自定义字符串码。中文和英文摘要／建议集中在语言资源中。完整示例、边界和字段规则见 [统一任务诊断](error_diagnostics.md)。处理建议不能自动添加节点或改写数值。

## 接入前的边界检查

详细示例与多起点语义见 [数据传输审查](architecture.md)。新 Task 复用统一接口，不添加执行器分支；请验证同一输入并发运行不会共享可写算法状态。

- 输入按 DataKind 匹配，不能因为 Spectrum 也有两条轴就当作 Series 解包。Grid→Series 必须明确切片/归约方式。
- 输出不满足契约会在生产者处失败；可选输出缺失时其消费者失败，不自动替换端口。
- 成功结果不要携带 error 或 report.diagnostic；不影响结果的提示放 warnings。
- 可捕获异常不等于崩溃隔离。检查第三方库返回码、越界条件和容量；使用会 exit/abort 的接口时先处理其前提，不能指望 catch 截住进程退出。
- 任务中的数据专有校验不可省略。工作流以外直接调用 execute 的代码，需自行完成参数规范化、通用输入和输出校验。

## 帮助与参数命名

新增任务时维护 `methodhelp.<method>`、`parameterhelp.<key>`。中文可以逐步补齐，缺失或空值回退到英文；英文也缺失时采用任务描述。多个任务共用参数键但含义不同的，使用 `parameter.<method>.<key>` 和 `parameterhelp.<method>.<key>` 覆盖，保留计算参数键以兼容已有工作流。

参数参考由任务定义与语言资源生成，不直接编辑 Markdown。中文为 `docs/reference/task_parameters.md`，英文为 `docs/reference/task_parameters_en.md`。两者的方法锚点均为 `m-<method>`，参数锚点为 `m-<method>-p-<parameter>`。生成、重新编译与校验流程见[参数参考工具](../tools/help_reference/README.md)。GitHub Actions 检查文档是否与当前定义一致。

新增内置页面加入 `resources/help.qrc`；界面回归检查注册任务的帮助覆盖与本地定位。不要将内部翻译键用作默认用户提示。
