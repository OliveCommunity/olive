# 消灭 EngineEventBridge：引擎纯库化计划

## 背景与目标

现状：app 通过 `EngineEventBridge`（app/engineeventbridge.*，十几个实例）+ 引擎 C 事件
（engine/src/capi/events.cpp，75 个 OAKENGINE_EVENT_*）接收引擎回调。问题：

- 约 60% 的事件是 app 自己调用引擎时同步产生的，app 本来就知道，不需要被"通知"。
- 裸 C 回调（userdata=this）无生命周期追踪，已制造多起 UAF 崩溃（最近修掉 3 处，仍有
  14 处靠人工 unsubscribe 保命）。
- `OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY(141)` 与
  `PLAYBACK_CACHE_INVALIDATED(141)` ID 撞号，音频续推订阅实际从未生效（现存 bug）。
- 引擎因此不是一个纯库，且后续 AI 助理等新写入方会进一步放大"变更来源"的复杂度。

目标架构（单向依赖）：

```
app 编辑路径 ──同步调用──> engine（纯库，只保留内部 Qt 信号供自身机制使用）
app 编辑路径 ──app 内部 Qt 信号──> app 各视图（播放头、undo、结构、参数）
engine 异步事件（任务/缓存/音频）──极窄 C 通道（queue 到 GUI 线程）──> app
```

- 引擎内部信号（Node::value_changed、Project::node_added 等）保留——它们是
  PreviewAutoCacher/ProjectCopier 等引擎自身机制的生命线，但不许越界给 app 同步事件。
- C ABI 事件面从 75 个收窄到 ~10 个真异步事件。
- 消灭 `EngineEventBridge` 类和全部裸 `oakengine_event_subscribe` 调用点。

## 事件分类结论（来自全量盘点）

- **(a) 同步 app 发起（~60%）**：Node 编辑族、folder、track/block 结构、marker、
  workarea、viewer 参数、color、context position、group passthrough。
  注意每个都有三个旁路来源：undo/redo 重放、项目加载批量、引擎内部联动——
  不能只在调用点处理，必须经"命令执行器/加载完成"统一钩子。
- **(b) 真异步（必须保留通知）**：TASK 族（120–127）、PLAYBACK_CACHE_VALIDATED/
  INVALIDATED、FRAME_CACHE_INVALIDATED、AUDIO_MANAGER_OUTPUT_PARAMS/NOTIFY。
