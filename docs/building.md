# 源码构建与开发

适用版本：Aster 1.2.0-beta.7。

[用户指南](../QUICK_START.md) · [发布与版本维护](release_maintenance_zh.md) · [文档索引](README.md)

本文面向从源码编译或维护 Aster 的开发者。安装发行包无需配置 Qt、vcpkg 或编译器。

## 依赖

| 依赖 | 用途 |
| --- | --- |
| C++17 编译器、qmake | 构建源码 |
| Qt 6 Core、Gui、Widgets、Sql、PrintSupport、Network | 界面、数据库、绘图与更新检查 |
| Qt Test、QSQLITE 插件 | 自动化测试及 SQLite 数据库访问 |
| LP64 LAPACK / OpenBLAS | 线性代数计算 |
| GSL | 插值与统计计算 |
| PocketFFT、wavelib、CppLowess、QCustomPlot | 随源码提供，原许可保留 |

线性代数和通用 FFT 后端固定为 **OpenBLAS/LAPACK + PocketFFT**。依赖配置见 [数值后端说明](numeric_backends.md)。

## Windows / Qt Creator

使用 Qt 6 / MSVC 64-bit Kit，并配置与编译器和架构匹配的 x64 依赖。

1. 将源码解压到独立目录，为它创建单独的构建目录。
2. 在 `config/local.pri` 中填写本机依赖路径；没有该文件时，可从 `config/local.pri.example` 创建。已有配置无需覆盖，不要提交该本地配置。
3. 当前 vcpkg 配置需要 `gsl:x64-windows`、`openblas:x64-windows` 和 `lapack-reference:x64-windows`。
4. Qt Creator 打开根目录 **`Aster.pro`**，选择 Kit 和 **Release**，运行 qmake，再构建整个工程。
5. 运行配置选择 **desktop / Aster**。程序输出通常位于构建根目录的 **`bin/Aster.exe`**。

根工程统一构建依赖模块，使用 `bin/` 和 `lib/` 输出目录；桌面入口为 `Aster`。直接运行开发构建时，仍需匹配的 Qt 插件和第三方运行库；Debug 与 Release 依赖不可混用。

## Linux

准备 Qt 6、QSQLITE、GSL 和 LP64 OpenBLAS/LAPACK 后，在包含 `Aster.pro` 的源码根目录执行以下命令。

```sh
mkdir build-release
cd build-release
qmake6 ../Aster.pro CONFIG+=release CONFIG-=debug
make -j4
./bin/Aster
```

部分环境中的 Qt 6 qmake 命令名为 `qmake`，请使用对应 Qt 6 安装提供的命令。

`CONFIG+=core_only` 只构建核心相关目标；`CONFIG+=no_gui` 排除桌面模块。需要完整桌面软件时不要添加这两个选项。

## macOS

使用对应架构的原生 macOS 环境构建 `.app`。没有 Mac 时，可从 Windows 在仓库 Actions 中启动随包工作流，分别构建 Apple Silicon 与 Intel 包。Qt、GSL 和 OpenBLAS/LAPACK 必须包含对应架构。签名、公证和原生验收见 [发布说明](release_maintenance_zh.md)。

## 打包与更新

程序版本的统一入口是根目录 [VERSION](../VERSION)。修改版本或图标构建配置后，重新运行 qmake 并构建。完整版本通过生成头文件编入程序；版本文件变更会触发 qmake 更新及相关源文件重新编译。

升级旧源码或发现界面版本与源码不一致时，使用新的 Release 构建目录编译整个根工程，避免沿用旧对象文件。打包脚本不编译源码，只复制 `BuildDir/bin/Aster.exe`；修改输出目录名、导出源码或收集材料均不会更新这个 EXE。发布前打开实际程序包，在“设置”中核对完整版本（包括 `beta` 后缀）。

- **源码导出：**`tools/release/package_source.py --output ../release-source` 生成 `Aster-source-export-<版本>/` 普通目录，排除本机配置、构建产物和用户数据库；不生成 ZIP 或独立校验文件。
- **Windows 程序包：**完成 MSVC x64 Release 构建后，使用 `tools/release/package_windows.ps1 -DependencyMode Stage` 部署 EXE、Qt 插件和第三方 DLL，再单独收集依赖源码与许可材料。已有同版本匹配材料时可用默认的 `Reuse` 模式离线复用；`Collect` 模式一次完成部署与收集。操作见[依赖材料说明](dependency_materials_zh.md#windows-本机打包)。项目源码通过独立导出命令生成普通目录。发布前应在未安装开发环境的 Windows 主机测试。
- **检查更新：**“设置 → 检查更新”按需读取本项目的 GitHub Releases，比较版本并打开发布页。默认检查正式版；草稿不会作为可用更新。
- **安装新版：**当前采用完整程序包手动更换；关闭软件后，将新版解压到新目录，再打开原数据库。
- **macOS / Linux：**`package_macos.sh`、`package_linux.sh` 收集各平台依赖并生成完整程序包。`.github/workflows/build-desktop.yml` 可在 GitHub 的原生系统上自动构建、测试和打包；不自动发布。

源码导出、依赖材料收集/复用和发布校验需要 Python 3.10 或更高版本；Windows 的 `Stage` 部署模式不需要 Python。收集材料还需匹配的依赖安装记录。具体命令见 [发布与版本维护](release_maintenance_zh.md)，依赖源码、缓存和 Release 附件的管理见 [依赖材料](dependency_materials_zh.md)。

常用命令行：

```sh
Aster --version
Aster --numeric-backend
Aster --database ./results.sqlite --language zh --theme light
```

## 开发与验证

| 入口 | 用途 |
| --- | --- |
| `src/core/`、`src/model/` | 工作流、任务契约与数据类型 |
| `src/processing/`、`src/math/` | 算法任务封装与数值计算 |
| `src/io/`、`src/application/` | 数据读取、存储与执行调度 |
| `src/ui/`、`src/update/` | 界面、交互与版本检查 |
| `resources/`、`config/` | 图标、主题、翻译、数据资源与构建配置 |
| `docs/`、`tools/` | 开发说明、验证工具与发布脚本 |

新增算法从 [功能扩展清单](Aster_development_checklist.md) 和 [任务接入说明](add_task.md) 开始。任务目录、参数面板和预览共用注册描述；新增 C++ 算法需要实现、注册、加入构建清单并验证。

修改参数定义或帮助译文时，按[参数参考维护说明](../tools/help_reference/README.md)生成并校验两种语言的文档，再重新构建程序。常规编译不需要 Python；生成和发布校验需要 Python 3.10 或以上版本。

当前源码的构建与回归范围见 [当前验证入口](validation_current.md)；发行验收以对应源码提交和程序包的记录为准。自动化测试不能替代各平台的实际部署与交互验收。

