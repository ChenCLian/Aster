# Acycle 对照约定与来源

对照对象为 Acycle 3.1.1 验证快照中的具体函数。不同版本、GUI 分支及修改快照分别记录。源码哈希及历史 MATLAB 验收摘要见 [来源记录](../reference/scientific_evidence.json)。当前独立固定参照位于 `analysis_tests/fixtures/`，日常回归由 `analysis_tests` 执行。

参照文件用于验证归一化、默认参数和统计定义，不参与运行时计算。科学解释仍须满足各方法的输入条件和统计假设。

## 算法对应与差异

| Aster 任务/部分 | 参照 Acycle 中的对应部分 | 当前约定 |
| --- | --- | --- |
| `preprocess` | `datapreproc.m`、`acycleInterpolateSeries.m` | 默认使用重复 X 求均值、中位采样间隔、线性插值；整数索引网格和 ULP 级端点判断对齐。仍须用户明确添加节点 |
| `detrend_lowess` | `detrending.m` 的 LOWESS / rLOWESS | span 默认同为 0.35；普通 LOWESS 默认 0 次稳健迭代。5 次可选对应 rLOWESS 家族；保留成熟 NETLIB/CppLowess 实现 |
| `linear_detrend` | `detrending.m` 中一阶 `polyfit`、`polyval` | 均为有截距 OLS、返回趋势和残差；稳定中心化是数值实现差异，保留 |
| `affine` | 基本数值运算 | `factor*y+offset`；X 保持不变 |
| `series_summary` | 常规描述统计 | `population_variance` 的分母为 N；MATLAB 默认样本方差的分母为 N−1，两者须区分 |
| `sine_source` | `linegenerator.m` 的正弦项 | 同样使用 `A*sin(2*pi*x/T)`；Aster 的线性斜率叠加是明确的测试信号扩展，非天文理论曲线；保留 |
| `mtm` 主谱 | `spectrum.m`、`acycleMtmTaperCount.m`、MATLAB `pmtm` | NW 默认 2，支持小数；NW=1 保留 2 窗，否则 round(2NW)-1；NFFT 默认 N，另可明确指定补零长度 |
| MTM AR1 | `redconfML.m`、`moveMedian.m` | 默认线性功率拟合、0.2 中位窗口；偶数窗口左少右多、端点截短；按完整单边谱拟合，固定 2K 近似自由度 |
| MTM SWA | `spectralswafdr.m` 调用的 `specswa.m` | 使用单边谱的 log10 功率；搜索/尾部/平滑规则与参照源码对应。少于 33 个频点时返回不适用状态，不输出替代的常数 SWA |
| MTM FDR | `spectralswafdr.m` 的 Hopkins/Weedon 路径 | 使用 H(floor(NW))、完整单边检验族及最后被拒绝的观测 p 值对应曲线；与通用 BY 校正区分 |
| CWT 默认 | `wave_readGUI.m` 的 method_sel=2、`ar1nv.m`、`wave_update_wavelet.m` | `acycle_cwt`：dj 默认 0.1、最大尺度目标 0.34Ndt、ar1nv 估计；原始/尺度校正/显示功率分开输出 |
| CWT 原始功率 | Torrence–Compo `wavelet.m`、`wave_signif.m`、`rhoAR1ML.m` | 作为 `torrence_compo` 明确保留，不与默认 Acycle CWT 混用；尺度数按无量纲量计算 |
| `mtm_ar1_simulation` | 无对应 Acycle 方法 | 从任务目录和添加菜单隐藏；注册表仍能识别已保存的任务，不删除既有数据库结果 |
| 文件、内存与数据库源 | 工作台基础设施 | 文件读取/列选择/预览与执行保持同一解析器；数据库 4 个 typed 来源、`input_series` 保留，不属于需仿照 MATLAB 的科学算法 |

### MTM 的精确约定

- 先去均值，采用对称单位能量 DPSS、自适应 Thomson 加权和单边 PSD。
- `nfft=0` 表示输入长度 N；正整数表示明确 FFT 长度。支持 `NW=1` 或 `NW>=1.25` 且 `<N/2`。不支持 NFFT<N 的折叠输入选项，避免把它当作普通补零。
- Acycle GUI 在未传 Fs 的 `pmtm` 后只换算频率轴。Aster 保留具有正确物理频率单位的 PSD：**Aster PSD = Acycle 功率 × 2πdt**。背景和阈值同时乘这个常数，因此谱比与显著性判断不因这一单位换算而改变。
- `frequency_max` 是以输入横轴单位倒数表示的输出频率上限，0 为自动、超出奈奎斯特频率时截到奈奎斯特频率。兼容字段 `fmax_ratio` 在没有指定正的绝对上限时仍按旧比例生效。两者仅裁剪发布的频谱/阈值和峰表；AR1/SWA/FDR 始终使用完整单边频谱。改变输出上限不会重新拟合背景。Acycle 本身将 fmax 用于显示；本程序将其作为输出裁剪，两者在统计拟合域上一致。
- AR1 和 SWA 阈值使用固定 2K 自由度，对 DC/Nyquist 沿用 Acycle 的近似。这是端点近似，不能解释为每个频点的精确分布。数值层提供的有效自由度供兼容实验读取，不用于正式 MTM 阈值。
- 直接使用 GSL 的上尾概率/逆上尾，代替 Acycle 的有限查表网格；这是数值精度差异，不更换检验定义。各 q 水平若得到相同曲线，保留其各自的输出键。精确的观测 p 值边界是**包含等号**的，避免临界峰因浮点取整消失。
- SWA/AR1 附加估计不可用时保留已生成的主谱，并记录原因；不会让一个无效背景破坏整个工作流。零功率、不可表示的极端阈值也不会变成 NaN 数据进入数据库。
- 检测峰仍是几何候选峰，不自动宣称显著；数据库按频率筛选保留原语义。

