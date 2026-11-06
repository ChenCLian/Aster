# Aster 数值接口与跨平台构建

适用版本：1.1.1。计算库固定为 **OpenBLAS/LAPACK + PocketFFT**。

## Windows / Qt Creator

使用 Qt 6 / MSVC x64，在 vcpkg 目录安装以下依赖：

```powershell
.\vcpkg.exe install gsl:x64-windows openblas:x64-windows lapack-reference:x64-windows
```

本工程的 vcpkg 配置分别链接 LAPACK 与 OpenBLAS；不能漏装 lapack-reference。复制 `config/local.pri.example` 为 `config/local.pri`，填写 VCPKG_ROOT 或 VCPKG_PREFIX。依赖路径和链接项以当前配置模板为准。

打开根 **Aster.pro**，在新构建目录重新运行 qmake，构建全部，运行 **desktop / Aster**，输出位于 `bin/Aster.exe`。不单独构建 desktop.pro，不运行测试程序作为桌面入口。

Debug 使用依赖的 `debug/bin`，Release 使用 `bin`；GSL 调试库名称带 d 后缀。运行时仍需 Qt/QSQLITE、GSL、OpenBLAS/LAPACK 及其运行依赖，具体 DLL 由实际依赖包决定。PocketFFT 随源码编译，无独立 DLL。Qt Creator 的运行环境需包含当前依赖目录的 PATH 配置。

## Linux

从包含 `Aster.pro` 的源码根目录执行以下命令。

准备 Qt 6、SQLite 驱动、GSL、LP64 OpenBLAS（含 LAPACK）。默认通过系统路径/pkg-config 查找；如 LAPACK 单独提供，在 `local.pri` 设置 `ASTER_LAPACK_LIBS = -llapack`，链接顺序在 BLAS 前。

```sh
mkdir build-release
cd build-release
qmake ../Aster.pro CONFIG+=release CONFIG-=debug
make -j4
./bin/Aster --numeric-backend
./bin/Aster
```

`CONFIG+=core_only` 仅核心，`CONFIG+=no_gui` 不包含桌面。需要界面时不加这两个参数。

## macOS

Intel Mac 和 Apple Silicon 使用各自原生架构的 Qt 6、GSL、LP64 OpenBLAS/LAPACK。数值实现相同，不引入平台专用算法分支。Homebrew 用户可按 `brew --prefix openblas`、`brew --prefix gsl` 的实际输出设置：

```qmake
OPENBLAS_ROOT = /your/openblas/prefix
GSL_PREFIX = /your/gsl/prefix
```

不要混用 x86_64 和 ARM64 依赖。macOS 构建生成 `.app`，输出 `bin/Aster.app`。各 Mac 架构须分别完成原生构建、依赖部署和桌面验收；签名及公证状态应在发布说明中注明。

## 模块和数值契约

| 文件 | 职责 |
| --- | --- |
| `src/math/backend/numeric_backend.h` | 算法使用的 C++17 接口、结果与 BackendError |
| `numeric_backend.cpp` | 形状、有限数、整数/字节溢出及输出检查 |
| `backend_detail.h` | 内部适配契约 |
| `lapack_backend.cpp` | LP64 DSTEVR 三对角特征求解、工作区、OpenBLAS 线程控制 |
| `pocketfft_backend.cpp` | 奇偶长度实数 FFT/逆变换及批次布局 |
| `config/numeric_backend.pri` | 各平台依赖路径和链接名称 |
| `science/numeric_backend_sources.pri` | 固定源文件清单 |
| `analysis_tests/backend_audit.*` | 数值定义、形状/异常、输入保护与并发测试 |

- MTM 只求实对称三对角矩阵最大的 K 个特征对，不展开 N×N 稠密矩阵。特征值升序，向量列优先。
- FFT 正向不缩放，逆向 1/N；半谱有 N/2+1 个复数。批次连续排列，DC 与偶数长度 Nyquist 必须为实数。
- 参数按值接收，保护调用者左值；一次性临时缓冲用 std::move。禁止把只读 DataRef 底层内存交给可写原生调用。
- 非法尺寸、NaN/Inf、溢出由统一入口拒绝；原生错误转换成 BackendError，继续沿用已有任务错误通道。算法内存预算和取消检查点保持。
- LAPACK 依赖必须满足 LP64 Netlib/gfortran 调用约定。不要混入 ILP64 或其他整数/字符参数 ABI。
- 一次原生计算内部1线程，外层仍并行处理不同数据。OpenBLAS 全局线程数初始化一次；SINGLE_THREADED 发行包的原生 LAPACK 调用保守串行化，其他处理不被整体串行化。

GSL 提供统计和插值功能，wavelib 提供小波计算；统一数值接口覆盖线性代数和通用 FFT 调用。MTM 报告保留实际库版本与线程策略元信息，通过任务 report JSON 存储。

新增科学算法继续继承 Task、实现 descriptor/execute、宏注册。需要线性代数或 FFT 时调用 numeric_backend.h，不直接引入原生库类型。无需重写工作流、数据库或 UI。

`Aster --numeric-backend` 可查看实际 OpenBLAS 配置与 PocketFFT 信息，仅查询，不切换。验证范围见 [验证记录](validation.md)。
