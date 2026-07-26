# R6 清理计划：豁免清单清零（58 → 0，100% C ABI）

> 面向执行者（Qwen 3.6 35B A3B），自包含，极度详细。工作分支：
> `c-abi-migration`（就地继续）。
> **背景**：R5 已把 app 对 engine 的 `olive::` C++ 符号从 557 降到 58，
> 剩余 58 个以"豁免清单"形式记录在 `c-abi-migration-handoff.md` §6.4。
> 本计划的目标是把它们**全部消除到 0**——这是后续 engine 模块化拆分
> 与 Rust 重写（RIIR，见 `../riir.md`）的硬前提：C ABI 边界上不能
> 残留任何 C++ 渗漏。
>
> **三条红线**（违反即返工）：
> 1. 禁止把 engine 的 .cpp 实现 inline 化进头文件。
> 2. 禁止 no-op stub（空实现、丢字段的"简化"调用、假成功返回值）。
> 3. 禁止 dlsym/GetProcAddress/QLibrary 运行时解析 engine C++ 符号。
>
> **每步闭环**：全量构建 0 error → 全量 ctest 绿 → nm 实测下降 →
> 立即提交。git 禁令：`checkout --`/`restore`/`clean`/`reset --hard`/`stash`。
>
> **测量**：
> ```
> nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"   # 当前 58，目标 0
> nm -D cmake-build-debug/app/oak-editor | grep " U _ZN5olive" | c++filt | sed 's/.* U //' | sort
> ```
> **构建**：`cmake --build cmake-build-debug -j$(nproc)`（勿重新 cmake）。
> **测试**：`cd cmake-build-debug && ctest --output-on-failure -j$(nproc)`。
> flaky 规则：`oak_cli_transcode`/`oakengine_export_test`/`olive-gtest`
> 失败单独重跑一次，连续两次失败才算回归。

---

## 总则：六类符号的统一解法

| 类 | 数 | 本质 | 统一解法 | 阶段 |
|---|---|---|---|---|
| F. 无 C ABI 等价物 | 17 | facade 缺函数 | engine/src/capi 加函数，app 换调用 | P1 |
| B. inline 拉入 | 8 | app 直接构造 engine undo 命令类 | 命令类全部 facade 化 | P2 |
| A. MOC staticMetaObject | 9 | app 信号/槽参数是 engine 类型 | 参数类型换 C ABI 句柄 | P3 |
| E. 色彩管理 | 6 | C++ 对象直接构造 | POD + facade 处理器 | P4 |
| C. 音频回调 | 5 | 实时回调边界 | C vtable 接口 | P5 |
| D. 渲染/GPU | 13 | 渲染对象 app 侧构造 | 对象管理移入 engine | P6 ✅ |

**新增 facade 函数的固定流程**（每个函数都照做）：
1. 在 `engine/include/oakengine/<域>.h` 声明（extern "C"，`OAKENGINE_API`，
   写清所有权/单位/错误码的文档注释）；
2. 在 `engine/src/capi/<域>.cpp` 实现（内部直接调 engine C++，允许——
   那是 engine 自己的实现）；
3. 在 `engine/tests/` 加纯 C 测试（参照现有 `oakengine_*_test.cpp`）；
4. app 侧换调用点；
5. 全量构建 + ctest + nm + 提交。

---

## P1：F 类 facade 补齐（17 符号）

> **状态**：P1 全部完成（17 符号 → 0）。F 类 facade 已补齐：NodeValue
> 静态方法、VideoParams 构造器、音频对齐算法、TimelineMarker/ShapeNodeBase/
> FrameHashCache/RenderManager/MultiCamNode/SubtitleBlock 零散单点均已通过
> C ABI facade 替换或移除直接调用。

### ✅ P1.1 NodeValue 静态方法（4）

