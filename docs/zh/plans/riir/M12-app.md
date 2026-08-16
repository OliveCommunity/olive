# M12：app 重写（gpui）

> 前置：单库化完成（single-lib.md），liboakengine 对外只暴露冻结的
> `oakengine_*` 纯 C ABI；gpui fork 已作为子模块落地（gpui/）。本
> 计划把 app（Qt/Widgets 时代的主程序）完整重写为 gpui 桌面应用。
>
> 与 M1-M11 的关系：那些手册交付的是**引擎**；本手册交付的是**壳**。
> 壳只认识 C ABI —— app 不链接 oakengine rlib，运行时动态链接
> liboakengine.so/.dylib（见根 build.rs），与 CLI/worker 并列成为
> engine 的三个前端。

## 0. 形态决策（2026-08，已定）

- **UI 框架：gpui fork**（gpui/ 子模块，含 gpui_widgets 扩展件：
  dock、menu、viewer、timeline、effect_stack、node_graph、scopes、
  audio_meter、dialog、theme、i18n 钩子）。不用 egui/iced：gpui 的
  数据驱动组件模型已在 fork 里按本编辑器需求扩展完毕。
- **引擎接缝：`src/oakui/`**。`EngineGateway`/`AppEngine` 两个 trait
  把 UI 与引擎解耦；`RealEngine` 只经 `src/oakui/ffi.rs` 声明的
  C ABI 调 liboakengine，`MockEngine` 提供可测试的假实现。
  **任何面板不许直接碰 FFI**——一律走 trait 方法。
- **布局照 design/ 三张设计图**：顶部菜单；dock 区（项目 | 素材
  查看器 | 序列查看器+节点编辑器 | 检查器+历史记录），下方全宽
  时间线（31px 工具栏置顶）；底栏状态栏。全部停靠窗口。
- **扁平化现代 UI**，图标用 C++ 版素材（assets/icons/{dark,light}，
  16px 网格），双主题，中英双语（src/i18n.rs + gpui i18n 钩子）。
- **平台**：macOS（Apple Silicon）优先；Linux 链接已通（build.rs
  rpath+--export-dynamic）；Windows 卡在 DLL 不允许未定义符号
  （oakcore_* 宿主导入），需 stub import lib 或 delay-load（见
  §5 风险表）。
- **插件 GUI**（OFX Interact/Dialog、第三方语言插件自绘窗口）走
  方案 2（已立项时的决议）：引擎侧离屏渲染，app 侧贴图；窗口类
  需求由插件自建窗口、app 不嵌套。对应 M11 第 4 期（UI 类 suite）。

## 1. 现状盘点（2026-08-12，全量测试绿）

已完成：

- 应用壳：菜单栏（含二级菜单）、dock 布局、状态栏、tick 循环
  （播放/播放头同步/音频表）、模态对话框（偏好设置/导出/进度/
  文件对话框）、中英双语切换并持久化（oakengine_config_*）。
- 时间线：轨道增删/高度、clip 选择/修剪/分割/删除/ripple、
  同轨移动、吸附、undo/redo 全通 facade；工具栏图标化。
- 查看器：序列/素材两个 monitor，facade CPU 渲染器出真帧
  （F32 RGBA，480px 长边代理），走带图标、帧缓存按播放头键控。
- 示波器：直方图/波形/矢量三个，数据来自已渲染帧（BT.709）。
- 效果栈：选中 clip 的效果链枚举/插入/删除/排序/开关，全部
  undoable；检查器内嵌加效果菜单。
- 工程：新建/从库打开（写穿持久化，无保存按钮）；导出工程文件…
  （ove/otio/fcpxml，扩展名分发）、导出对话框 + 导出任务（进度事件经
  task subscribe 回调）。
- CLI / worker 两个前端同在 workspace（oak-cli、oak-worker）。
- 测试：根 crate 60+ 测试全绿（含真渲染 e2e、示波器分析数学、
  偏好设置崩溃回归、i18n 未翻译检测）。

已知缺口（按阻塞面排序）：

| # | 缺口 | 阻塞 | 位置 |
|---|------|------|------|
| G1 | oakrender eval 的 footage 解码 hook 未落地 | 两个 monitor 只有几何/格式正确的透明黑帧；导出同理 | crates/oakrender/src/eval.rs |
| G2 | 音频播放路径不存在（render_audio 是 stub、无输出回调、无混音） | 音量表有读数管道但无数据；播放无声 | oakrender ticket samples 路径、oakaudio 输出回调 |
| G3 | 节点编辑器数据源为空（facade 图枚举/连线表面未绑） | 节点编辑器画布空白 | src/oakui/real.rs NodeGraphDataSource |
| G4 | 项目浏览器是 demo 假数据 | 素材箱不真实 | RealEngine.bin_* |
| G5 | 全分辨率异步渲染（facade worker 进程面）未绑 | 代理分辨率外的画质 | oakengine::worker NDJSON |
| G6 | 时间线音频波形未显示 | 波形提取已是真实现（oakaudio/ffmpeg-next） | src/panels/timeline.rs |
| G7 | move_clip 跨轨 | facade 签名冻结无目标轨参数（需新增导出） | crates/oakengine |
| G8 | Windows 构建 | DLL 未定义符号问题 | 根 build.rs 注释 |

