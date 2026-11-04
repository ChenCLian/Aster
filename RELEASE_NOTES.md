# Aster 1.1.0

Aster 首个公开发行版本，面向地学与天文时间序列分析。支持中文和英文界面，以及 Windows、macOS 和 Linux。

## 主要功能

- **数据与工作流：**分隔文本、数据库输出、内置 ODP 数据及生成信号；可视化任务树、单记录后台预览和多输入批处理。
- **序列处理：**整理与重采样、标准化、去趋势、平滑、Gaussian/Taner 带通、Butterworth 滤波和 Hilbert 振幅包络。
- **频谱与时频：**FFT、Welch、MTM、Lomb–Scargle、CWT、演化 FFT/MTM，以及峰值检测和相关背景输出。
- **天文与年代：**多套轨道解、日照曲线与多纬度图、年龄模型、逐周期赋龄和 COCO 分析。
- **结果与绘图：**SQLite 结果库、筛选、批量删除、兼容数据库合并与备份；曲线、热图、读点及 CSV/PNG/PDF 导出。

## 下载

在本次发布的 Assets 中选择程序包，完整解压后运行。

| 平台 | 程序包 | 启动入口 |
| --- | --- | --- |
| Windows x64 | `Aster-1.1.0-windows-x64.zip` | `Aster.exe` |
| macOS 15+，Apple Silicon | `Aster-1.1.0-macos-arm64.zip` | `Aster.app` |
| macOS 15+，Intel | `Aster-1.1.0-macos-x86_64.zip` | `Aster.app` |
| Linux x64 | `Aster-1.1.0-linux-x64.tar.gz` | `./AppRun` |

Linux 包以 Ubuntu 22.04 为构建基线，需兼容的系统库、X11/XWayland 图形环境和字体。其他发行版的兼容性需单独确认。

默认 macOS 包使用 ad-hoc 签名，未经 Apple Developer ID 签名与公证；首次打开可能受到 Gatekeeper 限制。启动与字体问题见 [FAQ](https://github.com/ChenCLian/Aster/blob/main/FAQ.md)。

GitHub 自动生成的 `Source code` 是源码，需要编译，不能直接运行。附件及校验文件以发布页实际提供的内容为准。

## 使用与更新

第一次使用可从 [快速开始](https://github.com/ChenCLian/Aster/blob/main/QUICK_START.md) 中的正弦信号示例入手。即时预览不写入结果库，点击“运行流程”后保存完整结果。

“设置 → 检查更新”显示可用版本并打开下载页。本版使用完整程序包手动更新；安装新版前退出软件，保留原数据库和设置。

[项目介绍](https://github.com/ChenCLian/Aster#readme) · [方法文档](https://github.com/ChenCLian/Aster/tree/main/docs/methods) · [反馈问题](https://github.com/ChenCLian/Aster/issues)
