# Timeline 类完整覆盖映射表（C++ oaktimeline → oaktimeline Rust crate）

> 逐类盘点 `src/timeline/src/*.h`。每一行标注 Rust 侧的落点：
> `common`/`marker`/`workarea`/`util` = 对应 domain 模块，
> `undo*` = 对应 undo 命令模块，`bridge` = 经 C ABI 出模块，
> `drop` = 刻意不迁移（附理由）。`// CPP-PARITY` 注释义务不变。
> 命令类统一在结构体上暴露 `prepare()/redo()/undo()`（todo!()），
> 经 `bridge::undo` 的 vtable 对外暴露（`to_command()` 工厂）。

## 1. Timeline 命名空间（timelinecommon.h）

| C++ | Rust 落点 |
|---|---|
| `Timeline::MovementMode`（k_none/k_move/k_trim_in/k_trim_out） | `common::MovementMode`（与 include/timeline/edit.h 的 `OAKTIMELINE_MOVEMENT_*` 值兼容） |
| `Timeline::ThumbnailMode` | `common::ThumbnailMode`（与 include/timeline/displaymode.h 的 `OAK_TIMELINE_THUMBNAIL_*` 值兼容） |
| `Timeline::WaveformMode` | `common::WaveformMode`（与 `OAK_TIMELINE_WAVEFORMS_*` 值兼容） |
| `Timeline::is_a_trim_mode` | `common::is_a_trim_mode` |
| `Timeline::EditToInfo` | `common::EditToInfo` |
| `PLAYHEAD_COLOR` | `drop`（UI 取色宏，facade/app 职责，不含核心逻辑） |

## 2. Marker（timelinemarker.h）

| C++ | Rust 落点 |
|---|---|
| `TimelineMarker`（time/name/color/parent，de-Qt） | `marker::TimelineMarker` |
| `TimelineMarker::time()/set_time()` | `marker` 查询/编辑（set_time 触发 list resort，CPP-PARITY） |
| `TimelineMarker::has_sibling_at_time` | `marker` |
| `TimelineMarker::name()/set_name()` / `color()/set_color()` | `marker` |
| `TimelineMarker::load()/save()` | `marker`（golden XML，见 tests） |
| `TimelineMarkerList`（`markers_`，按时间排序） | `marker::TimelineMarkerList` |
| `empty/size/at/back/front` | `marker` 查询 |
| `add_marker`（排序插入）/ `remove_marker` / `get_marker_at_time` / `get_closest_marker_to_time` / `resort` | `marker`（resort 由 set_time 调用） |
| `MarkerAddCommand` / `MarkerRemoveCommand` / `MarkerChangeColorCommand` / `MarkerChangeNameCommand` / `MarkerChangeTimeCommand` | `marker` 5 个命令结构体，`to_command()` 经 vtable 暴露 |

## 3. WorkArea（timelineworkarea.h + timelineundoworkarea.h）

| C++ | Rust 落点 |
|---|---|
| `TimelineWorkArea`（enabled + range） | `workarea::TimelineWorkArea` |
| `enabled()/set_enabled()` / `in()/out()/length()/range()/set_range()` | `workarea` |
| `k_reset_in` / `k_reset_out` | `workarea::RESET_IN` / `RESET_OUT`（对应 `oaktimeline_workarea_reset`） |
| `load()/save()` | `workarea`（golden XML，见 tests） |
| `WorkareaSetEnabledCommand` / `WorkareaSetRangeCommand` | `workarea` 2 个命令结构体 |

## 4. Undo 通用助手（timelineundocommon.h）

| C++ | Rust 落点 |
|---|---|
| `node_can_be_removed(Node/Block)` | `undocommon::node_can_be_removed` |
| `create_remove_command(Node/Block)` | `undocommon::create_remove_command` |
| `create_and_run_remove_command(Node/Block)` | `undocommon::create_and_run_remove_command` |
| `free_command_handle` | `undocommon::free_command_handle` |
| `CHandleCommandWrapper` | 已删除（M14 R5）：wrapper 是旧 C ABI 时代的产物；命令现在直接以 `UndoCommand` 值存在，经 `box_command` 装箱 |

