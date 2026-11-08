# Aster 算法开发与功能扩展清单

适用版本：**Aster 1.2.0-beta.6**。数值后端为 OpenBLAS/LAPACK + PocketFFT。

当前验证见 [验证记录](validation_current.md)。算法说明按职责归入 `methods/`，任务入口见 [分类表](task_catalogue.md)。

## 1. 功能扩展的基线与边界

任务模型、数据传递、调度、存储、界面、绘图、设置和诊断通过既有接口连接。新增算法应按本清单接入，并为其数学结果和错误边界提供独立验证。

平台部署和实际数据验收需分别执行。当前验证范围见 `validation_current.md`，`PACKAGE_STATUS.json` 提供机器可读记录入口；`validation.md` 和 `validation.json` 保留 1.1.1 历史证据。测试计数包含初始化与清理，不等价于所有平台或任意数据均已验证。

### 1.1 现有能力

| 项目 | 当前结论 |
| --- | --- |
| 单父节点任务树 | 每个节点最多左、右、下三个子槽；可有多个独立根 |
| 分支执行 | 普通运行按输入有界并发；联合批次按实际任务依赖就绪调度 |
| 联合分析 | TaskInput.data/second 显式 A/B；同工作区普通支路及源根间配对，稳定身份追溯 |
| 结构编辑 | 普通节点插入/替换；图、来源与配对原子提交，共用最多 100 次有效编辑历史 |
| 多输出 | 一个节点可返回多个具名输出；一个子节点选择其中一个输出 |
| 注册与界面生成 | 菜单、任务目录、常规参数表单读取任务声明 |
| 文件与数据库来源 | 新类型的来源选择界面仍需按具体需求实现 |
| 自动保存与 ID 追溯 | 计算状态与保存状态分开；保存失败可以重试 |
| 频率筛选 | 基于峰表索引和精确单位匹配，有明确端口约定 |
| 多图页、样式、导出 | 有容量上限与显示抽稀，见后文 |
| 设置与主题 | 语言重启生效，主题即时生效，设置保存在 INI |
| 统一诊断 | 正式来源读取、任务计算、执行结果链路已接入；并非所有 UI 提示都已结构化 |
| 本机验收 | 每个发行包对应独立的平台构建与桌面验证记录 |

### 1.2 扩展边界

当前不提供以下功能：任意 DAG 合流与节点重接、联合输出再次配对、直接改写联合节点输入/输出结构、免编译科学任务 DLL / 脚本插件、独立项目草稿保存、图页样式跨会话恢复、全页签多页 PDF、无限规模数据处理。

双输入比较可复用已实现的配对契约；交叉谱以数值表的实部/虚部列表示复数估计，并用有效性掩码表示未定义值。通用复数数组或三维场仍需单独设计类型。

### 1.3 平台回归清单

- [ ] 打开根工程 `Aster.pro`，确认 `bin/Aster.exe` 可以启动，运行目标不是 demo 或 tests。
- [ ] 用一份已知正确的小文件验证列选择、单位、表头和坏行策略。
- [ ] 数据满足前提时，直接运行“读取 → LOWESS → MTM”；不强制插入整理与重采样。
- [ ] 同一数据建立 MTM / CWT 两个分支；再用两个文件运行同一流程，核对不同记录 ID 与各自步骤。
- [ ] 验证同源与跨源 A/B 选择、实际预览输入、全部配对运行及 CSV 未定义值；来源删除后不误绑定同名数据。
- [ ] 验证普通节点插入/替换的端口映射、取消、撤销/重做、运行锁和过期选择窗口。
- [ ] 输入一份倒序或重复 X 的数据，确认失败原因、位置、下游阻断与详情正确；其他输入仍可完成。
- [ ] 重启后按 ID 回读结果；验证峰频率筛选、单位筛选，以及用结果建立新数据源。
- [ ] 验证切换图页、移除子图后右键、实时样式、原始 CSV、PNG / 当前页 PDF 导出。
- [ ] 验证中英文、深浅主题、数据库位置切换；新数据库位置不会被误认为自动迁移原库。

## 2. 开发时应保持的架构规则

1. `math` 放数值计算，`processing` 放 Task 包装；计算代码不读取 QWidget、QSettings，不自行决定界面颜色。
2. 每个 Task 实例拥有自己的参数与临时缓冲区，不用可变全局变量或静态参数 setter。
3. 参数与小型元数据使用 JSON；数值数组通过 `DataRef` 传递，不序列化成 JSON 再在节点间来回解析。
4. `DataRef` 是 `std::shared_ptr<const DataObject>`。上游只读；需要原地计算的旧函数，先复制到本任务的可写缓冲区。
5. 桌面编辑通过 `WorkflowDocument` / `WorkflowEditor` 提交完整候选，不能直接修改图形项或内部模型。执行前冻结树、参数与来源，后台使用快照。
6. 普通子节点的 `InputBinding` 是树连边依据；联合计算依赖由 `BatchGrouping` 明确引用 A/B。位置相邻不产生计算依赖，左右下不是调度优先级。
7. 普通算法只返回结果；普通路径由 `BatchRunner`、联合批次由 `runJointBatch` 配合 `ResultRepository` 协调保存，不在每个算法中重复写库。
8. 稳定键与显示文字分离：方法键、端口键、参数键、选项值不随中英文切换变化。
9. 科学约束由算法明确检查。失败可以建议排序、去重或重采样，预处理由显式节点完成。
10. 新算法通常只增加新实现及注册/构建项。只有契约确实变化时，才修改核心模型。

一条数据的实际执行路径是：来源绑定 → 冻结运行计划 → 后台执行器通过注册表创建 Task → 校验并转发具名输出 → 保存运行快照、结果与诊断 → 结果页按 ID 回读 → 按需绘图/导出。

记录 ID 对应一次具体运行的保存结果，不是一个文件永远不变的身份。相同文件重跑可产生新 ID；`RunId` 标识运行，`NodeId` 标识节点，不能把三者混用。

## 3. 新功能应该放在哪里

下表中的目录均相对于工程根目录；新文件名只是命名示例，并非当前已有文件。

