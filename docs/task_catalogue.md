# Aster 任务分类

任务目录由注册描述生成。下表说明分类原则；实际可用任务与可见性以当前注册表为准。

分类只有一个来源：`src/processing/task_categories.h` 中的 `ASTER_TASK_CATEGORIES`。任务的 `descriptor().category` 引用对应枚举。界面按描述构造目录，分类调整不改变方法 ID、数据库端口、算法版本或任务树连接。

## 通用目录

| 一级 | 二级／三级 | 方法（稳定 ID） |
|---|---|---|
| 数据来源 | 示例数据 | ODP 深海记录 按内置目录注册的 ODP 示例（标识、数据及引用见 `resources/examples/catalog.json`） |
| 数据来源 | 文件 | 文本／CSV 读取 `file_source` |
| 数据来源 | 生成信号 | 正弦曲线 `sine_source`；合成轨道代理信号 `synthetic_orbital` |
| 数据来源 | 天文曲线 → 轨道参数与组合 | La2004 轨道曲线 `orbital_source`；ETP `etp_source`；偏心率轨道解 `eccentricity_source`；岁差与倾角轨道解 `precession_tilt_source` |
| 数据来源 | 天文曲线 → 日照与季节积分 | 日平均日照 `insolation_source`；季节平均／积分 `seasonal_insolation`；多纬度日照图 `insolation_map` |
| 数据来源 | 天文曲线 → 目标周期 | 米兰科维奇目标周期 `milankovitch_period` |
| 数据处理 | 区间选取 | 保留／删除区间 `select_interval` |
| 数据处理 | 清理 | 排序与去重复 `clean_series` |
| 数据处理 | 采样 | 整理与重采样 `preprocess` |
| 数据处理 | 年龄与深度 | 沉积速率转年龄模型 `sedrate_to_age`；应用年龄模型 `apply_age_model`；峰谷逐周期赋龄 `cycle_age_model` |
| 数据处理 | 振幅 | 缩放与偏移 `affine`；去均值 `demean`；标准化 `standardize` |
| 数据处理 | 趋势 | 线性去趋势 `linear_detrend`；LOWESS 去趋势 `detrend_lowess` |
| 数据处理 | 平滑 | 移动平均 `moving_average`；Savitzky–Golay `savitzky_golay` |
| 数据处理 | 滤波 | Gaussian `gaussian_filter`；Taner `taner_filter`；Butterworth 低通 `butterworth_lowpass`；高通 `butterworth_highpass` |
| 分析 | 频谱 → 功率谱 | MTM `mtm`；FFT `fft_spectrum`；Welch `welch_spectrum`；Lomb–Scargle `lomb_scargle` |
| 分析 | 频谱 → 显著性检验 | AR1 全局检验 `ar1_global` |
| 分析 | 时频 → 小波 | CWT `cwt` |
| 分析 | 时频 → 演化谱 | 演化 FFT `evolution_fft`；演化 MTM `evolution_mtm` |
| 分析 | 振幅 | Hilbert 振幅包络 `hilbert_envelope` |
| 分析 | 旋回地层与年代 → 天文周期匹配 | COCO 固定目标扫描 `coco_scan`；蒙特卡洛检验 `coco_monte_carlo`；滑窗 `evolution_coco` |
| 分析 | 统计 | 序列摘要 `series_summary` |
| 分析 | 数据质量 | 采样与间隔检查 `sampling_check` |

“数据处理”产生供下游继续使用的序列；“分析”产生谱、热图、表格或分析量。采样检查以诊断为主，但同时原样转发输入，便于放在流程中使用。滤波的主功能归数据处理，包络提取归分析。采样检查与清理分开，不把检查结果自动变成数据修改。

数据库数据源 `database_series`、`database_spectrum`、`database_grid`、`database_table` 仍由数据库结果入口建立；内存绑定源 `input_series` 仍是内部入口。旧实验方法 `mtm_ar1_simulation` 继续隐藏，但保留对旧记录的识别能力，不能把它当作无用代码直接删除。

## 新算法如何选择分类

```cpp
#include "processing/task_categories.h"

// 方法描述里使用已存在的分类。
auto d = task_detail::seriesTransform("My filter",
    taskCategory(TaskCategory::Filtering));
```

只有新功能确实不适合已有分类时，才给 `ASTER_TASK_CATEGORIES` 增加一个路径，并在两份语言资源中补齐 `category.<稳定分类词>`。不要在主窗口、右键菜单和侧栏分别维护分类副本。

内部保留 `Preprocess` 根词，以兼容已有菜单排序；中英文分别显示“数据处理”与“Data processing”。分类标题是显示信息，不能用来识别算法或判定数据类型。方法 ID 和输出端口仍承担持久化身份。

普通数值参数、分组、帮助和下拉选项继续从任务描述与语言资源生成。新增 C++ 方法仍须登记注册宏、加入构建清单并重新编译；不是放入 JSON 就能执行任意新算法。

## 验证

任务目录与画布菜单共享注册分类。当前数值与交互回归见 [验证记录](validation.md)。分类单击、任务双击、半透明拖放卡片保留。
