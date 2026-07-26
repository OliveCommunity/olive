# R5 最终冲刺计划：88 → 豁免清单（≤6）

> 面向执行者（GLM-5.2 继续，或任何接手代理）。自包含。
> 基线：`d188ef116` 之后。前置：`c-abi-migration-handoff-v6.md`（状态与
> 规则，全部仍然有效）、`r5-phase3-final-guide.md`（红线和验收）。
> 本文只定义剩余 88 个符号的收尾批次 G1-G4。
> 每批闭环不变：全量构建 0 error → 全量 ctest 绿 → nm 实测 → 立即提交。

---

## 0. 现状（GLM-5.2 R5 冲刺实测）

```
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"   # 58
```

从 88 消除至 58（-30）。G1（Node 信号清零，-22）、G2（渲染族信号 + RenderManager，
-8）已完成。G3（UndoCommand C ABI）已替换直接调用但符号从 inline 拉入。
G4（豁免落实）进行中：58 符号逐条写入 §6.4 豁免清单（6 类理由）。

| 簇 | 数 | 批次 | 状态 |
|---|---|---|---|
| MOC staticMetaObject | 9 | G4 豁免 | app 信号/槽参数类型引用 |
| Inline 函数拉入 | 8 | G4 豁免 | engine 头 inline 方法引用 |
| AudioProcessor | 5 | G4 豁免 | 实时回调边界（v3 预批） |
| 渲染/GPU 边界 | 13 | G4 豁免 | OpenGL 对象直接创建 |
| 色彩管理 | 6 | G4 豁免 | 无 C ABI 等价物 |
| 无 C ABI | 17 | G4 豁免 | 需新增 facade 函数 |

| 簇 | 数 | 批次 |
|---|---|---|
| Node（信号连接为主） | 26 | G1 |
| 渲染族（Renderer/PlaybackCache/Frame/DynamicRenderer/DraggableGizmo/Texture/OpenGLRenderer/ColorProcessor/AudioWaveformSync/AudioSynchronizer） | 25 | G2 |
| 长尾（NodeValue 4、ManagedColor 4、VideoParams 3、UndoCommand 3、TimelineMarker 2、Sequence 2、RenderManager 2、ViewerOutput 1、UndoStack 1、SubtitleBlock 1、ShapeNodeBase 1、Project 1、MultiCamNode 1、FrameHashCache 1、AudioWaveformCache 1） | 28 | G3 |
| 豁免候选（AudioProcessor 5、plugin 4） | 9 | G4 |

## 1. 批次 G1：Node 信号清零（26 符号，主攻）

**作战地图**：53 处 `connect(x, &Node::signal, ...)`，11 个文件
（按此顺序做，从依赖少的开始）：

1. `app/widget/nodeview/nodeviewitem.cpp`、`nodeviewcontext.cpp`、
   `nodeview.cpp`
2. `app/widget/nodeparamview/nodeparamviewitem.cpp`、
   `nodeparamviewarraywidget.cpp`、`nodeparamviewconnectedlabel.cpp`、
   `nodeparamviewkeyframecontrol.cpp`、`nodeparamviewwidgetbridge.cpp`、
   `nodeparamview.cpp`
3. `app/widget/keyframeview/keyframeviewinputconnection.cpp`
4. `app/widget/projectexplorer/projectviewmodel.cpp`（`label_changed` 的
   直连，换 `OAKENGINE_EVENT_NODE_LABEL_CHANGED`）

**做法**（每处相同）：
- 事件 ID 已分配（70-95），`EngineEventBridge` 的 node_* 信号已存在，
  只差在用类里加 `EngineEventBridge *bridge_` 成员、subscribe、
  connect bridge 信号。
- **sender() 陷阱**：bridge 迁移后槽函数里 `sender()` 是 bridge 不是
  Node。信号参数里带 `OakEngineNode *source`，用它。（ProjectViewModel
  段错误就是这个，已修过一次，别再来。）
- **订阅泄漏**：bridge_ 由父对象持有则随父析构自动解绑；裸
  `oakengine_event_subscribe`（userdata=this）必须在析构 unsubscribe。
- **重复连接守卫**：在会多次执行的函数里 `connect(bridge_, ...)` 要
  么加一次性 flag（参照 `seekablewidget.cpp::set_markers` 的
  `marker_connects_done_`），要么在成员初始化时只连一次。
- 消不掉的 `staticMetaObject`（qobject_cast/模板 connect 残留）按
  v3 §6.4 写理由进豁免清单，预期 1-2 项。

