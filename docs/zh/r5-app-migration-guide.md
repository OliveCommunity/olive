# R5 app 侧调用点迁移 — 实施指引

> 本文是 R5 阶段（消灭 oak-editor 对 `olive::` C++ 符号的引用）的执行手册。
> 面向没有此前对话记忆的执行者，自包含。
> 与 `c-abi-migration-handoff.md`（v3）、`c-abi-migration-handoff-v4.md` 的关系：
> 那两份管 facade（C API）建设；本文管 app 侧把对 engine C++ 类的直接调用
> 换成 facade 调用。**facade 已就位且全绿，R5 不需要再新建 C API 族。**
>
> **工作分支：`c-abi-migration`。每完成一个文件/小步立即提交。**

---

## 0. 当前已验证状态（接手先复核，不要采信转述）

```bash
cmake --build cmake-build-debug -j$(nproc)          # 必须 0 error
cd cmake-build-debug && ctest --output-on-failure -j$(nproc)   # 必须 44/44 绿
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"   # 当前 395
```

- 测试：**44/44 全绿**（R1–R4 facade 扩容已修复并通过）。
- 符号：**395**（`nm` 实测，按类统计见 §3）。
- facade 族已全部存在且带测试：node / timeline / project / footage / preview /
  audio / color / config / disk / encoding / events / gizmo / lut / plugin /
  proxy / serializer / sync / task / traverse / undo / videoparams / viewer /
  worker / playback / renderer / app。
- 事件 ID 已分配到 143，新事件从 **144** 起。

## 1. 关于"必须全文件集群重写、无法增量"——论断不成立

DS 报告称 R5 的符号"因 Q_OBJECT + 虚函数链 + 信号/槽耦合，必须全文件集群
同时重写，无法增量"。**这是错误的**，证据：

1. **前一次战役就是增量完成的**：同一套符号从 557 → 162，按 B1–B11a 分批、
   每批独立闭环。被回滚抹掉的是 app 调用点，不是方法。
2. **本阶段已经证明可增量**：符号已从回滚后的高点降到 395，全部是逐文件
   替换得来的，没有一次"集群重写"。
3. **facade 已就位**：R5 需要的 C API 族全部存在。剩下的工作是把 app 里的
   `olive::X::method()` 调用换成 `oakengine_x_method()`，**不重写任何 engine
   类**，自然不存在"虚函数链重写"。
4. **"信号/槽耦合"已被事件机制解决**：`oakengine_event_subscribe` +
   `app/engineeventbridge.{h,cpp}` 就是替代 `connect(engineObj, &Engine::sig, ...)`
   的标准通道（SOP 见 roadmap 附 D）。不需要为消信号而重写类。

结论：**R5 是机械的、可逐文件验证的调用点替换，按 §4 的顺序增量推进。**
"集群重写"是停工的借口，不是技术结论。

## 2. 增量方法（每个文件的标准动作）

对每一个 app 文件，按这个顺序做，做完立即提交：

1. **grep 该文件的 engine 直接引用**：`olive::X::`、`connect(` 到 engine 对象、
   `new SomeCommand(`（engine 命令类）、engine 头 include。
2. **逐点替换**为 facade：
   - 方法调用 → 对应 `oakengine_*` 函数（§4 表）。
   - `connect(engineObj, &Engine::sig, this, ...)` →
     `bridge->subscribe(handle, OAKENGINE_EVENT_*)` + 连 bridge 的 Qt 信号
     （没有 bridge 成员就 `new EngineEventBridge(this)`）。
   - `new EngineCommand(...)` 进 undo 栈 → facade 的 undoable 原语
     （多数已有；确实缺的按 §5.2 补 facade，不要新建 app 侧命令类）。
   - undo 压栈一律 `oakengine_undo_push(command, name)`（**2 个参数**，全局栈，
     不传栈句柄；`Core::undo_stack()` 返回的 `void*` 只作事件订阅 handle）。
3. **删不再需要的 engine 头 include**；加对应 `oakengine/*.h`。
4. **构建 + 该文件相关测试 + `nm` 双度量**，符号应净减。
5. **立即提交**，提交信息写清文件与消掉的符号数。

**禁做**：为消符号新建 app 侧 engine 命令子类、给 facade 加"stub 实现"
（见 §6 红线）、为省事把 engine 源码再编进 app。

## 3. 剩余符号分布（395，nm 实测，按类）

执行顺序按"依赖最少、符号最密集、facade 最现成"排。每行：`数量 类 — 主战场文件 — 用哪个 facade 族`。

### 第一批：工具/服务类（快、独立，先清 ~60）

