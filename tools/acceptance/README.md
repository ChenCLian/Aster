# 交互验收工具

可选的 QtTest 程序，不参与应用构建或发行程序包。连接已经完成的同一 Qt Kit、Release 配置的 Aster 静态模块；`ASTER_BUILD_ROOT` 指向包含 `lib/` 的构建目录。科学计算依赖沿用 `config/scientific_dependencies.pri`。

在单独的构建目录运行：

```sh
qmake /path/to/Aster/tools/acceptance/acceptance.pro ASTER_BUILD_ROOT=/path/to/Aster/build-release CONFIG+=release
make
./aster_interaction_acceptance
```

Windows/MSVC 使用对应 Kit 的 qmake 和 jom；不得混用编译器、Qt 版本或 Debug/Release 模块。可用 QtTest 的 `-functions` 列出用例，或在命令后指定函数名。设置 `ASTER_UI_CAPTURE_DIR` 可保留屏幕矩阵截图。Linux 无窗口服务器时可设置 `QT_QPA_PLATFORM=offscreen`；这不能代替原生拖放和显示缩放验收。

- 16 组主窗口/结果界面：中英文、深浅主题、1280×800 / 1920×1080，并检查缩小窗口后的布局。
- 53 个通用参数页面：中英文、200 / 320 像素宽度，核对参数往返和横向裁切；文件导入由主窗口矩阵覆盖。
- 三个固定随机种子，各 60 次混合绘图操作，验证页/子图身份、叠加、替换、删除、排布、样式、缩放、异步稳定和对象释放。
- 搜索按可见名称匹配、无匹配后旧选择失效；256 点双正弦的分段报错、输入修改、撤销/重做及预览与直接计算一致性。
- 上下文菜单打开期间关闭原页，确认失效对象不会访问新页；密集图例、滚动及隐藏/恢复图例。

主要数值夹具为 6×960 点序列及一条 4096 点序列，总输入 9856 点；工作流矩阵使用 3×512 点文件，短序列配对每方 256 点。热图矩阵为这些输入的派生输出。工具不执行大规模蒙特卡洛测试。

`MIXED` 日志含墙钟时间、RSS 采样和读图完成数。计时包含等待和绘制，RSS 包含 Qt/分配器缓存；它们不是独立算法基准或无内存泄漏证明。地址/未定义行为检查须将所链接的应用模块也重新构建为相同 sanitizer 配置。
