# 消灭 EventBridge：Good First Issue 清单 / Eliminating EventBridge: Good First Issues

目标 / Goal：app 不再通过 `EngineEventBridge` / `oakengine_event_subscribe` 接收引擎事件，
引擎回归纯库（只保留内部 Qt 信号自用）。
The app stops receiving engine events via `EngineEventBridge` / `oakengine_event_subscribe`;
the engine becomes a pure library (its internal Qt signals are for its own use only).

每个 issue 相互独立、可单独认领和提交（基建类两个建议最先做）。
Each issue is independent and can be claimed and shipped separately (do the two
infrastructure ones first — they simplify everything else).

---

## 通用规则 / Ground rules

- **每个 issue 都必须跑测试再提交 / Run the tests before submitting, every time**：
  ```bash
  cmake --build cmake-build-debug -j8 && cd cmake-build-debug && ctest -j4
  ```
  构建零错误、122 个测试全绿才算完成；另需按该 issue 的验收项手工验证。
  Zero build errors and 122/122 tests passing are the definition of done,
  plus the manual acceptance checks listed in each issue.
- 迁移模式只有三种，照搬即可 / Only three migration patterns, copy them:
  - **A. app 内部 Qt 信号 / app-internal Qt signal**：事件其实是 app 自己引发的
    （播放头、编辑、undo），在发起处直接 emit app 内信号，订阅者改连它。
    The event is app-initiated (playhead, edits, undo) — emit an app-internal
    signal at the origin and re-point subscribers to it.
  - **B. 异步保留 / keep async**：事件真是异步的（任务、缓存、音频节拍），收敛到
    唯一 dispatcher（issue 0b），回调统一 QueuedConnection 回 GUI 线程。
    Truly async events (tasks, caches, audio beats) go through one dispatcher
    (issue 0b) that queues everything back to the GUI thread.
  - **C. 删除 / delete**：调用点本来就知道结果，直接就地刷新，订阅整个删掉。
    The call site already knows — refresh inline and drop the subscription.
- 每完成一个 issue：删掉对应 `bridge_->subscribe` / `oakengine_event_subscribe`
  调用，并在本文勾掉该条。
  When done: remove the matching subscribe calls and check off the item here.
- 架构总览 / Architecture background：`docs/zh/plans/eliminate-event-bridge.md`

---

## 基建 / Infrastructure（建议先做 / do first）

### issue 0a — 修音频事件 ID 撞号 / Fix the audio event ID collision（真 bug，半天）
`engine/include/oakengine/events.h:210` 的 `AUDIO_MANAGER_OUTPUT_NOTIFY = 141`
与 :213 的 `PLAYBACK_CACHE_INVALIDATED = 141` 撞号；events.cpp 的 case 141 只做
PlaybackCache 转换，`AudioManager::output_notify` 从未接线，`viewer.cpp:1406` 的
音频续推订阅实际永远收不到。
`AUDIO_MANAGER_OUTPUT_NOTIFY` shares ID 141 with `PLAYBACK_CACHE_INVALIDATED`;
the audio notification is never wired up, so the viewer's audio keep-alive
subscription never fires.
- 改动 / Change：给 OUTPUT_NOTIFY 分配新 ID（>=144），events.cpp 补 AudioManager
  的 connect。/ Assign a fresh ID (>=144) and wire up AudioManager in events.cpp.
- 验收 / Acceptance：播放长素材（>5s）音频不中断。
  Audio keeps playing past 5 seconds without cutting out.
- **必做 / Required：build + ctest 全绿（见通用规则）。**

### issue 0b — 建唯一的异步事件 dispatcher / Single async event dispatcher（1 天）
新建 `app/asyncengineevents.{h,cpp}`（QObject，Core 持有单例），只订阅
TASK 族（120–127）、PLAYBACK_CACHE_VALIDATED/INVALIDATED、
FRAME_CACHE_INVALIDATED、AUDIO_OUTPUT_PARAMS/NOTIFY。
C 回调里只做 `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`，
然后 emit 类型化 Qt 信号。
Create one dispatcher (Core-owned QObject) subscribing only to the truly async
events; C callbacks just queue to the GUI thread and re-emit typed Qt signals.
- 验收 / Acceptance：转码一次，状态栏进度条照常走；之后所有 (b) 类迁移都指向它。
  Transcode once — the status bar progress still works. All later (b) migrations
  must use it instead of their own subscriptions.
- **必做 / Required：build + ctest 全绿。**

### issue 0c ✅ (done) — 建 app 内 PlaybackController（1 天）
新建 `app/playback/playbackcontroller.{h,cpp}`，信号
`playhead_changed(oak::Node viewer, Rational)`。
把 app 内所有 `oakengine_viewer_set_playhead` 调用点（ViewerWidget 播放循环、
timelinewidget、timeruler、multicam、export）统一经它转发并 emit。
One app-side controller that every `oakengine_viewer_set_playhead` call site
goes through; it re-broadcasts `playhead_changed` as an app-internal Qt signal.
- 验收 / Acceptance：播放/拖动/seek 时各视图照常刷新（先并存，后续逐个切换）。
  All views keep updating during playback/seek (bridge may coexist until later
  issues migrate each subscriber).
