# R5 终局计划：181 → 豁免清单（≤6）

> 面向执行者（DeepSeek Flash），自包含。工作分支：`c-abi-migration`。
> 前置文档：`facade-migration-roadmap.md`（批次记录）、
> `r5-app-migration-guide.md`（R5 总指引）、`c-abi-migration-handoff.md`
> （v3，§6.4 豁免清单格式）。本文是 R5 的**最后一个阶段**：
> 处置当前 WIP → 修完已记录缺陷 → 把剩余 181 个 `olive::` 符号收到豁免清单。
> 每批闭环：全量构建 0 error + 全量 ctest 绿 + nm 复核 + 立即提交。
>
> **状态**：第 0 批（§3）与第 1 批（§4）已由 Kimi 完成。期间新增两处
> 计划外修复：ProjectViewModel 的 `sender()` 崩溃（bridge 迁移后槽函数
> 仍用 `sender()` 取 Folder，拿到的是 bridge 指针，段错误）与
> `oakengine_folder_move_child` 语义修正（原来只加不删，"移动"后节点
> 同时存在于两个文件夹；已改为 删旧+加新，并新增批量版
> `oakengine_folder_move_children` 供拖放一次移动多项）。
> 剩余：批 F1-F6（§5）。

---

## 1. 现状（2026-07-24 实测，GLM-5.2 交接）

```
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"   # 88
```

GLM-5.2 从 131 消除至 88（-43）。已完成：F3（Task/TimelineWorkArea/
ViewerOutput/Project 全部）、F4 方法调用（14 符号 + 7 新 facade 函数）、
F6 部分长尾（7 构造器 + 5 静态字符串）。

剩余 88 符号分布：

| 簇 | 数 | 说明 |
|---|---|---|
| Node 信号+staticMetaObject | 26 | 22 信号 + staticMetaObject + link + set_standard_value + set_value_at_time |
| AudioProcessor | 5 | 豁免候选（实时音频回调边界） |
| plugin::PluginProgressReporter | 4 | 豁免候选（MOC 四件套） |
| 渲染族 | ~25 | Renderer/PlaybackCache/Frame/DynamicRenderer/DraggableGizmo/OpenGLRenderer/Texture/ColorProcessor/AudioWaveformSync/AudioSynchronizer 等 |
| 长尾 | ~28 | NodeValue/ManagedColor/VideoParams/UndoCommand 等 1-4 符号类 |

**下一任主攻**：Node 信号连接迁移（23 符号，事件 ID 70-95 已全部分配，
EngineEventBridge 信号已存在，需为 9 个类添加 bridge 成员）。

## 2. 红线（新增两条，违反即返工）

R5 既有红线不变：不改 `oakengine_*` 已发布签名、不删测试、不用
`git checkout --`/`restore`/`clean`/`reset --hard`/`stash`、每批立即提交。
新增：

1. **禁止 inline 化 engine 实现刷符号**。把 engine 的 `.cpp` 实现搬进头文件
   （如本次 WIP 对 `ManagedColor`、`TimelineWorkArea` 做的那样）不会让
   依赖消失，只是让 app 内联编译 engine 代码——C ABI 边界被架空，nm 数字
   是假的。判定方法：engine 目录下的任何 `.h` 出现新的非平凡函数体即违规。
2. **禁止 no-op stub**。`load()` 返回 true、`save()` 空体、`// apply ...`
   空循环这类"假成功"是最严重违规（本次 WIP 的
   `TimelineWorkArea::load/save` 即为例：项目文件的工作区会全部丢失）。
   任何行为变更必须在提交信息里写明并接受 review。
3. **禁止 dlsym/GetProcAddress 等运行时解析 engine C++ 符号**（DS 在
   `app/common/nodefactorywrapper.cpp` 用过，nm 统计不到 ≠ 依赖不存在）。
   facade 只能在 `engine/src/capi/` 实现、`engine/include/oakengine/`
   声明。详见 `c-abi-migration-handoff-v5.md` §2。

## 3. 第 0 批：当前 WIP 处置（最先做，单独一个提交）

工作区现有 DS 停手时留下的未提交改动，分两类：

### 3.1 回退（engine 被改坏的部分，4 个文件 + 2 个 CMake）

用 `git show HEAD:<path> > <path>` 恢复（**禁止** `git checkout --`）：

- `engine/render/managedcolor.h`、`engine/render/managedcolor.cpp`、
  `engine/render/CMakeLists.txt`（inline 化，红线 1）
- `engine/timeline/timelineworkarea.h`、`engine/timeline/timelineworkarea.cpp`、
  `engine/timeline/CMakeLists.txt`（inline 化 + load/save stub，红线 1+2）

