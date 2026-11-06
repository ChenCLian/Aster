# 第三方代码、依赖与数据

Aster 整体采用 GPL-3.0-only；原创部分版权为 Copyright (C) 2026 ZiJie Chen。第三方原始许可与版权继续有效，详见 [COPYRIGHT.md](../COPYRIGHT.md)。随包许可声明不替代二进制发行时的[对应源码提供要求](../SOURCE_CODE.md)。

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
- GNU Scientific Library：https://www.gnu.org/software/gsl/ 。GPL；应保留实际使用版本的版权与完整许可，并提供对应源码。
- Qt：https://www.qt.io/ 。按实际模块与发行版本保留 Qt 及其内含第三方组件的许可，不以项目 GPL 声明替换 Qt 原有许可。

实际发行包的组件、版本、源码与配方由[依赖材料收集器（在线说明）](https://github.com/ChenCLian/Aster/blob/main/docs/dependency_materials_zh.md)记录；程序包内的 `SOURCE_DEPENDENCIES.md` 提供该平台的实际下载入口。Qt、GSL 及其他库继续适用各自条款；OpenBLAS/LAPACK 的 BSD 类许可要求保留原版权、条件与免责声明，Aster 的对应源码范围另按 GPL 确定。

开发和分发时分别遵守所用依赖版本的许可。科学测试 JSON 由随包脚本使用 NumPy/SciPy 及 statsmodels 0.14.6 生成，Python 库本身未随包分发。

## Acycle 比较基线

使用Acycle 3.1.1 验证快照（Mingsong Li 等）核对算法约定，未捆绑该归档、MATLAB runtime 或 MathWorks 专有源码。其 README 标注 GPL；具体科学方法还分别注明 Mann–Lees、Hopkins/Weedon、Torrence–Compo、Grinsted 等来源。参考文件哈希和保留差异见 `docs/reference/scientific_evidence.json` 与 `docs/methods/acycle_compatibility.md`。MATLAB 对照脚本调用独立安装的 Acycle 函数，独立参考生成器使用 NumPy/SciPy。


## La2004 官方轨道数据

`resources/astronomy/la2004/INSOLN.LA2004.BTL.ASC` 为 IMCCE 官方公开名义过去轨道表，原始字节保留，作者及原始版权声明见同目录 `README.TXT`。来源、完整 SHA-256、列约定见 `manifest.json`。引用 Laskar et al. (2004), A&A 428, 261–285, DOI:10.1051/0004-6361:20041335。此数据保留原始归属；再分发条件见下方“轨道表再分发核验”。

`analysis_tests/fixtures/acycle_etp_la2004.csv` 为验证用 Acycle 3.1.1 随附数值示例（原名 Example-La2004-1E.5T-1P-0-2000.txt），只用于验证，不参与运行时计算。日照数学代码为公式的独立实现，没有复制 IMCCE Fortran 或 Acycle MATLAB 程序。


## La2010 / ZB / PT 公开轨道数据

10 张官方原始表以 qCompress/zlib 无损保存在 `resources/astronomy/solutions/`，未降采样或改写原始数值；来源、引用、下载日期和完整 SHA-256 见 `catalogue.json`。La2010 原作者说明及版权声明随 `La2010_README.TXT` 保留，NOAA PT 说明随 `PT_README.txt` 保留；ZB 作者说明位于压缩文件内的原始表头。可用 `tools/orbital_data/manage_data.py unpack` 恢复原始文件。

引用 Laskar et al. (2011), DOI:10.1051/0004-6361/201116836；Zeebe & Lourens (2022), DOI:10.1016/j.epsl.2022.117595；Zeebe & Lourens (2022), DOI:10.1029/2021PA004349，PT 数据 DOI:10.25921/wmfq-hx24。请保留原归属与引用，勿把深时数值覆盖范围当作相位准确性保证。未复制其轨道积分器源码，未增加运行时第三方依赖。

## 轨道表再分发核验

La2004 与 La2010 的随附 README 记录了作者、版权和使用方法，但未明确授予将完整数据表随软件再分发的许可。[IMCCE 法律声明](https://www.imcce.fr/mentions-legales)列有资料使用条件与数据复制申请入口；公开下载地址和论文引用本身不足以确认本发行方式已获许可。

发布包含这些表的源码包或程序包前，维护者须确认适用于具体文件的再分发条款或取得权利人许可，并记录授权范围、来源和日期。截至 2026-11-06，本工程未收录能确认该再分发范围的授权材料。此项是尚待核实的授权依据，不是对数据违法使用的认定。

核验范围包括 `resources/astronomy/la2004/` 中的 La2004 原表及 `resources/astronomy/solutions/` 中的 La2010 压缩表。压缩存储不改变其归属和适用条件。其他轨道表和数据依各自来源条款核对；不能套用 Aster 的 GPL 声明替代。

## 合成轨道代理信号

基于轨道代理信号正演参考实现与 AstroGeoFit 1.1（Copyright 2025 CNRS, Lead author J. Laskar），GPL-3.0-or-later。完整许可位于 licenses/。深度模板来自 PANGAEA.817206（Westerhold & Röhl, 2013），CC BY 3.0；详细归属及移植边界见 resources/synthetic/NOTICE.md。