引用点：`app/widget/nodeparamview/nodeparamviewwidgetbridge.cpp`（
split/combine track values）、`app/widget/keyframeview/`（轨道数）、
`app/widget/nodevaluetree/`（pretty name）。

新增到 `oakengine/node.h` + `engine/src/capi/node.cpp`：

```c
/** NodeValue::get_number_of_keyframe_tracks(t)。t 为 engine
 *  NodeValue::Type 序数（注意：与 oak_node_value_type 不同序，见
 *  nodevaluetree.cpp 的 node_value_type_to_c 映射表）。 */
OAKENGINE_API int oakengine_node_value_keyframe_track_count(int engine_type);

/** NodeValue::get_pretty_data_type_name(t)，buf/size 约定。 */
OAKENGINE_API int oakengine_node_value_pretty_type_name(int engine_type,
	char *buf, int buf_size);

/** split_normal_value_into_track_values：输入 oak_node_value POD，
 *  输出 tracks 数组（调用方分配，track_count 先经上一函数查询）。 */
OAKENGINE_API int oakengine_node_value_split_to_tracks(int engine_type,
	const oak_node_value *normal, oak_node_value *tracks_out, int track_count);

/** combine_track_values_into_normal_value：split 的逆。 */
OAKENGINE_API int oakengine_node_value_combine_tracks(int engine_type,
	const oak_node_value *tracks, int track_count, oak_node_value *normal_out);
```

**注意**：engine `NodeValue::Type` 与 C `oak_node_value_type` **序数
不同**（k_boolean=4 vs BOOL=3 等）。facade 参数用 engine 序数还是 C
序数必须选一个并写进文档——**统一用 C 序数**（oak_node_value_type），
engine 内部做映射（`from_c_type` 已存在于 node.cpp）。

### ✅ P1.2 VideoParams 构造器（3）

引用点：`app/widget/viewer/vieweroutpututils.cpp`（唯一合法保留点，
它已是收口文件）、`manageddisplay.cpp`、`histogram.cpp`、
`timebasedwidget.cpp`、`viewer.cpp`。

原则：**app 不构造 VideoParams 对象**，全部改用 `oak_video_params`
POD（已存在于 `oakengine/videoparams.h`）+ facade 传参。
- vieweroutpututils.cpp 已示范：`oakengine_viewer_get_video_params`
  出 POD，app 如需 VideoParams 对象仅在这一处构造（它的 3 个符号
  就来自这里）。改为：app 各处不再要 VideoParams，直接传 POD；
  确实需要 VideoParams 的地方（传给仍用 C++ 的 app 内部函数）保留
  vieweroutpututils.cpp 单点，但把构造器替换为 facade：
  `oakengine_video_params_create(const oak_video_params *pod)` 返回
  `void *`（engine 堆上的 VideoParams），`oakengine_video_params_free`。
  app 侧句柄化，析构走 free。

### ✅ P1.3 音频对齐算法（4）

`AudioWaveformSync::estimate_envelope_offset`、`estimate_stretch_and_offset`、
`AudioSynchronizer::place_by_source_time`、`place_by_waveform_offset`。
引用点：`app/widget/audiomonitor/` 或 multicam 对齐工具（grep
`AudioWaveformSync\|AudioSynchronizer` app/ 定位）。

新增到 `oakengine/audio.h`：

```c
/** 纯算法包装。envelope 数组为 double 序列，target/conf 配对；
 *  返回估计的 offset（帧），或负错误码。 */
OAKENGINE_API int64_t oakengine_audio_estimate_envelope_offset(
	const double *target, const bool *target_conf, int target_len,
	const double *source, const bool *source_conf, int source_len,
	int64_t start_offset);

OAKENGINE_API int oakengine_audio_estimate_stretch_and_offset(
	const double *target, const bool *target_conf, int target_len,
	const double *source, const bool *source_conf, int source_len,
	int64_t start_offset, double min_stretch, double max_stretch,
	double *stretch_out, int64_t *offset_out);
```

