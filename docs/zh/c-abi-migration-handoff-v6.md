# C ABI 迁移交接 v6（执行者：GLM-5.2）

> 本文自包含。工作分支：`c-abi-migration`（就地继续，不新开分支）。
> 你是第三任执行者：第一任 DeepSeek 因符号作弊被解除（inline 化 engine
> 实现、no-op stub、dlsym 偷符号）；第二任 K2.7 按 v5 交接文档完成了
> 全部修复性工作（§3/§4）和 F1/F2 批次，额度耗尽退出。当前基线由
> Kimi K3 验证并提交（`b00a3e22e`）。
>
> **核心原则：符号数只是测量结果，不是目标。** 目标是 app 与 engine
> 之间只剩真实、可验证的 C ABI 调用。任何让 nm 数字下降但不减少真实
> 依赖的手段都是作弊（见 §3 三条红线）。
>
> 每步闭环：全量构建 0 error → 全量 ctest 绿 → nm 实测 → 立即提交。
> git 禁令：`checkout --`/`restore`/`clean`/`reset --hard`/`stash`。

---

## 1. 当前状态（R6 完成，2026-07-26 实测）

- 符号：`nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"`
  = **0**（R5 遗留 58 → R6 清零，100% C ABI 达成）。
- oak-render-worker = **0**（保持）。
- 测试：ctest **45/45** 全绿。
- 构建：`cmake --build cmake-build-debug -j$(nproc)`（**勿重新 cmake**）。
- 反作弊：app 无 dlfcn/dlsym/QLibrary（仅 main.cpp wglGetProcAddress
  为 OpenGL 驱动能力检测，与 engine 符号无关）；engine 无 inline 化。
- **§6.4 豁免清单：无豁免。** 原 AudioProcessor(5) 经 P5 C vtable 消除；
  plugin(4) 经 P3.2 去 Q_OBJECT 消除；渲染/GPU(13) 经 P6 display.h 消除。

> 历史基线（仅供追溯）：v6 接手时 88 → GLM-5.2 R5 冲刺降至 58 →
> R6 六阶段（P1-P6）清零。详见 `r6-cleanup-plan.md` 与
> `facade-migration-roadmap.md` 附 C R6 节。

符号分布（131）：

| 簇 | 数 | 处理 |
|---|---|---|
| Node | 37 | F4 主攻，最难（qobject_cast、staticMetaObject、inline 方法） |
| TimelineWorkArea / Task | 6+6 | F3，见 §4 |
| 渲染族（Renderer/PlaybackCache/Frame/DynamicRenderer/DraggableGizmo/OpenGLRenderer/Texture/ColorProcessor/AudioWaveformSync/AudioSynchronizer/ManagedColor） | ~25 | F5 |
| NodeValue / VideoParams / UndoCommand / ViewerOutput / Sequence / Project / RenderManager 等中尾 | ~30 | F3/F4 顺带 |
| 长尾 1-2 符号类（VolumeNode、TransitionBlock、TransformDistortNode、TimelineMarker、SubtitleBlock、TextGeneratorV3、SolidGenerator、ShapeNode(Base)、MultiCamNode、CrossDissolveTransition、Folder、FrameHashCache、AudioWaveformCache、AudioVisualWaveform、UndoStack、TrackListRippleToolCommand 等） | ~30 | F6 |
| ~~豁免候选：AudioProcessor(5)、plugin(4)~~ | ~~9~~ | ✅ 已全部消除（P5 C vtable + P3.2 去 Q_OBJECT），无豁免 |

## 2. 已完成（不要重做）

- v5 §3 全部：dlsym wrapper 已删，`oakengine_node_factory_*` 在
  `engine/src/capi/node.cpp` 正经实现；F1 的四处 undo 语义破坏已修；
  recording_callback 的 task 所有权已修；F2/F3 保留项的漏已补。
- v5 §4：**undo 分组 facade 已存在**——`oakengine_undo_group_begin/
  end/abort`（`engine/include/oakengine/undo.h`）+
  `UndoStack::push_pre_executed`。一次用户操作需要多条 facade 调用时
  **必须**用它聚合，不许拆成 N 条撤销记录。
- F1（撤销命令族）、F2（Track/ClipBlock/NodeGroup/NodeKeyframe）已完成。
- K2.7 留下的 engine facade 新增（在基线里，可直接用）：
  `oakengine_viewer_set_video_params`、`oakengine_viewer_set_audio_params`、
  `oakengine_cli_task_dialog_run`。

## 3. 三条红线（违反即返工）

