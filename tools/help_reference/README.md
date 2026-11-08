# 参数参考生成与校验

参数参考共用任务定义和语言资源，分别生成中文与英文页面：

- `docs/reference/task_parameters.md`：中文；缺少中文的条目回退到英文。
- `docs/reference/task_parameters_en.md`：英文。

默认值、数值范围、可选值、显示条件和输出标识来自 `makeScientificRegistry()`，不维护第二份参数清单。名称与说明来自 `resources/i18n/zh.json`、`en.json`，最终回退到任务描述中的标签与帮助。程序值、单位和引用标识不翻译。

## 更新步骤

构建步骤必须在所使用的构建目录中执行；下方 Python 命令在仓库根目录执行。

在已有 Qt、科学计算依赖和构建配置的开发环境中，从仓库根目录创建或使用 `build-release`，配置时增加可选项：

```sh
cd build-release
qmake ../Aster.pro CONFIG+=release CONFIG-=debug CONFIG+=help_reference
```

使用该 Qt Kit 对应的 qmake（部分 Linux 环境名为 `qmake6`）。按平台正常构建（Windows/MSVC 使用 `jom`，Linux/macOS 使用 `make`），将生成 `bin/aster_help_catalog`（Windows 带 `.exe`）。这个工具只读取任务描述，不运行算法，不读写数据库，也不需要图形窗口。

回到仓库根目录，使用 Python 3.10 或以上版本：

```sh
python tools/help_reference/generate.py --build build-release
python tools/help_reference/generate.py --build build-release --check
python -m unittest discover -s tools/help_reference -p "test_*.py"
```

生成后重新构建 Aster，让 `resources/help.qrc` 收录最新文档，并提交两份生成文件及相应的任务定义或语言资源改动。普通用户构建不需要 Python，也不必启用 `help_reference`；源码已包含生成好的文档。

工具须在能找到 Qt 和科学计算动态库的构建环境中运行。任务定义或版本变化后先重新编译工具，避免读取旧二进制的描述。`--check` 只比较内容，忽略平台换行差异，不修改文件；内容过期、版本不匹配、工具缺失或重复参数标识都会报错。GitHub Actions 在各平台构建后自动执行检查，过期文档会阻止打包。

## 语言与链接约定

参数字段按以下顺序查找非空文字：任务专用的当前语言、任务专用英文、通用的当前语言、通用英文、任务描述。任务专用说明优先，避免相同参数键在不同算法中含义不同。缺少中文并不阻止生成；不要求逐项补全翻译，也不在中文后重复附上已有英文说明。

枚举选项使用界面的显示名称，并在括号中保留保存设置所用的参数值；默认值采用相同显示方式。选项名称按当前语言、英文、原值的顺序回退。仅改变显示名称不改写任务定义或已保存参数。

两份文档使用相同的稳定锚点：方法 `m-<method>`，参数 `m-<method>-p-<parameter>`。中文文件沿用原路径，已有链接继续有效。程序中的帮助入口按界面语言选择路径；本地缺页或缺章节时，后备 GitHub 地址使用同一语言路径和锚点。操作指南及科学方法文章仍单独维护，本工具只生成任务与参数参考。

不要直接编辑生成的 Markdown。新增参数时修改任务描述；修改名称或说明时更新语言资源，然后运行生成器。
