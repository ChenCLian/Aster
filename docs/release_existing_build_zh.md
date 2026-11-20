# 从已有构建补建发布草稿

适用于四平台已完成构建、测试和打包，且启动时没有勾选 `prepare_draft`，或只有最后的草稿任务失败的情况。不需要重新编译，也不需要把大型附件下载到个人电脑后再上传。

在 `ChenCLian/AstroBatch` 中，选择 **Build desktop packages** 生成的 AstroBatch 构建。只有 Linux 成功、仅执行检查或构件已过期时，须先完成四平台打包。

1. 在 GitHub Actions 中打开 **Prepare draft from existing build → Run workflow**。
2. 在 `build_number` 输入所选构建标题里 `#` 后面的编号。地址栏里的 Run ID 用于标识 GitHub 运行，与此处的编号不同。
3. 完成后到 Releases 核对程序草稿 `v<版本>` 和依赖材料草稿 `materials-v<版本>`。先发布材料，再发布程序。

同一运行的四个 `program-*` 和四个 `dependency-materials-*` 构件都须存在、非空且未过期。运行总状态为失败时，选择器逐项核对资源检查、四个平台打包和草稿任务的结果；仅草稿任务失败时可继续。下载使用核实的八个构件 ID。每个平台都要有对应版本的依赖锁文件和记录 ZIP，支持旧上传产生的嵌套目录和新上传的平铺目录。

程序包直接复用所选运行的附件；源码和公告取自该构建对应的提交，发布标签也指向该提交。材料汇总、草稿创建和上传使用启动补建工作流时的发布工具，并显式传入所选构建的版本号和提交。这样可以修复发布工具而不重新编译程序，也不会将工具提交误当作程序源码版本。下载程序包后仍检查包内平台/版本标记并生成 SHA-256 和更新元数据。

只准备或更新草稿，不自动发布，不自动同步 Gitee；已公开的同名 Release 不会被覆盖。依赖材料仍只放在 GitHub，Gitee 的同步规则不变。既有草稿的手动标题和公告保留。

创建草稿后直接使用 GitHub 返回的 Release ID 核验、更新和上传附件，不再等待标签列表发现新草稿。已有同标签草稿会被复用；旧工具留下的无标签草稿，仅在默认标题和目标提交均匹配时才自动接续。存在多个候选或目标不匹配时停止并提示检查，不擅自删除草稿。已上传附件的 SHA-256、大小和状态与本地一致时跳过重复上传。

GitHub 可能改写带特殊字符的附件名。发布工具在上传前统一规范名称，并预先检查命名冲突；源归档和程序包的内容不变。`SHA256SUMS.txt` 使用实际发布名，`AstroBatch-asset-filenames.json` 保存原名、发布名和 SHA-256，`DEPENDENCY_DOWNLOADS.md` 提供下载入口和原名恢复说明。Debian 源码重建前，按对照表恢复原始文件名，以匹配 `.dsc` 引用。所选旧构建中已打包的文档不会被修改；其中含特殊字符的旧依赖链接可改用 Release 上的下载索引。

上传后核验失败会列出具体字段及期望值、实际值。文件名问题不会通过取消大小或哈希核验来绕过。对于旧工具留下的改名附件，只有找到唯一且 SHA-256、大小、状态均匹配的文件时才修正远端名字并复用内容，不重复上传该文件。

如果补建中途失败，先查看 Releases，暂时不要发布不完整草稿。更新发布工具后，应通过 **Run workflow** 发起新的补建运行，仍填写原构建编号；旧运行的 **Re-run jobs** 不会采用新提交中的修复。

### 创建历史版本草稿时遇到 HTTP 403

如果日志在 `POST /repos/ChenCLian/AstroBatch/releases` 报 `Resource not accessible by integration`，先确认失败步骤。若构件已汇总成功，可以继续复用这次构建。仓库的补建任务已经申请 `contents: write`。当目标提交中的工作流文件与默认分支有相关差异时，GitHub 还可能要求写入工作流的权限。内置 `GITHUB_TOKEN` 无法获得这项权限。[GitHub 创建 Release 的权限说明](https://docs.github.com/en/rest/releases/releases#create-a-release)

1. 使用对 AstroBatch 有发布权限的账户，在 GitHub 个人设置 → **Developer settings → Personal access tokens → Fine-grained tokens** 创建有期限的令牌。资源所有者选 AstroBatch 的仓库所有者，在 **Only select repositories** 只选 `ChenCLian/AstroBatch`。将 **Contents** 和 **Workflows** 设为 **Read and write**。
2. 在 AstroBatch 仓库的 **Settings → Secrets and variables → Actions → New repository secret** 中，名称填 `ASTROBATCH_RELEASE_TOKEN`，值填令牌。
3. 从默认分支启动 **Prepare draft from existing build**，填写同一个构建编号。选择构建和打包历史源码的任务只读；独立的草稿任务只在创建或更新草稿及其附件的步骤使用上述令牌。未配置时沿用内置令牌。
4. 成功后到 **Releases** 核对两个草稿。若仍返回 403，检查令牌的有效期、仓库授权、账户对目标标签的权限，以及覆盖该标签的仓库规则。

该流程仍需 GitHub 下载并上传附件，耗时取决于材料大小，但不重新执行 qmake、C++ 编译或依赖源码收集。附件已过期或被删除时，应重新构建，不能用不完整附件发布。
