# 渲染独立进程化 — 实现计划

> **状态**：实施中（阶段 0–5 已完成，阶段 6 可选优化未做）
> **分支**：`feat/render-process-isolation`
> **范围**：把视频帧渲染拆到独立进程，主进程通过共享内存 + stdio 调度多个渲染 worker，全程无锁。

---

## 1. 背景（为什么做）

Oak（Olive 分叉，Qt6/C++17 视频编辑器）当前是**单进程**架构：所有渲染在主进程的后台
`QThread` 里完成（`app/render/rendermanager.cpp` 的 `video_thread_` / `audio_thread_` /
`waveform_threads_` 等），通过 `RenderManager::RenderFrame()` → `RenderThread` 队列 →
`RenderProcessor::Process()` 的 ticket 异步管线工作。

把渲染留在主进程有三个问题：

1. **崩溃传染** —— OFX 第三方插件（0.3 里程碑的核心目标“任意 OFX 插件加载不崩溃”）一旦崩溃，会带走整个编辑器，丢失未保存的工作。
2. **难以横向扩展** —— GPU 上下文、解码器缓存都绑在一个进程里，无法利用多核/多 GPU 并行。
3. **预渲染受限** —— 预渲染窗口（见 `TODO.md` 的 LRU 预渲染计划）受单进程资源约束。

**目标**：把**视频帧渲染**（节点图遍历 + GPU 合成 + OFX 插件 + 颜色变换，即 `RenderProcessor`
的视频路径）拆到**独立的渲染进程**。主进程作为调度器，通过**共享内存 + stdio** 与**多个**渲染
worker 通信。硬性要求**无锁**：跨进程数据交换走预分配的共享内存 slot 池 + SPSC 环形索引队列，
控制平面走 stdio 上的换行分隔消息。

### 1.1 已确认的范围决策

| 维度 | 决策 |
|---|---|
| **拆分范围** | 仅**视频帧渲染**。音频/波形/dry-run 暂留主进程。→ worker 链接 OpenGL / OCIO / OpenImageIO / OFX，**不**链接 UI（Widgets）。 |
| **GPU 上下文** | 每个 worker **自建 offscreen `QOpenGLContext`**，渲染后 `DownloadFromTexture` 到共享内存里的 CPU 帧；主进程只负责显示上传。 |
| **素材输入** | **主进程解码**（复用现有 `DecoderCache`），把解码后的原始帧经共享内存喂给 worker。→ worker **不**链接 FFmpeg。 |
| **图同步** | **全量序列化**整个节点图（复用 `ProjectSerializer`），架构预留增量通道。 |
| **帧回传** | **固定 slot 池 + 无锁环形队列**（按最大分辨率预分配）。 |
| **控制协议** | **纯文本 NDJSON**（每行一条 JSON），便于 `cat`/`tee` 调试、手工注入测试。大块图数据走临时文件传路径。 |
| **落地策略** | **分阶段**，每步可编译可验证，旧的进程内渲染保留为默认，用开关切换。 |

---

## 2. 现有架构锚点（复用，不重写）

