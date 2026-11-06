# Aster 统一任务诊断

诊断协议记录缺失输出端口和运行时类型信息，与当前数据库格式一致。

## 模块职责

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| 核心诊断协议 | src/core/error_info.h/.cpp | ErrorInfo、宏声明的内置错误码、稳定字符串编码、JSON 编解码 |
| 界面显示适配 | src/ui/common/error_presenter.h/.cpp | 简短原因、可选建议、可复制详情；中文／英文模板读取 |

错误码不会随显示语言或枚举顺序改变。新增普通算法沿用 Task::execute 和 NodeResult。旧的 failure(QString) 入口继续可用；它被标为 UNCLASSIFIED，保留原文。`presentation::resultError()` 通过适配器获取显示文字。

## 统一输出和存储

NodeResult.state 仍决定执行状态；NodeResult.error 保留简短兜底字符串。结构化信息放在 report.diagnostic：

```json
{
  "version": 1,
  "code": "INPUT_X_DUPLICATE",
  "context": {
    "point_before": 12,
    "point": 13,
    "x_before": 1720,
    "x": 1720,
    "method": "detrend_lowess",
    "node_id": "…",
    "run_id": "…",
    "input_port": "series"
  },
  "technical": ""
}
```

执行器在失败／阻断／取消时补充方法、节点、运行标识和输入来源端口。RunEvent 携带同一 ErrorInfo，经现有 RunController 信号跨线程传递。ResultRepository 在 report 和 error 中保存诊断，使用 schema 3 和数值编码 v1。没有结构化内容的记录显示原错误字符串；旧导入和 X 检查错误提供兼容解释。

诊断只承载小型标量元信息：最多 64 个上下文字段，单个字符串最多 1024 字符、底层信息最多 4096 字符；不放数组、嵌套对象和数值数据块。计算输出通过只读 DataRef 传递。

## 算法与来源覆盖

| 范围 | 具体信息 |
| --- | --- |
| LOWESS | 输入类型、点数、倒序／重复 X 的两个位置和值、底层返回码与参数 |
| MTM | X 间隔异常的位置、实际／参考间隔和容差；NW、样本数及排他上限；计算异常原文 |
| 整理与重采样 | 重复值策略、原输入点位置、不同 X 值数量、重采样间隔与范围、插值最低点数、GSL 返回码 |
| 线性去趋势 | 样本不足、X 范围退化、拟合系数数值范围 |
| CWT | 样本不足／过多、采样间隔、母小波阶数、网格点数和 dj、内存分配失败 |
| 缩放偏移、统计汇总、正弦源 | 原统一参数／输入／输出校验与异常捕获接入结构化诊断；不改变计算公式 |
| 正式文件读取 | 打开失败、列缺失、格式／数值错误的实际文件行列及单元格原文、行长度／行数上限、解码和有效点数 |
| 四种数据库来源 | 无效引用、打开／读取失败、数据库位置及记录引用、输出类型不匹配 |
| 通用执行器 | 参数／工厂失败、输入／输出校验、未知异常、任务返回状态错误、下游不可用、取消 |

输入点索引和文件行号都按 1 开始显示，但它们不是同一种位置。算法诊断只声明“输入点”；数据经过筛选、合并或插值后，不虚构它在原文件中的行号。正式文件读取错误使用真实物理行号，包含表头和注释行。

可选 AR1／SWA 估计失败时，在 warnings 中记录原因并保留主要输出。数学库原始细节和系统返回文字可能仍为英文。

## 界面行为

- 结果列表和图上方先显示简短原因，不堆叠长说明。
- 失败、阻断或取消步骤显示“查看详情”。详情使用 Qt 对话框组件组装；可调整尺寸、滚动、选择文字和复制。
- 下游阻断的详情先显示实际失败来源，再列当前未执行步骤。只追溯一个实际原因，不把相同错误复制到整条路径。
- 当前版本不强制添加整理、排序或重采样节点。数据满足算法条件时，可以直接执行读取 → LOWESS → MTM。提示中的处理方式均为可选建议；不隐式修改输入或执行顺序。



## 新算法如何接入

常见条件复用内置码，例如：

```cpp
return NodeResult::failure(makeError(
    ErrorCode::TooFewSamples,
    {{"actual", double(source->x.size())}, {"minimum", 6}}));
```

需要共享新错误时，在 ASTER_ERROR_CODES(X) 中登记一次稳定名称，再补充 zh.json / en.json 中的 diagnostic.<CODE>.summary 和可选 .suggestion。模板可用 {actual}、{minimum} 等命名占位符；上下文字段标签集中于 diagnostic.field.<key>。

独立扩展也可以使用自己的字符串错误码，无需给执行器或数据库增加分支：

```cpp
return NodeResult::failure(ErrorInfo{
    "my_method.INVALID_PARAMETER_COMBINATION",
    {{"parameter_a", a}, {"parameter_b", b}},
    "The selected parameter combination is unsupported"});
```

可在语言资源中为该码和字段添加说明；缺少翻译时保留底层原文和稳定码。ErrorPresenter 对导入值按纯文本处理，模板插值只进行一轮，不将数据里的花括号继续解释为占位符。

## 验证与构建

构建与回归批次见 [验证记录](validation.md)。各发行平台须完成对应编译器构建和桌面验收。文件预览、参数即时提示和独立导出服务仍使用原有提示接口；结构化诊断覆盖正式来源读取、节点计算及工作流结果。

Qt Creator 打开根目录 `Aster.pro`，重新运行 qmake 并构建整个工程，运行目标选择 Aster。设置与数据库管理见 [快速开始](../QUICK_START.md)。

## 数据传输诊断

缺少可选父端口使用 INPUT_PORT_UNAVAILABLE，包含 input_port、parent_id 和 expected；错误类型使用 INPUT_TYPE，包含 expected / actual_type。失败的后继仍记录最早失败原因。可捕获的 std::bad_alloc 使用 MEMORY_ALLOCATION_FAILED；线程内不承诺捕获原生访问违规或进程退出。成功结果不得带 report.diagnostic，非致命提示放 warnings。详见 [数据传输审查](architecture.md)。
