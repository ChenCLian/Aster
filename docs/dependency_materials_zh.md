# 依赖源码与许可材料

适用源码：AstroBatch 1.3.3-beta.2。[发布维护](release_maintenance_zh.md) · [第三方说明](third_party.md)

## 文件放在哪里

| 内容 | 存放与发布方式 | 是否参与编译 |
| --- | --- | --- |
| AstroBatch 源码、脚本、配置模板、项目许可、已有第三方源码 | 提交到 GitHub 仓库；随版本提供项目源码包 | 项目源码参与编译 |
| 下载的 Qt、GSL、OpenBLAS 等依赖源码归档 | 放在独立材料目录；作为 GitHub Release 附件发布 | 不编入 AstroBatch，不加入 `.pro` 或 `.qrc` |
| 下载缓存 | 本机保留，下一次收集复用；不提交仓库、不上传 Release | 不参与 |
| 实际分发依赖的许可证、版权及归属说明 | 打包时复制到程序包的 `licenses/dependencies/` | 作为文件随包分发，无需编入可执行文件 |
| 依赖清单、源码校验值、构建配方、补丁 | 生成独立的 `dependency-records.zip`；上传 Release | 用于查验与重建依赖 |

源码仓库根目录可以保持原来的位置。大型依赖源码不必放进仓库，更不必提交到 Git 历史。默认输出位于 `dist/`，已被 `.gitignore` 和项目源码打包脚本排除；也可以将输出和缓存放到仓库旁边的目录。

项目原有的 `src/third_party/`、`licenses/` 和资源归属文件继续提交，它们是源码及许可的一部分，不应删除。

## 跨版本复用与自动清单

Actions 打包前调用 `dependency_history.py`，从官方 GitHub 已发布的正式版或 beta 版读取最近三个可用的平台材料记录。只下载较小的记录 ZIP；记录 ZIP 和被引用源码附件的 SHA-256、大小、已上传状态均通过 GitHub 附件元数据核对。草稿和 alpha 不作为复用来源。

收集器仍从本次程序包识别依赖及其准确版本，重新生成构建配方、补丁记录、许可证索引和二进制清单。组件版本、来源及源码校验值匹配时，沿用已发布的源码附件链接，不重复下载、解包或上传这份大文件。Linux 源包还须逐项匹配当前 APT 源码索引中的 SHA-256。依赖升级、新增模块、校验值变化或附件缺失时，自动收集相应新材料；查询失败时回退完整收集，不凭历史通过状态跳过。

每次输出 `dependency-lock.json`，记录当前 AstroBatch 版本、平台、组件版本、来源、源码摘要和 `processed` 状态。成功后同时发布 `AstroBatch-<版本>-<平台>-dependency-lock.json`，并将清单及许可证保留在程序包中。未完成时 `processed` 为 false，错误原因在 `report.json`；不要手工改为 true。下一次发布直接读取已发布记录，因此不需要手动回写仓库中的通过标志。

`VERSION` 仍是应用版本的唯一来源。增加直接或间接依赖时，核对平台安装命令、构建配置、第三方说明和自动清单；程序版本变化本身不会使相同的依赖源码失效。材料 ZIP 仍按每次构建生成，通常远小于源码归档。必须保留 `SOURCE_DEPENDENCIES.md` 引用的历史 Release 附件，不随旧程序清理。四个平台的依赖材料只留在 GitHub，不同步至 Gitee。

本机需要同样复用时，先运行 `python tools/release/dependency_history.py --platform windows-x64 --directory <历史记录目录>`，再将 `ASTER_DEPENDENCY_HISTORY` 环境变量设为该目录并使用 `Collect` 模式；需要 GitHub CLI 可读取官方仓库。未设置该变量时保留完整收集方式。Windows 的 `Reuse` 是另一种同版本离线复用，仍要求依赖二进制逐字节一致；它不会重新确认远端附件可用性，发布前须检查引用链接。

## 自动收集范围

Windows 支持分步打包：先用 `-DependencyMode Stage` 部署程序，再单独运行 `tools/release/collect_dependency_materials.py` 收集材料。已有匹配材料时可用默认的 `Reuse` 模式离线复用，也可显式使用 `Collect` 一次完成部署与收集。Linux/macOS 原生打包脚本在完成产物之前调用收集器；macOS 在复制许可进入 `.app`、签名和公证之前完成收集。