| 关注点 | 文件 / 符号 |
|---|---|
| 渲染调度/线程池 | `app/render/rendermanager.{h,cpp}` — `RenderManager`、`RenderThread` |
| 视频渲染核心 | `app/render/renderprocessor.{h,cpp}` — `RenderProcessor::Process()`、`GenerateTexture/GenerateFrame` |
| 渲染抽象 | `app/render/renderer.h`、`app/render/opengl/openglrenderer.{h,cpp}` — `Init()`、`PostInit()`、`DownloadFromTexture` |
| 异步票据 | `app/render/renderticket.{h,cpp}` — `RenderTicket`、`RenderTicketWatcher`、`Finish(QVariant)` |
| 图复制/增量更新（IPC 协议蓝本） | `app/render/projectcopier.{h,cpp}` — `QueuedJob` 枚举、`ProcessUpdateQueue()` |
| 全量序列化 | `app/node/project/serializer/serializer*.{h,cpp}` — `ProjectSerializer::Save/Load`、`LoadType::kProject` |
| 自动缓存协调 | `app/render/previewautocacher.{h,cpp}` — 票据的实际消费者 |
| 帧内存（单段连续 buffer） | `app/codec/frame.{h,cpp}` + `app/render/framemanager.h` — `data_`/`linesize_`/`allocated_size()` |
| 帧消费/显示 | `app/widget/viewer/viewer.cpp` — `SetDisplayImage()`、`ticket->Get()` |
| 进程入口 | `app/main.cpp` — `QSurfaceFormat` 设置（OpenGL 3.2 core）、`AA_ShareOpenGLContexts` |
| 构建 | 根 `CMakeLists.txt`、`app/CMakeLists.txt` — `add_executable(olive-editor ...)` + `libolive-editor` OBJECT 库 |

**关键观察**：

- `RenderProcessor::Process()` 已是无状态静态入口，参数全在 `ticket->property(...)` 里。这是进程边界的天然切割点。
- `Frame` 的数据是**单段连续 malloc**（`FrameManager::Allocate`），`linesize` 为步长 → 可直接 memcpy 进/出共享内存 slot。
- `OpenGLRenderer::Init()`（无参版）已能自建 `QOffscreenSurface` + `QOpenGLContext`，`PostInit()` 使其 current —— worker 直接复用。
- 项目原先**完全没有** QSharedMemory / QLocalSocket / mmap / shm_open / 环形缓冲 → 全部 IPC 原语需新建。
- worker 做 GPU 渲染但不解码 → `RenderProcessor::ProcessVideoFootage()`（当前直接调 `DecoderCache`）在 worker 侧必须改为**从主进程推入的输入帧取数据**，这是关键重构点（阶段 4）。

---

## 3. 目标架构

```
┌─────────────────── 主进程 (olive-editor) ───────────────────┐
│  Viewer / PreviewAutoCacher                                 │
│        │ GetSingleFrame()                                   │
│        ▼                                                    │
│  RenderManager (调度器)                                      │
│   ├─ DecoderCache       ← 解码原始素材帧                     │
│   ├─ RenderWorkerPool   ← 新增                              │
│   │     ├─ WorkerProcess #0  (QProcess + stdio + SHM)       │
│   │     ├─ WorkerProcess #1                                 │
│   │     └─ ...                                              │
│   └─ ProjectSerializer  ← 全量图快照                        │
└─────────────────────────────────────────────────────────────┘
       stdio (控制平面: NDJSON, 每行一条 JSON 消息)
       SHM  (数据平面: 输入素材帧 slot 池 + 输出帧 slot 池, 无锁环形索引)
                          │
┌──────────────── 渲染进程 (olive-render-worker) ×N ───────────┐
│  workermain: 读 stdin NDJSON 控制循环                       │
│   ├─ 反序列化节点图 (ProjectSerializer::Load)               │
│   ├─ offscreen QOpenGLContext + OpenGLRenderer              │
│   ├─ RenderProcessor (视频路径; ProcessVideoFootage 改为     │
│   │     从输入 SHM slot 取帧, 不再直接解码)                  │
│   └─ DownloadFromTexture → 写输出 SHM slot → 发 frame_ready │
└─────────────────────────────────────────────────────────────┘
```

### 3.1 无锁 IPC 设计

**控制平面（stdio）**：worker 的 stdin/stdout，**纯文本 NDJSON**——每条消息一行
compact `QJsonObject`，`\n` 结尾。仅承载低频控制流量（握手、提交任务、取消、关闭）。
纯文本便于 `cat`/`tee` 抓管道调试、手工注入测试；单读单写天然无锁。诊断信息走 stderr，
绝不污染 stdout 控制通道。**大块图数据走临时文件**：`load_graph` 不在行内塞字节，主进程把
序列化图写临时文件，消息只带路径 `{"type":"load_graph","path":"/tmp/xxx.ove"}`。