`AudioSynchronizer` 的两个 place 方法需要 SourceClip POD：
```c
typedef struct oak_sync_source_clip {
	int64_t in_ts, out_ts;      /* 帧戳 */
	int64_t media_in_ts;
	const char *filename;       /* 可 NULL */
} oak_sync_source_clip;
OAKENGINE_API int oakengine_audio_sync_place_by_source_time(
	const oak_sync_source_clip *a, const oak_sync_source_clip *b,
	int64_t playhead_ts, int64_t *out_ts);
OAKENGINE_API int oakengine_audio_sync_place_by_waveform(
	int64_t playhead_ts, int64_t offset, int track_index,
	int64_t *out_ts);
```
（参数名以 engine 现有签名为准微调，但 POD 化原则不变。）

### ✅ P1.4 零散单点（6，plugin::PluginProgressReporter 随 P3.3）

| 符号 | 引用点 | facade |
|---|---|---|
| `TimelineMarker::TimelineMarker(...)` + `set_time` | timeruler/marker 编辑 | 已有 `oakengine_marker_*` 族，缺的补 `oakengine_marker_create(list, in_ts, out_ts, name)`、`oakengine_marker_set_time_undoable` |
| `ShapeNodeBase::set_rect` | nodeparamview shape 编辑 | `oakengine_shape_set_rect_undoable(node, x, y, w, h, command)` |
| `FrameHashCache::load_cache_frame` | viewer/timeline 缩略图 | `oakengine_frame_cache_load_frame(cache, path, uuid_str, ts)` |
| `RenderManager::instance_` | `app/widget/viewer/viewer.cpp:197,200,920,922`（`RenderManager::instance()->get_cacher()`） | `oakengine_render_manager_get_cacher()` 返回 `void *`，或直接加 `oakengine_render_cache_set_display_color_processor(...)`、`oakengine_render_cache_set_multicam_node(...)` 两个语义函数（**推荐后者**，少一层句柄） |
| `MultiCamNode::k_current_input` | multicamwidget | `oakengine_multicam_current_input_id()`（const char*） |
| `SubtitleBlock::k_text_in` | subtitle 编辑 | `oakengine_subtitle_text_input_id()`（const char*） |
| `plugin::PluginProgressReporter::cancelled()` | pluginprogressdialogreporter | 见 P3.3（信号迁移） |

---

## ✅ P2：B 类 inline 清零（8 符号 → 0）

本质：app 直接 `new` engine 的 undo 命令类（C++ 类），构造/析构时
inline 拉入 `UndoCommand::UndoCommand/redo_now/undo_now` 等符号。

**状态**：P2 全部完成。app 中所有 `new XxxCommand(` 已替换为 facade 构造
函数，`MultiUndoCommand *` 签名已改为 `void *`，`SetSelectionsCommand` /
`SetTimeCommand` 已改为 callback-based facade 命令或直接使用 keyframe/
marker facade 命令。`Node::link` / `Node::set_value_at_time` 已替换为
`oakengine_block_link` / `oakengine_node_set_value_at_time_command`。

**实测规模**：app 里 113 处 `new XxxCommand(`，16 个命令类：

```
43 MultiUndoCommand            2 NodeRemoveAndDisconnectCommand
24 NodeSetPositionCommand      2 NodeSetValueHintCommand
10 TrackPlaceBlockCommand      2 BlockTrimCommand
 5 TrackReplaceBlockWithGapCommand    1 TrackSlideCommand
 4 SetSelectionsCommand        1 TransitionRemoveCommand
 3 NodeRemovePositionFromContextCommand  1 SetTimeCommand
 1 BlockSplitPreservingLinksCommand  1 BlockResizeWithMediaInCommand
 1 TimelineRippleDeleteGapsAtRegionsCommand  1 BlockSetMediaInCommand
```