- **Qt**：从匹配的 `qmake` 读取版本，下载官方源码模块及 SHA-256。默认包含 qtbase、qtsvg、qtimageformats、qttranslations；根据实际 Qt 库增加需要的模块。提取许可及 Qt 第三方归属文件指向的许可证。
- **Windows / vcpkg**：读取指定 triplet 的安装数据库、文件所有权和 SPDX；匹配实际 DLL，收集依赖闭包。使用 SPDX 记录的 URL 和摘要下载源码，按文件摘要恢复相同的 port 配方与补丁，不使用最新版本替代。
- **Linux / Debian、Ubuntu**：以 ELF build ID 匹配发行包和构建机上的库，由 dpkg 查询实际源包版本。使用已配置的软件源获取该版本的 `.dsc`、原始源码和发行版补丁包，并检查 `.dsc` 中的 SHA-256。OpenSSL 和被打包的 Fortran 等运行库一同检查。Qt 官方套件自带的 ICU 单独收集。
- **macOS / Homebrew**：按目标架构的 Mach-O UUID 匹配实际库，读取已安装 keg 的配方和 receipt，收集对应源码、资源及补丁。Qt 框架中的同名头文件不参与二进制匹配。本地补丁优先从安装目录、匹配配方的源缓存或安装记录中的固定提交恢复。API 安装缺少提交号时，允许查询 Homebrew 元数据，但仅接受与已安装版本及配方内容一致的候选；配方比较仅忽略 bottle 信息段，以及 GNU `ftpmirror.gnu.org` 下载指令中已知的 `/gnu/` 前缀差异；源码文件名、校验值、补丁和其他构建内容仍须一致。配方与补丁均从固定提交获取并核对 SHA-256，恢复依据及已安装配方摘要保存在 `local-patch-recovery.json`。版本、配方或校验值不符时停止收集。
- **已有第三方代码与数据声明**：继续由现有打包步骤收集。AstroBatch 的匹配源码仍由项目源码包提供。

脚本仅下载、校验、读取归档和复制材料，不编译下载的源码，不安装或更新依赖，不自动发布 GitHub。源归档完整保留；解读归档时不展开可执行程序或符号链接。

自动 Qt 路径适用于官方、未经自行修改的 Qt 桌面套件。自行编译或打补丁的 Qt、非标准包管理器和无法恢复来源的第三方二进制，应提供实际源码、配方及补丁。脚本不能从 DLL 自动恢复修改过的源代码。

## Windows 本机打包

要求已完成的 Release 构建，以及匹配的 Qt、vcpkg 安装。材料收集和复用另需 Python 3.10 或更高版本；仅部署程序的 `Stage` 模式不需要 Python。使用已配置 MSVC x64 工具的 PowerShell，在源码根目录按以下顺序运行。

**第一步：部署程序。** 首次打包、新版本打包或主动重新收集材料时，使用 `Stage`：

先确认根工程已完成当前源码的 Release 构建。部署不会重新编译；`BuildDir` 必须指向这次构建目录。三种模式均在 DLL 部署后运行程序核对完整版本，不接受“基础版本相同、beta 后缀不同”的 EXE。

```powershell
$Version = (Get-Content ./VERSION -Raw).Trim()
$ReleaseDir = "../AstroBatch-releases/$Version"
$BuildDir = Read-Host "Release 构建目录"
$QtBin = Read-Host "匹配的 Qt bin 目录"
$VcpkgPrefix = Read-Host "实际使用的 installed/x64-windows 目录"
$VcpkgRoot = Read-Host "实际使用的 vcpkg 仓库根目录（包含 ports）"
powershell -ExecutionPolicy Bypass -File tools/release/package_windows.ps1 `
  -BuildDir $BuildDir `
  -QtBin $QtBin `
  -VcpkgPrefix $VcpkgPrefix `
  -DependencyMode Stage `
  -OutputDir $ReleaseDir
```

`Stage` 正常结束后会显示已部署目录和后续材料步骤，不因缺少 `report.json` 报错。它不运行收集器或复用检查，也不下载文件；程序目录保留 `DEPENDENCY_MATERIALS_PENDING.txt`，此时尚未完成发布准备。Qt 部署、DLL 缺失或架构等实际错误仍会终止命令。

**第二步：收集并安装材料。** 对第一步生成的实际程序目录运行以下命令，不要另填一个尚不存在的 `staging` 目录：

```powershell
python -u -X utf8 tools/release/collect_dependency_materials.py `
  --platform windows-x64 `
  --package-dir "$ReleaseDir/AstroBatch-$Version-windows-x64" `
  --qmake "$QtBin/qmake.exe" `
  --vcpkg-prefix $VcpkgPrefix `
  --vcpkg-root $VcpkgRoot `
  --output "$ReleaseDir/dependency-materials/windows-x64" `
  --cache ../AstroBatch-dependency-cache
```