- **必做 / Required：build + ctest 全绿。**

---

## playhead 族迁移 / Playhead migrations（模式 A，约半天/个）

### issue 1 ✅ (done) — timebasedview 的 playhead 订阅
`app/widget/timebased/timebasedview.cpp:168`（裸 C 回调）。改连
PlaybackController::playhead_changed。
- 验收 / Acceptance：播放时 ruler 播放头线正常移动；无裸 C 订阅残留。
- **必做 / Required：build + ctest 全绿。**

### issue 2 ✅ (done) — NodeParamViewWidgetBridge playhead
`app/widget/nodeparamview/nodeparamviewwidgetbridge.cpp:1140`（裸 C）。
- 验收 / Acceptance：播放时关键帧插值滑条值随播放头更新。
- **必做 / Required：build + ctest 全绿。**

### issue 3 ✅ (done) — NodeParamViewKeyframeControl playhead
`app/widget/nodeparamview/nodeparamviewkeyframecontrol.cpp:222`（裸 C）。
- 验收 / Acceptance：播放头移动时 prev/next/toggle 关键帧按钮状态正确。
- **必做 / Required：build + ctest 全绿。**

### issue 4 ✅ (done) — NodeParamViewConnectedLabel playhead
`app/widget/nodeparamview/nodeparamviewconnectedlabel.cpp:131`（裸 C）。
- 验收 / Acceptance：值树随播放头刷新。
- **必做 / Required：build + ctest 全绿。**

### issue 5 ✅ (done) — ExportDialog playhead
`app/dialog/export/export.cpp:311`（裸 C）。
- 验收 / Acceptance：导出对话框 in/out 时间随播放头同步。
- **必做 / Required：build + ctest 全绿。**

### issue 6 ✅ (done) — timelinewidget / viewerdisplay 的 playhead 族（bridge 订阅）
`app/widget/timelinewidget/timelinewidget.cpp`（:741-743 一带）与 viewerdisplay
的相关订阅。改连 PlaybackController。
- 验收 / Acceptance：时间线时间码/播放头位置显示正确。
- **必做 / Required：build + ctest 全绿。**

---

## undo/modified 族迁移 / Undo & modified migrations（模式 A，约半天/个）

### issue 7 ✅ (done) — historywidget 的 UNDO_INDEX_CHANGED
`app/widget/history/historywidget.cpp:33,127`（裸 C 两处）。
在 Core 的 undo/redo/push 出口 emit app 内 `undo_index_changed(int)`，
historywidget 改连它。
- 验收 / Acceptance：undo/redo 后历史列表 model reset 且选中行正确。
- **必做 / Required：build + ctest 全绿。**

### issue 8 ✅ (done) — core.cpp 的 PROJECT_MODIFIED_CHANGED
`app/core.cpp:1108`（裸 C）。modified 由 undo 栈驱动，app 在
push/undo/redo/load 处即可推导；改为 app 内信号驱动 `setWindowModified`。
- 验收 / Acceptance：编辑/撤销/保存后标题栏修改标记正确。
- **必做 / Required：build + ctest 全绿。**

---

## 结构类订阅迁移 / Structure migrations（模式 C/A，0.5–1 天/个）

### issue 9 — seekablewidget 的 marker/workarea 订阅
`app/widget/seekable/seekablewidget.cpp:95-101,146-154`。
编辑走 undo 命令：命令执行处直接 `viewport()->update()`；undo 后同样刷新
（复用 issue 7 的信号）。
- 验收 / Acceptance：增删 marker、改 workarea 后标尺即时刷新，undo 同样正确。
- **必做 / Required：build + ctest 全绿。**

### issue 10 — resizabletimelinescrollbar 的 marker/workarea
`app/widget/resizablescrollbar/resizabletimelinescrollbar.cpp:83-98,124-129`。
- 验收 / Acceptance：滚动条上 marker 绘制正确。
- **必做 / Required：build + ctest 全绿。**

### issue 11 — nodeviewitem 的 label/color/message/array 订阅
`app/widget/nodeview/nodeviewitem.cpp:85-119`。
label/color 编辑就地刷新；array size 走结构命令调用点；undo 由 issue 7 信号兜底。
- 验收 / Acceptance：改名/换色/数组增删后节点块即时刷新。
- **必做 / Required：build + ctest 全绿。**

### issue 12 — NodeParamViewItem / arraywidget / keyframecontrol 桥订阅
`app/widget/nodeparamview/nodeparamviewitem.cpp:95-296`、
`nodeparamviewarraywidget.cpp:43-45`、keyframecontrol 的 keyframe 族。
- 验收 / Acceptance：参数项 label/flags/array/keyframe 按钮随编辑与 undo 正确刷新。
- **必做 / Required：build + ctest 全绿。**

### issue 13 — nodeparamviewwidgetbridge 的参数值订阅
`app/widget/nodeparamview/nodeparamviewwidgetbridge.cpp:161-178`。
滑条编辑路径直接 set；undo/加载后统一重读。
- 验收 / Acceptance：滑条值在编辑、undo、加载后都正确。
- **必做 / Required：build + ctest 全绿。**