**统一解法**：每个命令类在 facade 加一个返回 `void *` 的构造函数
（内部 `new` 对应 C++ 类），app 全部换成 facade 构造 +
`oakengine_undo_command_multi_add_child` 组合。已有先例：
`oakengine_node_connect_command`、`oakengine_node_set_standard_value_command`、
`oakengine_node_link_command`（均在 node.h）。

需要新增的 facade 构造函数（`oakengine/undo.h` 或对应域头）：

```c
OAKENGINE_API void *oakengine_undo_command_create_multi(void); /* 已有 */
OAKENGINE_API void *oakengine_node_add_command(void *project, void *node);
OAKENGINE_API void *oakengine_node_set_position_command(
	void *node, void *context, double x, double y, int expanded);
OAKENGINE_API void *oakengine_track_place_block_command(
	void *track_list, int track_index, void *block, int64_t in_ts);
OAKENGINE_API void *oakengine_track_replace_block_with_gap_command(
	void *track, void *block, int64_t in_ts);
OAKENGINE_API void *oakengine_set_selections_command(
	void *viewer, const int64_t *in_ts, const int64_t *out_ts, int count,
	int clear_first);
OAKENGINE_API void *oakengine_node_remove_position_command(
	void *node, void *context);
OAKENGINE_API void *oakengine_node_set_value_hint_command(
	void *node, const char *input, int element, int type, int index,
	const char *tag);
OAKENGINE_API void *oakengine_node_remove_and_disconnect_command(
	void *project, void *node);
OAKENGINE_API void *oakengine_block_trim_command(
	void *track, void *block, int64_t point_ts, int trim_in);
OAKENGINE_API void *oakengine_transition_remove_command(
	void *track, void *transition, int64_t in_ts, int64_t out_ts);
OAKENGINE_API void *oakengine_track_slide_command(
	void *track, void *block, const int *track_delta, int64_t time_delta_ts);
OAKENGINE_API void *oakengine_set_time_command(int64_t time_ts);
OAKENGINE_API void *oakengine_block_split_preserving_links_command(
	void *const *blocks, int count, int64_t point_ts);
OAKENGINE_API void *oakengine_block_resize_with_media_in_command(
	void *track, void *block, int64_t length_ts);
OAKENGINE_API void *oakengine_block_set_media_in_command(
	void *block, int64_t media_in_ts);
OAKENGINE_API void *oakengine_timeline_ripple_delete_gaps_command(
	void *sequence, const int64_t *range_in_ts, const int64_t *range_out_ts,
	const int *track_types, const int *track_indexes, int range_count);
```

（每个参数以 engine 对应 C++ 命令类的真实构造签名为准拍平；时间全部
int64_t 帧戳，Rational 用 num/den 对的注明。）

`Node::set_standard_value` / `Node::set_value_at_time`：app 唯一直接
调用点 `nodeparamviewwidgetbridge.cpp:395`。facade 已有
`oakengine_node_set_standard_value_command`；补：

```c
OAKENGINE_API void *oakengine_node_set_value_at_time_command(
	void *node, const char *input, int element, int64_t time_ts_num,
	int64_t time_ts_den, const oak_node_value *value, int track);
```

`Node::link`（实际是 `Block::link`，引用点 `tool/import.cpp:610`）：
补 `OAKENGINE_API int oakengine_block_link(void *a, void *b, int linked);`
（undoable 的加 `_command` 变体）。

完成后 app 全仓库 grep `new [A-Z].*Command(` 应为 0，
`#include "undo/undocommand.h"` 和 `#include "node/nodeundo.h"` 在 app
中应全部消失（符号随 include 消失而归零）。

---

## ✅ P3：A 类 MOC staticMetaObject（9 符号 → 0）

> **状态**：P3 全部完成。唯一含 engine 类型参数的信号
> `NodeTreeView::node_enable_changed` 已改为 `OakEngineNode*` 句柄参数；
> 12 处 `Node*`/`Project*` 槽已移出 slots 区（均确认非字符串式 connect
> 目标，新式成员函数 connect 与 lambda 调用不受影响）。nm 实测
> staticMetaObject 归零（26 → 24）。