1. 禁止把 engine 的 .cpp 实现 inline 化进头文件刷符号。
2. 禁止 no-op stub（空 redo/undo、空命令顶替真功能、假成功返回值）。
3. 禁止 dlsym/GetProcAddress/QLibrary 运行时解析 engine C++ 符号。
   facade 只能在 `engine/src/capi/` 实现、`engine/include/oakengine/` 声明。

## 4. 剩余工作（按序）

### F4（续）：Node 信号连接(23) — 下一任主攻

事件 ID 已全部分配（events.h 70-95），EngineEventBridge 信号已存在
（engineeventbridge.h 140-192）。46 处 `connect(node, &Node::signal, ...)`
跨 11 文件，其中 9 个类缺 `EngineEventBridge` 成员。迁移模式：
1. 类中加 `EngineEventBridge *bridge_` 成员 + 析构清理；
2. `connect(node, &Node::signal, slot)` → `bridge_->subscribe(node, EVENT_ID)`
   + `connect(bridge_, &EngineEventBridge::node_signal, slot)`；
3. 注意信号参数类型差异（bridge 用 C ABI 类型，slot 需适配）。

剩余 3 个 Node 符号（link、set_standard_value、set_value_at_time）无直接
调用，从 engine inline 函数拉入。消除需找到引用的 inline 函数并替换。

### F5：渲染族（~25）

先查 `oakengine/playback.h`、`preview.h`、`renderer.h`、`gizmo.h`
有无现成 facade。已有：
- `oakengine_render_manager_backend_to_string` / `_requested_backend`（RenderManager）
- `oakengine_gizmo_drag_start/move/end`（DraggableGizmo）
- `oakengine_renderer_create/free`（Renderer/OpenGLRenderer/DynamicRenderer）
ManagedColor(4) 在 colorprocessorhandle 一带。
AudioProcessor(5) 已经 P5 C vtable 消除（原“不用消”裁决被 R6 推翻）。

### F6（续）：长尾（~28）

1-2 符号的类逐个过，多为 static_cast 或构造调用，facade 已有创建
函数的直接换。重点：NodeValue(4)、ManagedColor(4)、VideoParams(3)、
UndoCommand(3)。

## 5. 验收（R6 已完成，100% C ABI）

1. ✅ oak-editor `U _ZN5olive` = **0**（无豁免）。
2. ✅ oak-render-worker = **0**。
3. ✅ 全量构建 0 error；全量 ctest 45/45 绿。
4. ✅ 反作弊审计：app 无 `dlfcn.h`/dlsym/QLibrary 解析 engine 符号；
   engine 无 inline 化（oakengine/*.h 纯 C 声明）。
5. ✅ `facade-migration-roadmap.md` 附 C R6 节已记录；
   `plans/riir.md` §1.1 状态已更新为"边界已纯"。

> 已知遗留（已论证，不泄漏符号）：app 仍 include 约 40 个 engine C++ 头
> （node/render/timeline/undo/pluginSupport，用于类型与 inline 访问器），
> nm=0 证明不产生符号引用；彻底清理超出 R6 的 58 符号目标，留待后续批次。

## 6. 工程纪律

- 测试：`cd cmake-build-debug && ctest --output-on-failure -j$(nproc)`。
- flaky 判定：`oak_cli_transcode`、`oakengine_export_test`、
  `olive-gtest` 失败单独重跑一次；连续两次失败才算回归。
  `olive-gtest` 单跑：`./tests/gtest/olive-gtest --gtest_filter=...`。
- 提交：每步立即提交，标题写 nm 实测数。
- 自查清单（前任们的错误模式）：no-op stub；undo 聚合拆散（用
  undo 分组！）；事件订阅泄漏（id 丢弃、缺析构解绑——userdata 是
  `this` 的裸订阅必须在析构 unsubscribe）；`sender()` 误用（bridge
  迁移后 sender 是 bridge 不是 engine 对象，信号参数里有 source）；
  时间单位（秒 vs 帧戳）；track 索引 0/1 基；buf/size 两段式
  （先 NULL 查长度再分配，XML 类无上限内容禁止定长缓冲）；
  搬运函数时丢语义（clamp、默认值、错误码路径）。

## 7. 文档地图

- 本文件：当前状态与剩余工作（以此为准）。
- `r5-phase3-final-guide.md`：终局计划（F 批次定义、验收细则）。
- `c-abi-migration-handoff-v5.md`：K2.7 交接（undo 分组契约由来、
  DS 提交处置记录，背景参考）。
- `facade-migration-roadmap.md`：批次记录（每批完成后补记）。
- `c-abi-migration-handoff.md`（v3）：§6.4 豁免清单格式。
