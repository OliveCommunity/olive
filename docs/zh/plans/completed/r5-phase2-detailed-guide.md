# R5 第二阶段详细指引（剩余 339 符号）

> 本文是 `r5-app-migration-guide.md` 的续篇，针对当前剩余的 339 个符号给出
> **逐组、逐符号**的替换映射。面向没有此前对话记忆的执行者。
> 工作分支：`c-abi-migration`。每文件/小步立即提交。

---

## 0. 验收结论（2026-07-24 实测）

- 符号：395 → **339**（DS 的 R5 1B–2 批次，质量良好）。
- 构建：绿。测试：**44/44 绿**（`oak_cli_transcode` 一次 SEGFAULT 单独重跑即过，
  属已知 flaky；期间修复 `ExportFormatComboBox` 初值应为 format 总数而非 -1 的
  1 个回归，已提交）。
- 当前状态符合 R5 指引预期，可以按本文继续。

## 1. "剩余符号必须集群重写"——第三次证伪

DS 再次声称剩余符号"都需要文件集群级的全量重写（虚函数链、Q_OBJECT 信号槽、
模板头文件引用）"。**逐符号核对后，每一组都有现成、已测的 facade 函数可直接
替换**（§2 全表）。所谓"Q_OBJECT 信号槽"，由事件机制（
`oakengine_event_subscribe` + `EngineEventBridge`）解决；"虚函数链"从不涉及——
迁移替换的是 **app 侧调用点**，engine 类原样留在 liboakengine 内部；"模板头文件
引用"（`NodeTraverser`、`NodeValueTable`）已由 `oakengine/traverse.h` 覆盖。
**没有一组需要重写。**

判据：如果某符号在 §2 表里能查到 facade 对应物，它就是普通替换，不是重写。

## 2. 逐组符号 → facade 映射

### 2.1 ColorManager（15）——colordialog、colorbutton、colorwheel 系列、projectproperties、colordialog

| 符号 | facade（`oakengine/color.h`） |
|---|---|
| `list_available_colorspaces` | `oakengine_color_manager_colorspace_count/_at` |
| `list_available_displays` | `oakengine_color_manager_display_count/_at` |
| `list_available_views` | `oakengine_color_manager_view_count/_at` |
| `list_available_looks` | `oakengine_color_manager_look_count/_at` |
| `get_compliant_color_space(ColorTransform,bool)` | `oakengine_color_manager_compliant_transform` |
| `get_compliant_color_space(QString)` | `oakengine_color_manager_compliant_color_space` |
| `get_default_display` | `oakengine_color_manager_default_display` |
| `get_default_view` | `oakengine_color_manager_default_view` |
| `set_config_filename` / `get_config_filename` | `oakengine_color_manager_set/get_config_filename` |
| `set_default_input_color_space` | `oakengine_color_manager_set_default_input_color_space` |
| `get_default_config` | `oakengine_color_config_load_default` + `oakengine_color_config_free` |
| `create_config_from_file` | `oakengine_color_config_load_file` + `oakengine_color_config_free` |
| `config_changed` / `reference_space_changed`（信号） | 事件 `OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED`/`_REFERENCE_SPACE_CHANGED`（60/61）+ EngineEventBridge |
| `ColorProcessor::create` | `oakengine_color_processor_create`/`_free`/`_convert_color` |
| `staticMetaObject` | 随信号迁走事件机制后消失 |

`oakengine_color_manager_from_project` 取项目色彩管理器句柄；
`reference_color_space`/`default_luma_coefs` 已有。全部 buf/size 约定。

### 2.2 EngineCore（17，含 Q_OBJECT）——core.cpp、mainwindow、各 panel

| 符号 | facade（`oakengine/app.h`） |
|---|---|
| `staticMetaObject` / `qt_metacall` / `qt_metacast` / `EngineCore(CoreParams)` / `CoreParams()` | app 不再直接引用 `olive::EngineCore` 类型（`Core` 已组合转发），MOC 符号随类型引用消失而消失 |
| `set_active_project` | `oakengine_app_set_active_project` |
| `add_open_project` | `oakengine_app_add_open_project` |
| `remove_recently_opened_project` | `oakengine_app_remove_recent_project` / `oakengine_app_clear_recent_projects` |
| `get_auto_recovery_index_filename` | `oakengine_app_auto_recovery_index_filename` |
| `set_language` | `oakengine_app_set_language` |
| `set_autorecovery_interval` | `oakengine_app_set_autorecovery_interval` |
| `on_project_saved` | `oakengine_app_on_project_saved` |
| `set_close_project_handler` / `set_confirm_image_sequence_handler` / `set_load_layout_handler` / `set_relink_handler` / `set_save_project_handler` | `OakEngineAppCallbacks` + `oakengine_app_set_callbacks`（回调结构一次性注册） |

**MOC 消除范式（Q_OBJECT 通用）**：app 侧不再出现 `EngineCore*`/`ColorManager*`/
`RenderTicketWatcher*` 等 QObject 类型的成员、信号参数或 `qobject_cast`，全部改持
facade 句柄 + `EngineEventBridge` 订阅。类型引用消失 → moc 生成的
`staticMetaObject/qt_metacall/qt_metacast` 符号自动消失。**这就是"Q_OBJECT 信号槽"
的全部解法，不需要动任何类。**

### 2.3 NodeTraverser（6）——nodeparamview、curvewidget、viewerdisplay

| 符号 | facade（`oakengine/traverse.h`） |
|---|---|
| `generate_database` | `oakengine_traverse_generate_database`（返回 `OakEngineTraverseDb*`，配 `oakengine_traverse_db_free` + `db_input_count/id` + `row_count` + `row_*` 访问器） |
| `generate_table` | `oakengine_traverse_generate_table` |
| `generate_row` | `oakengine_traverse_generate_row` |
| `generate_row_value_element_index` | `oakengine_traverse_table_element_index_for_hint` |
| `transform` | `oakengine_traverse_transform` |
| `NodeTraverser()`（构造） | 不再直接构造，全部走上述函数 |

