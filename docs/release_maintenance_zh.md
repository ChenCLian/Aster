# 构建、打包与发布维护

适用版本：Aster 1.1.0。[源码构建](building.md) · [文档索引](README.md)

## 版本与发布方式

根目录 `VERSION` 是程序版本入口；GitHub 标签使用对应的 `v` 前缀，例如 `1.1.0` 对应 `v1.1.0`。正式版与预发布版分别使用相应 Release 标记，预发布版本号可采用 `1.1.1-beta.1`。

软件通过公开 GitHub Releases 检查版本，显示说明并打开下载页。更新采用完整程序包手动安装。正式版通道排除预发布版；需要测试预发布时，在更新窗口选择包含预发布版的通道。

源码仓库根目录应直接包含 `Aster.pro`、`VERSION`、`src/`、`resources/`、`tools/` 和 `.github/`。本机配置 `config/local.pri`、构建目录、用户数据库和凭据不提交到仓库。

## GitHub Actions 构建

工作流 `.github/workflows/build-desktop.yml` 在原生 Linux/macOS runner 上编译、测试和打包。可从任意系统的浏览器触发，不要求维护者本机运行 macOS 或 Linux。

1. 提交待发布源码并推送到 GitHub。
2. 打开 **Actions → Build desktop packages → Run workflow**，选择待构建分支。
3. 确认 `linux-x64`、`macos-arm64`、`macos-x86_64` 三个任务的构建、回归和打包步骤通过。
4. 下载运行页面底部的 Artifacts。`Aster-*` 内是程序包及 `.sha256`，`logs-*` 为诊断日志。GitHub 为 Artifact 添加的外层 ZIP 需先解开，再上传内部程序包到 Release。
5. 保留该次运行链接、源码提交、测试日志和程序包摘要。失败的构建不作为可发布产物。

工作流只上传构件，不自动创建或修改 Release。当前配置为 Qt 6.8.3、Python 3.12；Linux 使用 Ubuntu 22.04，macOS 使用 `macos-15`（Apple Silicon）和 `macos-15-intel`（Intel）。具体值以工作流文件为准。

## 平台与产物

| 平台 | 1.1.0 程序包 | 启动入口 |
| --- | --- | --- |
| Windows x64 | `Aster-1.1.0-windows-x64.zip` | `Aster.exe` |
| macOS Apple Silicon | `Aster-1.1.0-macos-arm64.zip` | `Aster.app` |
| macOS Intel | `Aster-1.1.0-macos-x86_64.zip` | `Aster.app` |
| Linux x64 | `Aster-1.1.0-linux-x64.tar.gz` | `./AppRun` |

macOS CI 的部署目标为 **15.0**，依赖库须与目标架构一致。降低系统要求需要重建兼容依赖并重新验收，不能只修改标签。

Linux 使用便携目录，不依赖 AppImage/FUSE。程序包部署 Qt、SQLite 插件和数值依赖；glibc、图形驱动、显示服务和字体由系统提供。以 Ubuntu 22.04 构建并不表示所有发行版均已验证。当前图形入口使用 X11 或 XWayland。

## 本机打包

在目标系统完成 Release 构建后，从源码根目录执行脚本。脚本检查程序版本，并拒绝覆盖已有同名输出。

### Windows

在配置好 MSVC x64 工具的 PowerShell 中输入各依赖目录。以下命令不包含固定的个人路径。

```powershell
$BuildDir = Read-Host "Release 构建目录"
$QtBin = Read-Host "匹配的 Qt bin 目录"
$VcpkgPrefix = Read-Host "vcpkg 的 installed/x64-windows 目录"
powershell -ExecutionPolicy Bypass -File tools/release/package_windows.ps1 `
  -BuildDir $BuildDir `
  -QtBin $QtBin `
  -VcpkgPrefix $VcpkgPrefix `
  -OutputDir ./dist/1.1.0
```

可通过 `-VcRedist` 附带官方 Visual C++ x64 运行库安装程序。SQL 插件仅部署 `qsqlite.dll`；Debug 与 Release 依赖不可混用。

### macOS / Linux

将对应 Qt 6 的 `bin` 目录加入当前终端的 PATH，使 `qmake` 指向用于构建的 Qt；若命令名为 `qmake6`，相应替换下面的命令名。

```sh
# macOS Apple Silicon；Intel 将 arm64 改为 x86_64
bash tools/release/package_macos.sh build-release "$(command -v qmake)" arm64 dist

# Linux x64
bash tools/release/package_linux.sh build-release "$(command -v qmake)" dist
```

