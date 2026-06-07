# 代理媒体 v0.4 实施计划

## 背景

v0.4 已合并“调色、音频与性能”范围，其中代理媒体工作流负责解决 4K/8K 素材在时间线预览、剪辑和调色时的可用性问题。当前代码里已有音频 conform：`ConformManager` 会把音频流转为 PCM cache，但它不适合作为视频代理的直接扩展，因为视频代理需要保留容器、视频编码参数、文件生命周期和解码路由。

## 当前状态

实施进度：

- 阶段 1 已完成：已写计划，新增代理状态、稳定文件名函数、`Footage` 代理字段和 XML roundtrip 测试。
- 阶段 2 已完成：已新增 `ProxyTask` 和 `ProxyManager`，使用 `.working` 临时文件、成功 rename、失败清理，并覆盖状态测试。
- 阶段 3 已完成：`FootageJob` 携带代理解码信息，预览路径使用 ready 代理，导出/online 路径默认原片，代理缺失自动回退。
- 阶段 4 已完成：时间线右键已有 `Generate Proxy`、`Use Proxy`、`Reveal Proxy`、`Delete Proxy`。
- 阶段 5 自动验证已完成；仍需实际 4K/8K 素材做手工播放、重开项目和导出确认。

- `app/codec/conformmanager.{h,cpp}` 只处理音频 PCM conform，输出按声道拆分的 `.pcm` 文件。
- `app/task/conform/conform.{h,cpp}` 只调用 `Decoder::ConformAudio()`。
- `Decoder::CodecStream` 当前只包含原始 `filename + stream index + block`，解码时会检查该文件存在。
- `RenderProcessor::ProcessVideoFootage()` 和 `ProcessAudioFootage()` 通过 `FootageJob` 的 filename/decoder/stream index 打开素材。
- `Footage` 当前保存原始文件名、探测参数、source start time 等项目级元数据，但还没有代理文件状态。
- timeline 已有 cache/thumbnail/waveform 机制，但这是渲染缓存，不是替代源媒体的代理媒体。

## 目标

第一阶段交付一个最小但完整的代理工作流：

- 右键选中项目素材或时间线 clip 可生成代理。
- 代理文件写入项目 cache/proxy 目录，使用稳定 hash 命名。
- `Footage` 记录代理状态，项目保存/加载后仍能识别代理。
- 播放/预览时可选择使用代理，导出默认使用原始素材。
- 代理缺失、生成中、失败时能安全回退原始素材。
- 生成任务进入现有 `TaskManager`，支持取消和失败清理。

## 非目标

- 不在第一版实现复杂代理 preset UI。
- 不在第一版实现云端/跨机器代理 relink。
- 不在第一版替代现有 sequence render cache。
- 不改变音频 conform 的 PCM 路径。
- 不让导出默认走代理，避免质量风险。

## 设计

### 1. 新增 ProxyManager

新增 `app/codec/proxymanager.{h,cpp}`，职责类似但独立于 `ConformManager`：

- 根据源文件、stream index、代理参数生成目标文件名。
- 判断代理状态：missing、generating、ready、failed。
- 避免同一素材重复生成任务。
- 使用 `.working` 临时文件，成功后原子 rename。
- 发出 `ProxyReady` 信号通知 UI/缓存失效。

### 2. 新增 ProxyTask

新增 `app/task/proxy/proxy.{h,cpp}`：

- 输入原始文件、decoder id、视频 stream、代理参数、输出路径。
- 第一版优先使用 FFmpeg CLI 或内部 FFmpeg 编码路径生成 H.264/MP4 代理。
- 目标默认参数：较短边不超过 720p，保持宽高比，8-bit 4:2:0，CRF 23 左右。
- 失败时写清晰 error，删除 `.working`。

如果当前构建环境不适合直接调用外部 `ffmpeg`，则优先使用项目内部编码接口；否则在任务内检测 `ffmpeg` 可执行文件并给出失败信息。

### 3. 扩展 Footage 代理元数据

在 `Footage` 中增加：

- `proxy_enabled`
- `proxy_path`
- `proxy_state`
- `proxy_video_stream_index`
- `proxy_generation_preset/version`

项目 XML 写入 `<proxy enabled="..." state="..." stream="..." preset="...">path</proxy>`。

`Clear()` 不能无条件清掉已保存代理路径，只有换源文件或重新探测时才重置不兼容代理。

### 4. 解码路由

提供统一方法选择实际解码源：

- 在线预览/时间线播放：如果项目/素材启用代理且代理 ready，则使用代理文件。
- 离线渲染/导出：默认使用原文件。
- 用户以后可加“导出使用代理”选项，但默认关闭。

优先在 `FootageJob` 构造或 `RenderProcessor::ProcessVideoFootage()` 前完成选择，避免把代理逻辑散落到 decoder 内部。

### 5. UI 入口

第一版入口：

- 时间线 clip 右键：`Generate Proxy`、`Use Proxy`、`Reveal Proxy`、`Delete Proxy`。
- 项目素材右键若已有菜单结构可复用，也增加同样入口；如果项目素材菜单结构分散，先实现时间线入口。
- 菜单启用规则：仅视频素材可生成代理；代理生成中禁用重复生成；代理缺失时 `Use Proxy` 可显示但禁用。

### 6. 缓存和失效

代理 ready 后：

- 触发相关 footage/clip 的 video frame cache、thumbnail cache invalidation。
- 不触碰 audio conform cache。
- 不删除已有原始媒体 render cache，避免用户切换代理/原片时状态不可恢复。

## 实施阶段

### 阶段 1：计划和基础数据结构

- 写本计划。
- 添加 proxy 状态枚举和 filename 生成函数。
- 添加 `Footage` 代理字段和 XML 保存/加载测试。

### 阶段 2：代理生成任务

- 添加 `ProxyTask`。
- 添加 `ProxyManager`。
- 生成 `.working` 文件，成功 rename。
- 增加单元测试覆盖文件名稳定性和状态转换。

### 阶段 3：解码路由

- 扩展 `FootageJob` 或其创建点，携带“实际解码文件”。
- 在线模式优先代理，离线/导出默认原片。
- 代理缺失自动回退原片。

### 阶段 4：时间线 UI

- 时间线右键增加生成/启用/删除代理动作。
- 动作进入 undo 或直接项目状态变更；代理生成任务本身不进 undo。
- 代理 ready 后刷新 timeline/viewer。

### 阶段 5：验证

- `ninja -C cmake-build-debug olive-gtest olive-editor -j22`
- 代理字段 XML roundtrip 测试。
- ProxyManager 状态测试。
- 手工测试：导入 4K 素材、生成代理、启用代理、播放、关闭重开项目、删除代理、导出确认默认原片。

## 风险

- 外部 `ffmpeg` 依赖不可用会让代理生成失败；需要清晰错误并不影响原片播放。
- 代理视频 stream index 可能不同于原片，需要解码路由在代理文件里使用正确 stream。
- 代理分辨率改变会影响 thumbnails/cache，需要切换后明确 invalidation。
- 项目保存相对/绝对代理路径策略要谨慎；第一版使用 cache 目录内路径并可重建。

## 完成标准

- 用户能从时间线对视频 clip 生成代理。
- 代理生成结束后，启用代理的在线预览路径实际读取代理文件。
- 项目重开后代理状态保留。
- 代理缺失或生成失败时不影响原始素材播放。
- 相关构建和 gtest 通过。