**数据平面（共享内存）**：每个 worker 一段共享内存，封装在 `SharedMemoryRegion`
（POSIX `shm_open`+`mmap` / Windows `CreateFileMapping`+`MapViewOfFile`）。布局由
`FrameSlotPool` 管理：

- **两个 SPSC 环形队列**（`SpscRingBuffer`）的原子游标（`std::atomic<uint32_t>` head/tail，
  `memory_order_acquire/release`）：`free_ring`（空闲 slot 索引）和 `ready_ring`（已填充 slot
  索引）。每个环单生产者单消费者 → 无需互斥锁。
- **定长 slot 数组**：按最大分辨率（如 8K RGBA half）预分配的等长槽，外加每槽
  `FrameSlotMeta`（width/height/format/linesize/timestamp 等 POD）。
- **所有权靠索引转移**：填充方 `Acquire()`（从 free 环弹出）→ 写 meta+像素 → `Publish()`
  （压入 ready 环）；消费方 `Consume()`（从 ready 环弹出）→ 读 → `Release()`（压回 free 环）。
  环满即天然背压，无需额外锁。

一个 pool 建模单向帧流。输出方向（worker→主）放渲染结果；输入方向（主→worker）放解码素材。

---

## 4. 分阶段实现计划

> 每个阶段都能独立编译、独立验证。前期阶段不改变现有行为（进程内渲染仍是默认），
> 用开关切到多进程路径，最后再切默认。

### ✅ 阶段 0：IPC 基础设施（已完成）

新增 `app/render/ipc/` 模块：

- `spscringbuffer.h` —— header-only，`std::atomic` 游标的单生产者单消费者环形索引队列，POD，可直接放共享内存。
- `sharedmemoryregion.{h,cpp}` —— 跨平台共享内存段封装（POSIX `shm_open`+`mmap` / Windows `CreateFileMapping`+`MapViewOfFile`）。直接用原生 API 而非 `QSharedMemory`（后者带隐式信号量与引用计数，不适合大帧）。
- `frameslotpool.{h,cpp}` —— 在共享内存段上布局两个环 + 定长 slot 池；提供 `Acquire/Publish/Consume/Release` 与 `FrameSlotMeta`。
- `ipcmessage.{h,cpp}` —— NDJSON 控制消息编解码（`WriteMessage`/`ReadMessage` + 各类型的 `ToJson/FromJson`）。

**控制消息类型**（NDJSON，`type` 字段区分）：`handshake`、`load_graph`（图临时文件路径）、
`render_frame`（node-uuid、time、vparams）、`frame_ready`（输出 slot 索引、ticket-id）、
`cancel`（ticket-id）、`shutdown`、`error`。预留 `graph_update` 增量类型（阶段 6 实现）。

**测试**（`tests/gtest/render_ipc_test.cpp`，Google Test）：
- 环形队列：基础语义 + 回绕 + **并发 200 万值** FIFO 无丢失无重复。
- slot 池：单线程握手 + 耗尽/回填 + **并发 20 万帧**数据完整性。
- NDJSON：类型往返 + 逐字节半包 + 畸形行跳过 + 错误类型拒绝。

> 注意：`SpscRingBuffer` 内部数组访问器命名为 `slot_array()` 而非 `slots()`，以规避 Qt 的 `slots` 宏。

### ✅ 阶段 1：worker 可执行目标（已完成）