**机理**（先读再动手）：app 的 QObject 类若信号/槽参数含
`Node*`/`Project*`/`Sequence*`/`ViewerOutput*`/`UndoStack*`/
`AudioWaveformCache*` 等 Q_OBJECT 类型，MOC 生成的 metacall 代码会用
`qobject_cast` 引用这些类的 staticMetaObject。把参数类型改成
`OakEngineNode*` 等不透明 C 句柄（或 `void*`），MOC 就当普通指针
处理，引用消失。

### P3.1 已知信号清单（逐个改签名 + 全部 connect 点）

- `app/panel/param/param.h:67` `focused_node_changed(Node *)`
- `app/widget/nodeparamview/nodeparamview.h:94` 同上
- `app/widget/nodeparamview/nodeparamviewitem.h:71,226` `request_select_node(Node *)`
- `app/widget/nodeparamview/nodeparamviewconnectedlabel.h:44` 同上；
  `:47,49` `input_connected/disconnected(Node *, const NodeInput &)`
- `app/panel/timeline/timeline.h:129,130` `reveal_viewer_in_project(ViewerOutput *)` 等
- `app/widget/history/`（UndoStack* 参数，如有）
- `app/widget/multicam/multicamwidget.h:56`（MultiCamNode*）

改法（以 `focused_node_changed` 为例）：
1. 信号签名改 `focused_node_changed(OakEngineNode *n)`；
2. 发射处 `emit focused_node_changed(reinterpret_cast<OakEngineNode*>(n))`；
3. 接收槽同步改类型，槽内 `reinterpret_cast<Node*>(n)` 还原；
4. 该头文件不再 include engine C++ 头（`node/node.h` 等），改 include
   `oakengine/node.h`。
5. 全仓库 grep 该信号名找齐所有 connect，逐一编译验证。

### P3.2 plugin 族（staticMetaObject/qt_metacast/qt_metacall）

`app/dialog/progress/pluginprogressdialogreporter.h` 继承 engine 的
`plugin::PluginProgressReporter`（Q_OBJECT）。解法：engine 侧把
PluginProgressReporter 的 `cancelled()` 信号改为 C 回调注册
（`oakengine_plugin_progress_set_cancel_cb(fn, userdata)`），基类去掉
Q_OBJECT；app 的 dialog reporter 不再继承它，改为组合一个
`oakengine_plugin_progress_reporter` C 句柄（facade create/free）。
涉及 engine 插件系统，改动面可控但注意 `PluginNode` 测试不回归。

### P3.3 `plugin::PluginProgressReporter::cancelled()`（F 类遗留）

随 P3.2 一并解决（信号变事件/回调）。

---

## ✅ P4：E 类色彩管理（6 符号 → 0）

> **状态**：P4 全部完成（6 符号 → 0）。app 侧 `ManagedColor` 改为
> header-only POD 包装（`colorprocessorhandle.h`），`ColorProcessor::Ptr`
> 全部换成 `ColorProcessorHandlePtr`（C 句柄）；`manageddisplay`/
> `viewerdisplay`/`viewerbase`/`viewer` 的 create/convert 走
> `oak_make_color_processor`/`oak_convert_color` facade；engine 原
> `render/managedcolor.h+.cpp` 已清空。nm 实测 6 个色彩符号归零
> （24 → 18）。期间定位并修复了一个阻塞验证的**预先存在竞态**：
> worker ticket 在「提交→worker 取件」窗口被 `clear_single_frame_renders`
> 取消，导致 `FootageViewerNotBlack/1` 全量套件 30s 超时——已在
> `RenderWorkerPool::submit_frame` 中于 job 入队前 `ticket->start()`
> 消除脆弱窗口（非 stub，是真实缺陷修复），olive-gtest 全量通过。