- **(b') playhead/length**：playhead 是 app 播放循环自己 set 的，消费者遍布 10+ 组件
  ——本质是 app 内部广播，挪进 app 侧 PlaybackController，引擎零参与。
  length 是引擎推导状态，改为编辑后主动查询。
- **(c) undo 驱动**：UNDO_INDEX_CHANGED、PROJECT_MODIFIED_CHANGED 及全部会被
  undo 重放重发的结构事件——app 侧 UndoController 发粗粒度信号，视图惰性刷新。

## 阶段计划

### P0 — 冻结分类表 + 修音频事件 ID 撞号（先行 bug fix）
- events.h 给 AUDIO_MANAGER_OUTPUT_NOTIFY 分配新 ID（如 144），events.cpp 补
  AudioManager::output_notify 的 wire-up（当前 case 141 只做 PlaybackCache cast，
  音频订阅永远返回 0）。
- 产出 docs/zh/event-bridge-inventory.md：75 事件 → (a)/(b)/(b')/(c) 四类的映射表
  （基于本次盘点结果），作为后续迁移的对账单。

### P1 — PlaybackController（消灭 playhead/length 订阅，~12 处）
- 新建 `app/playback/playbackcontroller.{h,cpp}`（QObject，Core 持有单例）：
  - 信号：`playhead_changed(oak::Node viewer, Rational time)`、
    `viewer_length_changed(oak::Node, Rational)`。
  - ViewerWidget 播放循环/seek 的 `oakengine_viewer_set_playhead` 调用处统一经它发出；
    其它 set_playhead 调用点（timelinewidget、timeruler、multicam）同样经它。
  - length：各 viewer 参数编辑点 + undo 后主动 `viewer_output_length()` 查询并广播变化。
- 迁移订阅者：timebasedview、resizabletimelinescrollbar（部分）、
  nodeparamviewwidgetbridge/keyframecontrol/connectedlabel（3 个裸 C）、export dialog、
  timelinewidget、viewerdisplay。
- 收益：顺带消灭 4 个裸 C 订阅中的 3 个和播放头族全部事件。

### P2 — EngineAsyncEvents（收窄 C 事件面到 ~10 个）
- 新建 `app/asyncengineevents.{h,cpp}`（唯一 dispatcher，Core 持有）：
  - 对 TASK 族、CACHE_VALIDATED/INVALIDATED、FRAME_CACHE_INVALIDATED、
    AUDIO_OUTPUT_PARAMS/NOTIFY 各建一个 C 订阅，回调里只做
    `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` 统一回 GUI 线程，
    再 emit 类型化 Qt 信号。
  - 明确文档化：这是引擎→app 的唯一反向通道，只覆盖真异步事件。
- 迁移订阅者：mainstatusbar、taskmanager 面板、taskviewitem、task dialog、
  timeruler 缓存条、viewer 的 cache invalidated、viewer 音频续推（接 P0 修好的 ID）。
- engine/include/oakengine/events.h：同步事件常量标注 deprecated（先不删，P5 后清理）。

### P3 — UndoController + 粗粒度模型变更信号（消灭 undo 族 + 结构类订阅）
- 新建 `app/undo/undonotifier.{h,cpp}`：
  - 包装所有 `oakengine_undo_push/undo/redo` 的 app 调用点（集中在少数几处：
    oakengine_undo_push_or_run 的 app 侧、Core、各面板直接 push 处）；
    命令执行后、undo/redo 后、项目加载完成后，emit：
    - `structure_changed()`（节点/边/track/block/folder 增删移动）
    - `params_changed()`（值/关键帧/label/flags）
    - `modified_changed(bool)`（给 core.cpp:1108 的 setWindowModified）
    - `undo_index_changed(int)`（给 historywidget）
  - 项目加载（TaskDialog 完成）后也发一次 structure_changed + params_changed，
    替代加载期批量事件驱动建视图的路径（nodeviewcontext、projectviewmodel 依赖它）。
- 迁移结构类订阅：nodeviewcontext（节点/边增删）、nodeview（removed_from_graph）、
  timelinewidget（track/block 结构）、projectviewmodel（folder/label）、
  mainwindow（ViewerPanel 关闭）、trackviewitem。
  迁移方式：订阅粗粒度信号 → 对应视图做惰性重建/刷新（NodeView/Timeline 本就按
  context 重建，成本可控；projectviewmodel 用 model reset）。
- historywidget 与 core.cpp 的 UNDO_INDEX_CHANGED/MODIFIED 订阅改为 UndoController 信号。

### P4 — 参数类订阅迁移（最大量、逐面板）
- 原则：编辑发起处直接刷新 + UndoController::params_changed 惰性兜底。
  - nodeparamviewwidgetbridge（滑条值）：SliderBase 编辑路径已知道新值（直接 set）；
    undo/加载后靠 params_changed 触发受影响 item 重读。
  - keyframeview/keyframecontrol（关键帧按钮/曲线）：编辑路径直接刷新 +
    params_changed 全量重读。
  - nodeviewitem（label/color/message/array size）、nodeparamviewitem/arraywidget、
    connectedlabel、seekablewidget（marker/workarea）、viewerdisplay（字幕）、
    multicamwidget（size/par）、manageddisplay（OCIO 配置）、audiowaveformview。
- 每迁完一个面板，删除其 EngineEventBridge 成员与全部 `bridge_->subscribe` 调用。

### P5 — 拆除与收尾
- 删除 app/engineeventbridge.{h,cpp} 及全部引用；删除各裸
  `oakengine_event_subscribe` 调用点（P1–P4 应已清零，grep 验证）。
- engine/src/capi/events.cpp：同步事件订阅路径下线（保内部引擎信号）；
  events.h 同步事件常量删除或移入内部头；C ABI 只保留 P2 的异步集合 + undo 族
  （undo 族是否保留视 P3 后 app 是否还有 C 调用方——AI 助理未来仍可能需要，
  保留 UNDO_INDEX_CHANGED 和 MODIFIED 两个 C 事件是可接受的例外）。
- 更新 docs/zh/event-bridge-inventory.md 为"迁移完成对照表"，
  更新 docs/zh/investigation-edge-display-and-playback.md 收尾记录。

## 验证方案（每阶段都要过）

1. `cmake --build cmake-build-debug -j8` 零错误零新警告。
2. `ctest -j4` 122/122。
3. 手工检查表（每阶段对应项）：
   - 播放：画面/声音正常、播放头移动、拖动播放头不崩。
   - 节点图：边全部显示、增删节点/边即时刷新、切换素材不丢。
   - 时间线：增删 clip/track 即时刷新、缓存条随渲染增长。
   - 参数面板：滑条值随编辑/undo 刷新、关键帧按钮状态正确。
   - 项目浏览器：导入素材/改名/撤销即时刷新。
   - 状态栏/任务面板：转码任务进度实时更新。
   - 历史面板：undo/redo 列表与选中行正确，标题栏修改标记正确。

## 风险与对策

- **粗粒度信号导致过度刷新**：NodeView/Timeline 重建成本高。对策：P3 先只在
  structure_changed 上做惰性（queued + 合并多次变更一次刷）；profiling 不行再退回
  按面板细化。
- **漏掉旁路发射源**（引擎内部联动、multicam）：对策：迁移期在引擎 emit 点临时加
  计数日志（复用 OAK_DEBUG_EDGES 模式），手工操作对照表逐项过一遍确认无未覆盖事件。
- **加载期建视图路径**：原来靠批量事件驱动；P3 的"加载完成后 structure_changed"
  必须覆盖 nodeviewcontext、projectviewmodel、timelinewidget 三处的初始构建。
- **UndoController 覆盖不全**（某些面板直接 oakengine_undo_push）：grep 全部调用点
  收口，禁止新增未经 UndoController 的 push（code review 条目）。