独立收集成功后会补齐该目录的许可和源码入口，并自动移除待处理标记；无需再次运行部署命令，也不要手动删除标记。收集失败时修复原因后对同一目录重试。`--plan-only` 或 `--no-install` 不会移除标记。

**第三步：测试并压缩。** 确认收集命令成功、`report.json` 为 `collected`、待处理标记已移除，再测试并压缩完整的 `AstroBatch-<版本>-windows-x64/` 目录。程序包和 `release-assets/` 中的材料按下文发布。项目源码导出独立于这三步，可在它们之前或之后执行。

其他打包方式：

| 模式 | 适用情况 | 材料步骤 |
| --- | --- | --- |
| `Stage` | 首次打包、新版本或希望手动收集材料 | 部署后正常结束，材料保持待处理；随后运行独立收集命令 |
| `Reuse`（默认） | 同一 AstroBatch 版本、相同运行依赖已有完整材料 | 离线验证并安装材料；缺失或不匹配则报错 |
| `Collect` | 希望部署与收集一次完成 | 部署后运行收集器；失败则报错 |

`Reuse` 读取 `OutputDir/dependency-materials/windows-x64` 的既有材料，也可通过 `-DependencyMaterials <材料目录>` 指定其他位置。该参数指向包含 `report.json` 和 `release-assets/` 的平台目录。离线复用会核对当前 AstroBatch 版本、平台、Qt 版本、全部原生运行文件的路径及摘要、依赖源归档摘要和记录 ZIP 的完整性。仅允许项目可执行文件 `AstroBatch.exe` 变化；依赖 DLL 增加、移除或内容变化均要求重新收集。复用不联网、不解读依赖源码归档、不更新原材料目录；会将记录 ZIP 中的许可复制到新程序包，更新包内 `licenses/dependencies/DEPENDENCIES.json` 的当前程序指纹并记录复用来源。原材料报告和记录 ZIP 保留收集时的快照。

误用 `Reuse` 导致材料检查失败时，若已显示 `Verified executable version` 并完成部署，可直接对保留的程序目录运行第二步，不必删除目录重新部署。版本检查失败不属于这种情况，必须先重新构建正确的程序。不要把复用失败当成发布成功。`Excluding unused SQL driver` 仅表示排除不用的 SQL 插件，是正常提示。

需要部署与收集一次完成时，将打包命令改为 `-DependencyMode Collect`，并按需传入 `-DependencyCache`、`-VcpkgRoot` 和 `-DependencySupplement`。`-DependencyMaterials` 在该模式下指定收集输出目录。`Stage` 与 `Reuse` 不使用下载缓存，也不接受新的补充声明；分步收集时将 `--cache`、`--vcpkg-root`、`--supplement` 传给独立收集命令。发布版本号改变时需重新生成对应版本的材料索引，下载缓存仍可复用。

`VcpkgPrefix` 指向库安装位置，`VcpkgRoot` 指向用于安装这些库的 vcpkg 配方仓库，两者不是同一个目录。收集材料时，若使用二进制缓存或版本化 manifest，必须能恢复与已安装 SPDX 匹配的配方；仅有 DLL 和头文件不足以恢复补丁。

`Reuse` 和 `Collect` 在部署之前依次尝试 `py -3`、`python`、`python3`，实际执行检查并要求 Python 3.10 或更高版本。需要指定解释器时，在打包命令中追加 `-PythonExe` 和解释器的完整路径。仅存在命令名或系统商店别名不足以通过检查。`Stage` 跳过解释器预检；独立收集时直接使用所需 Python 解释器运行命令。

AstroBatch 的 QWidget/QCustomPlot 视图使用 CPU 绘制。Windows 打包不附带未使用的软件 OpenGL 渲染器或系统 D3D 编译器；对于支持相应选项的新版 Qt，也排除自动附带的 DXC 编译器。实际通过导入表发现的运行依赖仍会检查。

额外附带的第三方安装器（包括通过 `-VcRedist` 指定的安装器）不被当作 AstroBatch 或 Qt 源码。来源/许可无法自动确认时会列为未解决项。通常保留启动文档中的微软官方下载入口即可，无需附带安装器。

## Linux 与 macOS

在同一原生构建机上运行原有打包命令即可；可设置共享缓存：

```sh
export ASTER_DEPENDENCY_CACHE="$(pwd)/../AstroBatch-dependency-cache"
# Linux
bash tools/release/package_linux.sh build-release "$(command -v qmake)" dist
# macOS Apple Silicon；Intel 使用 x86_64
bash tools/release/package_macos.sh build-release "$(command -v qmake)" arm64 dist
```