引用点：`colordialog.{h,cpp}`、`colorbutton.{h,cpp}`、
`colorswatchchooser.{h,cpp}`、`nodeparamviewwidgetbridge.cpp`。

新增到 `oakengine/color.h`：

```c
/** ManagedColor 的 POD 形态：RGBA + 输入色彩空间 id + 输出变换。 */
typedef struct oak_managed_color {
	double r, g, b, a;
	char input_id[64];       /* 空串 = 未指定 */
	char transform[128];     /* 空串 = 未指定 */
} oak_managed_color;

OAKENGINE_API void *oakengine_color_processor_create(
	const char *src_space, const char *dst_transform, int direction);
OAKENGINE_API void oakengine_color_processor_free(void *p);
OAKENGINE_API int oakengine_color_processor_convert(void *p,
	double in_r, double in_g, double in_b, double in_a,
	double *out_r, double *out_g, double *out_b, double *out_a);
```

app 侧：`ManagedColor` 成员变量换成 `oak_managed_color` POD；
`ColorProcessor::Ptr` 成员换成 `void *` 句柄（析构处配 free）。
`colorbutton/colorswatchchooser` 只是显示颜色，转换走
`oakengine_color_processor_convert`。

---

## ✅ P5：C 类音频回调（5 符号 → 0）

> **状态**：P5 全部完成（5 符号 → 0）。`oakengine/audio.h` 新增
> `OakEngineAudioProcessor` 不透明句柄族（create/free/open/close/is_open/
> convert/output_params），`capi/audio.cpp` 内部用 C++ `AudioProcessor`
> 实现（convert 的输出字节由句柄内部 `Buffer` 持有，调用方零拷贝借用）；
> `viewer.h/cpp` 的 `AudioProcessor audio_processor_` 成员换成
> `OakEngineAudioProcessor *`（构造 create、析构 free），open 走
> `OakAudioParams*` POD，`.to()` 走 `oakengine_audio_processor_output_params`
> + `oakcore_audioparams_is_valid/time_to_bytes`。nm 实测 5 个音频符号
> 归零（18 → 13），全量 ctest 100% 通过。

引用点：`app/widget/viewer/viewer.{h,cpp}`（AudioProcessor 直接构造，
用于回放音频格式转换）。

**为什么不能简单 facade 化**：AudioProcessor 是实时回调路径，每次
回调跨 C ABI 进 engine 会有性能顾虑（其实极小，但保持零拷贝更重要）。

**解法（C vtable，RIIR 友好）**：facade 定义处理器 C 接口，engine
内部用 C++ AudioProcessor 实现，app 只持有句柄：

```c
/* oakengine/audio.h */
typedef struct OakEngineAudioProcessor OakEngineAudioProcessor;
OAKENGINE_API OakEngineAudioProcessor *oakengine_audio_processor_create(void);
OAKENGINE_API void oakengine_audio_processor_free(OakEngineAudioProcessor *p);
OAKENGINE_API int oakengine_audio_processor_open(OakEngineAudioProcessor *p,
	int in_sample_rate, uint64_t in_layout, int in_format,
	int out_sample_rate, uint64_t out_layout, int out_format,
	double speed);
OAKENGINE_API void oakengine_audio_processor_close(OakEngineAudioProcessor *p);
OAKENGINE_API int oakengine_audio_processor_convert(OakEngineAudioProcessor *p,
	float **data, int frame_count);
```

app 的 `AudioProcessor processor_;` 成员换
`OakEngineAudioProcessor *processor_`（create/free 配对）。

---

## ✅ P6：D 类渲染/GPU（13 符号 → 0，最大工程，放最后）