| 数量 | 类 | 主战场 | facade 族 |
|---|---|---|---|
| 9 | QtUtils | 各 widget | 纯搬 app（B10 模式，`app/common/`，hidden visibility，见 §6-R1） |
| 9 | EncodingParams | dialog/export/*、speedduration | `oakengine/encoding.h`（已含 OakEngineEncodingParams 全字段） |
| 7 | ExportFormat | dialog/export/*、sequence 对话框 | `oakengine/encoding.h`（format/codec 元数据） |
| 11 | AudioManager | viewer、core、preferencesaudiotab | `oakengine/audio.h` + 事件 140 |
| 7 | PreviewAutoCacher / 7 RenderTicketWatcher / 5 RenderTicket / 3 RenderManager | viewer.cpp、timeruler | `oakengine/preview.h`（cacher + OakEnginePreviewRequest） |
| 7 | plugin | pluginSupport | `oakengine/plugin.h` + 事件 |
| 5 | ProxyManager | proxydialog、projectexplorer、timelinewidget | `oakengine/proxy.h` |
| 5 | ProjectSerializer | keyframeview、seekablewidget、timelinewidget、nodeview、nodeparamview、main | `oakengine/serializer.h`（OakEngineClipboard） |
| 5 | FileFunctions / 4 ColorCoding / 4 qHash / 2 Html / 1 xml_read / 1 debug_handler / 2 operator<<>> | 各 widget | 纯搬 app（B10 模式） |
| 3 | LUTLibrary | colordialog、nodeparamviewwidgetbridge、preferencesluttab | `oakengine/lut.h` |
| 2 | Config | preferences、mainmenu、core | `oakengine/config.h` + `app/common/configwrapper.h` |

### 第二批：项目/素材/序列数据类（~90）

| 数量 | 类 | 主战场 | facade 族 |
|---|---|---|---|
| 10 | Project / 7 Folder / 10 Footage / 5 Sequence / 5 TrackList | projectexplorer、projectproperties、footageproperties、projectviewmodel | `oakengine/project.h` + `footage.h` + `timeline.h`（folder 族、`oakengine_folder_move_child`） |
| 17 | EngineCore | core.cpp、mainwindow、各 panel | `oakengine/app.h`（`Core` 已组合转发） |
| 9 | ViewerOutput | viewer、footageviewer、各 panel | `oakengine/viewer.h`（workarea、playhead、params） |
| 15 | ColorManager / 5 ColorProcessor / 1 ManagedColor | manageddisplay、colordialog、colorbutton、scopebase、colorvalueswidget、projectproperties | `oakengine/color.h` + `app/widget/manageddisplay/colorprocessorhandle.h` |
| 9 | NodeGroup / 6 MultiCamNode | nodeview、multicam 面板 | `oakengine/node.h`（group passthrough、multicam 族） |

### 第三批：时间线/标记/命令类（~70）

| 数量 | 类 | 主战场 | facade 族 |
|---|---|---|---|
| 13 | Track / 12 ClipBlock / 8 TimelineWorkArea / 6 TimelineMarker | timelinewidget、timeruler、seekablewidget、trackview、timelineview | `oakengine/timeline.h`（track/clip/marker/workarea 全族） |
| 6 | Task / 6 NodeTraverser | taskview、export、viewer、nodeparamview | `oakengine/task.h`、`traverse.h` |
| 5+ | UndoStack / 各 Marker/Node 命令类（MarkerAdd/ChangeColor/ChangeName/ChangeTime/Remove、NodeAdd/EdgeAdd/EdgeRemove/Rename/OverrideColor/ParamSetStandardValue、FolderAddChild、TimelineAddTrack、TrackListRippleToolCommand） | historywidget、nodeview、timelinewidget、nodeparamview、projectviewmodel | `oakengine/undo.h` + 各 undoable 原语；**不要**新建 app 侧命令类（`TrackListRippleToolCommand` 是遗留评估点，最后单独定） |

### 第四批：Node 大族（~55）

| 数量 | 类 | 主战场 | facade 族 |
|---|---|---|---|
| 40 | Node / 7 NodeKeyframe / 3 NodeFactory | nodeview、nodeparamview、curvewidget、keyframeview、nodetableview、nodevaluetree、multicam、nodecombobox | `oakengine/node.h`（~60 函数：输入元数据、值读写、关键帧、dragger、undoable 批量、context、group、multicam） |
| 7 | TextGizmo / 3 DraggableGizmo | viewerdisplay | `oakengine/gizmo.h`（POD） |
| 各 1–2 | CrossDissolveTransition / SubtitleBlock / TransitionBlock / VolumeNode / TransformDistortNode / SolidGenerator / TextGeneratorV3 / ShapeNode | timeline 工具、nodeview | `oakengine_node_create_undoable` + input id getter（B4c 模式） |

### 第五批：GPU/帧路径（~15，R6 收口）

| 数量 | 类 | 主战场 | facade 族 |
|---|---|---|---|
| 5 Frame / 3 Renderer / 2 OpenGLRenderer / 1 DynamicRenderer / 2 Texture / 1 RenderManager | viewerdisplay、manageddisplay | `oakengine/renderer.h`（texture/frame 句柄，R6 已完成 B7 桥移除） |

## 4. 每批闭环（不可省）

```
nm 基线 → 逐文件替换（§2）→ 构建 0 error → 全量 ctest 44/44 绿
→ nm 双度量（族符号 + 总数净减）→ 立即提交 → roadmap 附 C 补记
```

**全量 ctest 不绿不得进入下一批。** 已知 flaky（`oak_cli_transcode`、
`oakengine_export_test`、`olive-gtest` 偶发 SEGFAULT）单独重跑两次仍败才算真失败。

## 5. 缺的 facade 怎么办

绝大多数调用点已被现有族覆盖。确实缺的时候：

1. **先查**：该功能是否已被某族覆盖（grep `oakengine_*` 头）。多数"缺"是没找到现成函数。
2. **能搬 app 的纯工具**（Qt 类型、纯函数、纯数据）→ 搬 `app/common/`，
   对该源文件加 `-fvisibility=hidden`（§6-R1），不新增 C API。
3. **必须跨边界的** → 最小 facade 族（只包 app 实际用到的成员），
   头文件规则同现有族（纯 C 类型、buf/size、owned/borrowed 注释、错误码）。
   **新 C 函数必须配单元测试**（注册 `make_oakengine_test`）。
4. **undoable 编辑** → 照 `engine/src/capi/node.cpp` 的 `push_or_run` 模式；
   用户语义上的单次操作必须单条 undo（`oakengine_folder_move_child` 是样板）。

## 6. 红线（本阶段修过的真实 bug，不得再犯）

- **R1（ODR/符号介入）**：app 侧严禁用与 engine 相同限定名定义非 inline 符号。
  确需同名本地副本，必须对该源文件 `-fvisibility=hidden`（`app/CMakeLists.txt`
  有样板；`#pragma GCC visibility` 对已被 engine 头以 default 声明过的符号无效）。
- **R2（禁 no-op stub）**：facade 函数不许返回假成功（`oakengine_export_render_
  with_params` 曾是 stub；`oakengine_clip_set_media_in` 曾是直接写不可撤销）。
  不可撤销的改图操作就是 bug——`set_media_in` 不入栈导致 `project_undo` 误删 clip。
- **R3（undo 语义）**：`oakengine_undo_push(command, name)` 只 2 参。删除任何
  `push` 必须同步接上命令执行路径（命令不压栈 = 静默不执行 + 泄漏）。
- **R4（单位与索引）**：facade 的 `time_ts` 是**秒**（toggle/has/closest/dragger/
  get_input_at_time 等）；keyframe 的 `track`/`track_for_time` 是 **1-based**，
  `set_*_many` 的 tracks 是 **0-based**；序列/clip 的 ts 用 `timestamp_to_time`
  换算，不许硬编码 `/30`。
- **R5（POD 构造）**：`VideoParams` 用带参构造（四参 w/h/format/channels，depth=1；
  默认构造 depth=0 会让 Vulkan 上传 0 字节纯黑）；`Rational` 分子是 **32 位 int**，
  哨兵值用 `INT_MAX`（`RATIONAL_MAX`），不许 `INT64_MAX`（溢出成负数）。
- **R6（engine 语义边界）**：`Track::is_range_free` 排除 GapBlock；probe 句柄
  （`oakengine_footage_probe`）不带项目节点，import-only 族必须返回 E_INVALID；
  `oakengine_sequence_add_sequence_clip` 必须查间接循环嵌套（上游依赖图含目标
  序列即拒绝），否则真成环导致 `invalidate_cache` 数万帧递归栈溢出。
- **R7（buf/size 约定）**：`string_to_buf` 传 NULL 也返回长度；返回 `>0` 的
  长度查询不得对 NULL buf 特判返回 0（`group_add_input_passthrough` 曾犯）。
- **R8（接手验证）**：任何交接后先全量构建 + 全量 ctest + nm 复核，再动手；
  不采信上一手的完成声明（包括本文 §0，以你实测为准）。

## 7. 里程碑

1. 第一批（工具/服务）清零 → 总数应跌破 ~330。
2. 第二批（项目/素材/序列）清零 → ~240。
3. 第三批（时间线/命令）清零 → ~170。
4. 第四批（Node 大族）清零 → ~115。
5. 第五批（GPU）+ B11c/B11d 收口 → 只剩豁免清单（AudioProcessor 4 +
   Block/Track::staticMetaObject = 6）→ `nm -D --defined-only liboakengine.so
   | grep -c " T _Z"` = 0 → 全量终验。
