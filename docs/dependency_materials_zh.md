# 依赖源码与许可材料

适用版本：Aster 1.1.1。[发布维护](release_maintenance_zh.md) · [第三方说明](third_party.md)

## 文件放在哪里

| 内容 | 存放与发布方式 | 是否参与编译 |
| --- | --- | --- |
| Aster 源码、脚本、配置模板、项目许可、已有第三方源码 | 提交到 GitHub 仓库；随版本提供项目源码包 | 项目源码参与编译 |
| 下载的 Qt、GSL、OpenBLAS 等依赖源码归档 | 放在独立材料目录；作为 GitHub Release 附件发布 | 不编入 Aster，不加入 `.pro` 或 `.qrc` |
| 下载缓存 | 本机保留，下一次收集复用；不提交仓库、不上传 Release | 不参与 |
| 实际分发依赖的许可证、版权及归属说明 | 打包时复制到程序包的 `licenses/dependencies/` | 作为文件随包分发，无需编入可执行文件 |
| 依赖清单、源码校验值、构建配方、补丁 | 生成独立的 `dependency-records.zip`；上传 Release | 用于查验与重建依赖 |

源码仓库根目录可以保持原来的位置。大型依赖源码不必放进仓库，更不必提交到 Git 历史。默认输出位于 `dist/`，已被 `.gitignore` 和项目源码打包脚本排除；也可以将输出和缓存放到仓库旁边的目录。

项目原有的 `src/third_party/`、`licenses/` 和资源归属文件继续提交，它们是源码及许可的一部分，不应删除。

## 自动收集范围

三个原生打包脚本在压缩程序包之前调用 `tools/release/collect_dependency_materials.py`。macOS 在复制许可进入 `.app`、签名和公证之前完成收集。

- **Qt**：从匹配的 `qmake` 读取版本，下载官方源码模块及 SHA-256。默认包含 qtbase、qtsvg、qtimageformats、qttranslations；根据实际 Qt 库增加需要的模块。提取许可及 Qt 第三方归属文件指向的许可证。
- **Windows / vcpkg**：读取指定 triplet 的安装数据库、文件所有权和 SPDX；匹配实际 DLL，收集依赖闭包。使用 SPDX 记录的 URL 和摘要下载源码，按文件摘要恢复相同的 port 配方与补丁，不使用最新版本替代。
- **Linux / Debian、Ubuntu**：以 ELF build ID 匹配发行包和构建机上的库，由 dpkg 查询实际源包版本。使用已配置的软件源获取该版本的 `.dsc`、原始源码和发行版补丁包，并检查 `.dsc` 中的 SHA-256。OpenSSL 和被打包的 Fortran 等运行库一同检查。Qt 官方套件自带的 ICU 单独收集。
- **macOS / Homebrew**：以 Mach-O UUID 匹配实际库，读取已安装 keg 的配方和 receipt，收集对应源码、资源及补丁；不使用更新后的在线配方代替旧安装记录。
- **已有第三方代码与数据声明**：继续由现有打包步骤收集。Aster 的匹配源码仍由项目源码包提供。

脚本仅下载、校验、读取归档和复制材料，不编译下载的源码，不安装或更新依赖，不自动发布 GitHub。源归档完整保留；解读归档时不展开可执行程序或符号链接。

自动 Qt 路径适用于官方、未经自行修改的 Qt 桌面套件。自行编译或打补丁的 Qt、非标准包管理器和无法恢复来源的第三方二进制，应提供实际源码、配方及补丁。脚本不能从 DLL 自动恢复修改过的源代码。

## Windows 本机打包

要求 Python 3.10 或更高版本、已完成的 Release 构建，以及匹配的 Qt、vcpkg 安装。使用已配置 MSVC x64 工具的 PowerShell，在源码根目录运行：

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

`VcpkgPrefix` 指向库安装位置，`VcpkgRoot` 指向用于安装这些库的 vcpkg 配方仓库，两者不是同一个目录。使用二进制缓存或版本化 manifest 时，必须能恢复与已安装 SPDX 匹配的配方；仅有 DLL 和头文件不足以恢复补丁。

脚本在部署之前依次尝试 `py -3`、`python`、`python3`，实际执行检查并要求 Python 3.10 或更高版本。需要指定解释器时，在打包命令中追加 `-PythonExe` 和解释器的完整路径。仅存在命令名或系统商店别名不足以通过检查。

