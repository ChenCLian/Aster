# 外部代码与测试参考来源

## 随源码分发

- `src/third_party/pocketfft/`：Martin Reinecke 等，C++ 头文件实现，BSD 3-Clause。来自 https://github.com/mreineck/pocketfft 的 cpp 分支，使用 2026-11-02 的已固定源码快照；原头文件、README、LICENSE 原样保留。头文件 SHA-256：`518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d`。

- `src/third_party/qcustomplot/qcustomplot.h/.cpp`：QCustomPlot 2.1.1，两个源码文件未修改；文件头保留 GPL v3 或更高版本声明，完整文本放在同目录 `COPYING`。仅由桌面 UI 模块编译，不进入无 GUI 核心。

- `src/third_party/lowess/Lowess.h`：CppLowess，Copyright 2015 Hannes Roest。保留原文件内 BSD 3-Clause 许可全文；局部修正奇数样本鲁棒残差中位数，见 methods/acycle_compatibility.md。
- `src/third_party/wavelib/`：Rafat Hussain / Holger Nahrstaedt，BSD 3-Clause，许可见本目录 `COPYRIGHT`。来自 https://github.com/rafat/wavelib ，源码快照 commit `7f61bf592f3c470b2a7d8199431fde821d7253ac`（GitHub tar 的 pax comment）。下载归档 SHA-256：`c7b90f2846142d0801606bab4ce6de1fa644420758131879bac5216c648af31e`。

wavelib 包装时将 src 与 header 中需要的文件置于同一目录；修改两个 include 为 `wavelib.h`，给 `_USE_MATH_DEFINES` 加重复定义保护，并在 cwt_init 初次 malloc 后增加空指针返回保护；小波核和 FFT 计算保持该快照。在当前 CWT 调用链增加 `cwt_checked` / `cwavelet_checked`，校验内部 FFT 计划及缓冲分配失败并清理返回；旧 void API 保留包装，非法 padding 不再 exit。FFT计划 malloc 空指针检查同步补齐。没有改写小波核、尺度或FFT计算公式；其他未使用的第三方变换接口不在上述分配检查的覆盖范围。

## 项目数值实现与参照

`math/numeric_utils.*`、`math/spectrum/spectrum_peaks.*` 与统计代码属于项目数值实现。数学约定和差异见 `docs/methods/acycle_compatibility.md`，验证来源见 `docs/reference/scientific_evidence.json`。

MTM 数值对照使用随包独立 Python 生成器及固定 JSON 参照，CWT 参照使用密集 DFT 与解析小波。

## 外部构建依赖

- LAPACK（portable）：https://www.netlib.org/lapack/
- OpenBLAS（portable）：https://www.openmathlib.org/OpenBLAS/
- GNU Scientific Library：https://www.gnu.org/software/gsl/
- Qt：https://www.qt.io/

开发和分发时分别遵守所用依赖版本的许可。科学测试 JSON 由随包脚本使用 NumPy/SciPy 及 statsmodels 0.14.6 生成，Python 库本身未随包分发。

## Acycle 比较基线

使用验证时提供的 Acycle 3.1.1（Mingsong Li 等）核对算法约定，未捆绑该归档、MATLAB runtime 或 MathWorks 专有源码。其 README 标注 GPL；具体科学方法还分别注明 Mann–Lees、Hopkins/Weedon、Torrence–Compo、Grinsted 等来源。参考文件哈希和保留差异见 `docs/reference/scientific_evidence.json` 与 `docs/methods/acycle_compatibility.md`。MATLAB 对照脚本调用独立安装的 Acycle 函数，独立参考生成器使用 NumPy/SciPy。


## La2004 官方轨道数据

`resources/astronomy/la2004/INSOLN.LA2004.BTL.ASC` 为 IMCCE 官方公开名义过去轨道表，原始字节保留，作者及原始版权声明见同目录 `README.TXT`。来源、完整 SHA-256、列约定见 `manifest.json`。引用 Laskar et al. (2004), A&A 428, 261–285, DOI:10.1051/0004-6361:20041335。此数据不因本软件源代码的许可而改变其原始归属；发布时请保留原说明及出处。

`analysis_tests/fixtures/acycle_etp_la2004.csv` 为验证用 Acycle 3.1.1 随附数值示例（原名 Example-La2004-1E.5T-1P-0-2000.txt），只用于验证，不参与运行时计算。日照数学代码为公式的独立实现，没有复制 IMCCE Fortran 或 Acycle MATLAB 程序。


## La2010 / ZB / PT 公开轨道数据

10 张官方原始表以 qCompress/zlib 无损保存在 `resources/astronomy/solutions/`，未降采样或改写原始数值；来源、引用、下载日期和完整 SHA-256 见 `catalogue.json`。La2010 原作者说明及版权声明随 `La2010_README.TXT` 保留，NOAA PT 说明随 `PT_README.txt` 保留；ZB 作者说明位于压缩文件内的原始表头。可用 `tools/orbital_data/manage_data.py unpack` 恢复原始文件。

引用 Laskar et al. (2011), DOI:10.1051/0004-6361/201116836；Zeebe & Lourens (2022), DOI:10.1016/j.epsl.2022.117595；Zeebe & Lourens (2022), DOI:10.1029/2021PA004349，PT 数据 DOI:10.25921/wmfq-hx24。请保留原归属与引用，勿把深时数值覆盖范围当作相位准确性保证。未复制其轨道积分器源码，未增加运行时第三方依赖。

## 合成轨道代理信号

基于轨道代理信号正演参考实现与 AstroGeoFit 1.1（Copyright 2025 CNRS, Lead author J. Laskar），GPL-3.0-or-later。完整许可位于 licenses/。深度模板来自 PANGAEA.817206（Westerhold & Röhl, 2013），CC BY 3.0；详细归属及移植边界见 resources/synthetic/NOTICE.md。
