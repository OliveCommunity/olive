# M4 · oaktimeline 拆分手册

> 内容：`engine/timeline/`（TimelineMarker/TimelineMarkerList/
> TimelineWorkArea/timelinecommon/timeline undo 命令族：
> timelineundogeneral/timelineundopointer/timelineundoripple/
> timelineundosplit/timelineundotrack/timelineundoworkarea）。
> 依赖：node 32（重）、common 4、config 2、undo 1、core 3。
> 拆分顺序第 4 位。

## 1. 目标形态

```
oaktimeline/
  include/oaktimeline/{marker.h, workarea.h, edit.h, types.h, export.h}
  src/
  tests/  # oaktimeline_gtest
```

timeline 的 undo 命令类**不出模块**（01 §5）：消费侧经
`edit.h` 的语义函数或 `_command` 工厂拿基类 `OakUndoCommand *`
（oakundo 句柄）组进自己的 MultiUndoCommand。

## 2. 冻结 C API

### 2.1 `oaktimeline/marker.h`

```c
typedef struct OakTimelineMarkerList OakTimelineMarkerList;
/* list 句柄为 borrowed（属 Sequence/Clip 所有），不需要 free */
OAKTL_API OakTimelineMarkerList *oaktimeline_marker_list_of(
	OakNodeNode *owner /* sequence 或 clip 句柄 */);

OAKTL_API int oaktimeline_marker_count(const OakTimelineMarkerList *l);
OAKTL_API int oaktimeline_marker_at(const OakTimelineMarkerList *l, int i,
	int64_t *in_ts, int64_t *out_ts, int *color, char *name_buf, int n);
OAKTL_API int oaktimeline_marker_add(OakTimelineMarkerList *l,
	int64_t in_ts, int64_t out_ts, const char *name, int color,
	OakUndoCommand *command);    /* command=NULL 时自行入栈（2026-08：void* → 有类型句柄） */
OAKTL_API int oaktimeline_marker_remove_at(OakTimelineMarkerList *l,
	int i, OakUndoCommand *command);
OAKTL_API int oaktimeline_marker_set_time(OakTimelineMarkerList *l,
	int i, int64_t in_ts, int64_t out_ts, OakUndoCommand *command);
OAKTL_API int oaktimeline_marker_set_props(OakTimelineMarkerList *l,
	int i, int color, const char *name, OakUndoCommand *command);
/* 无事件接口（2026-08 修订，04 §3）：marker 的增删改都是调用方发的
 * 命令，MARKER_ADDED/REMOVED/MODIFIED 通知由调用方所在层（facade，
 * id 沿用 oakengine events 段）在命令后发出。 */
```

### 2.2 `oaktimeline/workarea.h`

```c
typedef struct OakTimelineWorkarea OakTimelineWorkarea;
OAKTL_API int oaktimeline_workarea_get(const OakTimelineWorkarea *w,
	int64_t *in_ts, int64_t *out_ts, int *enabled);
OAKTL_API int oaktimeline_workarea_set_range(OakTimelineWorkarea *w,
	int64_t in_ts, int64_t out_ts);                    /* live */
OAKTL_API int oaktimeline_workarea_set_range_undoable(
	OakTimelineWorkarea *w, int64_t in_ts, int64_t out_ts,
	int64_t old_in_ts, int64_t old_out_ts, OakUndoCommand *command);
OAKTL_API int oaktimeline_workarea_set_enabled_undoable(
	OakTimelineWorkarea *w, int enabled, OakUndoCommand *command);
OAKTL_API void oaktimeline_workarea_reset(int64_t *in_ts, int64_t *out_ts);
/* load/save 经 oakcommon_xml 句柄在 oaknode 序列化路径调用 */
OAKTL_API int oaktimeline_workarea_load(OakTimelineWorkarea *w,
	OakCommonXmlReader *r);
OAKTL_API int oaktimeline_workarea_save(const OakTimelineWorkarea *w,
	OakCommonXmlWriter *x);
```

### 2.3 `oaktimeline/edit.h`（timeline undo 命令族的语义入口）

照 `oakengine/timeline.h` 的 timeline 编辑原语逐一对齐（那本来就是
这一族的 facade 版，签名模板直接搬）：

```c
OAKTL_API void *oaktimeline_add_track_command(OakNodeTrackList *list);
OAKTL_API void *oaktimeline_remove_track_command(OakNodeTrackList *list,
	int index);
OAKTL_API void *oaktimeline_place_block_command(OakNodeTrackList *list,
	int track_index, OakNodeBlock *block, int64_t in_ts);
OAKTL_API void *oaktimeline_replace_block_with_gap_command(
	OakNodeTrack *track, OakNodeBlock *block, int64_t in_ts);
OAKTL_API void *oaktimeline_trim_command(OakNodeTrack *track,
	OakNodeBlock *block, int64_t point_ts, int trim_in);
OAKTL_API void *oaktimeline_split_command(OakNodeBlock *const *blocks,
	int count, int64_t point_ts);   /* preserving links 变体加 _links */
OAKTL_API void *oaktimeline_ripple_delete_gaps_command(
	OakNodeSequence *seq, const int64_t *in_ts, const int64_t *out_ts,
	const int *track_types, const int *track_indexes, int range_count);
OAKTL_API void *oaktimeline_slide_command(OakNodeTrack *track,
	OakNodeBlock *block, int track_delta, int64_t time_delta_ts);
OAKTL_API int64_t oaktimeline_nearest_block_ts(OakNodeTrack *track,
	int64_t ts, int direction /* -1/0/1 */);
```

## 3. 切割点（timeline → node 32 次）

全部经 oaknode C ABI + 适配类（01 §2）：

- `node/output/track/track.h`（5）、`tracklist.h`（3）：timeline 内部
  对 Track 的操作换 `OakNodeTrack*` 适配。
- `node/block/gap/gap.h`（4）、`transition.h`（4）：Gap/Transition 的
  构造换 `oaknode_factory_create_from_id`。
- `node/project.h`（3）：Project 适配类。

## 4. 测试（映射 03 §2/§3）

- marker：增删改查、undo 往返（push 后 undo 恢复）；每命令后读
  count/at 断言生效（无事件——通知在 facade 层测）。
- workarea：set/get、undoable 旧值恢复、reset 哨兵、xml 往返。
- edit：每个 `_command` 工厂 1 个"构造→入栈→undo 还原"用例
  （track 增删、place/replace/trim/split/ripple/slide）。
- `oaktimeline_debug_alive_count()` 泄漏断言。
