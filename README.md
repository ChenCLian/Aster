<img src="resources/app/aster.png" alt="Aster 图标" width="88" align="right">

# Aster

**面向地学与天文时间序列的桌面分析工作台。**

当前版本 **1.1.1** · Windows / macOS / Linux · 中文 / English

Aster 将数据导入、预处理、频谱与时频分析、天文曲线生成、结果管理和绘图导出组织在同一个工作流中。支持单条记录的节点输出预览和多输入批处理，并保存计算参数、数据来源及数值结果，便于复查与比较。

[下载程序](https://github.com/ChenCLian/Aster/releases) · [快速开始](QUICK_START.md) · [常见问题](FAQ.md) · [本版说明](RELEASE_NOTES.md) · [作者](AUTHORS.md)

## 下载与运行

在发布页的 **Assets** 中选择对应系统的程序包，完整解压后运行。

| 系统 | 1.1.1 程序包 | 启动入口 |
| --- | --- | --- |
| Windows x64 | `Aster-1.1.1-windows-x64.zip` | `Aster.exe` |
| macOS 15+，Apple Silicon | `Aster-1.1.1-macos-arm64.zip` | `Aster.app` |
| macOS 15+，Intel | `Aster-1.1.1-macos-x86_64.zip` | `Aster.app` |
| Linux x64，以 Ubuntu 22.04 为构建基线 | `Aster-1.1.1-linux-x64.tar.gz` | `./AppRun` |

各平台附件以发布页实际提供的文件为准。`Source code` 和名称带 `source` 的压缩包用于源码构建。

发行包已包含 Qt 和数值计算依赖。Linux 仍需兼容的系统库、X11/XWayland 图形环境与字体。**中文显示为方框时，见 [字体问题](FAQ.md#linux-fonts)**。macOS 包的签名与公证状态以发布说明为准，首次打开受阻时见 [macOS 启动问题](FAQ.md#macos-open)。

安装和第一次分析的完整步骤见 [快速开始](QUICK_START.md)。

## 主要功能

| 功能 | 内容 |
| --- | --- |
| 数据来源 | CSV、TXT、TSV、DAT 等分隔文本；已有数据库输出；内置 ODP 数据；正弦信号与合成轨道代理信号 |
| 数据处理 | 排序与去重、区间选取、整理与重采样、缩放与偏移、去均值、标准化、线性与 LOWESS 去趋势 |
| 平滑与滤波 | 移动平均、Savitzky–Golay、Gaussian 与 Taner 带通、Butterworth 低通/高通、Hilbert 振幅包络 |
| 频谱与时频分析 | FFT、Welch、MTM、Lomb–Scargle、连续小波变换（CWT）、演化 FFT/MTM；MTM 峰值检测及相关背景与检验输出 |
| 天文曲线 | 偏心率、倾角、气候岁差、ETP 组合、日平均日照、季节平均/积分日照、多纬度日照图及多套轨道解 |
| 年代与旋回分析 | 沉积速率转年龄模型、应用年龄模型、逐周期赋龄、COCO 固定目标扫描、蒙特卡洛检验与滑窗分析 |
| 工作流与预览 | 可视化任务树、多输入批处理、后台节点输出预览、参数即时生效、步骤错误诊断 |
| 结果与绘图 | SQLite 结果库、筛选与峰频率检索、批量选择和删除、兼容数据库合并与备份、多图页/子图、曲线与热图、读点坐标、CSV/PNG/PDF 导出 |

## 基本流程

1. 导入数据，核对 X/Y 列和单位，或添加内置数据源。
2. 在画布中连接任务，选择节点并调整参数，查看即时预览。
3. 点击“运行流程”，将计算结果、参数和来源保存到数据库。
4. 在“结果”页选择记录及步骤，绘图、比较并导出。

即时预览不写入数据库。输出有多条曲线或表格时，通过输出选择框切换。重采样和去趋势按研究目的添加，软件不自动插入这些步骤。

## 分析与引用

峰值表记录检测到的局部峰，统计解释需要结合背景模型、检验条件、采样和记录长度。AR1、SWA、FDR 与小波显著性输出的解释范围见 [统计说明（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/methods/statistical_limits.md)。

天文曲线和 ODP 数据的来源可在任务帮助及结果的“来源”页中查看。引用研究结果时，应保留原始数据、单位、处理流程、参数和 Aster 版本，并引用实际使用的方法与数据集。合成轨道代理信号的模型说明见 [生成器说明（在线）](https://github.com/ChenCLian/Aster/blob/main/resources/synthetic/NOTICE.md)。

## 文档与反馈

| 需要了解的内容 | 入口 |
| --- | --- |
| 完整操作与帮助 | [使用手册](https://github.com/ChenCLian/Aster/blob/main/docs/USER_GUIDE.md)、[English](https://github.com/ChenCLian/Aster/blob/main/docs/USER_GUIDE_EN.md) |
| 任务参数含义与默认值 | [中文](https://github.com/ChenCLian/Aster/blob/main/docs/reference/task_parameters.md) · [English](https://github.com/ChenCLian/Aster/blob/main/docs/reference/task_parameters_en.md) |
| 安装、导入、预览、运行和导出 | [快速开始](QUICK_START.md) |
| 启动、字体、数据库、频谱和显示问题 | [FAQ](FAQ.md) |
| 本版功能与平台说明 | [发布说明](RELEASE_NOTES.md) |
| 方法、架构与开发文档 | [文档索引（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/README.md) |
| 源码编译与依赖配置 | [构建说明（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/building.md) |
| 打包、GitHub Actions 与版本发布 | [发布维护（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/release_maintenance_zh.md) |

程序内按 F1 或选择“设置 → 使用手册”可离线查阅操作指南、参数参考及方法说明。参数“?”悬停显示简短提示，点击定位章节；缺失的本地章节转至 GitHub。发行包中的快速开始和 FAQ 也可直接阅读，源码包另含完整的 `docs/` 目录。

报告问题请附版本、操作系统、复现步骤及完整错误信息，提交到 [GitHub Issues](https://github.com/ChenCLian/Aster/issues)。可复现的数据示例有助于定位问题，提交前请移除不宜公开的内容。

## 作者、引用与许可

作者和联系方式见 [AUTHORS.md](AUTHORS.md)。引用 Aster 时请注明软件名称、实际使用版本、作者及项目地址。

第三方代码与数据的归属、许可保留在发行包的 `licenses/` 目录及 [第三方说明（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/third_party.md)。源码中的对应来源文件也予以保留。

Aster 原创贡献：Copyright (C) 2026 ZiJie Chen。软件整体采用 **GPL-3.0-only** 发行，完整条款见 [LICENSE](LICENSE)，适用范围见 [COPYRIGHT.md](COPYRIGHT.md)。第三方代码和数据保留各自原作者与许可。

与程序匹配的项目源码和依赖材料作为 Release 附件提供，获取方式见 [SOURCE_CODE.md](SOURCE_CODE.md)。程序包内的 `SOURCE_DEPENDENCIES.md` 列出该平台所用依赖及对应下载入口。软件不提供担保，具体以 GPL 条款及适用法律为准。软件内“设置 → 许可证”可离线查看许可说明与正文。
