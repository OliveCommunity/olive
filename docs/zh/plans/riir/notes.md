# 记录项

## common

CancelableObject只在render和task处有🚰，移入对应模块。

## 从 oakcommon（src/common）移除的类（2026-08-05）

判据：不严重依赖 common 其他类，且只有一个非 common 模块使用它
（tests/gtest 不计入使用方）。以下类已从 `src/common/` 移除，
拆分时应放到对应模块：

| 类 | 唯一使用方 | 应放到 |
|---|---|---|
| `Html`（html.h/.cpp） | engine/node | oaknode（M3）。注意它依赖 common 的 xmlutils，迁移时需连同 XML 辅助或改为调用 oakcommon C API |
| `JobTime`（jobtime.h/.cpp） | engine/render | oakrender（M7） |
| `OTIOUtils`（otioutils.h） | engine/task | oaktask（M8） |
| `PlaybackAudioClock`（playbackaudioclock.h） | engine/audio | oakaudio（M6） |
| `to_hex`（tohex.h） | engine/node | oaknode（M3） |
| `mid()`（util.h） | engine/node | oaknode（M3） |
| `AVFramePtr`（avframeptr.h） | engine/render | oakrender（M7） |
| `CrashpadInterface`（crashpadinterface.h/.cpp） | app/main.cpp | app（crashhandler 相关） |
| crashpadutils.h | app/crashhandler | app/crashhandler |
| `AutoScroll`（autoscroll.h，oakutil shim） | app/dialog、app/widget | app |
| `digit.h`（oakutil shim） | app/dialog | app |
| `range.h`（oakutil shim） | app/widget | app |

另：`power.h`、`memorypool.h`、`threadsafemap.h` 当前没有任何
common 模块外的使用方（零用户），不满足移除判据，暂保留在
oakcommon；后续若确认无用途可直接删除。

## oakcommon 去Qt化的删除与语义变更（2026-08-05）

去Qt化过程中以下函数被删除或语义变化，迁移调用方时需注意：

- `CommandLineParser::print_help()`：不再自动读取
  `QCoreApplication::applicationName()/applicationVersion()`，需先调
  `set_app_info()`（C API：`oakcommon_commandlineparser_set_app_info`），
  否则打印默认 `"oak"` + 空版本。
- `FileFunctions::get_unique_file_identifier`：哈希由 SHA-1 改为
  FNV-1a 64-bit，旧缓存 key 全部失效（需一次重建）。
  `get_configuration_location`/`get_application_path`/
  `get_temp_file_path`/`get_auto_recovery_root` 的标准路径改为手写
  平台实现，路径与原 Qt 版不同。`read_file_as_string(":/...")` 的
  qrc 资源路径不再可用，shader 等资源调用方需改为磁盘路径或内嵌。
- xmlutils：`xml_read_next_start_element(reader, CancelAtom*)`
  重载与 cancel_atom 参数删除（依赖 render/cancelatom.h），取消语义
  由调用方在循环外自行检查；`XMLAttributeLoop` 宏删除，由
  `XmlStreamReader::attributes()` + range-for 替代；
  `QXmlStreamReader/Writer` 由基于 expat 的
  `olive::XmlStreamReader/Writer` 替代（不支持 XML 命名空间）。
- `OIIOUtils::frame_to_buffer/buffer_to_frame`：未进 C API，C++ 侧签名
  拍平为 `(const void *data, int64_t linesize_bytes, OIIO::ImageBuf *)`，
  engine/codec/frame.cpp 迁移时传成员即可。`OakCommonPixelFormat` 枚举
  目前定义在 include/common/ocioutils.h，oiioutils 复用，后续可抽成
  独立的 include/common/pixelformat.h。
- `MemoryPool`：删除 QTimer 每 5 秒自动回收空 arena，改为公有
  `clear_empty_arenas()` 由调用方周期调用；不再继承 QObject。
  memorypool/threadsafemap 零使用方且为模板/多态基类，未包 C API。
- `debug_handler`：丢失 `QMessageLogContext`（文件/行号）参数（原实现
  本就未用）；`qInstallMessageHandler` 无对应物。
- `Current::set_current_video_params/audio_params`：由按值拷贝改为持有
  `std::shared_ptr<void>`（类型擦除），调用方需自行管理生命周期并用
  `static_pointer_cast` 还原（调用点：olivehost.cpp、
  timebasedwidget.cpp、plugin.cpp 等 6 处）。
