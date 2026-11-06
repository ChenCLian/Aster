# 构建、打包与发布维护

适用版本：Aster 1.1.1。[源码构建](building.md) · [文档索引](README.md)

## 版本与发布方式

根目录 `VERSION` 是程序版本入口；GitHub 标签使用对应的 `v` 前缀，例如 `1.1.1` 对应 `v1.1.1`。正式版与预发布版分别使用相应 Release 标记，预发布版本号可采用 `1.1.2-beta.1`。

软件通过公开 GitHub Releases 检查版本，显示说明并打开下载页。更新采用完整程序包手动安装。正式版通道排除预发布版；需要测试预发布时，在更新窗口选择包含预发布版的通道。

源码仓库根目录应直接包含 `Aster.pro`、`VERSION`、`src/`、`resources/`、`tools/` 和 `.github/`。本机配置 `config/local.pri`、构建目录、用户数据库和凭据不提交到仓库。

## GitHub Actions 构建

工作流 `.github/workflows/build-desktop.yml` 在原生 Linux/macOS runner 上编译、测试和打包。可从任意系统的浏览器触发，不要求维护者本机运行 macOS 或 Linux。

1. 完成参数参考生成与校验（见下文），提交待发布源码并推送到 GitHub。
2. 打开 **Actions → Build desktop packages → Run workflow**，选择待构建分支。
3. 确认 `linux-x64`、`macos-arm64`、`macos-x86_64` 三个任务的构建、文档同步检查、回归和打包步骤通过。
4. 下载运行页面底部的 Artifacts。`Aster-*` 内是程序包及 `.sha256`，`dependency-materials-*` 包含依赖源码与构建/许可记录，`logs-*` 为诊断日志。GitHub 为 Artifact 添加的外层 ZIP 需先解开，再上传内部程序包到 Release。
5. 保留该次运行链接、源码提交、测试日志和程序包摘要。失败的构建不作为可发布产物。

工作流只上传构件，不自动创建或修改 Release。依赖材料构件的 30 天保存期不能替代 Release 中持续可用的源码附件。当前配置为 Qt 6.8.3、Python 3.12；Linux 使用 Ubuntu 22.04，macOS 使用 `macos-15`（Apple Silicon）和 `macos-15-intel`（Intel）。具体值以工作流文件为准。

## 平台与产物

| 平台 | 1.1.1 程序包 | 启动入口 |
| --- | --- | --- |
| Windows x64 | `Aster-1.1.1-windows-x64.zip` | `Aster.exe` |
| macOS Apple Silicon | `Aster-1.1.1-macos-arm64.zip` | `Aster.app` |
| macOS Intel | `Aster-1.1.1-macos-x86_64.zip` | `Aster.app` |
| Linux x64 | `Aster-1.1.1-linux-x64.tar.gz` | `./AppRun` |

macOS CI 的部署目标为 **15.0**，依赖库须与目标架构一致。降低系统要求需要重建兼容依赖并重新验收，不能只修改标签。

Linux 使用便携目录，不依赖 AppImage/FUSE。程序包部署 Qt、SQLite 插件和数值依赖；glibc、图形驱动、显示服务和字体由系统提供。以 Ubuntu 22.04 构建并不表示所有发行版均已验证。当前图形入口使用 X11 或 XWayland。

## 本机打包

在目标系统完成 Release 构建后，从源码根目录执行脚本。脚本检查程序版本，并拒绝覆盖已有同名输出。

### Windows

在配置好 MSVC x64 工具的 PowerShell 中输入各依赖目录。

```powershell
$BuildDir = Read-Host "Release 构建目录"
$QtBin = Read-Host "匹配的 Qt bin 目录"
$VcpkgPrefix = Read-Host "实际使用的 installed/x64-windows 目录"
$VcpkgRoot = Read-Host "实际使用的 vcpkg 仓库根目录（包含 ports）"
powershell -ExecutionPolicy Bypass -File tools/release/package_windows.ps1 `
  -BuildDir $BuildDir `
  -QtBin $QtBin `
  -VcpkgPrefix $VcpkgPrefix `
  -VcpkgRoot $VcpkgRoot `
  -DependencyCache ../Aster-dependency-cache `
  -OutputDir ./dist/1.1.1
```

`VcpkgPrefix` 是库安装目录，`VcpkgRoot` 是包含构建配方的 vcpkg 仓库目录。已有同名输出时使用新的 `OutputDir`；下载缓存可以复用。