Linux 需要启用与二进制包仓库、发行版和更新通道匹配的 `deb-src` 索引。当前工作流同时识别传统 `.list` 和 deb822 `.sources` 配置，支持官方 HTTP(S) 仓库，以及 GitHub Ubuntu 构建环境使用的 `mirror+file:/etc/apt/apt-mirrors.txt`。本地镜像列表须位于 APT 配置目录内，且仅包含 Ubuntu/Debian 官方 HTTP(S) 镜像；镜像优先级和故障切换仍由 APT 处理。脚本保留发行版、组件和签名验证设置，再由工作流安装匹配的源码条目并更新索引。手动准备时，运行 `python3 tools/release/enable_deb_src.py --output-dir ./apt-source-config` 生成配置，检查后将生成的两个文件安装到 `/etc/apt/sources.list.d/`，再执行 `sudo apt-get update`。生成脚本仅处理已启用的仓库，不会自动修改系统软件源或启用第三方仓库。依赖安装和许可检查日志分别保存在 Actions 日志附件中的 `ci-logs/install-dependencies.log` 与 `ci-logs/license-tests.log`。

不能用 Windows 本机安装的库替代 GitHub Actions 中 Linux/macOS 实际使用的依赖材料。工作流在各平台打包时完成收集，并分别上传 `dependency-materials-<平台>` 构件。

## 输出与 GitHub Release

每个平台的材料位于 `dist/.../dependency-materials/<平台>/`：

| 文件或目录 | 用途 |
| --- | --- |
| `report.json` | 状态、实际组件、准确版本及未解决项 |
| `collector.log`（Windows 的 `Collect` 模式） | 收集器的标准输出、错误输出和退出码；本机诊断使用，不作为 Release 附件 |
| `release-assets/` | 应上传到 Release 的全部依赖源码与记录 ZIP；不额外生成 `.sha256` 文件 |
| `records/` | 记录 ZIP 的展开内容；用于本机查看，不必重复上传 |
| `cache/` 或指定共享缓存 | 已校验下载的缓存，不上传 |
| `SOURCE_DEPENDENCIES.md` | 该版本依赖源码入口，同时复制到程序包 |

`status: collected` 表示该次识别出的依赖材料已收集、摘要已核对、原生二进制已有来源映射；不是法律许可认证，也不表示已经上传、已做可重现构建或 AstroBatch EXE 已按当前源码重新编译。独立收集器不代替打包脚本的程序版本检查，不能用收集成功绕过版本不匹配错误。

本版本的 GitHub 发布流程使用程序和依赖材料两个发行页。

1. 程序发行页接收四个平台的最终程序包。从 Actions 手动取包时，先解开名为 `program-*` 的构件 ZIP，再取出其中的程序 ZIP 或 tar.gz。本机 Windows 打包脚本生成普通目录，需要压缩完整目录。
2. 在程序发行页核对 `AstroBatch-<版本>-source.zip`。Actions 根据已测试的提交导出源码并压缩，发布标签指向该提交。手工生成同一附件时，运行 `python tools/release/package_source.py --output ../release-source`，将导出的整个目录连同文件校验清单压缩。
3. 材料草稿 `materials-v<版本>` 接收依赖文件。手动操作时，下载并解开 Actions 的 `dependency-materials-*` 外层构件 ZIP，上传其中的依赖源文件和记录 ZIP；这些内容来自各平台的 `release-assets/`，构件解压后的目录层级可能不同。本机 Windows 从 `release-assets/` 取文件。
4. 多平台出现同名同摘要的依赖源文件时只上传一次；同名但内容不同的文件不能相互覆盖。推荐由发布工具上传：GitHub 会改写某些特殊字符，工具在上传前统一命名并检查冲突，同时生成文件名对照表。重建 Debian 源码前恢复 `.dsc` 引用的原始文件名。
5. 在发布说明中注明项目源码入口、各平台 `dependency-records.zip` 及依赖源码附件的位置。程序公告链接至本版本的材料页。先公开材料，再公开程序发行版。
6. 检查程序包内 `SOURCE_DEPENDENCIES.md` 的链接能下载相应附件。新材料链接指向当前版本，复用源码链接指向其原发布版本；对应的 Release 附件都必须保留。使用其他托管位置时同步该索引；之后重新打包，macOS 必须重新签名。

