# 节点图边显示异常 & 播放问题排查进展（2026-08-02）

本文记录当前排查状态，供手工继续排查。随调查更新。

## 当前未解决的两个现象

### A. 节点图边显示随机缺失
- 每次显示都不一样：有时全部显示，有时缺几条，缺的边每次不同，无规律。
- 手工重连能连上；切换选中素材（时间线点选）再切回来，又随机缺。
- 已确认**不是只有 footage 相关边**受影响，缺的边类型随机。

### B. 播放冻结（部分修复后仍有残留报告）
- 历史症状：播放头不动、无声音、画面不动。
- 已修复两个确定的根因（见"已修复"清单 8、9），复测中。

## 已验证的事实（不要再重复验证）

1. **引擎图是稳定的**。用 C ABI 直接加载 `~/Movies/bbb.ove`，12 条边在
   activate project + 多轮 frame request 后全部存活（/tmp/edge2_repro 验证）。
   边不显示 ≠ 边被引擎删除。
2. **项目文件内容正常**。bbb.ove 的 12 条连接（见下"图结构"）序列化无误，
   clip 节点只声明了 9 个输入（无 pos_in/tex_in/volume_in——那些警告见第 6 条）。
3. **NodeViewContext 的边创建没有走跳过分支**。`OAK_DEBUG_EDGES=1` 运行时，
   "no item for" 跳过日志一条都没有 → 每条边都调用了 `add_edge_internal`，
   边对象是创建了的。问题在创建之后：被事件删掉、或绘制/几何异常。
4. **删边路径只有两条**：`child_input_disconnected`（NODE_INPUT_DISCONNECTED
   事件驱动，`nodeviewcontext.cpp:251`）和 `remove_child`（节点移除时递归删边）。
5. **XML/context 解析已排除**。C ABI 实测：引擎解析出的 context 成员与
   bbb.ove 完全一致（sequence=[sequence,track,track]；
   clip1=[transform,clip1,footage]；clip2=[footage,volume,clip2]），
   12 条边全部存在。图在引擎里 100% 正确，问题只在 app 的
   NodeViewContext 的 item_map_ 里缺节点 item。
6. **根因已修复（边显示随机缺失）**：`oakengine_node_output_connection_at_ex`
   和 `oakengine_node_output_connection_at` 把 `output_connections()` 的
   `conn.first`（其实是 source 自身）当成目标节点返回，正确目标是
   `conn.second.node()`。后果：NodeViewContext 的 out-edge 枚举拿到的
   "对端"永远是节点自己，`item_map_` 查到自己 → from==to 静默丢弃 →
   out-edge 一条都画不出，只能靠 in-edge 补；哪些边缺取决于节点进入
   context 的顺序（成员顺序+时序）→ 表现为随机缺边。已加回归测试
   （oakengine_node_test.cpp test_edges：at_ex 的目标必须是 LUT 而非 solid）。
   同路径受益者：timelinewidget multicam、nodeparamview 的删边逻辑。
7. 日志里的 `Failed to retrieve array size of parameter "pos_in"... in Olive.clip`
   和 `"muted_in" ... in Olive.sequence` 是**显示层的错查**，调用栈：
   `NodeParamView::update_element_y`（`app/widget/nodeparamview/nodeparamview.cpp:1174`）
   把每个 item 的输入都拿去对 `contexts_.first()` 做 group resolve——硬编码
   第一个 context，解析错了节点。此 bug 未修，与边显示的关系未确定。
7. bbb.ove 图结构（12 条边）：
   - folder.child_in → footage(33755451136), sequence(33848831232)
   - sequence.tex_in ← track(33796807680)；sequence.samples_in ← track(33796810368)
   - sequence.track_in_0/1 ← 两个 track
   - track_v.block_in ← clip(33755511232)；track_a.block_in ← clip(33755497792)
   - clip1.buffer_in ← transform(33764063616)；transform.tex_in ← footage
   - clip2.buffer_in ← volume(33795740032)；volume.samples_in ← footage

