# Aster 1.3.1-beta.2 验证范围

[本版记录](validation_results/1.3.1-beta.2/checks.json) · [文档记录](validation_results/1.3.1-beta.2/documentation_checks.json)

发布工具已在本地运行 Python 测试，结果为 `Ran 225 tests; OK (skipped=2)`。两项跳过涉及 Homebrew Ruby 和 Qt 6 qmake。模拟发布验证了两个草稿的附件清单、历史依赖复用、Gitee 四包上传、SHA-256 核验和中断后的重试。版本同步、手册链接与 15 份内置资源的 SHA-256 检查通过。

四个平台的真实构建、Qt 客户端回归、包验收与线上镜像仍待新版 Actions 运行。

上一个版本的记录见[历史验证](validation_results/1.3.1-beta.1/checks.json)。
