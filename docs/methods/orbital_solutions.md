# 内置轨道解与外部轨道表

本文说明输入、输出、数学约定与适用范围。验证批次、环境和范围见 [验证记录](../validation.md)。

## 已提供的三组数据

| 数据组 | 任务入口 | 输出 | 实际可请求范围 |
| --- | --- | --- | --- |
| La2010a–d | 数据源 → 天文曲线 → 轨道偏心率 | 偏心率，无量纲 | 0–249999 ka |
| ZB18a / ZB20a–d | 同一“轨道偏心率”任务中的轨道解选项 | 偏心率，无量纲 | 0–300000 ka |
| PT-ZB18a | 数据源 → 天文曲线 → PT-ZB18a 倾角与岁差 | 地轴倾角 °、气候岁差 e·sin(ϖ*)、ECLIPJ2000 岁差角 rad | 0–100000 ka |

年龄统一为 J2000 之前的 ka，向过去为正。不是以 1950 年为零点的 BP。La2010 的文件末行为 −249999 ka，不能请求不存在的第 250000 ka 行。

默认参数由任务描述符唯一提供，UI 和执行器共用：年龄 0–2000 ka、输出间隔 1 ka；偏心率默认 La2010a；PT 默认内置 Ed=1、Td=1。新任务名、参数、帮助、警告和错误摘要均在中英文 JSON 中。
## 数据与科学定义

四张 La2010 表来自 IMCCE 2011-03-08 修订版，原始间隔 1 ka；五张 ZB 表来自 NOAA study 36415，原始间隔 1.6 ka。PT 默认表来自 NOAA study 35174，原生时间不等间距（约 0.022–0.582 ka）。

标量曲线按用户指定的等间距年龄网格线性插值。PT 岁差角用最短圆弧插值，并保持包裹角表示；气候岁差直接读取第四列并插值，不能用第三列取正弦代替。倾角由弧度转为度。加密插值不会提高源数据分辨率，过粗采样可能混叠；不暗中插入滤波或其他处理。非整除的末端不追加短间隔点，不允许外推。

ZB 原文件第三列是相对 HCI 坐标系的**轨道倾角**，不是地轴倾角，本版检查该列但不把它当成 obliquity 输出。PT 第三列是相对 ECLIPJ2000 的岁差角，不是相对移动春分点的近日点经度。第四列 e·sin(ϖ*) 不能唯一反演 ϖ*，因此本版**不会拿这份 PT 表直接生成日照，也不会混入 La2004 的近日点经度**。既有 La2004 相关任务仍可正常使用。

NOAA PT README 的变量清单把 climatic_precession 的单位写为 radian，但同一 README 正文定义为无量纲 e·sin(ϖ*)；程序采用公式对应的无量纲单位。NOAA 通用元数据中的 BP 标签与轨道解的 J2000 计算历元不能混为一谈；程序依据原作者的计算历元解释时间。

La2010 在约 50 Ma 以前、ZB 在约 66 Ma 以前的相位需谨慎。这些参考范围不是硬性的“精确到这里”保证，也不是数据表覆盖范围。PT 还受动力学椭率和潮汐耗散假设影响；不能把 100 Ma 覆盖等同于全时段相位可靠。超出参考范围仍允许运行，并保存警告；超出实际数据范围则明确失败。
## PT 其他参数表

为避免把约 143 张、合计约 3 GB 的原始 PT 参数表全部打包，只内置默认 Ed=1、Td=1。其余版本可在同一个任务的“PT 数据表（可选）”处点击省略号选择。清空路径恢复内置默认表。

官方目录：<https://www.ncei.noaa.gov/pub/data/paleo/climate_forcing/orbital_variations/zeebe2022/>

保留文件名 `PT.DeX.XXXXTdY.YYYY.dat`。支持官方网格 Ed=0.995–1.005、步长 0.001；Td=0–1.2、步长 0.1。程序核验全部数值、列数、时间方向、单调性、物理范围和完整 0–100000 ka 覆盖；错误文件不回退到默认表，节点明确失败。

也可用随包 Python 标准库工具下载单个版本（Python 仅用于此可选工具，运行 Aster 不需要 Python）：

```sh
python tools/orbital_data/manage_data.py fetch-pt ./orbital-data --ed 1 --td 0.4
```

结果记录实际文件名、Ed、Td、SHA-256、来源 URL 及本地路径。本地文件的模式校验不能认证其确实来自 NOAA，请保留官方原始文件。更改本地文件内容后请重新计算；已有数据库结果不会因外部文件变化而改写。此处没有运行时下载器、自动联网或新的数据库格式。
## 数据体积与存储

10 张内置表保留原始 ASCII 字节，使用 Qt qCompress/zlib 无损压缩，合计约 26 MB。读取时验证原始字节的 SHA-256、大小、行数和末端年龄。小目录元数据可供 UI 使用，数值表在后台任务首次需要时懒加载；同进程共享不可变数据，最多缓存这 10 张固定表。外部 PT 表每次执行重新读取，不无限累积缓存。

```sh
python tools/orbital_data/manage_data.py verify
python tools/orbital_data/manage_data.py unpack ./original-orbital-tables
```

`unpack` 可恢复原始官方文件，禁止覆盖已有用户文件。`.qz` 不改变数值、不降采样。数据目录：`resources/astronomy/solutions/`；来源和原始 SHA-256：`catalogue.json`；上游说明 `La2010_README.TXT`、`PT_README.txt` 一并保留。
## 模块与扩展

- `src/io/astronomy/orbital_table.h/.cpp`：目录、格式解析、能力边界、插值、缓存、来源；只提供标量通道，不伪造完整轨道三要素。
- `src/processing/orbital_table_tasks.h/.cpp`：统一 Task 接口、年龄网格、单位、输出、结构化错误和报告。
- `scientific_registry.h/.cpp`：宏注册两个任务；画布菜单和侧栏自动共用注册表。
- `ParameterSpec::fileFilter` 与参数面板：通用可选文件选择器，参数仍以字符串保存，既有参数行为不变。
- `analysis_tests/orbital_tables_audit.*` 与 fixtures：官方节点值、独立插值参考、异常文件、工作流、数据库测试。

同格式的新偏心率表通常只需增加目录元数据、资源与验证样例；新格式需要新增明确的解析分支及其测试。不能仅凭文件列数猜测物理意义。若以后要接入完整的 ZB 日照，需要含相对移动春分点近日点经度且与其余量一致的完整数据产品；当前未提供该完整数据产品。
## 一手来源

- IMCCE La2010 数据与说明：<https://ssp.imcce.fr/insola/earth/online/earth/La2010/index.html>；Laskar et al. (2011), <https://doi.org/10.1051/0004-6361/201116836>。
- ZB18a/ZB20a–d：<https://www.ncei.noaa.gov/pub/data/paleo/climate_forcing/orbital_variations/zeebe2022b/>；Zeebe & Lourens (2022), <https://doi.org/10.1016/j.epsl.2022.117595>。
- PT-ZB18a：<https://doi.org/10.25921/wmfq-hx24>；Zeebe & Lourens (2022), <https://doi.org/10.1029/2021PA004349>。
- 历元与参考系的补充核对：Kocken et al. (2026), <https://doi.org/10.1029/2025PA005287>。该文新的完整日照数据产品不在当前内置轨道表的范围内。