| 要添加的内容 | 主要落点 | 通常需要额外修改 |
| --- | --- | --- |
| 单输入处理/分析算法 | `src/math/`、`src/processing/xxx_task.h/.cpp` | 科学注册表、`science/science.pro`、语言资源、相关测试 |
| 无上游的理论曲线生成 | 独立生成 Task；数学部分放 `src/math/` | 同上；声明无输入即可成为独立根 |
| 本地 ODP 区间读取等来源 | `src/io/` 的读取实现 + 来源 Task | 需要浏览/选择时新增来源面板与绑定信息 |
| 新文件格式 | `src/io/files/` 的 reader | 文件筛选、预览、选项序列化、来源 Task |
| 新算法参数 | Task 的成员、构造函数、`descriptor()`、`execute()` | 参数翻译、默认值/范围及关联约束测试 |
| 专用绘图表现 | `src/ui/results/`、必要的加载适配 | 样式编辑、导出、主题；不放到数学层 |
| 新全局设置 | `src/ui/common/app_settings.*` | `settings_dialog.*`、消费设置的模块、语言与测试 |
| 新界面面板或工具 | `src/ui/panels/` / 独立 UI 子模块 | 信号槽装配；耗时工作交给 service |
| 新数据库筛选或管理操作 | `src/io/database/` | `record_directory_service.*`、筛选面板/结果页；按需改 schema |
| 新导出格式 | 数值：`src/io/exports/`；图形：`src/ui/exports/` | 导出控制器、选择界面、验证 |
| 新数据对象种类 | `src/model/data_types.*` | 核心校验、编解码、读取、导出、数据库来源、绘图与测试 |
| 新双输入算法 | `pairedInput`、`TaskInput.data/second`、`BatchGrouping` | 复用来源选择和批次调度；补充双输入科学校验、报告、绘图及回归 |
| 任意多输入/DAG 或三维类型 | 独立设计扩展契约 | 不能以现有双输入功能代替依赖、存储和界面设计 |

新科学任务统一在 `scientific_registry.*` 注册并由 `science.pro` 构建。`PROCESS_METHODS`、`builtin_tasks.*` 和 `core.pro` 承载核心示例；模块归属按职责确定。

## 4. 新算法的完整接入清单

### 4.1 调研结束后先写清楚的内容

- [ ] 稳定方法键、界面名称、分类、初始版本。
- [ ] 是处理任务还是无上游的数据源任务。
- [ ] 输入类型、X/Y 含义、单位、X 是否要求递增/等间隔、最少样本数。
- [ ] 参数的类型、默认值、范围、稳定枚举值和参数间关联条件。
- [ ] 输出键、输出类型、单位、必需/可选输出、默认输出。
- [ ] 空输入、常量序列、重复 X、非法参数、计算失败、取消的处理。
- [ ] 典型输入规模、临时内存、库线程行为和可取消位置。
- [ ] 可信对照样例或参考实现，以及正常误差量级。

不要在尚未明确输出物理含义时先设计大量 UI 控件。

### 4.2 实现和登记

1. 复用已有数学函数，或在 `src/math/` 中新增独立计算实现。
2. 新建 `xxx_task.h/.cpp`，继承 `Task`，实现 `execute(const TaskInput&, const ExecutionContext&)`。
3. 提供接受 `const QJsonObject&` 的构造函数；将归一化后的参数转为实例成员。
4. 实现静态 `descriptor()`，声明分类、参数和输入输出。`key` 由注册宏填入。
5. 在 `src/processing/scientific_registry.h` 的 `SCIENTIFIC_METHODS(X)` 中增加 `X(稳定键, 类名)`。
6. 在 `scientific_registry.cpp` 中 include 新头文件。
7. 在 `science/science.pro` 的 `SOURCES` / `HEADERS` 中加入新文件；新数学实现也加入此模块的构建清单。
8. 添加中文/英文显示资源及专用诊断模板。
9. 在 `analysis_tests/analysis_tests.cpp` 或按实际范围选择的测试中覆盖计算与接入。
10. 从根工程重新运行 qmake、构建并启动桌面程序。

宏清单的续行注意：在原最后一行之后追加任务时，原最后一行也要补 `\`；新的最后一行不加 `\`。不要在两个注册清单中重复同一方法键。

### 参数表单的声明与默认值

`ParameterSpec.defaultValue` 是唯一默认值来源；注册表 `normalize()` 补齐参数，UI 从同一声明读取，工厂只接收归一化后的参数。请勿另外在 UI 的构造函数设置另一套业务默认值。

可选元数据：`group`（分组）、`help`（英文回退说明）、`icon`（主题图标键）、`visibleWhen`（其他参数值相等时显示）。例如固定重采样间隔仅在 `spacing_mode == "fixed"` 时显示。隐藏字段仍保留参数值，不删除/改写后台默认值。任务本身仍负责日期合法性等跨参数约束，不把 UI 可见性当作数学校验。

语言资源键为 `parameter.<key>`、`parameterhelp.<key>`、`parametergroup.<group>`、`choice.<key>.<value>`。标签保持简短，单位放在标签或帮助中；详细说明放问号按钮/提示。图标由 `ui/common/parameter_icons.*` 绘制，自动使用主题色。

MTM 峰值约定继续使用输出键 `peaks` 及列 `frequency`；单位取同节点 `spectrum` 的频率轴。默认投频谱时显示峰标记。注册了新算法但不生成上述峰表时，数据库不会凭曲线猜测峰值。

### 4.3 这些部分会自动接入

- 右侧多级任务目录和任务槽的级联菜单。
- 根据输入类型与当前位置筛选可连接任务。
- 数字、整数、布尔、文本与枚举参数的通用编辑表单。
- 默认值补齐、字段类型/范围/选项校验。
- 任务工厂创建、状态事件、下游传递、失败阻断。
- 现有数值类型的持久化、回读、常规表格/曲线/热图、CSV 导出。
- 结构化错误的转发、保存与详情展示。

自动接入以契约正确为前提。特殊来源浏览器、复杂参数控件、特殊图形组合、新数据类型不属于“注册一行就自动完成”的范围。

## 5. 可复制的普通 Task 示例

这是一个仅用于说明接入方式的“区间限幅”任务。它不要求 X 排序，也不隐式整理数据；Y 的单位保持不变。复杂数学函数应抽到 `math`，这里的简单循环留在包装中便于阅读。

### 5.1 `src/processing/example_clip_task.h`

```cpp
#ifndef ASTER_PROCESSING_EXAMPLE_CLIP_TASK_H
#define ASTER_PROCESSING_EXAMPLE_CLIP_TASK_H

#include "core/task_registry.h"

namespace aster {
class ExampleClipTask final : public Task {
public:
    explicit ExampleClipTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput& input,
                       const ExecutionContext& context) override;
private:
    double m_lower;
    double m_upper;
};
} // namespace aster
#endif
```

### 5.2 `src/processing/example_clip_task.cpp`

```cpp
#include "processing/example_clip_task.h"
#include "processing/task_helpers.h"
#include <algorithm>