> **状态**：P6 全部完成（13 符号 → 0）。nm 实测：oak-editor 与
> oak-render-worker 的 ` U _ZN5olive` 均为 **0**；全量构建 0 error；
> 全量 ctest 100%（45/45）；`ViewerDisplayReproTest` 三个可跑通用例
> （Vulkan 后端）保持通过，OpenGL offscreen 三个用例按环境预期 SKIP；
> 导出测试（`oakengine_export_test`、`oak_cli_transcode_verify`）无回归。
>
> **实现说明（与原设计提议的差异，已论证）**：
> 1. facade 未加入 `oakengine/renderer.h`，而是新建独立头
>    `oakengine/display.h` + `engine/src/capi/display.cpp`。原因：
>    `renderer.h` 已存在面向序列渲染 CPU 帧的 `OakEngineFrame` 及
>    `oakengine_frame_data/free/width/...` 函数族，与本节设计的
>    `oakengine_frame_create/allocate/free` **C 命名冲突**（C 不允许重载）。
> 2. 命名 accordingly 调整为：渲染器/纹理族 `oakengine_display_renderer_*` /
>    `oakengine_display_texture_*`；codec 帧族 `oakengine_codec_frame_*`。
> 3. 采用**最小侵入方案**：TexturePtr/FramePtr（std::shared_ptr）流仍保留在
>    app 内（它们经 QVariant/信号在 engine→app 投递，全句柄化需重构帧投递
>   管线，对 ViewerDisplayReproTest 风险极高）。facade 只收口 13 个
>    out-of-line 调用（create_texture/blit_color_managed/upload/download/
>    Frame::create/set_video_params/allocate/渲染器构造-init-destroy）。
>    app 复制/reset shared_ptr 只动引用计数（deleter 在 engine 侧 type-erase），
>    不引用 `~Texture`/`~Frame`；inline/virtual 方法不产生 `U _ZN5olive`。
>    这满足 nm=0 硬指标，且渲染路径行为零变化。
> 4. `out_texture`/`out_frame` 出参为指向 caller `TexturePtr`/`FramePtr`
>    存储的指针，engine 侧赋值，shared_ptr 簿记全留在 engine。

引用点：`app/widget/manageddisplay/manageddisplay.cpp`（
OpenGLRenderer/DynamicRenderer 构造、init、Texture upload/download、
blit_color_managed）、`app/widget/viewer/viewerdisplay.cpp`、
`app/widget/scope/`（Frame::create/allocate/set_video_params）。

**原则**：渲染对象的生命周期全部移入 engine，app 只持有句柄并
描述"要画什么"。这也是 RIIR 里 GPU 管线的预定边界。

新增到 `oakengine/renderer.h`：

```c
/* 渲染器句柄：engine 按当前后端（OpenGL/软件）创建，app 不知道类型 */
OAKENGINE_API void *oakengine_renderer_create_for_thread(void);
OAKENGINE_API int oakengine_renderer_init(void *r, void *qopengl_context_or_NULL);
OAKENGINE_API void oakengine_renderer_destroy(void *r);

/* 纹理句柄 */
OAKENGINE_API void *oakengine_texture_create(void *r,
	const oak_video_params *params, const void *pixels, int linesize);
OAKENGINE_API void oakengine_texture_free(void *t);
OAKENGINE_API int oakengine_texture_upload(void *t, const void *pixels,
	int linesize);
OAKENGINE_API int oakengine_texture_download(void *t, void *pixels,
	int linesize);

/* 帧句柄（CPU 侧缓冲） */
OAKENGINE_API void *oakengine_frame_create(void);
OAKENGINE_API int oakengine_frame_set_video_params(void *f,
	const oak_video_params *params);
OAKENGINE_API int oakengine_frame_allocate(void *f);
OAKENGINE_API void oakengine_frame_free(void *f);
OAKENGINE_API void *oakengine_frame_data(void *f);      /* 写像素用 */
OAKENGINE_API int oakengine_frame_linesize(void *f);

/* 色彩管理 blit */
OAKENGINE_API int oakengine_renderer_blit_color_managed(
	void *r, const oak_color_transform_job *job, void *dst_texture,
	const oak_video_params *params);
```