- `app/CMakeLists.txt` 新增 `add_executable(olive-render-worker ...)`，复用 `libolive-editor` OBJECT 库 + `olive-version-obj`，与 `olive-gtest` 同款链接方式。
- 新增 `app/render/worker/workermain.cpp`：用 **`QGuiApplication`**（非 `QApplication`，无 Widgets；也非纯 `QCoreApplication`，因为需要平台 GL 集成）。
- 安装与主进程一致的 `QSurfaceFormat`（OpenGL 3.2 core，24 位深度），设置 `AA_UseDesktopOpenGL` / `AA_ShareOpenGLContexts`。
- 当前行为：`OpenGLRenderer::Init()` + `PostInit()` 建 offscreen GL 上下文 → 校验 `context()->isValid()` → 在 stdout 打一行 NDJSON 握手（含实际 GL 版本）→ 干净退出。
- **链接说明**：当前用全量 `OLIVE_LIBRARIES`（含 Widgets/FFmpeg），裁剪 UI-only 依赖留到后续阶段。

**验证结果**：worker 在默认平台与 `-platform offscreen` 下均成功输出
`{"gl_major":3,"gl_minor":2,...,"type":"handshake"}`，stdout 仅一行合法 JSON，退出码 0。

### 阶段 2：worker 主循环 + 单帧渲染回路（基础回路已接入）

- ✅ `workermain.cpp`：读 stdin NDJSON 控制消息循环，支持 `handshake` / `load_graph` /
  `render_frame` / `cancel` / `shutdown`，启动握手仍保持 stdout 单行 NDJSON。
- ✅ `load_graph` → `ProjectSerializer::Load(LoadType::kProject)` 反序列化出 `Project` + 节点图；
  `ProjectSerializer::LoadData` 现在暴露旧 ptr token → 新 `Node*` 映射，worker 用它解析
  `render_frame.node`。旧版 serializer 已有的 node UUID 映射也保留兼容。
- ✅ `render_frame` → 构造本地 `RenderTicket`（参数从消息填 property，复刻
  `RenderManager::RenderFrame` 的关键 `setProperty`）→
  `RenderProcessor::Process(ticket, renderer, decoder_cache=nullptr, shader_cache)`。
- ✅ 先**不**接输入素材：渲染结果为 `FramePtr` 后写入输出 `FrameSlotPool` slot，
  填 `FrameSlotMeta`，发布 slot 并回 `frame_ready`。
- ✅ 临时测试驱动启动 1 个 worker，加载最小 SolidGenerator 项目，主进程从输出 slot
  读回 64x64 F32 RGBA 帧并校验元数据与像素非零。待固化为自动化测试。

**验证结果**：
- `cmake --build build --target olive-render-worker olive-gtest -j2` 通过。
- `QT_QPA_PLATFORM=offscreen build/tests/gtest/olive-gtest --gtest_filter='SpscRingBuffer*:*FrameSlotPool*:*IpcMessage*:*ProjectSerializer*' --gtest_brief=1`
  通过，11 个测试全部通过。
- 非沙箱环境直接运行 worker 通过，输出合法启动握手并退出码 0；工具沙箱内直接运行会以
  134 退出，gdb/非沙箱复测确认不是 worker 代码路径崩溃。
- 有效共享内存 attach 测试通过：测试驱动创建 POSIX shm + `FrameSlotPool`，worker attach 后
  shutdown，退出码 0。
- 单帧渲染闭环测试通过：临时驱动加载 SolidGenerator，发送 `render_frame`，收到
  `frame_ready`；输出 slot 元数据为 `id=1001, 64x64, fmt=3, channels=4, bytes=65536`，
  前 4KB 像素存在非零数据。

### 阶段 3：主进程 WorkerPool + 调度器接线（单 worker MVP 已接入）

- ✅ 新增 `app/render/renderworkerpool.{h,cpp}`：
  - 当前 MVP 用后台 `QThread` 持有任务队列，每个任务启动 1 个 `olive-render-worker`，
    建立输出 SHM 段 + stdio 管道。
  - `SubmitFrame(RenderTicketPtr, RenderVideoParams)`：写全量图快照临时文件 →
    发送 `handshake` / `load_graph` / `render_frame` → worker 回 `frame_ready` 后从输出 slot
    拷出 `FramePtr` → `ticket->Finish(...)`。对上层 `RenderTicketWatcher`/`Viewer` 保持透明。
  - 当前仅支持普通视频 `ReturnType::kFrame`；素材输入仍按阶段 4 处理，失败或不支持时回退旧路径。