## 已修复的问题（本批，工作区内未全部提交）

| # | 问题 | 位置 | 状态 |
|---|------|------|------|
| 1 | get_distance_between_nodes 无递归出口栈溢出 | app/widget/nodeparamview/nodeparamview.cpp | 已提交 a4dfc62f0 |
| 2 | viewerdisplay texture_ 悬垂指针（GL 崩溃） | viewerdisplay.{h,cpp} assign_texture | 已提交 a4dfc62f0 |
| 3 | resignal_requests 遍历中改容器 | engine/render/playbackcache.h | 已提交 a4dfc62f0 |
| 4 | preview request 释放后 ticket 回调 UAF | engine/src/capi/preview.cpp | 已提交 a4dfc62f0 |
| 5 | 播放队列帧时间戳全为 0 → 画面不动 | preview.h/.cpp + viewer.cpp | 已提交 a4dfc62f0 |
| 6 | 浮动"查看器"绑定到序列节点 | mainwindow.cpp open_node_in_viewer | 已提交 a4dfc62f0 |
| 7 | Track 析构 UAF + undo remove_track 丢片段 | engine/node/output/track/track.{h,cpp} | 已提交 30853cbcf |
| 8 | 音频 sample_count API 缺失（无声音根因之一） | preview.h/.cpp 新增 get_audio_sample_count | 未提交 |
| 9 | teardown 系列：~Node 中断边事件打到半死对象 | node.cpp silent disconnect；project.cpp is_being_cleared_；clip.cpp marker disconnect 空指针；projectcopier/previewautocacher 项目死后野指针 | 未提交 |
| 10 | 输入 id memcpy 未 NUL 终止（参数名腐坏） | nodeparamviewitem.cpp:40、nodeparamviewwidgetbridge.cpp:145 | 未提交 |
| 11 | 裸事件订阅析构不退订（拖播放头崩溃） | nodeparamviewkeyframecontrol、nodeparamviewconnectedlabel、export dialog 各加析构退订 | 未提交 |

测试：`cd cmake-build-debug && ctest -j4` 目前 122/122 通过
（含新增 preview request 端到端回归：单帧+音频请求、teardown 不崩）。

## 仍在工作区里的调试代码（提交前需清理）

- `OAK_DEBUG_EDGES=1`：nodeundo.cpp（Add/RemoveCommand redo/undo）、
  node.cpp disconnect_edge、nodeviewcontext.cpp（edge added/removed/跳过）。
- `OAK_DEBUG_INVALID_INPUT=1`：node.cpp report_invalid_input 打调用栈。
- /tmp 下的复现程序（编译产物在 cmake-build-debug/app/ 下）：
  - `preview_repro`：C ABI 播放请求全流程（单帧+音频+teardown）
  - `edge_repro`：工厂建节点连边 + autocache churn
  - `edge2_repro`：加载 bbb.ove 验证 12 条边存活

## 下一步排查方向（按优先级）

1. 用最新构建（含 edge added/removed 日志）跑 `OAK_DEBUG_EDGES=1`，
   对照 12 条边在视图里的 add/remove 次数，找幽灵 remove。
2. 若证实是 load 线程事件与 GUI 建图交错：检查 EngineEventBridge 的
   事件入队时序 vs NodeView::set_contexts 的建图时机（重复边/乱序删边）。
3. 修 `update_element_y` 的 `contexts_.first()` 硬编码（应按 item 所属 context resolve）。
4. clip 缺少 transform 输入（pos_in 等）导致的参数面板/关键帧视图查询失败
   （bbb.ove 里没有这些输入声明，但 app 代码在查）——需确认 Olive 原版
   ClipBlock 是否有这些输入，是被 R8 弄丢的还是本来就不该查。
5. 音频残留：`Tried to allocate sample buffer with invalid audio parameters`
   仍在日志出现（audio processor 输出参数 channels=0 → fix_channel_layout
   修正为 2，但上游某处仍用 0 声道创建 buffer）。