### 小波三个功率输出

默认 `acycle_cwt` 模式下：

| 端口 | 数值定义 | 对应使用 |
| --- | --- | --- |
| `raw_power` | abs(W)² | 原始小波功率，Y 单位平方 |
| `scale_corrected_power` | abs(W)² / physical_scale | Acycle method 2 保存的功率 |
| `power`（默认） | abs(W)² / sample_variance / physical_scale | Acycle method 2 绘图所用功率；单位为 X 单位的倒数 |
| `significance_ratio` | 原始功率 / 原始局部阈值 | 与功率按尺度校正分开；>1 表示超过名义局部阈值 |
| `coi` | 母小波约定的影响锥周期 | 仍用原 X 单位，不在持久化数据中取对数 |

选择 `torrence_compo` 时，`power` 为原单位 abs(W)²，采用 OLS lag-1 估计与边界保护。跨模式比较时须核对功率定义。

默认模式不把合法的 rho=0.995 强行压成 0.99。若 ar1nv 得到 |rho|>=1，则不生成可选显著性比值，保留功率并说明非平稳背景问题。常量输入不能除以零方差，默认模式明确失败；需要查看全零原始功率时可选择 Torrence–Compo 模式。
## 保留且说明的差异

1. **不强制整理/重采样**，不在分析函数里静默排序、删除行或插值。这是显式预处理的数据契约。非有限值、维数、严格递增、均匀采样仍在入口验证。
2. LOWESS 使用成熟库，保留奇数样本稳健中位数修复、稳定坐标变换和显式“保留均值”选项。MATLAB `smooth` 的邻域取整、重复点和退化点规则不保证逐点相同；独立库/加权最小二乘参照保留在当前测试中。没有复制 MATLAB 专有源码。
3. AR1 使用相同的线性/对数最小二乘目标，但采用正幅度、|rho|<1 的有界求解。Acycle 无约束求解器或缺少工具箱时的粗网格回退不作为必须复现的数值行为。
4. 默认使用线性插值，与参照路径一致；GSL 自然三次样条和 Akima 是独立选项。
5. 保留奇异/溢出检查、资源上限和取消；默认 CWT 的工作内存预估也计入额外功率矩阵。原始功率无定义或数值无法表示时给出错误，不静默改变单位。
6. 保留高阶母小波的最短标称周期保护。Torrence–Compo 模式不照搬 GUI 中对带单位的 pt2 直接取 log2 来决定层数的写法：换时间单位不应额外改变尺度数量。默认 Acycle CWT 分支使用无量纲 N 的公式，无此混淆。
7. COI 两端存真实的零边界距离，作者 MATLAB 中的极小正数仅是绘图哨兵；跨语言对照比较内部点。
8. 峰突出度/宽度及样本方差与总体方差的字段语义不混改。辅助功能不等于 Acycle 全部功能已经移植。

本版未提供的功能包括 MTM F-test、幂律/分段幂律背景、自动超高分辨率背景截断建议、全局/尺度平均小波检验、相干小波等。**对照范围限于本页列出的路径与选项。**

## 直接 MATLAB 对照范围

归档的 MATLAB R2026b 对照结果覆盖 19 个完整案例（7 个 MTM、12 个 CWT）、133 项数组比较。121 项在原容差内；12 项是 Acycle 小波卡方分位数近似引起的已解释差异，采用相同精确分位数后匹配。AR1 拟合在 Acycle 工具箱缺失回退路径下存在小的优化误差。该证据不代表全部算法、FDR 原生查表分支或误报率均被直接验证。

直接核对时，应从待测构建的 C++ 测试导出实际数组，再运行 [对照工具](../../tools/acycle_validation/README.md)。上述历史比较仅适用于对应版本与案例，不能替代新版本的验证。

## 查表与数值计算

普通 MTM 的 FDR 保留约定的检验定义，概率及逆概率使用 GSL 数值计算，不要求复制 Acycle 有限查表网格。`ar1_global` 是另一项显式选择的蒙特卡洛全局检验，见 [AR1 全局检验](ar1_global.md)；不是 FDR 查表功能。
