# Node 类完整覆盖映射表（C++ `olive::Node` → oaknode Rust crate）

> 逐方法盘点 `src/node/src/node.h`（260 行声明，去重/去重载后 ~150 个）。
> 每一行标注 Rust 侧的落点：`trait` = [`NodeBehavior`]（node.rs），
> `core` = `NodeCore` 数据 + 查询方法，`graph` = `Graph` 方法，
> `ops` = 自由函数（ops.rs），`bridge` = 经 C ABI 出模块，
> `drop` = 刻意不迁移（附理由）。`// CPP-PARITY` 注释义务不变。
> 重载族合并为一行（string/NodeInput/TrackReference 三个重载 → Rust
> 侧统一为 `NodeInput`-风格键）。

## 1. 生命周期 / 归属

| C++ | Rust 落点 |
|---|---|
| `Node()` / `~Node()` / `NODE_DEFAULT_DESTRUCTOR` | `graph.add_node` / `graph.remove_node`（disconnect_all 副作用在 remove 内） |
| `parent()` / `set_parent()` / `project()` | `core.owner: ProjectRef`（arena 反向索引；不再是裸指针） |
| `folder()` / `set_folder()` / `is_item()` | `core.bin_folder: Option<NodeId>` + `is_item` 查询 |
| `AddedToGraphEvent` / `RemovedFromGraphEvent` | `trait` `added_to_graph` / `removed_from_graph` |
| `LoadFinishedEvent` / `PostLoadEvent` | `trait` `load_finished` / `post_load`（serializer 驱动） |

## 2. 身份 / 元数据

| C++ | Rust 落点 |
|---|---|
| `name()` / `short_name()` / `id()` / `category()` / `sub_category()` / `description()` | `trait`（name/type_id/short_name/categories/sub_category/description） |
| `retranslate()` / `data(DataType)` | `drop`（retranslate 是 UI 职责；data(icon) 归 facade/app —— 见 notes） |
| `get_label*` / `set_label` / `get_label_or_name` / `get_label_and_name` | `core.label` + 查询 |
| `color()` / `get_override_color` / `set_override_color` | `core.override_color`（经 bridge::common config） |
| `get_category_name` (static) | `ops::category_name` |
| `get_flags` / `set_flag` | `core.flags` |

## 3. 输入定义与属性

| C++ | Rust 落点 |
|---|---|
| `inputs()` / `add_input` / `insert_input` / `prepend_input` / `remove_input` | `core.inputs` + `graph` 编辑方法（变更经 undo 命令工厂） |
| `has_input_with_id` / `has_param_with_id` / `get_input_data_type` / `set_input_data_type` | `core` 查询/编辑 |
| `get_input_name` / `set_input_name` / `get_input_flags` / `set_input_flag` | `core`（`get_input_name` 是 virtual → `trait` `input_name`） |
| `is_input_hidden` / `is_input_connectable` / `is_input_keyframable` / `is_input_keyframing` / `set_input_is_keyframing` | `core` |
| `has/get_input_property`（单/全）/ `set_input_property` / `set_combo_box_strings` | `core.properties` |
| `get_effect_input` / `get_effect_input_id` / `set_effect_input` | `core.effect_input` |
| `ignore_inputs_for_rendering` / `is_input_connected_for_render` | `trait`（渲染期行为） |
| `get_internal_input_*` / `report_invalid_input` / `create_immediate` / `get_immediate` | crate 内部（`core` 私有 + `immediate` 子结构；immediate 是 C++ 的惰值/连接二态，Rust 侧为 `enum InputSlot { Value, Connected }`） |
| `clear_element` | `core` 编辑 |

## 4. 值存取（standard value / split value / at_time）

| C++ | Rust 落点 |
|---|---|
| `get/set_standard_value`（含 split 族与 on_track 族） | `core` + `value.rs`（SplitValue → `NodeValue` 分量访问） |
| `get/set_default_value` 族 | `core.defaults` |
| `get_value_at_time` / `get_split_value_at_time(_on_track)` | `traverser` 求值路径（读 keyframe/standard/连接输出） |
| `set_value_at_time` (static) | `ops::set_value_at_time`（undo 命令工厂） |
| `parameter_value_changed` | crate 内部（invalidation 触发点） |
| `get/set_value_hint_for_input` / `get_value_hints` | `core.hints`（`get_value_hint_for_input` 是 virtual → `trait`） |

## 5. 关键帧