## 2. 分期

每期结束判据都可命令验证；顺序按依赖排，P0/P1 可并行。

### P0 解码落地（引擎侧，app 直接受益）

目标：monitor 出真像素，导出生真视频。

1. oakrender eval 的 footage hook 接 oakcodec 解码（FFmpegDecoder
   已是真实现），帧缓存键 = (footage, time, divider)。
2. 序列渲染时混叠多轨（track 合成顺序对齐 C++）。
3. 判据：`cargo test -p oakrender` 新增"导入测试素材 → 渲染帧
   非全零且像素值符合已知内容"测试绿；app `real_render_frame_e2e`
   的"非黑断言"（代码里已留 TODO 注释）转正。
4. 测试素材：程序生成（ffmpeg-next 编码几帧已知图案），不依赖
   网络与外部文件。

### P1 音频播放（引擎 + app）

目标：播放有声、音量表活、走带流畅。

1. oakrender 的 render_audio samples 路径落地（评估 clip 音频 →
   交错 f32）。
2. app 播放循环：按播放头连续取音频 → facade push_to_output；
   oakaudio 输出回调接 PortAudio（portaudio crate，直调）。
3. 音量表即插即用（M 本轮已接 `oakengine_audio_output_levels`）。
4. 判据：oakaudio 输出回调单测（合成正弦 push→回调收到非零采样）；
   app 集成测试断言播放中 output_levels 非零。

### P2 节点编辑器绑定

1. facade 增补图枚举表面（节点列表、位置、连线；只增不改），
   RealEngine 实现 NodeGraphDataSource；连线/断开/删除走 undoable
   命令（facade 已有 node_connect 等）。
2. 判据：it_node 新增图枚举测试；app 节点编辑器对真实工程显示
   clip→输出 连线并可拖动，gpui 测试覆盖。

### P3 项目浏览器真实化

1. bin_roots/bin_children 改走 facade 工程结构（footage/folder
   枚举 facade 已有 `_footage_count/_footage_at`；folder 枚举缺的
   补齐）。
2. 拖拽导入素材（facade footage probe/import 已有）。
3. 判据：app 测试"导入 demo 素材 → 项目浏览器出现条目 → 双击进
   素材查看器"。

### P4 时间线完善

1. 音频波形：oakaudio_waveform_extract（真实现）结果画进 clip，
   异步提取 + 磁盘缓存（key=文件+mtime+samples_per_point）。
2. 跨轨移动：facade 新增带目标轨参数的导出（API 只增不改）。
3. 标记（marker）UI、工作区入出点、吸附开关行为对齐 C++。
4. 判据：oaktimeline/app 双层测试；波形缓存命中测试。

### P5 性能与交付

1. 全分辨率异步渲染：facade worker（NDJSON 控制面）绑定，代理
   分辨率实时 + 全分辨率后台填充。
2. 偏好设置完整化（缓存目录、代理策略、自动保存间隔、默认
   过渡等，全部落 oakengine_config_*）。
3. 键盘快捷键表（对齐 C++ 版快捷键，i18n 无关）。
4. Windows 支持：解决 DLL 未定义符号（stub import lib 或把
   oakcore_* 收进 dylib 本体），CI windows job 转绿。
5. CD 实跑验证六个包（deb/rpm/pkg.tar.zst/AppImage/NSIS/dmg）
   能装能起。

## 3. 测试规范（在 03-testing.md 基础上追加 app 层）

- 面板逻辑一律经 `MockEngine` 测（gpui::test 开真窗口）；
  `RealEngine` 路径经 dylib 集成测试（如 real_render_frame_e2e）。
- 每个新 facade 导出：正常 + NULL/非法输入测试（it_* 族）。
- UI 回归测试命名即场景（preferences_dialog_no_reentry_crash 式）。
- i18n 完整性由未翻译检测测试强制（新增 key 必须双语）。
- 覆盖率目标沿用全局约定：对外 C API 100%，其余 ≥80%。

## 4. 纪律（沿用 01 §0 与本仓约定）

- facade C ABI 冻结、只增不改；要改升大版本。
- app 代码不许出现 `unsafe`（ffi.rs 声明除外）；所有引擎交互
  经 oakui trait。
- crates.io 有就绝不造轮子；FFmpeg/OCIO/OIIO 直调不包 wrapper。
- 新文件带 Oak GPL 头；tab 缩进；英文注释；面板文案走 tr()。

## 5. 风险表

| 风险 | 影响 | 缓解 |
|------|------|------|
| Windows DLL 未定义符号 | Windows 无法出包 | oakcore_* 收编进 dylib 或生成 stub import lib；CI 先保 Linux/macOS |
| gpui fork 与上游分叉扩大 | 维护成本 | 只在 gpui_widgets 层扩展；上游同步按季度评审 |
| 音频回调实时性（PortAudio 回调里禁锁/分配） | 爆音 | 回调只读写无锁环形缓冲；oakaudio PreviewAudioDevice 已是此形态 |
| OFX 插件 GUI（方案 2）交互延迟 | 插件调参手感 | P5 后单独立项评审（M11 第 4 期） |
| 代理分辨率下文字/细节评审失真 | 体验 | P5 异步全分辨率填充解决 |