### issue 14 — NodeParamView group passthrough / context 订阅
`app/widget/nodeparamview/nodeparamview.cpp:189-205,350-352,871-874`。
- 验收 / Acceptance：group 打开/关闭、context 增删节点后参数面板正确重建。
- **必做 / Required：build + ctest 全绿。**

### issue 15 — nodeviewcontext 的节点/边增删订阅（结构核心）
`app/widget/nodeview/nodeviewcontext.cpp:99-138,180-183,427-430`。
增删来自：app 编辑命令（调用点处理）、undo（issue 7 信号兜底重建）、
项目加载（新增"项目加载完成"钩子，Core 在 TaskDialog 成功后广播，
替代批量事件驱动建图）。
- 验收 / Acceptance：增删节点/连线即时刷新；undo 恢复正确；重新打开项目视图完整。
- **必做 / Required：build + ctest 全绿。**

### issue 16 — nodeview 的 NODE_REMOVED_FROM_GRAPH
`app/widget/nodeview/nodeview.cpp:91,1911` 与 mainwindow.cpp:58,351。
删除节点的命令处 + undo 信号处理。
- 验收 / Acceptance：删节点后 item/ViewerPanel 关闭；undo 恢复。
- **必做 / Required：build + ctest 全绿。**

### issue 17 — timelinewidget 的 track/block 结构订阅
`app/widget/timelinewidget/timelinewidget.cpp:665-753,2177-2228`。
block/track 增删走 timeline 命令处直接增删 item；undo 与加载统一重建。
- 验收 / Acceptance：增删 clip/track 即时刷新，undo/加载正确。
- **必做 / Required：build + ctest 全绿。**

### issue 18 — trackviewitem 的 index/muted
`app/widget/timelinewidget/trackview/trackviewitem.cpp:66,117`。
- 验收 / Acceptance：轨道移动/静音状态即时刷新。
- **必做 / Required：build + ctest 全绿。**

### issue 19 — projectviewmodel 的 folder/label 订阅
`app/panel/project/projectviewmodel.cpp:45-61,505-512`。
导入/建文件夹/改名走命令处；加载完成统一 model reset（复用 issue 15 的钩子）。
- 验收 / Acceptance：项目浏览器增删/改名即时刷新。
- **必做 / Required：build + ctest 全绿。**

### issue 20 — 其余散点 / misc
multicamwidget（:107/114 裸 C）、manageddisplay（OCIO，:143）、
audiowaveformview（:57,76）、viewerdisplay（字幕，:100,264）、
panel/timebased（label，:162）。
- 验收 / Acceptance：各自 UI 随编辑刷新。
- **必做 / Required：build + ctest 全绿。**

---

## 异步类迁移 / Async migrations（模式 B，约半天/个，依赖 0b）

### issue 21 — mainstatusbar / taskmanager / taskviewitem / task dialog
`app/window/mainwindow/mainstatusbar.cpp:88-146`、
`app/panel/taskmanager/taskmanager.cpp:41-56`、
`app/widget/taskview/taskviewitem.cpp:82-88`、
`app/dialog/task/task.cpp:47-48`。全部改连 dispatcher 的 task 信号。
- 验收 / Acceptance：转码/导出任务进度条、任务列表实时更新。
- **必做 / Required：build + ctest 全绿。**

### issue 22 — timeruler 缓存条 + viewer 的 cache invalidated
`app/widget/timeruler/timeruler.cpp:82-113`、
`app/widget/viewer/viewer.cpp:366-394`。
- 验收 / Acceptance：播放时缓存绿条随后台渲染增长；编辑后缓存条正确失效。
- **必做 / Required：build + ctest 全绿。**

### issue 23 — viewer 的音频续推（AUDIO_OUTPUT_NOTIFY）
`app/widget/viewer/viewer.cpp:1406`（裸 C，当前因撞号失效）。依赖 0a+0b。
- 验收 / Acceptance：长素材播放音频持续不中断。
- **必做 / Required：build + ctest 全绿。**

---

## 收尾 / Final teardown（最后做 / do last）

### issue 24 — 删除 EngineEventBridge
前面 issue 全部勾掉后：`grep -rn "EngineEventBridge\|bridge_->subscribe" app`
应为零引用。删除 `app/engineeventbridge.{h,cpp}` 与 CMake 条目。
- 验收 / Acceptance：构建通过；全功能回归（播放/节点图/时间线/参数/历史/任务）。
- **必做 / Required：build + ctest 全绿 + 完整手工回归。**

### issue 25 — 收窄 engine 事件面
`engine/src/capi/events.cpp` 只保留 dispatcher 需要的异步事件 +
UNDO_INDEX_CHANGED / PROJECT_MODIFIED_CHANGED（给未来 AI 助理等外部写入方
保留的最小例外）；`engine/include/oakengine/events.h` 同步事件常量删除或移入
内部头；引擎内部 Qt 信号保持不变。
- 验收 / Acceptance：ctest 全绿；nm 检查 C ABI 不再暴露同步事件订阅面。
- **必做 / Required：build + ctest 全绿。**