macOS 默认从 Homebrew 查询 GSL/OpenBLAS，可通过 `GSL_PREFIX`、`OPENBLAS_ROOT` 指定依赖位置。Linux 可通过 `ASTER_QT_PLUGINS` 指定与构建版本匹配的 Qt 插件目录。linuxdeploy 下载使用固定 SHA-256 校验；资产变化时应审核新工具并更新摘要。

### macOS 签名与公证

默认工作流采用 ad-hoc 签名，未提供 Apple Developer ID 签名与公证。发布说明应明确实际签名状态。

需要正式签名时，在已配置证书与钥匙串的 macOS 构建环境设置以下变量，替换尖括号内的占位内容。

```sh
export ASTER_SIGN_IDENTITY='Developer ID Application: <证书名称>'
export ASTER_NOTARY_PROFILE='<钥匙串公证配置名称>'
bash tools/release/package_macos.sh build-release "$(command -v qmake)" arm64 dist
```

脚本检查依赖与架构并验证签名；提供公证配置时提交 Apple，成功后装订凭据。证书导入和账户配置需另行完成；云端凭据使用 GitHub Secrets，不写入源码。

## 随包文档

程序包包含 `README.md`、`QUICK_START.md`、`FAQ.md`、`RELEASE_NOTES.md`、`AUTHORS.md`、`START_HERE.txt` 和许可文件。README 的图标位于 `resources/app/aster.png`；该资源仅供随包文档显示，不改变应用内部资源。

Windows 文档由 `package_windows.ps1` 收集；macOS/Linux 使用 `stage_notices.py`。`START_HERE.txt` 由各平台脚本生成，只保留启动、更新和文档入口。完整方法和开发说明保留在源码 `docs/` 中。

更新运行要求时，同步 README 下载表、快速开始、FAQ、发布说明和对应脚本中的启动提示。

## 发布前验证

对实际准备上传的包执行以下检查，并记录源码提交、系统、架构、包的 SHA-256 及结果。

- 自动化：根工程 Release 构建及七组回归测试；本机可运行 `python tools/release/run_tests.py build-release`。
- 桌面操作：首次启动、中文字体与图标、文本导入、生成信号、MTM/CWT、预览与正式运行。
- 结果：SQLite 保存与重新打开、数据库备份、PNG/PDF/CSV 导出、语言与主题切换、公开更新检查。
- 平台部署：Windows 运行库与 SQLite；macOS 架构、最低系统和 Gatekeeper；Linux X11/XWayland 及运行依赖。
- 文档：程序包内链接、启动入口、最低系统要求和版权/数据来源与实际产物一致。

测试环境应尽量独立于开发机的 Qt、vcpkg 或 Homebrew 搜索路径。无窗口 CLI 检查和 WSL 图形测试不能替代目标原生桌面的全部验收。

[validation.md](validation.md)、[validation.json](validation.json) 保存具体批次的归档结果；相同程序版本号下的后续修改不能直接沿用旧批次的通过声明。科学参照的适用范围见 [验证证据](reference/scientific_evidence.json)。

## 创建 GitHub Release

1. 确认待发布提交及各平台包已完成对应检查。
2. 打开 **Releases → Draft a new release**，创建指向该提交的标签，例如 `v1.1.0`，标题填写 `Aster 1.1.0`。
3. 使用 `RELEASE_NOTES.md`，注明平台要求及实际 macOS 签名状态。
4. 上传程序包和对应 `.sha256`，需要时附源码包。GitHub 自动生成的 Source code 不替代可运行程序包。
5. 正式版不勾选预发布；测试版勾选预发布。发布后确认下载、校验值及旧版本“检查更新”的跳转结果。

数据库不放进发行包。更新程序时保留用户原数据库；软件不自动迁移程序目录内的自建文件。

## 后续维护

代码版本更新时同步 `VERSION`、发布说明和版本元信息，重新运行 qmake、构建、测试及相应平台验收，再创建对应的新版本标签。已发布二进制有修订时使用新版本，不静默替换同版本程序。

仅修改文档不要求重新编译。尚未发布的程序包可重新打包文档并重新计算 SHA-256；已经发布的版本可先更新仓库文档，随下一版程序包分发。

数据库格式与程序版本分别管理。当前 schema 为 3；改变表结构时须设计备份、事务迁移、失败恢复和版本兼容策略。一般新增算法不要求数据库迁移。

干净源码归档从源码根目录运行：

```sh
python tools/release/package_source.py --output ../release-source
```

输出目录须在源码树外。脚本保留 `config/local.pri.example`，排除个人 `config/local.pri`、构建产物和用户数据库，重新生成 `PACKAGE_MANIFEST.json` 与 ZIP 校验值。

## 参考资料

- [GitHub Actions runner](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job)
- [Qt macOS 部署](https://doc.qt.io/qt-6/macos-deployment.html)
- [Apple 应用公证](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [GitHub Release 管理](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