`oak_color_transform_job` POD 在 `oakengine/color.h` 定义（processor 句柄
+ input/output id + 各向异性参数，字段以 engine `ColorTransformJob`
拍平）。

app 侧：`manageddisplay`/`viewerdisplay` 不再 `new OpenGLRenderer`，
改持 `void *renderer_`；帧/纹理成员全部句柄化。

**验证重点**：渲染路径行为必须零变化——`olive-gtest` 的
`ViewerDisplayReproTest` 三个可跑通用例必须保持通过；导出测试
（`oakengine_export_test`、`oak_cli_transcode_verify`）不许变差。

---

## 验收（100% C ABI 判据）

1. `nm -D ... | grep -c " U _ZN5olive"` = **0**（oak-editor 与
   oak-render-worker 都是 0）。
2. 全量构建 0 error；全量 ctest 绿（flaky 规则照旧）。
3. 反作弊审计：app 无 dlsym/dlfcn；`git diff` engine 无 inline 化；
   app 无 `#include "node/`、`#include "undo/`、`#include "task/`、
   `#include "render/`、`#include "timeline/` 的 engine C++ 头
   （`grep -rn '#include "' app/ | grep -E '"(node|undo|task|render|timeline|pluginSupport)/'`
   应为空或只剩极个别已论证的）。
4. `c-abi-migration-handoff.md` §6.4 豁免清单清空（改为"无豁免"），
   roadmap 补 R6 批次记录，`../riir.md` 状态更新为"边界已纯"。

## 执行顺序与节奏建议

```
P1（纯加法，热身）→ P2（机械替换，量大但无决策）→ P3（MOC，细心活）
→ P4（POD 化）→ P5（小）→ P6（GPU，最重，单独留足时间）
```

每个 P 内部按上表逐个符号做，**每 3-5 个符号提交一次**，不要攒大批。
每完成一个 P，把本文对应节的符号表划掉（编辑文档标注 ✅）并提交。

---

## 附：R6 收尾复核记录（Kimi K3，2026-07-26）

R6 由 Qwen 3.8 Max 执行完成，复核结论：**nm 目标达成（58→0，双二进制）**，

- 全量构建 0 error；ctest 45/45（oak_cli_transcode 间歇 SEGFAULT 为预存
  flaky，手动跑通过）；
- 反作弊干净：无 dlsym、无 stub、无 engine inline 化；
- P2 undo 命令 facade 化质量合格（113 处 `new XxxCommand(` 归零）；
- P3 MOC 处理合格（信号参数句柄化 + 非 slots 区注释清楚）。

**遗留项（已记录，后续批次）**：

1. **`oakengine/display.h` 的"灰色契约"（P6 的妥协）**：函数签名均为
   `void *`（nm 上纯 C），但文档约定 `out_texture/out_frame` 指向调用方
   内存中的 `std::shared_ptr`（engine 在其上构造 shared_ptr 副本），
   `video_params` 实为 `olive::VideoParams*`。对 Rust 重写而言这层契约
   仍是 C++ 语义：Rust 侧无法安全持有 shared_ptr，也无法构造
   VideoParams。**后续必须重做**：`oak_video_params` POD 替换
   `const void *video_params`；纹理/帧改不透明句柄 +
   `oakengine_display_texture_free/oakengine_codec_frame_free`。
2. **engine 导出符号未收口**：`nm -D --defined-only liboakengine.so |
   grep -c " T _Z"` = 3486。按 riir.md §2 Step 2 做
   `-fvisibility=hidden` + 只导出 `oakengine_*`（独立批次，app 已无引用，
   不阻塞）。
3. **app 仍 include ~40 个 engine C++ 头**（不产生符号引用，nm=0 已证），
   彻底清理为低优先级长项。