## 5. Track 命令（timelineundotrack.h）

| C++ | Rust 落点 |
|---|---|
| `TrackRippleRemoveBlockCommand` | `undotrack` |
| `TrackPrependBlockCommand` | `undotrack` |
| `TrackInsertBlockAfterCommand` | `undotrack` |
| `TrackReplaceBlockCommand` | `undotrack` |

## 6. 通用命令（timelineundogeneral.h）

| C++ | Rust 落点 |
|---|---|
| `BlockResizeCommand` | `undogeneral` |
| `BlockResizeWithMediaInCommand` | `undogeneral` |
| `BlockSetMediaInCommand` | `undogeneral` |
| `TimelineAddTrackCommand`（含 `run_immediately`） | `undogeneral` |
| `TimelineRemoveTrackCommand` | `undogeneral` |
| `TransitionRemoveCommand` | `undogeneral` |
| `TrackReplaceBlockWithGapCommand` | `undogeneral` |
| `BlockEnableDisableCommand` | `undogeneral` |
| `TrackListInsertGaps` | `undogeneral` |
| `TimelineAddDefaultTransitionCommand` | `undogeneral` |

## 7. 指针/滑动/放置命令（timelineundopointer.h）

| C++ | Rust 落点 |
|---|---|
| `BlockTrimCommand`（含 `set_trim_is_a_roll_edit` / `set_remove_zero_length_from_graph`） | `undopointer` |
| `TrackSlideCommand` | `undopointer` |
| `TrackPlaceBlockCommand` | `undopointer` |

## 8. Ripple 命令（timelineundoripple.h）

| C++ | Rust 落点 |
|---|---|
| `TrackRippleRemoveAreaCommand`（含 `get_insertion_index` / `get_spliced_block` / `set_allow_splitting_gaps`） | `undoripple` |
| `TrackListRippleRemoveAreaCommand` | `undoripple` |
| `TimelineRippleRemoveAreaCommand`（MultiUndoCommand） | `undoripple` |
| `TrackListRippleToolCommand`（`RippleInfo`/`WorkingData`） | `undoripple` |
| `TimelineRippleDeleteGapsAtRegionsCommand`（`has_commands`） | `undoripple` |

## 9. Split 命令（timelineundosplit.h）

| C++ | Rust 落点 |
|---|---|
| `BlockSplitCommand`（`new_block()`） | `undosplit` |
| `BlockSplitPreservingLinksCommand`（`get_split`） | `undosplit` |
| `TrackSplitAtTimeCommand` | `undosplit` |

## 10. 工具助手（timelineutil.h）

| C++ | Rust 落点 |
|---|---|
| `rat_nd` | `util::rat_nd` |
| `same_block/same_track/same_node` | `util` |
| `BlockHandleLess/TrackHandleLess` | `util` |
| `free_detached_handle` | `util`（取回 ownership 再释放） |
| `block_in/out/length` / `block_set_length_and_media_out/in` | `util` |
| `track_length` / `block_previous/next/track` | `util` |
| `track_project` / `block_add_to_graph` / `block_remove_from_graph` | `util` |

## 11. 刻意不迁移（drop） / 通过 C ABI（bridge）

| 项 | 理由 |
|---|---|
| `oaknode_c_api::to_native` / `oakundo_capi::make_command_handle`（C++ 内部助手） | 不复制；Rust 侧把 handle 当 opaque，命令经 vtable、裸指针经 bridge |
| `Timeline::PLAYHEAD_COLOR` | UI 取色宏，归 facade/app |
| 全部 `*_internal` 私有辅助 / `MemoryManager` 语义 | 实现细节，重组于各模块内部；共享对象经 `Arc<Mutex<…>>` 表达，`CHandle` 只剩 facade 边界 |
