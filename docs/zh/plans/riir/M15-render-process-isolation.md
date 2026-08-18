# M15：渲染进程隔离（oak-worker 真实化）设计

> 状态：已批准（用户 2026-08-18 提出，作为独立追加任务，不阻塞 M12 其余阶段）。
> 前置调研：见会话调研报告（oak-worker/ipc.rs 传输层已完整、TicketArena 投递口收敛、上屏链路 6 处拷贝点）。

## 1. 目标（用户原文要求）

1. oak-worker 做成真实渲染进程；**删除主进程内部渲染线程池**（oakrender `WorkerPool`），统一走"渲染进程 + 主进程"隔离模型；OFX 插件崩溃不得连累主进程。
2. **全链路零拷贝**：主进程与 worker 之间 stdio 传控制指令、共享内存传帧；所有渲染操作在共享内存池内完成；除 GPU 上传外不做任何拷贝；渲染完直接从共享内存池零拷贝上屏。
3. **零锁**：帧槽交接用无锁 SPSC 环（已有）；调度器单线程；worker 单渲染线程。
4. **批量认领、无工作窃取**：worker 认领一批帧（约 120 帧量级）后，这些帧不再分配给别的 worker。
5. **调度均匀性**：相邻的帧要几乎同时渲染完——相邻帧分配给不同 worker。

## 2. 架构总览

```
主进程 (GUI / CLI / Export)
  ├── PreviewScheduler（新，oakrender/src/scheduler.rs）
  │     帧需求队列（播放窗口/seek/导出/缩略图）→ 分批 → 交织派发给 worker
  ├── ProcessDispatcher（新，oakrender/src/procpool.rs）
  │     N 个 WorkerHandle：spawn oak-worker、handshake、NDJSON 控制、崩溃检测重启
  ├── TicketArena（现有）—— 投递口从 WorkerPool.post 换成 ProcessDispatcher
  └── ShmPool（主进程侧，ipc.rs SharedMemoryRegion 复用）
        段 0..N：每 worker 一段；段内 FrameSlotPool（free/ready SPSC 环，已有）
              │  stdio NDJSON（控制面）
              ▼
        oak-worker × N（渲染进程）
          WorkerSession（已有骨架）+ 真实渲染栈（图反序列化/解码/合成/插件）
          渲染结果直接写入 shm 槽 → frame_ready(ticket, slot)
```

## 3. 关键设计决策

### 3.1 共享内存布局

- **每 worker 一个 shm 段**（主进程 create、worker attach，复用 `SharedMemoryRegion` 双模式与 `FrameSlotPool`，key 规范沿用 `olive-rw-<pid>-<index>`）。
- **槽由主进程统一编址**：render 指令携带目标 slot id，worker 无权自行选槽 → 主进程可以把"预览缓存"直接建在槽上：预渲染帧的槽即缓存，上屏读槽即零拷贝；槽释放 = 缓存淘汰。当前帧（播放头）上屏路径：**shm 槽切片 → `queue.write_texture`**（GPU 上传是用户许可的唯一拷贝）。
- **槽格式**：viewer 预览票请求 **BGRA8**（新 force_format；worker 在渲染管线末端 F32→BGRA8 转换后写入槽——格式转换不是拷贝）；导出/全分辨率/scopes 票请求 **F32 RGBA**。槽大小 = 该段服务过的最大帧（64 对齐），段按需 ftruncate 扩容或重建。
- **槽数**：每 worker 8 槽起步（决定单 worker 在飞帧数；内存 = N × 8 × 8.3MB(BGRA8 1080p)）。
- ⚠️ **Spike 必验**：macOS POSIX `shm_open` 单段大小上限（目标 ≥ 512MB）。不达标则回退"临时文件 + `mmap(MAP_SHARED)`"（接口封装在 `SharedMemoryRegion` 内，加 backend 枚举，协议不变）。Linux 用 POSIX shm 即可。

### 3.2 调度器（本任务核心，无 C++ 参照）

- **帧需求模型**：键 = (sequence, 帧号, 参数版本[图版本/代理档/分辨率档/色彩])。来源：
  1. 播放前向窗口（默认前向 120 帧 + 后向少量，随播放头滑动）；
  2. seek/当前帧（最高优先，插单帧）；
  3. 导出（连续全量，经 oaktask）；
  4. 缩略图/静止全分辨率帧。
- **交织批量认领**：W 个 worker。待渲帧流按 **round-robin 分片**：worker i 认领帧号 ≡ i (mod W) 的子序列。每次握手以"批"为单位：worker 认领自己等差序列中的下 B 帧（B ≈ 120/W，可配置）。性质：
  - 每帧恰好属于一个 worker（**无窃取**，满足要求 4）；
  - 相邻帧号落在不同 worker 上并行渲染 → 相邻帧完成时间近似相同（满足要求 5）；
  - worker 批内按帧号升序渲染，完成即发布（槽就绪顺序对主进程透明，主进程按帧号索引消费）。