Aster 的 QWidget/QCustomPlot 视图使用 CPU 绘制。Windows 打包不附带未使用的软件 OpenGL 渲染器或系统 D3D 编译器；对于支持相应选项的新版 Qt，也排除自动附带的 DXC 编译器。实际通过导入表发现的运行依赖仍会检查。

额外附带的第三方安装器（包括通过 `-VcRedist` 指定的安装器）不被当作 Aster 或 Qt 源码。来源/许可无法自动确认时会列为未解决项。通常保留启动文档中的微软官方下载入口即可，无需附带安装器。

## Linux 与 macOS

在同一原生构建机上运行原有打包命令即可；可设置共享缓存：

```sh
export ASTER_DEPENDENCY_CACHE="$(pwd)/../Aster-dependency-cache"
# Linux
bash tools/release/package_linux.sh build-release "$(command -v qmake)" dist
# macOS Apple Silicon；Intel 使用 x86_64
bash tools/release/package_macos.sh build-release "$(command -v qmake)" arm64 dist
```

Linux 需要启用与二进制包仓库、发行版和更新通道匹配的 `deb-src` 索引。当前工作流同时识别传统 `.list` 和 deb822 `.sources` 配置，保留发行版、组件和签名验证设置，再安装匹配的源码条目并更新索引。手动准备时，运行 `python3 tools/release/enable_deb_src.py --output-dir ./apt-source-config` 生成配置，检查后将生成的两个文件安装到 `/etc/apt/sources.list.d/`，再执行 `sudo apt-get update`。生成脚本仅处理已启用的 Ubuntu/Debian 官方仓库，不会自动修改系统软件源或启用第三方仓库。依赖安装和许可检查日志分别保存在 Actions 日志附件中的 `ci-logs/install-dependencies.log` 与 `ci-logs/license-tests.log`。

不能用 Windows 本机安装的库替代 GitHub Actions 中 Linux/macOS 实际使用的依赖材料。工作流在各平台打包时完成收集，并分别上传 `dependency-materials-<平台>` 构件。

## 输出与 GitHub Release

每个平台的材料位于 `dist/.../dependency-materials/<平台>/`：

| 文件或目录 | 用途 |
| --- | --- |
| `report.json` | 状态、实际组件、准确版本及未解决项 |
| `collector.log`（Windows） | 收集器的标准输出、错误输出和退出码；本机诊断使用，不作为 Release 附件 |
| `release-assets/` | 应上传到 Release 的全部依赖源码、记录 ZIP 与摘要文件 |
| `records/` | 记录 ZIP 的展开内容；用于本机查看，不必重复上传 |
| `cache/` 或指定共享缓存 | 已校验下载的缓存，不上传 |
| `SOURCE_DEPENDENCIES.md` | 该版本依赖源码入口，同时复制到程序包 |

`status: collected` 表示该次识别出的依赖材料已收集、摘要已核对、原生二进制已有来源映射；不是法律许可认证，也不表示已经上传或已做可重现构建。

在 GitHub 创建对应版本 Release 后：

1. 上传各平台程序压缩包和校验文件。
2. 上传与程序一致的 Aster 项目源码包及其 `.sha256`。可直接整理匹配源码，也可运行 `python tools/release/package_source.py --output ../release-source`。
3. 下载并解开 Actions 的 `dependency-materials-*` 外层构件 ZIP，上传其中的依赖源文件、记录 ZIP 和摘要文件；这些内容来自各平台的 `release-assets/`，构件解压后的目录层级可能不同。本机 Windows 从 `release-assets/` 取文件。
4. 多平台出现同名同摘要的依赖源文件时只上传一次；同名但内容不同的文件不能相互覆盖。保留 Debian `.dsc` 所引用的源文件原名。
5. 在发布说明中注明项目源码包、各平台 `dependency-records.zip` 及依赖源码附件的位置。源码附件和程序放在同一 Release 最便于维护。
6. 检查程序包内 `SOURCE_DEPENDENCIES.md` 的链接能下载相应附件。使用其他托管位置时同步该索引；之后重新打包，macOS 必须重新签名。

Actions 构件保存期为 30 天，只用于交接构建结果；不能用临时构件链接替代持续可用的 Release 源码入口。工作流不创建 Release，也不替用户上传 Release 附件。

依赖版本未变且源码摘要相同时可以复用既有下载，脚本先校验缓存再决定是否联网下载源归档。每个 Release 的下载入口仍须正确；默认索引指向本次版本标签下的附件。Qt 按模块下载，常用模块合计约数十 MB；额外模块和编译器运行库源码会增加体积。

## 缺项与重试