GitHub 为上传到 Release 的附件记录 SHA-256 摘要；Actions 程序构件也提供摘要。脚本不生成逐文件 `.sha256` 附件，发布工具生成统一的 `SHA256SUMS.txt`。依赖下载、缓存复用、来源匹配和 JSON 清单中的摘要校验继续保留。依赖源包保持上游格式与完整内容；本地原名及 `.dsc` 内容不变，远端发布名中的特殊字符使用统一规则规范。

`AstroBatch-asset-filenames.json` 记录原始文件名、发布名、大小和 SHA-256；`DEPENDENCY_DOWNLOADS.md` 提供下载链接。先以发布名验证 `SHA256SUMS.txt`，需要重建依赖时再恢复原名。当前依赖索引与历史复用共用同一命名规则。复用旧构建补建草稿时，不修改程序包内已签名或已验收的文件；包内旧链接若因特殊字符失效，使用该 Release 的 `DEPENDENCY_DOWNLOADS.md`。

Actions 构件保存期为 30 天，只用于交接构建结果；不能用临时构件链接替代持续可用的 Release 源码入口。启用草稿准备后，四个平台打包成功会自动创建或更新 GitHub Release 草稿及附件；它不会自动公开发布。

依赖版本未变且源码摘要相同时可以复用既有下载，脚本先校验缓存再决定是否联网下载源归档。每个 Release 的下载入口仍须正确；新材料索引指向本版本材料标签下的附件。Qt 按模块下载，常用模块合计约数十 MB；额外模块和编译器运行库源码会增加体积。

## 缺项与重试

收集时出现未识别库、摘要不一致、源包版本不可用、配方不匹配或许可文件缺失，脚本返回非零状态并生成 `report.json`，打包停止在发布产物完成前。离线复用失败会输出具体原因并保留待处理标记，不覆盖原材料报告，也不自动联网补齐。已经完成并验证的下载保留在缓存中。失败不会被标记为“已收集完成”。

Windows 打包的 `Collect` 模式另有进程诊断：Python 预检失败时，在部署前停止，不生成材料报告。预检通过后，收集器输出写入 `collector.log`；如果进程异常退出或没有生成有效报告，打包脚本在目录可写时补写 `phase: collector-process` 的失败报告。该状态表示收集过程未正常完成，不能据此认定缺少许可证。报告已列出具体材料问题时，保留原始错误内容。

| 报告问题 | 处理 |
| --- | --- |
| EXE 完整版本不匹配或版本检查超时 | 不继续材料步骤；核对 `BuildDir`、Qt 和运行依赖。版本错误时在新构建目录重新编译，再用新的 `OutputDir` 部署；依赖未变的同版本材料及下载缓存可保留 |
| Python 预检失败 | 安装 Python 3.10 或更高版本，或使用 `-PythonExe` 指定可运行的解释器；不需要重编译 AstroBatch |
| `phase: collector-process` | 查看同目录 `collector.log` 中的异常、参数错误或进程退出码；修复后对保留的程序目录单独运行收集器，或用新的 `OutputDir` 和 `-DependencyMode Collect` 重新打包 |
| Qt 源码/校验地址不可用 | 检查对应正式版本是否公开及网络连接；自定义 Qt 提供实际源码材料 |
| vcpkg recipe/patch hashes 不匹配 | 恢复安装时的 vcpkg checkout；脚本也会在本机最近 100 次 port 历史中寻找匹配文件 |
| 缺少 `vcpkg.spdx.json` | 还原完整安装记录，或补充经核实的准确版本、源包、许可及配方 |
| Debian 找不到确切源版本 | 启用正确的 `deb-src`，或从该发行版的历史源包获取原版本；不能以不同版本的源包代替 |
| Homebrew 旧 keg/配方已删除 | 恢复对应 keg/receipt/配方，或补充相同版本的材料 |
| `UNRESOLVED` 二进制 | 核对它的来源；不要通过忽略文件或改名跳过 |

可针对保留的未压缩 staging 目录单独运行收集器；先加 `--plan-only` 检查安装元数据，不下载源归档（仍会获取少量官方校验元数据）。`--no-install` 只输出材料，不改 staging 目录。

Windows 独立收集命令见[Windows 本机打包](#windows-本机打包)。

同一个收集输出目录可重试，过时清单会重新生成，缓存保留。整体打包脚本继续拒绝覆盖既有程序目录或程序包；仅材料步骤失败时，可对已经部署完成的 Windows 目录单独重新收集；重新部署程序时仍需使用新的 `OutputDir`，并可用 `-DependencyMaterials` 指向已收集的材料。已签名 macOS 应用应从构建输出重新打包，不能直接修改其内部许可文件。

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
