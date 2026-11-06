# 平滑、FFT 与 Welch

本文说明输入、输出、数学约定与适用范围。验证批次、环境和范围见 [验证记录](../validation.md)。

## 四项任务与默认值

| 方法键 | 目录 | 默认参数 | 输入与输出 |
| --- | --- | --- | --- |
| `moving_average` | 数据处理 → 平滑 | 11 点奇数窗口 | 严格递增 X 的有限 Series → `series`；X 与单位不变 |
| `savitzky_golay` | 数据处理 → 平滑 | 11 点，2 阶多项式 | 严格递增、等间隔有限 Series → `series`；X 与单位不变 |
| `fft_spectrum` | 分析 → 频谱 → 功率谱 | 周期 Hann 窗、去均值，FFT 点数 0（自动取整段长度） | 等间隔 Series → `spectrum`（单边物理 PSD） |
| `welch_spectrum` | 分析 → 频谱 → 功率谱 | 每段 256 点、重叠 128 点，周期 Hann、各段去均值，FFT 点数 0（取段长） | 等间隔 Series → `spectrum`（各段 PSD 的算术平均） |

默认值只在 Task 描述中定义，注册表归一化后同时供 UI 和执行使用。等间隔条件沿用共同检查（相对间隔容差 1e-6）。不强制加入整理/重采样，不自动减小窗口，不截断输入来满足 FFT 长度。
## 数学定义与端点

移动平均：对位置 i，取 `[max(0,i-h), min(N-1,i+h)]` 内的已有样本等权平均，h=(窗口点数−1)/2。端点使用截短窗口，不补零、不重复端点。窗口按点数，不按 X 距离；非等间隔输入会提示实际覆盖的物理跨度不固定。该定义为 clipped centered mean；与 MATLAB `smooth` 比较时须核对端点约定。

Savitzky–Golay：在等间距样本坐标上拟合指定阶数的最小二乘多项式。内部点取居中完整窗口；首尾使用第一个/最后一个完整窗口的多项式，在已有样本位置求值，相当于 SciPy 的 `mode='interp'`。没有填充或在记录外外推。实现将坐标缩放到 [-1,1]，两遍修正 Gram–Schmidt 生成正交基，用 QQᵀ 投影，避免正规方程的条件数平方。允许 0–5 阶，必须低于窗口点数。多项式平滑可以过冲，不保证每点仍在输入极值范围内。

FFT 单段：先去均值或线性最小二乘趋势，再乘窗。周期 Hann 为 `0.5−0.5*cos(2*pi*j/L)`，矩形窗为 1。设 U=Σw²，采样间隔为 dt，则

`PSD[k] = c[k] * dt/U * |FFT(w*y_detrended)[k]|²`，`f[k]=k/(nfft*dt)`。

c 在 DC 以及偶数 nfft 的 Nyquist 点取 1，其余单边频点取 2。FFT 不额外除以 nfft；以频率间隔求和后对应窗能量归一化的平均平方量。矩形窗正弦测试另核对 Parseval/方差。零填充只细化频率栅格，不提高固有分辨率。

Welch：起点依次为 `0, L-overlap, 2*(L-overlap), ...`，只取完全落在输入内的段；逐段独立去趋势和加窗，以上述单段 PSD 算术平均。末尾未覆盖点数写入报告，不隐式补齐最后一段。重叠不得大于等于段长。没有生成完整时频矩阵再求均值，只保留当前段、累加谱及有界窗口位置。

当 X 单位为 ka、Y 单位为 u 时，频率为 1/ka，PSD 为 u²·ka；有任一原单位缺失时不猜测单位。这里的 PSD 不等同于 FFT 幅值，也不能把峰高度直接当成振幅。

**该模块 FFT/Welch 只输出描述性功率谱，不生成 p 值、置信阈值或峰频率索引。** 峰值检测、峰标记和数据库频率筛选由 MTM 提供。平滑任务不输出 bootstrap 置信带。与 SciPy 的数值对照不证明任意噪声条件下的统计推断有效。
## 与 Acycle 和独立参照的关系

核查了参考 Acycle 包中的 `evoperiodogram.m`：它使用局部线性去趋势、矩形窗及 5 倍段长补零；该模块公开提供对应窗/去趋势/FFT 长度参数，但默认采用周期 Hann、去均值、无补零。物理 PSD 定义一致的情况下可比较，默认值不同不能直接判成算法错误。没有复制原 GUI、固定 5 倍补零或旧演化谱中心定位习惯。

参考文档：

- [SciPy periodogram](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.periodogram.html)
- [SciPy Welch](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.welch.html)
- [SciPy Savitzky–Golay](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.savgol_filter.html)

固定参照由 `analysis_tests/fixtures/generate_basic_signal_reference.py` 独立生成：NumPy 截短均值、SciPy SG、periodogram/Welch 共 18 例；不调用产品计算代码。另有多项式保持、常量、大数、错误输入、取消、功率积分和数值传输测试。验证未包含直接 MATLAB 运行对照、Acycle GUI 的全部默认值或 bootstrap 行为。
## 预算与失败行为

- 平滑：最多 100 万点，奇数窗口 1–501 点，`N*window <= 1e8`，阶数 0–5。固定小维正交基和有界权重表；参数不合条件会失败。
- FFT：整段至少 8 点，nfft 最大 262144 且不得短于整段。不会静默舍弃尾部以匹配 nfft。
- Welch：输入最多 100 万点，每段至少 8 点，nfft 最大 262144，窗口最多 20000 个，沿用 `windows*nfft <= 1e8` 工作预算。
- 空/错误维度、非有限值、不递增、非等间隔、非法窗口、超预算及可检测的数值溢出均通过统一诊断返回；上游失败正常阻断后续节点。后台取消按扫描、权重构造和窗口/输出循环检查，不强行终止原生 FFT 内部调用。