- qtutils：engine 未使用的纯 UI 函数（`q_font_metrics_width`、
  `create_horizontal/vertical_line`、`set_combo_box_data`、
  `word_wrap_string`、`flip_control_and_shift_modifiers`、
  `get_formatted_date_time`、`to_q_color`、`core::qHash`、
  `Q_DECLARE_METATYPE` 宏组）未迁入 oakcommon，app 层继续用 Qt 版。
  `ptr_to_value/value_to_ptr` 载体由 QVariant 改为 `uintptr_t`，
  迁移时调用点需同步改（renderprocessor.cpp、rendermanager.cpp、
  previewautocacher.cpp、src/capi/worker.cpp）。`get_parent_of_type`
  依赖 C++ 模板+RTTI，未进 C API。
- define.h：删除 `MACRO_NAME_AS_STR`/`MACRO_VAL_AS_STR`/
  `OLIVE_NS_CONST_ARG`/`OLIVE_NS_ARG`/`OLIVE_NS_RETURN_ARG`
  （依赖 Qt QArgument，全仓无使用）。
- decibel.h：删除 `ALLOW_RETURNING_INFINITY` 编译开关分支（代码库
  未定义该宏，行为不变）。
- `FFmpegUtils`：新增类内常量 `k_rgb_channel_count/k_rgba_channel_count`
  （3/4）取代对 `VideoParams` 同名常量的引用，消除 common→render
  反向依赖。

## oakundo 去Qt化的删除与语义变更（2026-08-05）

`src/undo/`（olive::UndoCommand / MultiUndoCommand / UndoStack）去Qt化
过程中以下删除与语义变化，迁移调用方时需注意：

- `UndoStack` 不再继承 `QAbstractItemModel`（删除 `Q_OBJECT`、
  `columnCount/data/index/parent/rowCount/headerData/hasChildren` 全部
  model overrides 及 `begin/end*Rows` 通知）。历史面板（QTreeView 等）
  属 app UI 层，迁移时应以 facade 的行式查询
  （command_count/done_count/command_name/command_is_done）自行实现
  model。
- `UndoStack::GetUndoAction/GetRedoAction/update_actions`（QAction 成员
  及其文本/使能维护）删除——QAction 属 app 菜单层，app 应自行创建
  action 并连接 undo/redo；`index_changed(int)` signal 改为 C++ 侧
  `std::function<void(int)>`（`set_index_changed_callback`），C ABI
  不暴露事件，调用方在变更命令后读 `oakundo_undostack_index`。
- `UndoCommand` 与 `olive::Project` 解耦：删除纯虚
  `get_relevant_project()`（含 MultiUndoCommand/EmptyCommand 的
  override）与 `project_` 成员；`redo_and_set_modified/
  undo_and_set_modified` 的修改标记语义改为可选回调对
  `set_modified_callbacks(is_modified, set_modified)`：redo 时记录当前
  修改标记并置 true，undo 时恢复记录值；不挂回调则退化为纯
  redo/undo。读写顺序与 Qt 版一致（redo_now() 之后读取并置位），
  行为对齐。Project 侧迁移时在适配层把 `Project::is_modified/
  set_modified` 绑到这对回调即可。
- `UndoStack` 析构不再调用 `clear()`（Qt 版在析构里 clear 会再 push
  一个 EmptyCommand 造成泄漏），改为直接删除所有持有命令。
- 顺带修复三处 engine 原版就存在的缺陷（迁移调用方无需适配，但
  行为与旧版不同，特此记录）：
  - `jump(0)` 死循环：底部 EmptyCommand 不可 undo，`jump` 现以
    `can_undo()/can_redo()` 为守卫，跳到 0 时停在 index 1；
  - `MultiUndoCommand` 原来不拥有子命令（析构泄漏），现析构删除
    全部 children；
  - `push_pre_executed` 原来不置 `done_` 标记，导致入栈后 undo
    为空操作（facade undo-group 的组撤销实际不生效），现入栈前
    `set_done(true)`（UndoCommand 新增 `set_done()`）。
- `clear()` 推入的占位命令名固定为字面量 "New/Open Project"（原
  `tr()` 翻译移除，翻译由 app 层负责）。
- `push/push_pre_executed` 的命令名由 `QString` 改为 `std::string`；
  C ABI 中 NULL name 视同空串。
- C ABI 句柄语义：`oakundo_command_init/init_multi` 返回的句柄拥有
  底层命令；push 或 `oakundo_command_multi_add_child` 成功后所有权
  转移、wrapper 被消费（不可再用/再 free）；
  `oakundo_command_multi_child` 返回 borrowed wrapper（free 只释放
  wrapper）。