| C++ | Rust 落点 |
|---|---|
| `get_keyframe_tracks` / `get_number_of_keyframe_tracks` / `get_track_from_keyframe` | `core.keyframes`（keyframe.rs 的 `KeyframeTrack`） |
| `get_keyframes_at_time` / `get_keyframe_at_time_on_track` / `has_keyframe_at_time` | `keyframe.rs` 查询 |
| `get_earliest/latest_keyframe` / `get_closest_keyframe_before/after_time` / `get_best_keyframe_type_for_time_on_track` | `keyframe.rs` 查询 |
| `add_keyframe` / `remove_keyframe` | `core` 编辑 + undo 工厂 |
| `invalidate_from_keyframe_*`（5 个） | crate 内部 → invalidation 走 `graph` 下游标记 |
| `get_range_affected_by_keyframe` / `get_range_around_index` | `keyframe.rs` |

## 6. 连接 / 图遍历

| C++ | Rust 落点 |
|---|---|
| `connect_edge` / `disconnect_edge` (static) / `disconnect_all` | `graph.connect/disconnect/remove_node`（环检测在 connect） |
| `input_connections` / `output_connections` / `is_input_connected` / `get_connected_output(_render_output)` | `graph` 邻接查询（`get_connected_render_output` 是 virtual → `trait`，Group 覆写） |
| `get_dependencies` / `get_exclusive_dependencies` / `get_immediate_dependencies` / `find_input_nodes<T>` / `find_ways_node_arrives_here` / `find_path` | `graph` 遍历自由函数（模板 find_input_nodes<T> → 按 `type_id` 过滤） |
| `inputs_from` / `context_contains_node` / `is_node_expanded_in_context` / `*_node_position*_in_context` / `remove_node_from_context` | `core.context_positions` + `graph`（Group 上下文语义） |
| `transform_time_to` | `ops::transform_time_to`（track 时间换算，C++ static） |

## 7. 求值 / 渲染

| C++ | Rust 落点 |
|---|---|
| `value()` | `trait NodeBehavior::value` |
| `process_samples` / `generate_frame` | `trait`（generate_frame 供 CPU 直渲节点） |
| `get_shader_code` / `gizmo_*` / `get_gizmos` / `add/remove_gizmo` / `add_draggable_gizmo` / `current_gizmo` | `trait` 渲染/gizmo 子面（gizmo 数据在 crate，绘制归 facade/app） |
| `get_active_elements_at_time` | `trait` |
| `get_video_cache_range` / `get_audio_cache_range` | `trait` |
| `video_frame_cache()` 等四个 + `copy_cache_uuids_from` / `are_caches_enabled` / `set_caches_enabled` | `core.caches`（bridge::render 句柄） |
| `invalidate_cache` / `invalidate_all` / `send_invalidate_cache` | `traverser::invalidate_downstream` + `graph` 直接扇出（无信号） |
| `input_time_adjustment` / `output_time_adjustment` | `trait` |

## 8. 序列化

| C++ | Rust 落点 |
|---|---|
| `load` / `save` / `load_input` / `save_input` / `load_immediate` / `save_immediate` | `serializer.rs` 主驱动（NodeCore 部分） |
| `load_custom` / `save_custom` | `trait`（已在底稿） |
| `get_input_id_for_legacy_id` | `trait`（旧版输入 id 映射，默认恒等） |

## 9. 链接 / 拷贝 / 组

| C++ | Rust 落点 |
|---|---|
| `link` / `unlink` / `are_linked` / `has_links` / `links` / `LinkChangeEvent` | `core.links` + `trait` `link_changed` |
| `copy()` / `NODE_COPY_FUNCTION` | `trait NodeBehavior::duplicate` |
| `copy_inputs` / `copy_input` / `copy_values_of_element` (statics) | `ops::copy_*` |
| `copy_dependency_graph` / `copy_node_in_graph` / `copy_node_and_dependency_graph_minus_items(_internal)` | `ops::copy_subgraph`（undo 打包经 bridge::undo） |
| `get_connect_command_string` / `get_disconnect_command_string` | `ops`（undo 命令文案） |

## 10. 事件（C++ virtual，Qt 信号已删后的直接回调点）

| C++ | Rust 落点 |
|---|---|
| `InputValueChangedEvent` / `InputConnectedEvent` / `InputDisconnectedEvent` / `OutputConnectedEvent` / `OutputDisconnectedEvent` / `ConnectedToPreviewEvent` | `trait`（`input_value_changed` 已在底稿；补全其余 5 个） |

## 11. 刻意不迁移（drop）

| C++ | 理由 |
|---|---|
| `getPlugin` / `getPluginInstance` / `setPluginInstance`（OFX 裸指针） | 越界类型；PluginNode 经 plugin crate C ABI（M11）持有实例 |
| `retranslate` / `data(icon)` / `add_draggable_gizmo` 的 UI 半 | UI/翻译归 facade/app；crate 只保留数据与键控位置 |
| `create_immediate` 的 QObject 父子语义 | Rust 所有权原生表达 |
| 全部 `*_internal` 私有辅助 | 实现细节，重组于 graph/ops 内部 |