namespace aster {
TaskDescriptor ExampleClipTask::descriptor()
{
    auto d = task_detail::seriesTransform(
        "Clip values", {"Preprocess", "Amplitude"});
    d.version = 1;
    d.parameters = {
        task_detail::number("clip_lower", "Lower bound", -0.5, -1e12, 1e12),
        task_detail::number("clip_upper", "Upper bound",  1.0, -1e12, 1e12)
    };
    return d;
}

ExampleClipTask::ExampleClipTask(const QJsonObject& p)
    : m_lower(p.value("clip_lower").toDouble()),
      m_upper(p.value("clip_upper").toDouble()) {}

NodeResult ExampleClipTask::execute(const TaskInput& input,
                                    const ExecutionContext& context)
{
    if (context.cancellation.isCancelled())
        return NodeResult::cancelled();

    const auto* source = dataAs<SeriesData>(input.data);
    if (!source)
        return NodeResult::failure(makeError(
            ErrorCode::InputType, {{"expected", "Series"}}));

    // 单字段范围由注册表校验；两个参数之间的关系在任务中校验。
    if (m_lower > m_upper)
        return NodeResult::failure(ErrorInfo{
            "example_clip.INVALID_BOUNDS",
            {{"lower", m_lower}, {"upper", m_upper}},
            "Lower bound must not exceed upper bound"
        });

    SeriesData output = *source;
    qsizetype changed = 0;
    for (qsizetype i = 0; i < output.y.size(); ++i) {
        if (task_detail::cancelled(context, i))
            return NodeResult::cancelled();
        const double v = std::clamp(source->y[i], m_lower, m_upper);
        changed += (v != source->y[i]);
        output.y[i] = v;
    }
    return NodeResult::success(
        {{"series", makeData(std::move(output))}},
        {{"clipped_samples", double(changed)}});
}
} // namespace aster
```

### 5.3 注册与构建片段

把以下行加入 `SCIENTIFIC_METHODS(X)` 的宏体，而不是放在宏体之外：

```cpp
X(example_clip, ExampleClipTask)
```

`scientific_registry.cpp` 增加：

```cpp
#include "processing/example_clip_task.h"
```

`science/science.pro` 增加：

```qmake
SOURCES += ../src/processing/example_clip_task.cpp
HEADERS += ../src/processing/example_clip_task.h
```

在对应 JSON 对象中合并这些字段，不要用片段覆盖完整语言文件：

```json
{
  "method.example_clip": "区间限幅",
  "parameter.clip_lower": "下限",
  "parameter.clip_upper": "上限",
  "diagnostic.example_clip.INVALID_BOUNDS.summary": "下限 {lower} 不能大于上限 {upper}。",
  "diagnostic.example_clip.INVALID_BOUNDS.suggestion": "请调整上下限后重新运行。"
}
```

```json
{
  "method.example_clip": "Clip values",
  "parameter.clip_lower": "Lower bound",
  "parameter.clip_upper": "Upper bound",
  "diagnostic.example_clip.INVALID_BOUNDS.summary": "Lower bound {lower} exceeds upper bound {upper}.",
  "diagnostic.example_clip.INVALID_BOUNDS.suggestion": "Adjust the bounds and run again."
}
```

最小预期结果：默认参数下，Y = `[-2, -0.2, 0.8, 3]` 输出 `[-0.5, -0.2, 0.8, 1]`，X、单位与原输入不变；`clipped_samples = 2`。下限大于上限应返回结构化失败。正常执行应走注册表/执行器，避免直接用未归一化参数构造 Task。

接入该示例时，应验证注册、参数校验、工作流执行、输入不变性、结果及单位、错误与取消、SQLite 回读和 CSV 导出。当前版本的构建与回归结果见 [验证记录](validation_current.md)。

## 6. 输入、输出和参数的精确约定

### 6.1 数值载荷

| 类型 | 结构 | 必须遵守的约束 |
| --- | --- | --- |
| `SeriesData` | `x`, `y`, `xAxis`, `yAxis` | 非空、等长、有限值；递增/等间隔属于具体算法约束 |
| `SpectrumData` | `frequency`, `power` 及轴信息 | 非空、等长、有限值；频率与 power 都不能为负 |
| `GridData` | `x`, `y`, `values`, `rows`, `columns` 及轴信息 | `rows == y.size()`，`columns == x.size()`；行优先 `values[row * columns + column]` |
| `TableData` | `columns`、二维 `double` 行、可选 `defined` 掩码 | 列名非空且唯一；行宽一致且数值有限；掩码为空或与表同形、只含 0/1；可以无数据行 |

当前 Table 是数值表，不是包含任意文本、日期或复数的通用数据框。负值相位曲线不应塞入要求非负的 Spectrum.power；可根据语义用 Series。未定义单元格使用 `defined` 掩码，不用 NaN 冒充数值；存储、表格、曲线和 CSV 必须保留这一状态。通用复数阵列、三维场若无法合理表示，应单独设计类型。

元数据保存在 `NodeResult.report`，提示保存在 `warnings`，错误通过 `failure(...)`。不要把大数组、指针、QPen、QWidget 塞进 report。

### 6.2 任务声明

- `key`：稳定程序标识；内置方法/参数/输出键遵循小写字母开头，随后小写字母、数字或下划线。
- `version`：算法或参数/输出语义变化时提高；仅改显示文字通常不需要。
- `category`：层级列表，例如 `{"Analysis", "Spectrum"}`；分类文字可翻译。
- `inputKind`：普通任务指定一种 DataKind；无上游来源任务不设置。
- `pairedInput`：双输入任务设为 true，A/B 均遵循 inputKind；原单输入执行器拒绝此类任务。
- `outputs`：列出每个端口的稳定键、显示名、类型与 `required`。
- `defaultOutput`：必须指向已声明端口。建议指向正常情况下总会生成的主要输出。
- `parameters`：Number、Integer、Boolean、Text；Text 的 `choices` 非空时生成枚举下拉框。
- `catalogVisible`：是否进入通用目录；需要专用选择器才能创建的来源可设 false。

执行器会拒绝未声明输出、缺少必需输出、输出类型错误、非法数值等情况。可选输出没有生成时，不要返回一个空指针占位；直接不放入输出 map。下游若选择了未生成的可选端口，应接受相应的不可用处理。

版本号不是自动兼容系统：当前不支持同一方法键的多个版本随意并存，也不会自动迁移旧参数定义。改变语义时保留历史结果可读性，并明确哪些旧快照不能直接重跑。

### 6.3 参数界面的边界

通用表单覆盖常用标量、分组、visibleWhen 条件显隐，以及带 fileFilter 的文本文件选择；数组编辑、动态行表或其他特殊交互仍需单独设计。参数自动提交，新的普通读取/计算任务自动进入节点输出预览。浮点参数编辑支持科学计数法，并保留可往返到同一 double 的显示精度；新增参数须验证界面读回、节点切换、预览及正式运行的数值一致性。

如果需要复杂表单，在 UI 层新增独立编辑器，通过节点 ID 与 `WorkflowDocument::setParameters()` 提交参数；Task 不持有控件指针。关联约束仍需在任务中检查，不能只依赖控件的限制。

## 7. 新数据源与理论曲线生成

### 7.1 无上游的生成任务

日照、偏心率或其他理论曲线的生成可以放在任务界面，作为独立根节点。

- 声明无 `inputKind`；如果从 `seriesTransform()` 辅助函数起步，调用 `d.inputKind.reset()`。
- 纯参数生成不需要外部绑定，`requiresSourceBinding` 保持 false。
- 参数声明时间起止、步长、方案等；单位与约定由对应算法明确定义。
- 返回 Series / Grid / Table 等标准输出，后面可连接现有处理任务。
- 每个无绑定根运行一次，不会因为另一个文件源选择了多个文件而自动重复生成多次。

参考现有 `src/processing/astronomy_source_tasks.h/.cpp`（真实 La2004 数据源）和 `sine_source_task.h/.cpp`（简单测试信号）。La2004 已支持 0–50000 ka；其余天文解仍需分别核对资料、年代和参数，不自动视作已支持。

### 7.2 外部文件、本地资料库或 ODP 区间

“按站点、孔号、变量、时间/深度区间选一段本地数据”建议实现为来源任务：

1. 在 `io` 层实现读取与范围选择，返回标准数据和来源元信息。
2. Task 声明无上游、`requiresSourceBinding = true`。
3. 若绑定的是内存载荷，设置 `boundDataKind`，从 `context.boundSource` 读取。
4. 若绑定的是文件/记录定位符，从 `context.sourceMetadata` 读取；不要把所有来源都强行当作 boundSource。
5. 专用来源选择面板调用 `WorkflowDocument::setSources(rootId, QVector<SourceItem>)`。
6. 冻结站点、变量、区间、路径/记录标识、导入选项和资料版本；工作线程不再读取正在变化的控件。
7. 运行结果保留实际来源输出和来源说明，以便按 ID 回查。

来源绑定接口已有，但专用 ODP 浏览器、下载器、缓存和站点目录并未实现。对需要专用选择器的来源设 `catalogVisible = false`，由选择器创建根节点。

### 7.3 新文件格式

新增 Excel、NetCDF 等格式时，不能只修改文件选择框后缀：需要 reader、有限预览、正式读取、列/变量选择、单位和错误定位，并统一使用相同解析规则。按需给 `io/io.pro` 和依赖配置加入库；列识别只探查少量行；曲线预览只在展开时后台读取当前文件，并限制显示点数。不要在 GUI 线程解析，也不要预加载整个文件列表。

现有文本读取参考 `series_file_reader.*`、`file_source_task.*`、`file_options_editor.*`、`source_panel.*`。

## 8. 频率筛选与峰值展示：新增频谱算法必须注意

这是当前框架中有明确语义约定的一处，不能只看到“自动存库”就忽略。

`src/io/database/peak_index.cpp::indexPeaks()` 目前读取同一个节点的：

- 端口 `peaks`：类型 `TableData`。
- 表中列名 `frequency`：用于频率筛选，只索引有限且大于 0 的值。
- 同节点端口 `spectrum`：类型 `SpectrumData`，从 `frequencyAxis.unit` 取得单位。

若需要在频谱上显示峰点，当前绘图还要求 `peaks` 中有 `power` 列，并以 `spectrum` 为主输出进行叠加。

建议新频谱算法采用：

```text
spectrum : SpectrumData
peaks    : TableData，至少包含 frequency 和 power 列
```

端口键和列名必须保持这些稳定英文标识，翻译只改界面标签。单位要按项目约定保持一致，例如 `1/ka` 与 `1/yr` 不会被自动当作相同单位换算。没有 `spectrum` 时峰表仍可能以空单位建立索引，该索引不具有已知的物理单位。

当前“频率筛选”是检索检测到的峰频率，不是查找所有覆盖该频率范围的谱，也不是扫描谱中每个频率采样点。

如果新算法需要不同端口/列名或独立峰表的单位描述，建议先抽出小型“峰输出语义描述”再扩展索引与绘图。当前遵守已有约定即可，不必为尚未出现的需求立即重构。已有通用存储会保存任意合法端口，但不会自动识别任意命名的峰表。

## 9. 新算法的报错与警告

普通错误优先复用 `src/core/error_info.h` 中的内置码：

```cpp
return NodeResult::failure(makeError(
    ErrorCode::TooFewSamples,
    {{"actual", double(source->x.size())}, {"minimum", 8}}));