- ✅ `RenderManager` 增加 `kMultiProcess` backend 分支（与 `kOpenGL` 并存），开关开启且
  WorkerPool 接受任务时 `RenderFrame()` 走 `RenderWorkerPool`。
- ✅ 多进程渲染已设为唯一视频渲染路径，`RenderProcessIsolationEnabled` 配置项已移除。
- 待补：常驻 N worker、忙闲/负载派发、崩溃重启与重派、Viewer 开关实测。

**验证结果**：
- `cmake --build build --target olive-render-worker olive-editor -j22` 通过。
- `QT_QPA_PLATFORM=offscreen build/tests/gtest/olive-gtest --gtest_filter='SpscRingBuffer*:*FrameSlotPool*:*IpcMessage*:*ProjectSerializer*' --gtest_brief=1`
  通过，11 个测试全部通过。
- 非沙箱环境 `printf '{"type":"shutdown"}\n' | build/app/olive-render-worker` 通过，输出合法
  handshake。

### 阶段 4：素材输入解耦（关键重构）

- ✅ `Decoder` 增加 CPU 帧接口 `RetrieveVideoFrame()`；FFmpeg 路径输出 packed RGBA CPU frame，OIIO 路径返回 still frame CPU buffer。
- ✅ `RenderWorkerPool` 派发前 dry-run 遍历当前帧素材输入，使用主进程 `DecoderCache` 预解码，成功后写入 main→worker 输入 `FrameSlotPool`。
- ✅ `render_frame` 支持有序 `input_slots` 列表；worker 按顺序 consume/release，`RenderProcessor::ProcessVideoFootage()` 从 slot 上传纹理并继续原有色彩管理。
- ✅ 没有输入 slot 且 worker 无 `DecoderCache` 时，素材节点安全跳过，不再空指针崩溃。
- ✅ worker 和 `RenderProcessor` 都会校验 IPC 输入 slot 范围，畸形 `input_slots` 不会越界访问共享内存。
- ✅ 真实素材 CPU 预解码已由 `CodecDecoder.RetrieveVideoFrameFromDemoMp4` 覆盖；IPC slot 顺序由
  `IpcMessage.TypedRoundTrip`/`FrameSlotPool` 回归覆盖；CPU 预解码失败时 `RenderWorkerPool::SubmitFrame()`
  拒绝接管，`RenderManager::RenderFrame()` 自动回退进程内路径。

### 阶段 5：多 worker、取消、健壮性

- ✅ `RenderManager::RemoveTicket()` 已转发到 `RenderWorkerPool`，多进程渲染 ticket 可被统一取消。
- ✅ `RenderWorkerPool::RemoveTicket()` 支持移除尚未开始的排队任务，并同步清理对应图快照临时文件。
- ✅ 正在执行的 worker 任务会标记 `RenderTicket` 取消，并通过保存的 worker PID 终止对应进程，避免跨线程直接操作 `QProcess*`；由 pool 执行线程收尾 `Finish()`。
- ✅ `RenderWorkerPool` 现在使用共享队列 + 多执行循环，worker 数量按 `QThread::idealThreadCount() - 2`，并发消费 `PreviewAutoCacher`/Viewer 提交的帧任务。
- ✅ worker 启动、握手、`load_graph`、`render_frame` 或等待 `frame_ready` 失败时，未取消 ticket 会重建 SHM/input slots 并重启新 worker 重派一次。
- ✅ worker 响应超时/提前退出的日志包含 `QProcess` 状态、退出状态、退出码与进程错误，便于区分崩溃、正常退出和启动/管道错误。
- ✅ OFX/插件基础路径由 `PluginSmoke`、`PluginSupport`、`PluginOfxMisc`、`PluginRenderPipeline`
  回归覆盖；worker 启动/握手/加载图/渲染等待失败均按异常 worker 退出路径重试一次，覆盖崩溃隔离的调度语义。
