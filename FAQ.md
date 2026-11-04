# Aster 常见问题

适用于 Aster 1.1.0。[快速开始](QUICK_START.md) · [软件介绍](README.md)

## 安装与启动

<a id="linux-fonts"></a>

### Linux / WSL 的汉字或符号显示为方框

英文和数字正常、汉字统一显示为方框，通常是系统缺少对应字体。Aster 使用系统字体，当前发行包未附带中文字体。

先关闭 Aster。Ubuntu / Debian 用户在 Linux 终端执行以下命令。

```bash
sudo apt update
sudo apt install fontconfig fonts-noto-cjk fonts-noto-core fonts-noto-color-emoji
fc-cache -f
```

重新运行 `./AppRun`。中文字体为 `fonts-noto-cjk`，其余字体补充通用字符和表情符号。无需重新编译 Aster。其他发行版应使用各自的软件包管理器安装 Noto CJK 或其他中文字体。

若仍有方框，运行下面的命令查看系统可用中文字体，并将输出随问题报告提供。

```bash
fc-list :lang=zh family
```

系统中已有中文字体却仍显示异常，需进一步检查字体识别与配置。若只有少数图标缺失，请同时注明图标所在位置。

参考 [Ubuntu 字体包](https://packages.ubuntu.com/jammy/all/fonts-noto-cjk) 与 [Qt 字体说明](https://doc.qt.io/qt-6/qt-embedded-fonts.html)。

<a id="linux-xcb"></a>

### Linux 提示无法加载 Qt 的 xcb 插件

先确认解压完整，并通过 `./AppRun` 启动。不要移动 `usr/`，也不要直接运行 `usr/bin/Aster` 来绕过启动脚本。

同一句“找到 xcb 但无法加载”可能来自不同原因，包含缺失依赖、插件不匹配或图形环境问题。在程序目录运行以下命令，可将插件诊断写入日志。

```bash
QT_DEBUG_PLUGINS=1 ./AppRun 2>aster-startup.log
```

若窗口能打开，关闭后再读取日志。根据日志中的具体缺失库处理。

- Ubuntu / Debian 日志明确缺少 `libxcb-cursor.so.0` 时，可安装 `sudo apt install libxcb-cursor0`。
- 缺少 `libQt6XcbQpa.so.6` 或其他随包 Qt 库时，应重新下载完整、已修复的发行包，并反馈打包问题。
- 提示无法连接显示服务器时，检查是否存在可用的 X11/XWayland 或 WSLg 图形会话。

仅看到 xcb 通用提示不足以确定缺哪一个库。依赖说明见 [Qt Linux 要求](https://doc.qt.io/qt-6/linux-requirements.html)。

<a id="wsl"></a>

### WSL 中能运行命令，但没有窗口或窗口较卡

`QT_QPA_PLATFORM=offscreen` 用于无窗口检查。以下命令能够确认程序版本和数值后端能加载，但不会显示窗口。

```bash
QT_QPA_PLATFORM=offscreen ./AppRun --version
QT_QPA_PLATFORM=offscreen ./AppRun --numeric-backend
```

打开界面需要使用普通的 `./AppRun`，并有可用图形环境。WSLg 支持 WSL 2，系统要求与配置见 [Microsoft WSL 图形应用说明](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps)。

如需更新 WSL，先保存并关闭所有 WSL 中的工作，再在 Windows PowerShell 中执行。`wsl --shutdown` 会关闭所有正在运行的 WSL 发行版。

```powershell
wsl --update
wsl --shutdown
```

界面延迟应分开观察窗口拖动、缩放、菜单点击与计算过程。WSL、外部 X 服务器、远程显示和显卡驱动都可能影响体验。如果仅 Aster 的某个操作稳定卡顿，请提交该操作的复现步骤。原生 Linux 的性能仍需在原生环境确认。

<a id="windows-runtime"></a>

### Windows 提示缺少 DLL 或运行库

先完整解压程序包并确认使用 x64 版本。若缺少 `VCRUNTIME140*.dll` 或 `MSVCP140*.dll`，安装微软官方的 [Visual C++ x64 运行库](https://aka.ms/vs/17/release/vc_redist.x64.exe)。

若缺少 Qt、GSL 或 OpenBLAS DLL，重新下载完整程序包。此类依赖由发行包提供，不要从不明 DLL 网站逐个下载，也不要混入其他 Qt 版本的文件。

<a id="macos-open"></a>

### macOS 无法打开应用或无法验证开发者

核对处理器架构与最低系统版本。当前 CI 包要求 macOS 15+，Apple Silicon 使用 `arm64`，Intel 使用 `x86_64`。

当前默认构建使用 ad-hoc 签名，未完成 Apple Developer ID 签名及公证。若确认文件来自本项目发布页，可按 Apple 的说明，在尝试打开后到“系统设置 → 隐私与安全性”检查该应用的“仍要打开”选项。

如果提示应用已损坏、含恶意软件，或错误原因不明确，应先核对文件来源、重新下载并反馈具体提示。不要通过关闭系统整体安全保护来解决。

参考 [Apple 关于安全打开 Mac 应用的说明](https://support.apple.com/102445)。

## 数据与设置

### 预览已经有结果，为什么结果目录里没有记录？

即时预览不写数据库。点击“运行流程”，完成后到“结果”页查看。若运行失败，查看最早失败步骤的诊断。

### 导入后提示 X 不递增、重复或采样间隔不一致

先核对列选择和单位，再根据数据情况添加“排序与去重”或“整理与重采样”。采样间隔必须依据记录分辨率选择，插值不会增加原始信息。不同方法的输入要求见任务帮助。

### 切换数据库后，旧结果不见了

切换路径只改变当前使用的数据库。重新选择原来的文件即可查看旧记录，切换本身不会复制或删除记录。需要汇集结果时，使用“合并数据库…”。

### 筛选后，为什么还有当前列表以外的已勾选数据？

批量勾选跨筛选保留。这样可先勾选全部记录，再筛选并取消需要保留的部分。删除按已勾选集合执行，确认框会显示筛选范围外的条数。开始新的批量操作时可先“清空勾选”，切换数据库会清空勾选。

### 新版的并行数据数为什么仍是 2？

已有配置会保留。新配置的默认值为可用逻辑处理器数与 8 中的较小值，至少为 1。需要更改时，在“设置 → 并行数据数”调整。该数值控制同时处理的输入数量，较大值会增加内存使用，并非越大越快。

### 更新或删除旧程序目录会丢失结果吗？

在“设置”中确认数据库的实际路径，并使用“备份当前数据库…”创建备份。若数据库自行放在旧程序目录里，清理该目录前须将它另行保留。保留原始输入文件也便于以后重算。

## 频谱与绘图

<a id="spectral-peaks"></a>

### 峰值表里的周期怎样计算？靠近零的强峰是什么？

正频率对应的周期为 `T = 1 / f`。X 使用 ka 时，频率使用 `1/ka`，周期使用 ka。例如 `f ≈ 0.0243 1/ka` 对应约 41.15 ka。

零频对应均值项。**很小但非零的频率不等于零频**。Aster 的 MTM 在计算前去均值，但不会自动去除长期趋势。靠近最低非零频率的强功率还可能与长期变化、有限记录长度或谱泄漏有关。

需要判断低频来源时，可比较原序列、线性去趋势和 LOWESS 去趋势等明确处理后的结果。接近记录总长度的“周期”不能仅凭一个谱峰认定为稳定周期。

峰值表是局部峰检测结果。背景曲线、显著性阈值和研究解释应另行判断，见 [统计解释范围（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/methods/statistical_limits.md)。

### 为什么即时预览和放大后的图细节不同？

即时预览对密集曲线和热图使用有上限的显示副本。结果页可按视口读取更细数据，读点提示会标出抽稀预览值。CSV 导出对应输出的完整数值，不受画面抽稀影响。

### 小波图有影响锥，却没有显著性轮廓

影响锥与显著性轮廓来自不同输出。检查是否生成显著性比值、显示开关是否启用，以及比值是否跨过阈值。网格全部低于或全部高于阈值都可能没有内部边界，窄小结构也可能在缩略预览中遗漏。具体约定见 [绘图说明（在线）](https://github.com/ChenCLian/Aster/blob/main/docs/ui/plotting.md)。

### 热图改成对数颜色映射后有些区域空白

颜色对数映射需要正值，零或负值会被屏蔽。该设置只影响显示，不改写原始数据；CSV 和读点数值不会自动变成对数值。

## 问题仍未解决

向 [GitHub Issues](https://github.com/ChenCLian/Aster/issues) 提供以下信息即可，无需上传整个数据库。

- Aster 版本、操作系统及处理器架构；Linux 补充分发版，WSL 补充是否使用 WSLg 或外部 X 服务器。
- 从启动到出现问题的操作步骤，以及期望与实际表现。
- 完整错误文本或启动日志，必要时附截图。
- 能复现问题的最小数据及相关参数，提交前移除不宜公开的信息。