`TimelineWorkArea` 的 3 个符号（enabled/range 信号与 staticMetaObject）
按 §5 批 F4 的正路处理。

### 3.2 保留并修复（app 侧 ProjectSerializer → clipboard facade 迁移，方向正确）

涉及：`timelinewidget.{h,cpp}`、`nodeparamview.{h,cpp}`、`nodeview.cpp`、
`keyframeview.cpp`、`resizabletimelinescrollbar.{h,cpp}`。修三处：

1. `nodeview.cpp::copy_selected`：`char buf[65536]` 固定缓冲会截断大节点图
   的 XML。改两段式：先 `oakengine_clipboard_save_to_xml(cb, nullptr, 0)`
   取所需长度，再 `QByteArray(len+1, '\0')` 分配写入。全仓库同类
   buf/size 调用（`oakengine_project_filename` 等 512/256 定长）一并不再
   扩大，仅本处改两段式（XML 体积无上限，文件名有）。
2. `resizabletimelinescrollbar.cpp::connect_work_area`：已改为
   `oakengine_event_subscribe`（方向对，解决了直连 engine 信号），但裸
   订阅的 userdata 是 `this`，**必须在析构里
   `oakengine_event_unsubscribe` 两个 id**，否则 widget 先死、workarea
   后发事件即悬垂回调。
3. `nodeparamview.cpp::paste` 里的空循环 `// apply position from map`：
   原代码同样是建了 `PositionMap` 未使用（上游 Olive 遗留死代码），不算
   回归，但既然碰了就删掉这个死块，别留占位注释。

闭环后提交（提交信息注明：clipboard 迁移 + 三处修复 + engine 回退）。

## 4. 第 1 批：已记录缺陷修复（review 累积清单，决策已写死）

按序修，一个提交；每项都给出现象与定死的修法：

1. **NodeParamView::DeleteSelected 重连静默失败**（严重）。
   现码先 `oakengine_node_connect` 后 `oakengine_nodes_delete_many`，
   而契约规定输入已占用时 connect 返回 `E_STATE`——重连必然失败。
   修法：facade 新增
   `int oakengine_nodes_delete_many_ex(nodes, contexts, node_count, edge_outputs, edge_input_nodes, edge_input_ids, edge_input_elements, edge_count, reconnect_outputs, reconnect_input_nodes, reconnect_input_ids, reconnect_input_elements, reconnect_count)`
   ——engine 内部在**同一条** `NodeViewDeleteCommand` 里先删后连
   （redo 序：delete → reconnect），undo 序反向。NodeParamView 只收集
   重连边传入，不自己 connect。`oakengine_nodes_delete_many` 保留，
   等价于 `_ex` 传 reconnect_count=0。
2. **ProjectViewModel 切项目丢事件**（严重）。`set_project` 重建
   `bridge_` 后没重连 4 个 folder 信号。修法：把构造里的 4 个
   `connect(bridge_, ...)` 抽成私有 `connect_bridge_signals()`，
   构造函数与 `set_project` 重建后都调。
3. **NodeParamViewWidgetBridge dragger 泄漏**。类无析构，
   `oakengine_dragger_create` 的句柄永不 free。修法：加析构调
   `oakengine_dragger_free(dragger_)`。
4. **TimeBasedWidget 旧订阅泄漏**。`connect_viewer_node` 只
   `disconnect(bridge_, nullptr, this, nullptr)`，engine 侧订阅不释放。
   修法：`EngineEventBridge` 加 `unsubscribe_all()`（对
   `subscriptions_` 逐个 unsubscribe 并清空），在 disconnect 旁调用。
5. **边-only 删除 undo 拆分**。`NodeView::delete_selected` 纯边分支逐边
   `oakengine_node_disconnect_ex`，N 条边 N 条 undo。修法：放宽
   `oakengine_nodes_delete_many` 契约允许 `node_count==0 && edge_count>0`
   （纯边删除，报错条件改为两者同时为 0），边-only 分支改走 delete_many。
6. **seekablewidget marker 订阅泄漏**。`set_markers` 建 3 个订阅
   （ADDED/REMOVED/MODIFIED）只存 1 个 id。修法：
   `QVector<int64_t> marker_subs_` 存全 3 个，重设/析构全解（对齐
   `resizabletimelinescrollbar.cpp::connect_markers` 的正确模式）。
7. **core_params 0x1 陷阱 + core.h 死代码**。`app/core.cpp:1509`
   `Core::core_params()` 解引用 `0x1`；`app/core.h:337` 残留
   `EngineCore *engine_core_` + `#include "coreengine.h"` + 5 个空操作
   handler setter。修法：全删（core_params 无调用方，直接删方法）。