- **故障恢复（非窃取）**：worker 崩溃（管道 EOF/退出码非零）→ 其**未开始**的批整体回收重派；**已开始**的批中未 frame_ready 的帧标记失败并重派给健康 worker（崩溃帧重派属故障恢复，不违反无窃取）。慢 worker 不干预（防卡死优先于 fairness）。
- **流量控制**：主进程只在目标 worker 有 free 槽时派批（槽即信用）；worker 批内渲到无 free 槽时等待（SPSC 环 poll + 超时让出）。
- **优先级**：seek/当前帧 > 播放窗口（距播放头近者优先）> 导出后台。取消 = cancel 消息 + 帧键失效（版本号 bump）。

### 3.3 协议（NDJSON v2，向后兼容 v1 消息名）

已有：handshake / load_graph / render_frame / frame_ready / cancel。
新增：
- `hello_caps`（worker→main）：支持格式、最大帧尺寸（协商槽大小）。
- `render_batch { tickets: [{ticket_id, time, slot_id, params…}] }`：一批帧 + 指定槽。
- `batch_accepted { batch_id, tickets[] }`：认领确认（认领语义显式化）。
- `frame_failed { ticket_id, error }`：渲染失败（主进程兜底紫帧）。
- `shutdown` / 崩溃检测：主进程 waitpid + EOF。

### 3.4 拆除线程池的影响面（调研结论）

唯一投递口 `TicketArena.pool.post`（ticket.rs:321,390）。消费者 4 个（app renderops、oak-cli、oaktask 导出、oakengine facade）API 不变。`WorkerBackend::{Threads,Processes}` 枚举已预留。oaktask 导出自建私有池改为自建私有 ProcessDispatcher（max_inflight 语义由调度器接管）。**WorkerPool 及其线程在 S2 彻底删除**（用户明确要求；单测需要的同步执行语义由 dispatcher 的 `run_inline` 测试模式提供，不保留生产线程池）。

### 3.5 上屏零拷贝改造（S2，src/ 侧）

现状拷贝点（调研报告）：ticket result `frame.data.clone()` → linesize 重排 → F32→BGRA8 → 缓存 → atlas write_texture。
改后：ticket 完成返回 `ShmFrameRef{worker, slot, meta}`；viewer 预览帧（BGRA8 槽）直接切片喂 `write_texture`；scopes 分析改为读 BGRA8（精度足够）或另请 F32 票；长期缓存（静止全分辨率单帧槽、缩略图）从 shm 拷出一次（必要拷贝，槽需回收）。断言手段：`renderops` 加拷贝字节计数器，测试断言播放路径帧字节拷贝 = 0（GPU upload 除外）。

### 3.6 OFX 崩溃隔离

插件执行器（oakplugin `install_render_executor`）装在 **worker 进程**；主进程不再链接执行栈（app 只经 ticket API）。测试插件加"崩溃模式"（环境变量触发 raise(SIGSEGV)）→ 验收：主进程存活、受影响帧重派、worker 自动重启、渲染结果仍正确。

### 3.7 音频

音频票同协议走 shm（AudioSamples 入槽）。S1/S2 可先保持主进程音频路径（崩溃风险主要来自视频插件），S3 迁移。

## 4. 分期

| 期 | 范围 | 验收 |
|---|---|---|
| S1（crates only） | shm spike（macOS 段上限）；协议 v2；ProcessDispatcher + WorkerHandle + 崩溃重启；worker 侧真实渲染（图快照反序列化 + montage/解码/合成 + 插件执行器）；PreviewScheduler（交织批量认领）；与线程池**并存**（配置切换）。单测 + 集成测试（崩溃隔离/无窃取/均匀性/零拷贝计数）。 | `cargo test -p oakrender -p oak-worker` 全绿；集成测试演示 4 worker 渲 480 帧无重分配、相邻帧完成时间差有界 |
| S2 | RenderManager 默认 Processes；**删除 WorkerPool**；oaktask/oak-cli 接入；src/ 上屏零拷贝（real.rs/renderops.rs/frames.rs）；app 播放前向窗口接入调度器 | `cargo test` 全绿；`cargo run` 播放流畅；拷贝计数=0；kill -SEGV worker 后播放继续 |
| S3 | 音频迁移；压测调优（B、槽数、worker 数自适应）；README/docs 收尾 | 性能报告；文档 |

## 5. 风险

1. **macOS POSIX shm 上限** → 3.1 回退方案。
2. worker 内 wgpu Device：montage/合成现状为 CPU 路径，S1/S2 保持 CPU；GPU 合成后续议。
3. 图快照一致性：GraphSnapshotStore 引用计数已有；图变更（编辑）→ 版本 bump + 重新 load_graph。
4. 调度器位于主进程 UI tick 驱动，需避免 tick 内阻塞（全部非阻塞 poll）。
5. oakengine facade（冻结 C ABI）引用 WorkerPool 的部分同步改为 dispatcher（非默认构建成员，但仍需编译通过）。