```

专用错误可以像示例一样使用带命名空间的稳定字符串码，不需要改执行器或数据库。跨多个算法共享的新错误再加入 `ASTER_ERROR_CODES(X)`。

- `diagnostic.<CODE>.summary`：简短原因。
- `diagnostic.<CODE>.suggestion`：可选处理建议。
- `diagnostic.field.<字段>`：详情里的字段显示名。
- `technical`：底层异常或库返回信息，允许保留原文。

错误上下文只放小型标量。当前最多 64 个上下文字段，单字符串 1024 字符，technical 4096 字符。执行器补充 method、node_id、run_id、input_port 等定位信息；算法不自行造数据库 ID。

算法数据索引显示为从 1 开始的“输入点”；正式文件 reader 才能报告物理文件行号。插值、排序或筛选后的数组索引不对应原文件行号。

主要结果仍可用的附加分析失败可以进入 `warnings`；不能把整个算法失败包装成成功。下游没有执行的状态是 Blocked，不应伪造成它自己的数学计算失败。

## 10. 数据库、导出与持久化扩展

### 10.1 普通算法无需新增表

沿用四种数值类型时，已有仓库保存任务方法/版本、参数、状态、报告、警告、错误、运行快照、来源和具名数值输出。数值采用版本化 BLOB 编码；新增一个合法端口一般不需要新增 SQL 表。

既有 `ResultRepository` 负责事务与回读，`BatchRunner` 区分计算和持久化结果。算法中不要直接操作结果数据库，以免破坏保存重试与并发协调。

### 10.2 新的数据库操作

- [ ] 先判断新增字段是备注/来源等元信息，还是必须可索引检索的业务字段。
- [ ] 元信息可复用现有 report/provenance；频繁查询的字段再考虑结构化列/索引。
- [ ] 仓库接口放 `result_repository.*` 或独立数据库组件。
- [ ] UI 通过 `record_directory_service.*` 等服务调用，避免耗时 SQL 阻塞界面。
- [ ] 新删除/引用操作遵循已有依赖保护和事务；错误交回界面。
- [ ] 测试新建库、写入、回读、筛选和删除语义。

当前 schema 为 3，普通数值载荷为 v1、带有效性掩码的表为 v2，表前缀为 `aster_`。1.1.1 不能读取新掩码表。数据库结构变更需声明支持的来源版本，并设计备份、事务迁移和失败恢复；不兼容格式应明确拒绝，不能静默误读或删除用户数据。程序版本变化本身不要求数据库迁移。

同数据库已提交的结果引用有删除保护；不要把它描述成跨数据库的全局引用管理。数据库位置设置只切换使用的库，不搬迁旧库。

### 10.3 导出

- 数值导出：扩展 `src/io/exports/` 与 `ui/services/export_controller.*`。
- 图形导出：扩展 `src/ui/exports/`，复用与屏幕一致的绘图准备逻辑。
- 原始 CSV 应读取完整数值输出；不能把显示抽稀后的点当原始分析数据。
- PNG/PDF 当前采用显示准备数据；密集曲线可能是保留极值的显示近似。PDF 的曲线/文字可矢量化，但矢量格式不代表包含每个原始采样点。
- 遵守已有导出目标绑定、后台准备、取消和数据库路径保护。

## 11. 新绘图功能与新 UI 功能

### 11.1 通用图形

Series/Spectrum 可复用曲线显示，Grid 可复用热图，Table 可复用表格。它们的“能显示”不代表所有领域图形自动完成：置信带、专用多轴对照或特殊标注需要适配。

组合绘图通过 `plot_recipe.h` 的稳定方法/输出标识组织，默认样式由 `plotPreset` 集中生成。新增专用输出组合时，同时检查配方、默认样式、`PlotView`、结果读取和导出适配；预览、自动投图和手动投图应使用一致的语义。绘图配置不写入算法数值或结果数据库。

Grid 的配色、颜色级数、线性/对数映射、固定色阶和像素插值统一由 `HeatmapStyleEditor` → `PlotStyle` → `PlotView` 处理；不在算法中修改原始值以迎合颜色。新热图若需要局部 95% 边界，可提供与主网格完全相同 X/Y 坐标的 `significance_ratio` Grid（功率/该阈值），缺少时只显示主热图并给出提示。不同显著性水平或其他含义不能冒用该端口，应扩展语义与标签。COI 继续用 `coi` Series。

添加新的栅格叠加时同时检查 `ResultRepository` 的共同 family 选择和 `PlotRequest` 采样策略；阈值轮廓不能直接用块最大值替代实际坐标样本。渲染预算超限必须有提示，不能输出部分边界当完整结果。新样式 UI 使用 `style_section.h` 分组、语言 JSON 和主题模板，复用 ColorButton；导出仍调用相同 PlotView。显示数据通过 `PlotDataLoader` / `plot_reader` 准备，避免新增平行的数据读取路径。

新增绘图表现的清单：

- [ ] 明确是一个输出的渲染方式，还是多个输出的组合。
- [ ] 绘图适配放 UI 层；原数值保持不变。
- [ ] 必要时扩展 `PlotStyle` 和对应样式编辑器。
- [ ] 大数据准备通过 `PlotDataLoader` / `plot_reader`，图形对象只在 GUI 线程创建或更新。
- [ ] 绘图、当前子图导出、整页导出使用一致的语义。
- [ ] 主题切换、用户自定义颜色、图页关闭/切换、缩放范围恢复都要检查。
- [ ] 新图形只在确有需要时新增独立类，不继续把所有领域细节堆进 `MainWindow`。

### 11.2 新面板、工具按钮或页面

新面板自行管理自己的控件与布局，主窗口只负责创建、装配和信号槽连接。读取结果按 record/node/port 等稳定引用，不长期捕获可能被删除的图形裸指针。耗时工作放 `ui/services/` 或对应应用服务；返回时用稳定请求标识排除过期结果。

保持布局可伸缩、滚动、最小尺寸合理；使用现有 `CollapsiblePanel` 等容器。信号建立一次，重绑对象时解除旧绑定；面板删除/切页后不继续执行失效回调。

## 12. 新设置、语言和主题

### 12.1 增加一个全局设置

设置不是自动由 TaskDescriptor 生成。通常需要改四处：

1. `src/ui/common/app_settings.h/.cpp`：字段、默认值、稳定 INI 键、load/save 与范围校验。
2. `src/ui/settings/settings_dialog.*`：编辑控件与读取结果。
3. 使用该设置的服务/组件；主窗口只分发设置，不接管算法实现。
4. `resources/i18n/zh.json` / `en.json` 与设置回读测试。

明确生效时机：主题可以即时；并发数用于后续批次；语言当前需要重启；数据库路径切换后使用新库。新设置若影响科学计算，运行时应把实际值冻结并记录，避免只存一个全局开关导致历史结果无法解释。

全局偏好与节点参数不同：某次 MTM 的 NW 应是节点参数，不放成影响所有任务实例的可变全局值。

设置文件实际路径由 `AppSettings::configPath()` 使用 `QStandardPaths` 计算，不硬编码 Windows 用户名。必要时可在设置界面显示该真实路径。

### 12.2 语言

当前资源以 JSON 编译进 qrc，不是把磁盘上的任意 JSON 换掉就能热更新。新增字段后需要重新编译；已设置的语言在重启时加载。

| 内容 | 翻译键 |
| --- | --- |
| 方法名 | `method.<method_key>` |
| 分类 | `category.<分类字符串>` |
| 参数 | `parameter.<method_key>.<parameter_key>`，回退到 `parameter.<parameter_key>` |
| 参数帮助 | `parameterhelp.<method_key>.<parameter_key>`，回退到 `parameterhelp.<parameter_key>` |
| 枚举显示 | `choice.<parameter_key>.<stable_value>` |
| 通用端口 | `port.<port_key>` |
| 特定算法的端口文字 | `output.<method_key>.<port_key>` |
| 界面文字 | `ui.<key>` |
| 诊断 | `diagnostic.<CODE>.summary` / `.suggestion` |

同名参数在不同任务中的含义不同时，使用按方法区分的翻译键覆盖通用标签和帮助，不为显示差异修改已持久化的参数键。中文缺失或空值时回退到英文；查找顺序与生成参数参考一致，详见[帮助维护](../tools/help_reference/README.md)。输出名称也支持按方法区分。

算法/第三方库的原始技术细节可能仍是英文；不要将“已有双语显示”误写成“所有底层字符串已经翻译”。

### 12.3 主题

已有统一模板：`resources/themes/base.qss` 使用 `@surface@`、`@text@`、`@accent@` 等角色；`light.json` 和 `dark.json` 提供颜色。

- 新控件优先复用现有角色与 objectName 样式。
- 确需新颜色角色时，同时添加到两套主题并测试。
- 自绘图形使用 `themeColor("role")`，响应 `ThemeManager::changed`。
- 不在各面板中散落浅色背景和黑色字体的固定 QSS。
- 区分随主题变化的默认科学配色与用户手动指定颜色，主题切换不覆盖后者。
- 新图标/资源加入 `resources/aster.qrc`；不要依赖开发者电脑的绝对路径。

## 13. 何时才需要调整核心架构

### 13.1 双输入与跨节点引用

`TaskInput.data` 和 `second` 分别接收 A/B。`NodeDefinition` 保持一个树父节点；联合节点的实际计算输入由 `BatchGrouping` 中的来源节点、数据身份及端口明确声明。桌面选择器支持同工作区普通支路与不同源根，不提供任意输入数量或联合输出再次配对。

新增双输入算法可复用 `pairedInput`、`prepareLinkedBatch`、`BatchWorkflowExecutor`、配对界面及保存路径。算法仍须检查坐标、单位、长度等科学前提；不得通过全局结果表自行寻找另一个输入。更多输入、联合结果再次汇合、任意 DAG 接线或跨已保存批次的实时关联，需要先设计新的依赖、身份、持久化和界面约定。

详情见[联合分析执行约定](development/batch_joint_analysis.md)和[交叉谱接口](development/cross_spectrum_interfaces.md)。

### 13.2 新 DataKind

这是跨模块改动，不能只在宏中加一个枚举值。清单：

- [ ] `data_types.h/.cpp`：新结构、variant、种类映射、校验。
- [ ] `data_codec.*`、`data_format.h`：编码/解码、版本、容量与维度边界。
- [ ] 仓库的类型解析与对应数据读取；必要时扩展 `plot_reader.*`。
- [ ] CSV 或新格式导出；不能无定义地挤进现有二维表。
- [ ] 数据库来源 Task、结果复用中的种类选择与引用。
- [ ] 绘图/表格适配、参数/目录展示所需标签。
- [ ] 输入输出类型测试、存储回读、非法载荷、导出与 UI 接入测试。

优先复用现有类型，但不要为了少改文件而歪曲物理语义。

### 13.3 扩展原则

按算法的数据契约和复用需求扩展模块。引入插件、脚本执行或新的调度机制前，先确定接口、执行权限、兼容范围及验证要求。

## 14. 性能、线程与取消

- 普通请求的并发由 `BatchRunner` 管理，每个请求内部保持顺序；联合批次由 `BatchWorkflowExecutor` 按就绪任务调度。任务内部不再创建额外线程池。
- 当前 OpenBLAS 和 PocketFFT 的单次计算内部均为1线程，外层负责不同数据间并行；不要从每个任务反复修改全局线程数。
- 调整数值库线程策略时，联合评估外层并发和内层线程数，避免线程过量与内存峰值。
- 不使用跨任务可变静态缓冲区；第三方库的上下文/计划对象需核实是否可并行使用。
- 长循环周期检查 `context.cancellation`；封闭的底层调用可能无法中途取消，应记录真实响应边界。
- 输入共享只读，输出按需分配；避免为按元素操作构造巨大对角矩阵。
- 估算网格与中间矩阵的总内存，乘法先做溢出检查，再分配。
- 取消、失败与销毁采用 RAII 清理；失败不发布未完成数值结果。
- 后台进行读取/显示数据准备，最终 Qt 绘制在 GUI 线程；即时修改样式仍有实际绘制成本。

现有容量不是无限：最多 32 图页、每页 4 子图、每子图 8 层；数值编码每输出最多 2000 万数字、编码字节上限 192 MiB；文本文件读取默认最多 100 万行；算法可能有更小的专属上限。绘图准备还有独立预算，查看 `src/io/database/plot_request.h`、`data_format.h` 和各算法约束。

目前曲线缩放细化仍扫描 X/Y，算法与原始 CSV 仍会完整读取单个输出。有界显示控制绘图资源，并不提供完全按磁盘窗口随机访问的存储机制。

## 15. 每个新算法的验收与测试清单

按改动影响范围测试，不必为每个算法重写整个平台。

| 层次 | 至少检查什么 | 对应位置 |
| --- | --- | --- |
| 数学正确性 | 有已知答案的小样例/可信参考；与算法相关的尺度、边界、单位 | `analysis_tests/`，必要时独立数学测试 |
| Task 契约 | 参数默认值、输入不被修改、输出键/类型/单位、非法组合、取消 | 分析测试；纯核心契约放 `tests/` |
| 工作流接入 | 新任务可创建、可连接、多个输出明确选择、上游失败时阻断 | `tests/` / `ui_tests/` / `integration_tests/` |
| 持久化 | 从文件/生成源执行、保存、按 ID 回读、数值一致、诊断保留 | `analysis_tests/` / `integration_tests/` |
| 专用能力 | 新频谱的峰检索与单位、新图形的显示/导出、新来源的绑定 | 按实际修改选择相关测试 |
| 本地体验 | Windows 启动、真实数据、界面响应、中文/英文和深浅主题 | 目标平台桌面验收 |

已存在可参考的端到端测试：`integration_tests/interaction_safety_tests.cpp` 中的 `fileToBranchesToDatabaseAndCsv()`、`externalSourcesAndGeneratorsNeedNoDispatchEdits()`。它们展示了结果保存、峰频率检索、数据复用、信号线程与新来源的接入。

至少准备：一个正常样例、一个有明确原因的失败样例、一组典型实际规模数据。若修改类型、执行器、编码、数据库 schema 或公共绘图，则扩大到受影响的完整回归组。新算法“可编译”与“科学上得到正确结果”分别验收。

## 16. 算法调研工作单

每个候选算法先填这张表，确认可行后再写 Task：

| 字段 | 要填写的内容 |
| --- | --- |
| 科学/操作目的 | 用户用它回答什么问题，替代哪一步手工操作 |
| 数据前提 | 类型、单位、采样方式、长度、缺测、适用范围 |
| 文献与实现 | 方法出处、参考结果、可复用实现及依赖版本 |
| 输出解释 | 每个输出的物理含义、单位、置信/适用限制 |
| 参数设计 | 少量常用项、默认值来源、必要的高级参数 |
| 工作流角色 | 普通任务/生成源/外部来源；单输入是否足够 |
| 最小验证 | 合成或解析样例、真实数据、对照方法与容差 |
| 性能预算 | 典型点数、最大网格、内存、取消位置、线程行为 |
| 接入范围 | 仅 math+Task，还是涉及新类型/专用来源/特殊绘图 |
| 验收结果 | 能否复现、限制、剩余问题、版本与测试记录 |

接入顺序：明确文献、输入输出与验证参照，完成数值实现及任务封装，再验证存储、绘图和桌面交互。涉及双输入、新类型或专用界面时，先补充对应契约。

## 17. 发布前检查

- [ ] 基于当前发布版本或明确指定的开发基线。
- [ ] 头文件使用 `.h`；新功能职责独立，避免无关的全局重命名和文件搬迁。
- [ ] 新方法只登记一次，key / version / 参数 / 端口保持一致。
- [ ] 构建清单、语言资源、诊断、所需资源均已登记。
- [ ] 新增输出的数据库、CSV、绘图语义明确，频率检索契约没有被破坏。
- [ ] 不把缓存、构建目录、个人 `local.pri`、数据库和机器绝对路径打进源码包。
- [ ] 说明当前版本改动文件、验证范围、已知限制；平台验证未做的明确标注。
- [ ] 根据真实改动更新 `docs/add_task.md`、相关模块说明与 `PACKAGE_STATUS.json`，不把计划写成已完成。

## 18. 数据型任务的维护

数据型生成任务可参考 `orbital_table_tasks.*`。读取逻辑放在 `io/astronomy`，能力和来源声明放在 `resources/astronomy/solutions/catalogue.json`。参数声明的 `fileFilter` 可提供文件选择器，统一使用参数提交与后台执行路径。

## 19. 当前科学计算约定

| 方法 | 当前版本与默认行为 | 维护要点 |
| --- | --- | --- |
| 整理与重采样 | preprocess v4；重复 X 求均值、中位间隔、线性插值 | 用户显式添加；不强制排序/重采样；自然样条和 Akima 是单独选择 |
| LOWESS | detrend_lowess v4；fraction=.35，iterations=0 | 普通 LOWESS；5次可选稳健迭代，不能承诺与 MATLAB 退化邻域逐点相同；保留均值为 y−trend+mean(trend) |
| MTM | mtm v7；NW=2，NFFT=N | 正式 Task 使用 FullSpectrum 停止准则；未指定 Fs 的 Acycle 功率需乘 2πdt 才与物理 PSD 比较 |
| AR1/SWA/FDR | MTM 附加输出 | 完整单边频域拟合/检验；frequency_max（兼容 fmax_ratio）只裁剪输出；Acycle 带宽 H(floor(NW)) 修正，不标为通用 BY |
| CWT | cwt v3；acycle_cwt，dj=.1 | raw_power、scale_corrected_power、power 含义不同；significance_ratio 从原始功率计算，局部95%不代表全图检验 |
| AR1 全局检验 | ar1_global v1；64–1024 等间隔样本 | 固定平稳 Gaussian AR1 域、频率族与5%水平；内部 LOWESS 可选，任意祖先任务不会自动重放 |
| Gaussian / Taner | gaussian_filter / taner_filter v1；去均值、半功率通带 | 参数为 X 单位的倒数；输出滤波序列、包络和幅度响应；端点有边界效应 |
| Hilbert 包络 | hilbert_envelope v1；默认不去均值 | 同 N 点有限记录解析信号，输出包络/正交分量，不自动带通 |
| 多纬度日照 | insolation_map v1；La2004，纬度×年代 | 复用单纬度日平均日照；不代表其他轨道解或年内日期图 |
| 轨道偏心率 | eccentricity_source v1；默认 La2010a，0–2000 ka、间隔 1 ka | La2010a–d 末端 249999 ka，ZB18a/ZB20a–d 末端 300000 ka；独立线性插值，不外推 |
| PT-ZB18a | precession_tilt_source v1；默认 Ed=1、Td=1 | 倾角/气候岁差/岁差角；本地文件选其他 Ed/Td。岁差角不是近日点经度，不足以单独构造完整日照输入 |
| 演化 FFT / MTM | evolution_fft / evolution_mtm v1；窗口128点、步长16点、NFFT=窗口长度 | 完整窗口、默认单边物理PSD；最大频率直接输入，兼容旧比例；逐窗最大功率归一化可选、默认关闭；Grid为频率×窗口中心；平均谱不是整段谱；不输出显著性 |
| 区间 / 去均值 / 标准化 | select_interval / demean / standardize v1 | 区间含端点并保持原顺序；标准化默认N−1；常量标准化报错 |
| 采样与间隔检查 | sampling_check v1；大间隔阈值为正间隔中位数×3 | 原样转发输入，附诊断表；不修改数据、不推断地质间断 |
| 平滑 / 常规谱 | moving_average、savitzky_golay、fft_spectrum、welch_spectrum | 边界、窗与归一化见 methods/smoothing_spectra.md；无自动插值 |
| Welch 交叉谱 | cross_spectrum v2；段长256、重叠128、FFT=段长、周期Hann、段内去均值 | 有序 A/B、相同均匀 X 网格；复谱平均后计算相干度和相位，未定义值用掩码；见 methods/cross_spectrum.md |
| 不等距谱 / 高低通 | lomb_scargle、butterworth_lowpass、butterworth_highpass | 浮动均值 LS；Butterworth SOS 双向滤波的截止点、端点与限制见 methods/spectral_extension.md |
| 年龄映射 | apply_age_model、sedrate_to_age | 控制点/速率的单位、顺序、边界处理见 methods/chronology.md；不隐式改写源数据 |
| 目标周期 / 逐周期赋龄 | milankovitch_period、cycle_age_model | 周期包络、峰谷选择及歧义见 methods/cycle_tuning.md；不用未验证的自动峰谷序列替代控制点判断 |
| COCO 扫描 | coco_scan；固定相位平均目标、完整频带条件 | 描述性相关与诊断，不单独输出 p；见 methods/coco_scan.md |
| COCO Monte Carlo | coco_monte_carlo；固定 AR1 默认 rho=0、B=199、seed=1729 | 扫描最大值秩校正；随机峰为可选参考模式；前提见 methods/coco_analysis.md |
| 滑窗 eCOCO | evolution_coco；窗口512点、步长128点 | 速率×窗口热图、描述性候选；没有全图检验；单位与显式输出选择须保留 |
| 旧模拟方法 | mtm_ar1_simulation v1，目录隐藏 | 保留旧记录身份及诊断复现；不可作为默认全局检验替代品 |

- MTM/CWT/ar1_global 在入口要求相邻采样间隔与平均间隔的相对偏差不大于 1e−6。不得用 X 的绝对原点放大容差；大时间戳的小间隔可能已经丢失 double 精度，应明确报出实际间隔，不能假定未记录的均匀网格。
- 使用 GSL 上尾概率及分位数计算，与 Acycle 的离散 FDR 查表区分。拟合背景后的名义显著性仍有模型前提，数值对照不能单独证明任意数据下的误报率受控。
- 7组 MTM + 12组 CWT 数值对照及已记录的 MATLAB 验收见 `docs/methods/acycle_compatibility.md`；历史比较范围以记录为准，新构建应独立执行 C++ 回归和所需的 MATLAB 对照。
- 峰表仍是几何候选峰；默认频谱任务不把所有峰自动标为天文周期或统计发现。
- 高阶小波的最短标称周期、有限值/溢出/资源限制保留；COI 是经典边界影响约定，不是统一错误概率。
- 固定统计量、预处理、随机流或输出语义变化时应提升方法/缓存身份，并新增独立参照；普通算法只沿用 Task 接口，不必实现模拟库。

## 20. 预览、后台和导出的维护约定

- 即时预览为文件栏与画布间的独立列，默认展开。CollapsiblePanel 管收缩/宽度，内部 QSplitter 管上下高度。空数据/空节点不执行计算。
- SourcePanel 同时发出原始文件预览输入与包含草稿选项的 SourceItem。控制器以画布节点为准，普通路径由 prepareNodePreview 冻结根到节点的祖先路径并复用 WorkflowExecutor；联合路径由 batchDependencySlice 提取所选配对的真实依赖，交给 BatchWorkflowExecutor。
- ParameterEditor 立即发 edited 让旧结果失效，260ms 后 applyRequested 自动提交；切换/运行前仍 flush。预览 220ms 合并请求，每次一个 worker 和一个最新待执行请求；代次与合作式取消防止过时结果覆盖。
- 算法/端口/参数名来自 TaskDescriptor 和语言资源；不同输出用选择框，绘图沿用 PlotView。新增普通任务实现声明、工厂和 Task.execute，复用预览执行器。
- 原始概览和实际执行共用 readSeriesFile，但独立读取；执行完成后用本次路径实际使用的根输出更新原始图，防止外部文件改写或多输出端口选择不同导致上下图不一致。不缓存全部输入或把抽样数据当作算法输入。
- reducePreviewData 在 worker 内生成有界显示副本，原始/结果图复用；预算统一在 PreviewLimits。结果错误显示最早失败步骤。无关分支/空白点击不得触发计算。
- 隐藏/换页释放预览；正式批次暂停节点预览；关闭时取消且等待后台结束后再关闭。预览本身不调用 ResultRepository.save。含外部写入/删除副作用的新任务须先扩展明确的预览许可策略，不能直接加入自动预览。
- 多起点按 root NodeId 绑定；每个根×已绑定输入生成独立 RunRequest。同一文件绑定两根则各运行一次。事件必须以 RunId+NodeId 识别，不能只用 NodeId。
- 输出可选端口缺失时失败，不猜测另一个端口；Grid 不能隐式展平为 Series。计算失败与保存失败分别记录。
- `ExecutionContext::progress` 为可选回调。RunEvent.progressPercent=-1 表示状态迁移，0–100 表示节点进度；不是整批进度。
- CSV/图形导出先发送本次 finished，再发送 busyChanged(false)。finished 回调中任务仍占用；如需链式导出，在 idle 通知后启动，避免把旧任务完成状态应用到新任务。
- 取消是合作式的；不可中断的单次原生库调用需要返回后才响应。可捕获 C++ 异常与错误结果不等于原生进程崩溃隔离；项目没有计算子进程。
- 数值层须检查第三方前提、状态码与内存预算。现有 C 库仍不能承诺在系统级内存耗尽或原生库故障下完全隔离崩溃，正式发布须保留压力测试。
- 结果库采用 schema 3；普通载荷 v1、掩码表 v2；读取记录不改写历史结果。记录加载路径失效时直接失败，不重新创建空数据库。

- 绘图分组复用 `compatibleCurveAxes`；不要仅因两个输出都是 Series 就共用数轴。数据库预览报告的 `output_bounds` 按输出保留全数据范围，自动缩放只合并可见曲线；视口细化不得覆盖初始全范围。
- 导出期间拒绝关闭窗口要恢复实时预览；不要依赖并未运行的批次在将来发出 idle 信号。

## 帮助与参数命名

新增任务时维护 `methodhelp.<method>`、`parameterhelp.<key>`。中文可以逐步补齐，缺失或空值回退到英文；英文也缺失时采用任务描述。多个任务共用参数键但含义不同的，使用 `parameter.<method>.<key>` 和 `parameterhelp.<method>.<key>` 覆盖，保留计算参数键以兼容已有工作流。

参数参考由任务定义与语言资源生成，不直接编辑 Markdown。中文为 `docs/reference/task_parameters.md`，英文为 `docs/reference/task_parameters_en.md`。两者的方法锚点均为 `m-<method>`，参数锚点为 `m-<method>-p-<parameter>`。生成、重新编译与校验流程见[参数参考工具](../tools/help_reference/README.md)。GitHub Actions 检查文档是否与当前定义一致。

新增内置页面加入 `resources/help.qrc`；界面回归检查注册任务的帮助覆盖与本地定位。不要将内部翻译键用作默认用户提示。