**验证**：G1 完成后 Node 应只剩豁免项（staticMetaObject ± link/unlink
的 typeinfo）。`olive-gtest` 里 nodeview/nodeparamview/projectexplorer
相关用例必须全绿。

## 2. 批次 G2：渲染族（25 符号）

逐个 grep 定位，先查现成 facade：`oakengine/playback.h`、`preview.h`、
`renderer.h`、`gizmo.h`、`color.h`。引用点集中在
`app/widget/viewer/viewerdisplay.cpp`、`app/widget/manageddisplay/`、
`app/widget/audiomonitor/`、`app/widget/scope/`。

- `DraggableGizmo`(3)：gizmo facade 已有（B9e），把残留直连换完。
- `ManagedColor`(4)：在 `colorprocessorhandle` 一带，POD 经
  `oakengine/color.h` 传递。
- `Frame`(3)、`Texture`(2)：`FrameHashCache`/`AudioWaveformCache` 的
  句柄化参照 `cliphandle.h` 模式（app 侧 inline 适配头， facade 取数据）。
- 剩下 Renderer/PlaybackCache/DynamicRenderer/OpenGLRenderer/
  ColorProcessor/AudioWaveformSync/AudioSynchronizer：若是
  staticMetaObject/信号，同 G1 处理；若是虚函数链调用，包 facade 函数。

**GPU 边界提醒**：这批不许碰渲染管线的实际行为，只换调用方式。
渲染用例（`ViewerDisplayReproTest` 可跑通的那 3 个）不能变得更差。

## 3. 批次 G3：长尾（28 符号）

逐类处理，多数一处两处：
- `NodeValue`(4)：多是 `NodeValue::Type` 的 typeinfo/静态引用——用
  `oak_node_value_type` 的 C 枚举替换（**注意两套枚举序数不同**，映射
  函数参照 `nodevaluetree.cpp` 的 `node_value_type_to_c`，不要强转）。
- `UndoCommand`(3)：残留的 `UndoCommand*` 类型引用，换 `void*` + facade。
- `VideoParams`(3)、`Sequence`(2)、`Project`(1)、`ViewerOutput`(1)：
  vieweroutpututils 模式收口。
- `TimelineMarker`(2)、`RenderManager`(2)、`UndoStack`(1)、
  `SubtitleBlock`(1)、`ShapeNodeBase`(1)、`MultiCamNode`(1)、
  `FrameHashCache`(1)、`AudioWaveformCache`(1)：grep 定位单点，
  大概率是 static_cast/构造/qobject_cast，直接换 facade。

## 4. 批次 G4：豁免落实 + 终验

1. 把 AudioProcessor(5)、plugin::PluginProgressReporter(4)、
   staticMetaObject 残留（若有）逐条写进
   `c-abi-migration-handoff.md` §6.4 豁免清单（每条一句理由）。
2. 终验（全过才算 R5 完成）：
   - `nm -D` oak-editor ≤ 6 且全在豁免清单；oak-render-worker = 0。
   - 全量构建 0 error；全量 ctest 绿（flaky 规则照旧）。
   - 反作弊：app 无 dlsym/dlfcn；`git diff 476714ada~1..HEAD -- engine/`
     无 inline 化、无 stub；grep 全仓库无 `// simplified`、
     `// NOTE: simplified` 类"语义简化"注释。
3. 更新 `facade-migration-roadmap.md`（G 批次记录）、
   `r5-phase3-final-guide.md` 状态节、handoff §6.4。
4. **R5 完成哨**：向用户报告，由用户宣布 R5 结束——随后
   `plans/gtest-migration-guide.md` 与 `plans/ui-redesign-plan.md`
   解锁（两者互为并行，见各自文档）。

## 5. 给执行者的自查清单（含 GLM 本轮新增教训）

- **语义不可"简化"**：GLM 在 `set_value_hint` 里传 `(0, 0, nullptr)`
  并注释"simplified"——这就是 stub，不管名字叫什么。facade 参数映射
  必须完整（type/index/tag 一个不能丢），映射不了就扩 facade，不许
  丢字段。
- 两套值类型枚举（engine `NodeValue::Type` vs C `oak_node_value_type`）
  **序数不同**，必须显式映射函数，禁止 `int(t)` 强转。
- undo 聚合用 `oakengine_undo_group_*`；单个 facade 调用即一条 undo
  的场景才允许单推。
- 事件订阅：谁 subscribe 谁 unsubscribe（析构或换绑时）；connect
  bridge 信号防重复。
- 时间单位：facade 时间戳是 `int64_t` 帧戳（timebase 转换用
  `Timecode::time_to_timestamp`），秒是 Rational num/den 对，别混。
- 提交信息标题写 nm 实测数。