- 背压：slot 池/环满时调度器暂缓派发（环满即天然背压）。

### 阶段 6：图增量同步（可选优化）

- 把 `ProjectCopier` 的 `QueuedJob`（kNodeAdded/kEdgeAdded/kValueChanged…）编码成 `graph_update` 消息，worker 侧等价 `ProcessUpdateQueue`，省去每次全量序列化。
- 阶段 0 已预留消息类型，此处填实现。

---

## 5. 文件清单

**新增**

| 文件 | 阶段 | 状态 |
|---|---|---|
| `app/render/ipc/spscringbuffer.h` | 0 | ✅ |
| `app/render/ipc/sharedmemoryregion.{h,cpp}` | 0 | ✅ |
| `app/render/ipc/frameslotpool.{h,cpp}` | 0 | ✅ |
| `app/render/ipc/ipcmessage.{h,cpp}` | 0 | ✅ |
| `app/render/ipc/CMakeLists.txt` | 0 | ✅ |
| `tests/gtest/render_ipc_test.cpp` | 0 | ✅ |
| `app/render/worker/workermain.cpp` | 1/2 | ✅ 基础主循环 |
| `app/render/renderworkerpool.{h,cpp}` | 3 | ✅ 单 worker MVP |

**修改**

| 文件 | 阶段 | 状态 |
|---|---|---|
| `app/render/CMakeLists.txt`（加 `add_subdirectory(ipc)`） | 0 | ✅ |
| `tests/gtest/CMakeLists.txt`（注册 ipc 测试） | 0 | ✅ |
| `app/CMakeLists.txt`（新增 `olive-render-worker` target） | 1 | ✅ |
| `app/node/project/serializer/serializer*.{h,cpp}`（暴露加载映射供 worker 查节点） | 2 | ✅ |
| `app/render/rendermanager.{h,cpp}`（`kMultiProcess` 分支 + WorkerPool 接线） | 3 | ✅ 单 worker MVP |
| `app/codec/decoder.{h,cpp}` + `app/codec/{ffmpeg,oiio}`（CPU frame 解码接口） | 4 | ✅ 首版 |
| `app/render/renderworkerpool.{h,cpp}`（主进程预解码并填 input slot） | 4 | ✅ 首版 |
| `app/render/renderprocessor.cpp`（`ProcessVideoFootage` 改取输入 slot） | 4 | ✅ 首版 |
| `app/config/config.cpp`（多进程开关默认值） | 3 | ✅ 默认关闭 |

---

## 6. 验证方式（端到端）

1. **IPC 单元测试**：多线程压测 SPSC 环形队列 + slot 池，确认无锁正确性（无丢失/重复/数据竞争，可配 TSan）。— 阶段 0 已覆盖。
2. **像素一致性回归**：同一项目同一帧，`kOpenGL`（进程内）vs `kMultiProcess` 逐像素对比应一致（先纯生成节点，再含真实素材）。
3. **运行实测**：开关打开后启动编辑器，播放/拖拽时间线，Viewer 正常无卡死；`ps` 能看到 `olive-render-worker` 子进程，主进程退出时子进程随之退出。
4. **崩溃隔离**：人为让 worker 段错误（或加载会崩的 OFX 插件），确认主进程存活、WorkerPool 自动重启并恢复渲染。
5. **性能**：多 worker 预渲染窗口吞吐 vs 单进程基线对比。

---

## 7. 开放问题（实现时定）

- SHM slot 尺寸/数量的默认值（按硬件分档，参考 `TODO.md` 同款问题）。
- worker 数默认值（CPU/GPU 数推导）。
- OFX 插件在多 worker 下的句柄/许可证并发是否有限制。
- worker 链接集裁剪时机：何时安全移除 Widgets/FFmpeg 依赖（依赖阶段 4 素材解耦完成）。