`NodeValueTable`/`NodeValueRow` 的访问经 `oakengine_traverse_db_*` /
`oakengine_traverse_row_*` 访问器完成，模板类型不出边界。

### 2.4 QtUtils（9）——纯函数，搬 app（不是 facade）

`create_horizontal_line`、`create_vertical_line`、`flip_control_and_shift_modifiers`、
`get_formatted_date_time`、`q_font_metrics_width`、`set_combo_box_data(int)`、
`set_combo_box_data(QString)`、`to_q_color`、`word_wrap_string`

全部含 Qt 类型，**按 B10 模式搬到 `app/common/`**（新建 `app/common/qtutilsapp.h`，
inline 函数，namespace `olive` 不变以减少调用点改动；对该源文件
`-fvisibility=hidden`，防 ODR 介入，见 r5 指引 §6-R1）。engine 内部用副本继续用
engine 的 qtutils。

### 2.5 RenderTicketWatcher（7）+ RenderTicket（5）——viewer.cpp、timelinewidget

| 符号 | facade（`oakengine/preview.h`） |
|---|---|
| `RenderTicketWatcher::set_ticket` | `oakengine_preview_request_single_frame` / `oakengine_preview_request_audio_range`（内部即 ticket+watcher 封装） |
| `RenderTicketWatcher::finished`（信号） | `oakengine_preview_request_set_finished_callback`（facade 自有 C 回调，不占事件号） |
| `RenderTicketWatcher::get` / `has_result` | `oakengine_preview_request_get_frame` / `oakengine_preview_request_has_result` |
| `RenderTicketWatcher::cancel` | `oakengine_preview_request_free` |
| `RenderTicketWatcher::RenderTicketWatcher` | 不再直接构造 |
| `RenderTicket` ctor/`start`/`finish`/`get` | 由 request 族内部完成，app 不再接触 |

**红线**：`oak_playback_frame.linesize` 是**字节**；重建 display `Frame` 用四参
`VideoParams(w,h,format,k_internal_channel_count)`（depth=1），防 depth=0 黑屏
（r5 指引 §6-R5）。

### 2.6 Track（13）+ ClipBlock（12）——timelinewidget、trackview、timelineview、seekablewidget

全部走 `oakengine/timeline.h`：track 查询（count/at/type/length/is_range_free）、
clip 输入 id getter、`clip_get_range`/`clip_set_media_in`（undoable）/trim/
ripple/transition/add_footage_clip/add_sequence_clip。`ClipBlock::ClipBlock()` 等
ctor → `oakengine_node_create_undoable` / `oakengine_sequence_add_footage_clip`。

### 2.7 Node（40）+ NodeGroup（9）+ NodeKeyframe（7）——nodeview、nodeparamview、curvewidget、keyframeview、nodetableview、nodevaluetree、multicam、nodecombobox

最大一组，全部走 `oakengine/node.h`（~60 函数）：
- 输入元数据/值：`node_get_input_*`、`node_set_input`（undoable）、`get_input_at_time`
- 关键帧：`node_keyframe_*`（导航/toggle/set_type_many/dragger/clear）
- 图操作：`node_factory_*`、`node_add/connect/disconnect/copy_inputs/link`
- group：`group_add_input_passthrough`/`group_input_passthrough_*`/`group_resolve_input`
- context：`node_context_*`、`node_set_context_position`
- 命令类（`NodeAddCommand`/`NodeEdgeAddCommand`/`NodeRenameCommand` 等）→ 对应
  undoable 原语，**不要新建 app 侧命令类**。

### 2.8 ViewerOutput（9）+ VideoParams（5）——viewer、footageviewer、viewerdisplay、panels

`oakengine/viewer.h`（playhead、length、video/audio params、workarea、
`oakengine_viewer_from_node`）+ `oakengine/videoparams.h`（`oak_video_params` POD +
`oakengine_video_params_make/_equal/_is_valid/_bytes_per_pixel`）。

## 3. 执行顺序与闭环

按 DS 已验证有效的顺序继续（与 r5 指引一致）：

1. **2.4 QtUtils**（9，纯搬动，最快）+ **2.1 ColorManager**（15）
2. **2.3 NodeTraverser**（6）+ **2.5 RenderTicket/Watcher**（12）
3. **2.2 EngineCore MOC**（17，类型引用消除）
4. **2.6 Track/ClipBlock**（25）+ **2.8 ViewerOutput/VideoParams**（14）
5. **2.7 Node 大族**（56，最后攻坚）

每批闭环：`nm` 基线 → 逐文件替换（r5 指引 §2 方法）→ 构建 0 error →
**全量 ctest 44/44 绿** → `nm` 双度量净减 → 立即提交 → roadmap 附 C 补记。
**全量 ctest 不绿不得进入下一批。**

## 4. 每完成一组的预期

| 完成组 | 累计消除 | 剩余约 |
|---|---|---|
| 2.4+2.1 | 24 | 315 |
| +2.3+2.5 | 18 | 297 |
| +2.2 | 17 | 280 |
| +2.6+2.8 | 39 | 241 |
| +2.7 | 56 | ~185（含豁免 6） |

最终只剩豁免清单（AudioProcessor 4 + `Block/Track::staticMetaObject` 2 = 6）。
消不掉且确属架构原因的，按 v3 §6.4 格式进豁免清单并写理由（`TrackListRippleToolCommand`
是目前唯一预定的遗留评估点）。
