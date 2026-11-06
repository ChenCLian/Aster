# 合成轨道代理信号：来源与许可

本模块依据轨道代理信号正演参考实现及 AstroGeoFit 1.1 整理为 C++ 任务。AstroGeoFit：Copyright 2025 CNRS，主要作者 J. Laskar；GPL-3.0-or-later。保留原始许可：`licenses/AstroGeoFit_LICENSE.txt`、`licenses/GPL-3.0.txt`。

Aster 1.1.1（2026-11-06）中的 C++ 移植与集成修改：Copyright (C) 2026 ZiJie Chen。移植文件继续采用 GPL-3.0-or-later，保留 CNRS 的原始版权；项目整体采用 GPL 第 3 版。

深度模板来自 Westerhold & Röhl (2013)，PANGAEA.817206，CC BY 3.0。原始元数据见 `SOURCE_PANGAEA_817206.txt`；本模块使用深度坐标生成合成代理记录，不将生成的数值称为该数据集的实测值。

模型参数见 `model.json`；实现见 `src/math/synthetic/orbital_proxy.*` 与对应任务。随机数实现与参考 Python 实现不同，同一个种子不保证跨实现逐点相等。模型定义、独立数值对照和使用边界见项目验证记录及任务帮助。