8. **小项打包**：
   - `projectviewmodel.cpp::connect_item` 死参数 `subscribe`（无 false
     调用点）——删参数。
   - `vieweroutpututils.h:50` 死声明
     `viewer_output_video_params_from_oak`——删。
   - `curveview.cpp` 两处 `(type == 0) ? 1 : 0` 魔法数——facade 加
     `int oakengine_keyframe_opposing_bezier_type(int type)`，调用替换；
     `(opposing_type == 0) ? 0 : 1` 恒等式一并简化。
   - `export.cpp` `image_sequence_check_box_changed` 的裸 `{ }` 块缩进
     乱——clang-format 归位。

## 5. 第 2+ 批：符号收尾（按簇，难度从低到高）

每批做法相同：grep 定位引用源 → 按既有模式迁移（facade 函数 /
`cliphandle.h` 式 app 侧 inline 适配头 / CustomUndoCommand 回调 /
EngineEventBridge 订阅）→ 闭环提交。禁止走 §2 两条红线的捷径。

- **批 F1 撤销命令族（~30，25 个类）**：多数已有 facade 等价物直接换
  （`NodeEdgeAddCommand`→`oakengine_node_connect`，
  `NodeEdgeRemoveCommand`→`oakengine_node_disconnect_ex`，Marker 五命令
  →`oakengine_marker_*` 族）。无等价物的按 `a86cb6c99` 的
  `oakengine_undo_command_create` 回调模式迁移。`TrackListRippleToolCommand`
  按 v3 §176 行处理：先尝试现有 timeline 原语组合，不行设计
  `oakengine_tracklist_ripple_*` 小族，再不行写理由进豁免清单。
- **批 F2 Track/ClipBlock/NodeGroup/NodeKeyframe（30）**：属性访问走
  `trackhandle.h`/`cliphandle.h` 模式扩展；信号走 EngineEventBridge。
- **批 F3 Task/Project/ViewerOutput/VideoParams（18）**：剩余多为
  `VideoParams` 构造重载（vieweroutpututils 已收口一半）与 Task 信号
  （已有 task 事件族，照 taskviewitem 先例）。
- **批 F4 Node 大族（39，最难，放靠后）**：逐个符号 grep 定位。预期构成：
  `qobject_cast<Node*>`（改 `oakengine_node_type_id` 比较 + static_cast）、
  `staticMetaObject`（改字符串式 connect 或事件订阅，消不掉按 §6.4 豁免）、
  inline 方法拉的 vtable/typeinfo（把调用点换 facade）。
  `TimelineWorkArea`(3)、`UndoCommand`(3)、`Block`(3)、`NodeGroup` 残余
  与本批同法。
- **批 F5 渲染族（~25）**：Renderer/PlaybackCache/DynamicRenderer/
  DraggableGizmo/OpenGLRenderer/Texture/Frame/ColorProcessor/
  AudioWaveformSync/AudioSynchronizer。多数在 viewerdisplay、
  manageddisplay、audiomonitor——playback/preview facade 族已存在
  （B9a-B9c），先查 `oakengine/playback.h`、`preview.h`、`renderer.h`
  有无现成函数。AudioProcessor(5) 目标压到 4 后整体进豁免清单
  （实时回调边界，v3 已预批）。
- **批 F6 长尾（~35）**：1-symbol 类逐个过。`*Task`(3)、
  `RenderManager`(2)、`Sequence`(2)、`TimelineMarker`(2) 等，多为
  static_cast 或构造调用，facade 已有创建函数的直接换。

## 6. 验收（全部满足才算 R5 完成）

1. `nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"` ≤ 6，
   且每个剩余符号都在 `c-abi-migration-handoff.md` §6.4 豁免清单里、
   各带一句理由。
2. `nm -D` 检查 `oak-render-worker` 为 0。
3. 全量构建 0 error；全量 `ctest --output-on-failure` 绿（已知 flaky
   规则：单独重跑一次，连续两次失败才算回归）。
4. 反作弊审计：
   - `git diff <R5起点>..HEAD --stat -- engine/` 逐文件过一遍，确认无
     inline 化、无 stub（§2 两条红线的全量复核）；
   - grep engine 头文件无新增非平凡函数体。
5. 更新 `facade-migration-roadmap.md` 批次记录与
   `c-abi-migration-handoff.md` 状态节；本文标注"已完成"。

## 7. 协作分工

- 第 0/1 批（WIP 处置 + 缺陷修复）由 **Kimi** 执行——这些是语义陷阱，
  需要逐行判断。
- 批 F1-F6 由 **DeepSeek** 执行，Kimi 每批只读 review（不构建不运行），
  记录问题，批间统一修。
- 任何"消不掉"的符号：先写清尝试过的方案，再按 §6.4 格式进豁免清单，
  由 Kimi 复核理由是否成立。