脚本会在部署前验证 Python 解释器；可用 `-PythonExe` 指定完整路径。依赖收集失败时，查看材料目录中的 `report.json` 和 `collector.log`。解释器预检失败不会生成报告；具体区别见[缺项与重试](dependency_materials_zh.md#缺项与重试)。

打包需要 Python 3.10 或更高版本，同时收集对应依赖源码与许可材料，详见[依赖材料说明](dependency_materials_zh.md)。额外附带安装器需要独立核对其来源与再分发条款；通常使用启动说明中的微软官方下载入口。SQL 插件仅部署 `qsqlite.dll`；Debug 与 Release 依赖不可混用。

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

程序包包含 `README.md`、`QUICK_START.md`、`FAQ.md`、`RELEASE_NOTES.md`、`AUTHORS.md`、`LICENSE`、`COPYRIGHT.md`、`SOURCE_CODE.md`、`SOURCE_DEPENDENCIES.md`、`START_HERE.txt` 和许可文件。README 的图标位于 `resources/app/aster.png`；该资源仅供随包文档显示，不改变应用内部资源。

Windows 文档由 `package_windows.ps1` 收集；macOS/Linux 使用 `stage_notices.py`。`START_HERE.txt` 由各平台脚本生成，提供启动、更新和文档入口。`SOURCE_DEPENDENCIES.md` 与 `licenses/dependencies/` 由依赖收集器按实际程序包生成；依赖源码归档单独输出到 `release-assets/`。使用手册、参数参考和科学方法文档通过 Qt 资源内置于程序；维护者编辑的 Markdown 位于源码 `docs/` 中。开发与构建文档不要求离线内置。

更新运行要求时，同步 README 下载表、快速开始、FAQ、发布说明和对应脚本中的启动提示。

## 发布前验证

对实际准备上传的包执行以下检查，并记录源码提交、系统、架构、包的 SHA-256 及结果。

- 自动化：根工程 Release 构建及七组回归测试；本机可运行 `python tools/release/run_tests.py build-release`。
- 桌面操作：首次启动、中文字体与图标、文本导入、生成信号、MTM/CWT、预览与正式运行。
- 结果：SQLite 保存与重新打开、数据库备份、PNG/PDF/CSV 导出、语言与主题切换、公开更新检查。
- 平台部署：Windows 运行库与 SQLite；macOS 架构、最低系统和 Gatekeeper；Linux X11/XWayland 及运行依赖。
- 文档：程序包内链接、启动入口、最低系统要求和版权/数据来源与实际产物一致。

测试环境应尽量独立于开发机的 Qt、vcpkg 或 Homebrew 搜索路径。无窗口 CLI 检查和 WSL 图形测试不能替代目标原生桌面的全部验收。

[validation.json](validation.json) 是当前源码验证的统一记录；[validation.md](validation.md) 提供摘要，`validation_results/release/` 保存对应日志，`PACKAGE_STATUS.json` 只引用这些记录。更新源码后按影响范围重新验证并同步记录。未受影响的既有验证可保留，但须单独标明来源、环境和范围，不能计入本次新运行数量；被修改的功能不能沿用旧的通过声明。科学参照的适用范围见 [验证证据](reference/scientific_evidence.json)。

## 许可与对应源码

Aster 整体使用 GPL-3.0-only，原创部分版权署名为 ZiJie Chen。`LICENSE` 保留官方完整条款；`COPYRIGHT.md` 界定范围；`SOURCE_CODE.md` 说明源码获取与再分发要求。第三方原有许可、版权和数据归属不得被项目声明覆盖。

每次发布程序包时，同时提供与其对应的完整项目源码，并记录源码提交和版本标签以便追踪。可整理实际构建所用源码，或使用下列脚本生成 `Aster_<version>_source.zip`；项目源码包不包含本机配置与构建缓存。

```sh
python tools/release/package_source.py --output ../release-source
```

输出目录须在源码树外。脚本保留 `config/local.pri.example`，排除个人 `config/local.pri`、构建产物和用户数据库，重新生成 `PACKAGE_MANIFEST.json` 与 ZIP 校验值。

还需记录实际分发的 Qt、GSL、OpenBLAS/LAPACK 及其他库的版本、构建配置和补丁，为适用许可要求覆盖的依赖提供对应源码。vcpkg 应记录 ports 的固定提交与补丁，Homebrew/发行版包应记录对应配方或源包。可将依赖源包作为单独的 Release 附件，也可在发布页明确列出精确且可持续访问的下载位置。只有上游主页、`copyright` 文件或 DLL 不足以完成这一项。

Windows、macOS、Linux 打包脚本调用自动收集器，获取实际依赖的对应源码、配方/补丁和许可证，生成单独的 `release-assets/`。缺项会停止压缩/签名并保留报告；收集器不发布 GitHub。操作命令、支持范围和异常处理见[依赖源码与许可材料](dependency_materials_zh.md)。发布前仍须核对实际下载入口与内容。

### 发布材料核对

| 材料 | 必须记录的内容 |
| --- | --- |
| 项目源码 | 与各平台程序一致的完整源码包及 SHA-256；建议记录提交和版本标签 |
| 分发依赖 | 各平台实际组件、准确版本、源包链接及摘要、构建配置、配方或 port 修订、补丁 |
| 许可与归属 | 原始许可、版权、修改说明；数据集的适用条款和引用 |
| 下载入口 | 与二进制相邻的源码和依赖材料入口，可免费访问并持续维护 |

发布前还须完成 [La2004/La2010 轨道表再分发核验](third_party.md#轨道表再分发核验)。当前工程只有来源与版权记录，尚无足以确认完整表随软件再分发范围的授权材料；项目 GPL 声明不能替代该依据。核验适用于包含这些数据的源码包与程序包。

`SOURCE_CODE.md` 描述发布义务，打包脚本收集并下载对应材料；实际上传后还须从下载入口核实内容与可访问性。许可文件齐全、对应源码可获取、第三方材料再分发依据是分别检查的项目。

## 创建 GitHub Release

1. 确认待发布提交、各平台包、对应源码及第三方再分发依据已完成上述检查。
2. 打开 **Releases → Draft a new release**，创建指向该提交的标签，例如 `v1.1.1`，标题填写 `Aster 1.1.1`。
3. 使用 `RELEASE_NOTES.md`，注明平台要求及实际 macOS 签名状态。
4. 上传程序包及其 `.sha256`、同版本项目源码包及其 `.sha256`，以及各平台 `release-assets/` 中的全部文件。同名且摘要相同的依赖附件上传一次，文件名应与生成的源码索引一致。Actions 构件先解压，再上传内部文件。
5. 核对标签指向实际构建提交，检查程序包内 `SOURCE_DEPENDENCIES.md` 的附件链接。GitHub 自动生成的 Source code 不包含外部依赖源码。正式版不勾选预发布；测试版勾选预发布。发布后确认下载、校验值及软件“检查更新”的跳转结果。

数据库不放进发行包。更新程序时保留用户原数据库；软件不自动迁移程序目录内的自建文件。

## 后续维护

代码版本更新时同步 `VERSION`、发布说明和版本元信息，重新运行 qmake、构建、测试及相应平台验收，再创建对应的新版本标签。已发布二进制有修订时使用新版本，不静默替换同版本程序。

文档更新是否需要编译，取决于其是否列入 `resources/help.qrc`。修改 README、FAQ、操作指南或参数参考等内置页面后，需要重新构建程序并打包，才能更新软件中的手册。只改构建、发布维护等非内置文档时，无需重建程序。已经发布的版本可先更新仓库文档，内置副本随下一版程序分发；仅替换包外 Markdown 不会更新程序内的手册。

数据库格式与程序版本分别管理。当前 schema 为 3；改变表结构时须设计备份、事务迁移、失败恢复和版本兼容策略。一般新增算法不要求数据库迁移。

## 文档维护与内置手册发布

| 修改内容 | 维护位置 | 需要执行的步骤 |
| --- | --- | --- |
| 方法参数、默认值、范围或输出 | 任务的 `descriptor()` | 重新编译描述导出工具，生成两份参数参考，再重新构建程序 |
| 参数名称和解释 | `resources/i18n/zh.json`、`en.json` | 生成两份参数参考，再重新构建程序；缺少中文允许回退英文 |
| 操作指南 | `docs/USER_GUIDE.md`、`USER_GUIDE_EN.md` | 同步两种语言的操作语义，再重新构建程序 |
| README、FAQ、方法说明等内置页面 | `resources/help.qrc` 中列出的 Markdown | 修改相应文件并重新构建程序 |
| 构建、发布和开发说明 | 不在 `resources/help.qrc` 中的维护文档 | 更新仓库文档，无需重建程序 |

文档措辞遵循[维护约定](README.md#文档维护约定)：操作手册描述当前行为，发布说明描述可见变更，维护文档记录构建与发布要求。

参数参考由任务定义与语言资源生成，不直接编辑生成的 Markdown。详细命令见[参数参考维护流程](../tools/help_reference/README.md)。发布维护的顺序为：

1. 修改任务定义或说明；若改变程序版本，同步 `VERSION`、发布说明和版本元信息。
2. 运行 qmake，启用 `CONFIG+=help_reference`，完成 Release 构建，确保描述导出工具对应当前代码和版本。
3. 运行生成工具，更新两种语言的参数参考并执行 `--check`。只改操作指南等非生成页面时可跳过生成。
4. 再次构建程序，将修改后的手册编入 Qt 资源；运行受影响的测试。准备正式发布时完成上文列出的完整平台验收。
5. 将源码与文档一同提交，再触发 GitHub Actions。文档同步检查失败时不会进入打包；下载实际通过的程序包并验收。

内置页面离线可用；缺失的页面或章节转到 GitHub `main` 分支对应路径，在线内容可能比已安装程序更新。目录链接优先打开内置目录页；没有内置目录页时转到 GitHub 目录，不拼接不存在的 README。

验收时检查 F1、方法问号、参数问号、两种语言、页内查找、返回/前进，以及本地缺页后的 GitHub 路由。自动化路由测试拦截系统打开链接，不依赖网络或浏览器登录。

## 参考资料

- [GPL v3 第 6 条：对应源码提供方式](https://www.gnu.org/licenses/gpl-3.0.html#section6)
- [Google 技术文档：使用现在时描述行为](https://developers.google.com/style/tense)
- [Microsoft：操作说明检查清单](https://learn.microsoft.com/en-us/style-guide/checklists/procedures-and-instructions-checklist)
- [GitHub Actions runner](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job)
- [Qt macOS 部署](https://doc.qt.io/qt-6/macos-deployment.html)
- [Apple 应用公证](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [GitHub Release 管理](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
