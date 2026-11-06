# 序列整理与演化谱

本文说明输入、输出、数学约定与适用范围。验证批次、环境和范围见 [验证记录](../validation.md)。

## 模块范围

- 六项任务：演化 FFT、演化 MTM、区间保留／删除、去均值、Z-score 标准化、采样与间隔检查。
- 任务目录见 [任务分类](../task_catalogue.md)。
- 参数与默认值由任务描述提供，中英文名称、分组、帮助和诊断由语言资源提供。
- 数值后端为 OpenBLAS/LAPACK + PocketFFT。NumPy/SciPy 仅用于生成测试参考数据。
## 数值与数据契约

| 任务 | 输入／输出 | 约定 |
|---|---|---|
| 演化 FFT | Series → `power_map` Grid、`mean_spectrum` Spectrum | 仅完整滑动窗口；周期 Hann 默认、可选矩形窗；单边物理 PSD；默认窗口内去均值，可选去线性趋势 |
| 演化 MTM | 同上 | 每个窗口复用既有自适应 MTM，整次任务只构造一个不可变 DPSS 计划；与完整频段收敛约定一致 |
| 区间保留／删除 | Series → `series` | X 范围包含两端；X/Y 成对操作；保持原顺序；不插值、不排序、不自动填补删除产生的间隔 |
| 去均值 | Series → `series` | 减去 Y 的算术均值；X 与单位不变；常数输入合法，输出零 |
| 标准化 | Series → `series` | 默认样本标准差 N−1，可选总体标准差 N；输出 Y 无量纲；常数输入或点数不足时报错 |
| 采样检查 | Series → 原始 `series`、三张 Table | 检查重复 X、相邻逆序／相等、等间距与较大正间隔；主输出为同一只读 DataRef；不修改输入 |

演化谱默认窗口 128 点、步长 16 点；FFT 点数 0 表示使用窗口长度。输入小于 128 点时需明确降低窗口点数。严格递增、等间距是这两个任务的要求，算法不会隐式添加预处理。每个窗口中心取首末样本 X 的中点；Grid 的行是频率，列是窗口位置，数值按行存储。

窗口物理跨度为 `(L−1)×dt`，傅里叶时长为 `L×dt`。补零改变频率网格密度，不提高实际分辨率。输出 PSD 的单位为 `Y² × X单位`；不逐列归一化，也不自动取对数。对数显示应由热图样式完成，不改变数据库里的物理量。

`mean_spectrum` 是各窗口 PSD 的算术平均，不能当作整段 MTM 的同义输出。热图及其平均谱只用于描述，该模块不附带逐点或整图显著性结论；演化谱不建立峰值频率索引。

采样检查的较大间隔阈值为“相邻正间隔中位数 × 用户倍数”，默认 3。重复 X 统计覆盖全部序列；明细最多 4096 行，并明确标记截断。间隔提示不能独立证明地层间断。NaN／空值仍遵循已有导入策略和 DataRef 有限值契约，不通过此节点自动丢弃。
## 有限资源与失败处理

演化谱限制集中在 `EvolutionLimits`：输入最多 100 万点，FFT 最多 262144 点，最多 20000 个窗口、200 万输出网格，估算工作量 `窗口数 × FFT点数 × 窗函数数` 不超过 1 亿。MTM 原有工作区上限仍有效。

序列操作限制集中在 `SeriesOperationLimits`。任务检查输入类型、有限值、跨参数关系、内存预算与取消标记。数值越界、零标准差、空选择等返回诊断；失败节点不会把无效数据传给下游。任务不自行写库、不引用 QWidget、不修改源数据。
## 数学参考

- SciPy periodogram：https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.periodogram.html
- SciPy spectrogram：https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.spectrogram.html
- Acycle 演化谱功能说明：https://github.com/mingsongli/acycle/wiki/4.7.4-Evolutionary-Spectral-Analysis

演化 MTM 使用 Aster 的 MTM 核心，输出采用物理 PSD 单位。与 Acycle 比较时，须统一窗口、采样间隔和功率归一化。
