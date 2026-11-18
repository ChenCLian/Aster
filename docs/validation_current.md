# Aster 1.3.1-beta.2 验证范围

[本版记录](validation_results/1.3.1-beta.2/checks.json) · [文档记录](validation_results/1.3.1-beta.2/documentation_checks.json)

发布工具已在本地运行 Python 测试，结果为 `Ran 228 tests; OK (skipped=2)`。两项跳过涉及 Homebrew Ruby 和 Qt 6 qmake。模拟发布验证了两个草稿的附件清单、历史依赖复用、Gitee 四包上传、SHA-256 核验和中断后的重试。版本同步、手册链接与 15 份内置资源的 SHA-256 检查通过。

维护者提供的 Actions 截图显示，第 39 次构建的四个平台任务均成功，草稿汇总时因依赖附件的嵌套目录报错。本地用相同的目录结构复现了问题，再用修正后的补建工作流和模拟构件验证了汇总步骤。使用第 39 次真实构件补建草稿和后续线上镜像仍待执行。

上一个版本的记录见[历史验证](validation_results/1.3.1-beta.1/checks.json)。
