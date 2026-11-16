# Aster 1.3.0-beta.10 更新说明

本版完成帮助文档改造计划的 B 阶段，并汇集已完成的 A/C 内容整理和 beta.9 自动参数／预览提示修改。交付完整源码，说明文件位于压缩包外层，工程位于 source/。

## 本批改动

- 可折叠多级目录按五类主题组织，章节直接来自正文；任务参数归软件功能，共享概念集中维护。
- 左侧全文搜索可检索标题、正文和参数标识，结果跳到对应章节；有译文时按当前语言去重。
- 右侧页内查找提供匹配计数、前后匹配、循环与快捷键；Unicode 选区按实际匹配边界处理。
- 返回／前进保留阅读位置，参数帮助入口选中对应章节；主页按钮准确表达原有回到手册的功能。
- 用户手册、文档索引、开发维护说明和发布记录同步更新。

## 代码位置

- src/ui/help/help_catalog.cpp/.h：统一章节解析、分类与语言筛选。
- src/ui/help/help_dialog.cpp/.h：布局、全文搜索、页内查找和导航历史。
- resources/help_navigation.json：分类元数据，不复制正文。
- resources/help.qrc、resources/i18n、desktop_ui/desktop_ui.pro：资源、译文和编译入口。
- ui_tests/ui_tests.cpp：目录、搜索、导航、字符选区与布局回归。

本批未改算法、任务默认值或数据库格式；旧流程与参数沿用 beta.9 的兼容逻辑，不自动重算已有结果。

## 构建与验收

Qt Creator 打开 Aster.pro，配置本机依赖后重新运行 qmake 并构建根工程。内置帮助属于编译资源，替换磁盘 Markdown 不会更新现有可执行程序。包不包含 config/local.pri，请保留自己的机器配置。

本批实际检查与未覆盖平台见 [验证范围](../../../validation_current.md)。Linux Qt offscreen 结果不代表 Windows/macOS 原生发行验收。A/B/C 已按当前计划完成；原生打包、系统缩放及最终发行资产验收仍单独进行。
