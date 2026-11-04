# Acycle 直接对照工具

该工具用于开发验证，不参与桌面运行。输入为待测 Aster 构建导出的数值 JSON，参照为独立安装的 Acycle 与 MATLAB。

## 1. 导出 C++ 结果

先构建根工程中的 analysis_tests。以下示例从源码根目录运行，构建目录采用 `build-release`，输出存入 `build-validation`；使用其他构建目录时相应调整。

PowerShell：

```powershell
New-Item -ItemType Directory -Force build-validation | Out-Null
$env:ASTER_ACYCLE_EXPORT = Join-Path $PWD "build-validation/acycle_cpp_results.json"
& "./build-release/bin/aster_analysis_tests.exe"
Remove-Item Env:ASTER_ACYCLE_EXPORT
```

Linux/macOS：

```sh
mkdir -p build-validation
ASTER_ACYCLE_EXPORT="$PWD/build-validation/acycle_cpp_results.json" ./build-release/bin/aster_analysis_tests
```

确认测试通过且 JSON 含 19 个实际案例。带 `expected` 数组的固定参照文件不能代替待测程序输出。

## 2. 执行 MATLAB

需要 Signal Processing Toolbox（pmtm/dpss）。缺少 chi2inv 时，脚本临时使用基础 MATLAB 的 `2*gammaincinv(p,nu/2)`，并记录适配情况；不永久修改 Acycle 或 MATLAB 路径。

将 MATLAB 当前目录切换到源码根目录，运行以下命令并选择独立的 Acycle 源码目录。

```matlab
addpath(fullfile(pwd, 'tools', 'acycle_validation'));
acycleRoot = uigetdir(pwd, 'Select Acycle source directory');
if isequal(acycleRoot, 0), error('No Acycle directory selected.'); end
compare_acycle(acycleRoot, ...
    fullfile(pwd, 'build-validation', 'acycle_cpp_results.json'), ...
    fullfile(pwd, 'build-validation', 'aster_acycle_comparison.json'));
```

报告保留原容差比较、MATLAB 数组、独立错误，以及小波原近似分位数/精确分位数的分别核对。报告可能包含本机来源路径，公开前应脱敏；保留文件内容哈希以便追溯。

19 个历史案例的验收摘要和来源哈希见 [Acycle 对照说明](../../docs/methods/acycle_compatibility.md)。这些结果不覆盖全部算法、原生 FDR 查表或统计误报率。AR1 全局检验的验证工具见 [ar1_global_validation](../ar1_global_validation/README.md)。