出现未识别库、摘要不一致、源包版本不可用、配方不匹配或许可文件缺失时，脚本返回非零状态，生成 `report.json`，打包停止在压缩/签名前。已经完成并验证的下载保留在缓存中。失败不会被标记为“已收集完成”。

Windows 打包另有进程诊断：Python 预检失败时，在部署前停止，不生成材料报告。预检通过后，收集器输出写入 `collector.log`；如果进程异常退出或没有生成有效报告，打包脚本在目录可写时补写 `phase: collector-process` 的失败报告。该状态表示收集过程未正常完成，不能据此认定缺少许可证。报告已列出具体材料问题时，保留原始错误内容。

| 报告问题 | 处理 |
| --- | --- |
| Python 预检失败 | 安装 Python 3.10 或更高版本，或使用 `-PythonExe` 指定可运行的解释器；不需要重编译 Aster |
| `phase: collector-process` | 查看同目录 `collector.log` 中的异常、参数错误或进程退出码；修复后用新的 `OutputDir` 重试 |
| Qt 源码/校验地址不可用 | 检查对应正式版本是否公开及网络连接；自定义 Qt 提供实际源码材料 |
| vcpkg recipe/patch hashes 不匹配 | 恢复安装时的 vcpkg checkout；脚本也会在本机最近 100 次 port 历史中寻找匹配文件 |
| 缺少 `vcpkg.spdx.json` | 还原完整安装记录，或补充经核实的准确版本、源包、许可及配方 |
| Debian 找不到确切源版本 | 启用正确的 `deb-src`，或从该发行版的历史源包获取原版本；不能以不同版本的源包代替 |
| Homebrew 旧 keg/配方已删除 | 恢复对应 keg/receipt/配方，或补充相同版本的材料 |
| `UNRESOLVED` 二进制 | 核对它的来源；不要通过忽略文件或改名跳过 |

可针对保留的未压缩 staging 目录单独运行收集器；先加 `--plan-only` 检查安装元数据，不下载源归档（仍会获取少量官方校验元数据）。`--no-install` 只输出材料，不改 staging 目录。

```powershell
python tools/release/collect_dependency_materials.py `
  --platform windows-x64 `
  --package-dir ./dist/1.1.1/Aster-1.1.1-windows-x64 `
  --qmake "$QtBin/qmake.exe" `
  --vcpkg-prefix $VcpkgPrefix `
  --vcpkg-root $VcpkgRoot `
  --output ./dist/1.1.1/dependency-materials/windows-x64 `
  --cache ../Aster-dependency-cache
```

同一个收集输出目录可重试，过时清单会重新生成，缓存保留。整体打包脚本继续拒绝覆盖既有程序包；确认材料可获取后，用新的 `OutputDir` 重新打包。已签名 macOS 应用应从构建输出重新打包，不能直接修改其内部许可文件。

直接运行 Python 收集器时，不经过 Windows 包装脚本的日志和兜底报告逻辑。如果尚未生成 `report.json`，以终端输出为准。Windows 进程诊断回归可运行 `powershell -ExecutionPolicy Bypass -File tools/release/test_windows_packaging.ps1`，无需 Qt、MSVC 或网络。

特殊安装可使用 `--supplement <JSON>`（Windows 打包为 `-DependencySupplement`，Unix 为 `ASTER_DEPENDENCY_SUPPLEMENT`）。格式示例见 [dependency_supplement.example.json](../tools/release/dependency_supplement.example.json)。替换自动识别项必须明确 `replace: true`，版本必须与已识别记录一致，提供原始许可、构建配方/补丁、带 SHA-256 的完整源归档和来源说明。新增二进制映射同时检查文件 SHA-256。示例占位符必须替换，不能直接用作完成声明。

La2004/La2010 轨道表再分发依据单独核验；收集器记录为 deferred，不在本流程中下载或修改轨道表，也不将它们标为已获授权。

## 依据

- [GPL 第 3 版：对应源码与分发](https://www.gnu.org/licenses/gpl-3.0.html#section6)
- [Qt 许可与内含第三方组件](https://doc.qt.io/qt-6/licensing.html)
- [vcpkg 软件物料清单](https://learn.microsoft.com/en-us/vcpkg/reference/software-bill-of-materials)
- [vcpkg 安装目录](https://learn.microsoft.com/en-us/vcpkg/reference/installation-tree-layout)
- [Homebrew 安装记录与配方](https://docs.brew.sh/Formula-Cookbook)
- [Ubuntu 获取源包](https://documentation.ubuntu.com/project/contributors/setup/get-the-source-of-a-package/)
