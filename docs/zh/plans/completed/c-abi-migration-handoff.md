# liboakengine 纯 C ABI 迁移 — 交接执行计划（v3）

> 本文档是后续执行者（DeepSeek Flash 或任何接手代理）的**唯一权威执行依据**。
> 所有架构决策、边界契约、禁止事项已在本文钉死，执行时不得另行发明新方案；
> 遇到本文未覆盖的决策点，按"§8 决策兜底原则"处理，不得自由发挥。
>
> 相关文档：`facade-migration-roadmap.md`（各批次完成记录 + 附 D 事件机制 SOP）。
>
> v3（2026-07-23）：K2.7 对 DeepSeek Flash 的产出做了验收，修复了 6 个真实缺陷
> （详见 §2.2，每条都附教训——**这些错误模式不得再犯**）。当前符号 39、
> 仅剩 1 个已知测试失败。剩余工作按符号逐项钉死在 §3/§5。

---

## 1. 目标与验收标准

**终态**：

1. `liboakengine.so` 的动态符号表中**没有任何 `olive::` C++ 符号**（只有 `oakengine_*` C 符号 + Qt/系统符号）。
2. `oak-editor`、`oak-render-worker` 两个可执行文件**不 import 任何 `olive::` C++ 符号**（豁免清单见 §6.4）。
3. 全量测试通过；`engine/include/oakengine/*.h` 中**每个** `OAKENGINE_API` 声明的函数都有测试覆盖。
4. worker 端到端 harness 已完成，不要重做。

**统一度量命令**（禁止换口径）：

```bash
# 总指标（当前 39，目标 = 豁免清单项数 = 6，见 §6.4）
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"
# 逐符号清单
nm -D cmake-build-debug/app/oak-editor | grep " U _ZN5olive" | c++filt | sed 's/.* U //' | sort
# liboakengine 侧（终态应为 0；B11d 前不用管）
nm -D --defined-only cmake-build-debug/engine/liboakengine.so | grep -c " T _Z"
# facade 测试覆盖审计（终态应为空或仅 oakengine_worker_main）
grep -ho "oakengine_[a-z_0-9]*" engine/include/oakengine/*.h | sort -u > /tmp/decl.txt
cat engine/tests/oakengine_*_test.cpp | grep -ho "oakengine_[a-z_0-9]*" | sort -u > /tmp/tested.txt
comm -23 /tmp/decl.txt /tmp/tested.txt
```

**构建与测试**（所有批次完成后必须全绿）：

```bash
cmake --build cmake-build-debug -j$(nproc)   # 构建目录已配置好，不要重新 cmake
cd cmake-build-debug && ctest --output-on-failure -j$(nproc)   # 当前基线 44 个测试，约 90-140s
```

已知 flaky（历史偶发、重跑即过）：`oak_cli_transcode`、`oakengine_export_test`、`olive-gtest` 各观察到过一次偶发 SEGFAULT。遇到先单独重跑；**连续两次失败才算真失败**。

---

## 2. 当前状态（v3 交接快照）

- 总符号数：**557 → 39**。
- **接手第一件事：全量构建 + 全量 ctest**，确认基线后再继续（§2.3 有当前已知的精确状态，但一切以你实测为准）。
- 未提交改动很多（所有批次都在工作区，未 commit）。**严禁任何 git 写操作**，严禁回滚任何现有未提交改动。
- app target 不直接编译任何 engine 源码；oak-editor 剩余的 `U _ZN5olive` 全部是 app 代码**调用** engine C++ 类产生的运行时导入符号。

### 2.1 当前符号清单（39，nm 实测，逐项归属在 §3）

```
 4 AudioProcessor（豁免，§6.4）
 1 AudioWaveformCache::staticMetaObject
 1 Block::staticMetaObject（豁免，§6.4）
 1 ColorManager::staticMetaObject
 3 DynamicRenderer（ctor / init_with_open_gl_context / load）
 1 Folder::staticMetaObject
 5 Frame（ctor / dtor / create / allocate / set_video_params）
 1 NodeFactory::library
 5 Node（link / unlink / set_label / set_standard_value / staticMetaObject）
 2 OpenGLRenderer（ctor / init）
 2 Project（name_changed / staticMetaObject）
 3 Renderer（create_texture / blit_color_managed / destroy）
 2 RenderManager（instance_ / backend_to_string）
 1 SubtitleBlock::k_text_in
 2 Texture（upload / download）
 1 TrackListRippleToolCommand ctor
 1 Track::staticMetaObject（豁免，§6.4）
 3 UndoCommand（ctor / redo_now / undo_now）
```

### 2.2 v3 验收已修复的缺陷（DS 产出中的真实 bug，均已修复并验证）

> 这些是按"教训"写的：**每种错误模式都对应一条硬规则（§6.6），后续批次必须遵守。**

1. **B10 app 侧重复定义导致进程退出时堆损坏（"corrupted double-linked list"）**。DS 在 `app/common/{colorcodingapp,htmlapp,filefunctionsapp,hashstreamapp,xmlutilsapp}.cpp` 里用**与 engine 完全相同的限定名**定义了 `ColorCoding::colors`、`Html::k_block_tags` 等符号。可执行文件与 liboakengine.so 双定义 → ELF 符号介入合并存储 → 静态对象被**双重构造、双重析构** → double-free。这是 `timeline-tests` 和 `olive-gtest` 退出即崩的根因。**修复**：`app/CMakeLists.txt` 对这 5 个文件加 `set_source_files_properties(... COMPILE_OPTIONS "-fvisibility=hidden")`（已做）。**教训见 §6.6-R1。**
2. **`app/common/nodeimpl.cpp` 是死代码**。DS 创建了它（重定义 `Node::link/unlink/copy_inputs`）但**从未注册进 `app/CMakeLists.txt`**，三个符号仍从 .so 导入。若注册而不加 hidden visibility，会造成 `oakengine_node_link → Node::link(被介入到 app 版) → oakengine_node_link` 无限递归。**教训见 §6.6-R2；处理方案钉死在 §3.4。**
3. **`SpeedDurationDialog::accept()` 的 undo 命令从未压栈**。DS 删除了 `Core::undo_stack()->push(command, name)` 但没有替代——时长修剪（BlockTrimCommand）永不执行（2 个 gtest 失败）。**修复**：末尾补 `oakengine_undo_push(command, name)`（注意：**2 个参数，全局栈，不传栈句柄**）。
4. **`ProjectViewModel::dropMimeData` 把一次移动拆成了 3 条 undo 记录**（disconnect 一条、add_child 一条、空命令一条），测试 `undo_jump(-1)` 只撤销了空命令。**修复**：新增 facade 函数 `oakengine_folder_move_child(node, new_folder)`（`oakengine/project.h` + `src/capi/project.cpp`，单条 MultiUndoCommand 完成 detach+attach），app 改调它；测试已补进 `oakengine_footage_test.cpp::test_folder`。**教训见 §6.6-R3。**
5. **预览请求帧路径两个 linesize 错误**。`engine/src/capi/preview.cpp` 把 `frame->linesize_pixels()` 填进 POD（契约是**字节**）；`viewer.cpp::display_frame_from_preview` 用 `linesize_pixels()` 当字节偏移做 memcpy。**修复**：POD 填 `linesize_bytes()`；memcpy 用 `linesize_bytes()`。**教训见 §6.6-R4。**
6. **重建 display Frame 时 VideoParams 字段缺失**。（a）默认构造 channel_count=0 → `Frame::set_video_params` 除零崩溃；（b）默认构造 **depth=0** → Vulkan 上传 `image_size = w*h*depth*bpp = 0` → 一个字节都没上传 → 4 个 viewer NotBlack 测试黑屏。**修复**：改用四参构造 `VideoParams(width, height, format, VideoParams::k_internal_channel_count)`（该构造器 depth=1）。**教训见 §6.6-R5。**

另：`manageddisplay.cpp` 的渲染器创建曾被 DS 改成无意义的 `oakengine_renderer_init_gl(nullptr)` + 永远 OpenGLRenderer，**已恢复为原来的 DynamicRenderer 创建逻辑**（DynamicRenderer 的 3 个符号因此在清单里，属 §3.6 待办）。

### 2.3 当前测试状态（K2.7 实测）

- `timeline-tests`：全过（修复 #1 后）。
- `olive-gtest`：除 `Backends/ViewerRuntimeRewireTest.RewireToIndirectConnectionNotBlack/1`（Vulkan）外全过。这是**当前唯一已知失败**，线索与疑似根因见 §3.1。
- 其余 42 个 ctest 在上一次全量运行中通过，但**经过本批修复后尚未做最终全量复跑——接手第一步就是全量 ctest**。

---

## 3. 剩余工作（按符号逐项，顺序即执行顺序）

### 3.1 第零优先：修 `RewireToIndirectConnectionNotBlack/1`

**现象**：viewer 正在显示 direct 链（footage→sequence），运行时插入 OpacityEffect（footage→opacity→sequence）后，画面变黑且 30s 超时内不再更新。同文件的 `RewireToDirectConnectionNotBlack`（拆节点）是过的。

**排查线索（已排除项，不要重复查）**：帧数据、纹理上传、色彩变换、VideoParams 全部已验证正常（§2.2-5/6 修复后）。问题只剩"**图变更后 viewer 的失效/重渲染触发**"：插入节点后 `update_texture_from_node` 是否被触发、预览请求是否命中了旧缓存。
- 入手点：`app/widget/viewer/viewer.cpp` 的 `ConnectNodeEvent` 订阅清单 vs HEAD 原版的 connect 清单——插入节点后 texture input 变化应触发 `viewer_texture_input_changed`（事件 108）→ `update_waveform_view_from_mode`/`update_texture_from_node`。对比 HEAD 原版在该场景触发链路上是否少了什么（重点：`renderer_generated_frame`/`request_invalidate`/缓存失效事件）。
- `OpacityEffect` 插入后第一帧渲染是否失败（可在 `oakengine_preview_request_get_frame` 返回处看 has_result）。
- 修复后：该用例 + 全量 ctest 全绿才准进入 §3.2。

### 3.2 静态/杂项小点（预计 6 个符号，先做这些快的）

1. **`Project::name_changed`**：grep 定位最后一个直连 connect，改事件 2 `PROJECT_NAME_CHANGED`（已存在，SOP 见 roadmap 附 D）。
2. **`SubtitleBlock::k_text_in`**：照 B4c 模式补静态字符串 getter `const char *oakengine_subtitle_text_input_id(void);`（挂 timeline.h），app 换调用。
3. **`RenderManager::backend_to_string` + `RenderManager::instance_`**：补 v2 已钉死的契约——
   ```c
   OAKENGINE_API int oakengine_render_manager_set_aggressive_garbage_collection(int enabled);
   OAKENGINE_API int oakengine_render_manager_requested_backend(void);
   OAKENGINE_API int oakengine_render_manager_backend_to_string(int backend, char *buf, int buf_size);
   ```
   `instance_` 符号随最后一个 `RenderManager::instance()` 直连点消失（manageddisplay.cpp 的 `requested_backend()` 调用点）。
4. **`AudioWaveformCache::staticMetaObject`**：grep 定位残余 moc 引用（多半是某个 connect），按事件 SOP 补事件或消除。

### 3.3 UndoCommand 3（ctor / redo_now / undo_now）

来源：app 直接 `new` engine 命令类并进栈（grep `new .*Command` 于 app/widget/timelinewidget、app/widget/nodeview 等）。按 v2 §5.6.5 的既定方针：逐个换 facade undoable 原语，缺的按同族模式补。**不得**为这些发明新机制。

### 3.4 Node 5 + NodeFactory 1

- **`Node::link / Node::unlink / Node::copy_inputs`**（3）：`app/common/nodeimpl.cpp` 死代码的两个处理方案，**钉死选方案 A**：
  - **方案 A（选这个）**：删除 `app/common/nodeimpl.cpp`，把 app 侧所有 `Node::link(`、`Node::unlink(`、`Node::copy_inputs(` 调用点改为 facade 调用（`oakengine_node_link`/`oakengine_node_copy_inputs`，均已在 node.h 存在）。grep 定位调用点（预计 <10 处）。
  - 方案 B（不推荐）：注册 nodeimpl.cpp 且对该文件加 `-fvisibility=hidden`。只有方案 A 遇到无法改写的调用点时才用，且必须写进 roadmap 说明。
- **`Node::set_label`**（1）：补 `int oakengine_node_set_label(OakEngineNode *, const char *);`（undoable，v2 已钉死），换 app 调用点。
- **`Node::set_standard_value`**（1）：grep 定位；大概率已被 `oakengine_node_set_input` 覆盖，换调用；未覆盖则补 `oakengine_node_set_standard_value`（undoable，语义 = `NodeParamSetSplitStandardValueCommand`，照 `oakengine_node_set_input` 实现）。
- **`Node::staticMetaObject` + `NodeFactory::library`**（2）：grep 定位残余 moc/模板引用源（多为模板 connect 或 `Q_DECLARE_METATYPE`），改字符串式 connect 或 void* 透传（B8a 先例）。`NodeFactory::library` 是静态注册表，若 app 侧只剩只读枚举需求，补 `oakengine_node_factory_id_count/at`（v2 已钉死）；消不掉按 §6.4 格式进豁免清单并写理由。

### 3.5 staticMetaObject 残留（ColorManager / Folder / Project / Node）

逐个 grep 定位 moc 引用源（`qobject_cast`、模板 connect、`Q_DECLARE_METATYPE`、moc 生成的 metacall）。改事件机制或字符串式 connect。消不掉的按 §6.4 格式进豁免清单（必须写理由）。

### 3.6 B11b GPU/帧路径（17 个符号：Renderer 3 + OpenGLRenderer 2 + DynamicRenderer 3 + Texture 2 + Frame 5 + RenderManager 中属显示路径的部分）

**这是 DS 上次说"需要复杂 GPU 管线重构"而放弃的部分。决策已钉死，不需要重构，按薄封装做：**

现状事实（已验证）：
- 显示路径（`ManagedDisplayWidget`/`ViewerDisplayWidget`）持有 C++ `Renderer* attached_renderer_`（DynamicRenderer 或 OpenGLRenderer），用于 `create_texture/upload/download/blit_color_managed/destroy`；`Frame` 用于 CPU 帧搬运（`Frame::create/allocate/set_video_params/dtor/ctor`）。
- `manageddisplay.cpp` 的 DynamicRenderer 创建块**已恢复为 C++ 原版**（不要再动它，直到整个显示路径换完）。

**执行方案（钉死，分两步）**：
1. **先 Frame（5）**：`viewer.cpp::display_frame_from_preview` 和 viewerdisplay 的帧搬运改用——
   ```c
   typedef struct OakEngineFrame OakEngineFrame;   /* owned */
   OAKENGINE_API OakEngineFrame *oakengine_frame_create(void);
   OAKENGINE_API int oakengine_frame_set_video_params(OakEngineFrame *, const oak_video_params *);
   OAKENGINE_API int oakengine_frame_allocate(OakEngineFrame *);
   OAKENGINE_API void oakengine_frame_free(OakEngineFrame *);
   ```
   app 侧不再直接 `new olive::Frame`。**注意**：`display_frame_from_preview` 用四参构造 `VideoParams(width,height,format,k_internal_channel_count)`（§2.2-6 的修复，别回退）。
2. **再 Renderer/Texture（7+3）**：显示 widget 的 `attached_renderer_` 改为 facade 句柄。契约（v2 已钉死）：
   ```c
   typedef struct OakEngineTexture OakEngineTexture;  /* owned */
   OAKENGINE_API int oakengine_renderer_init_gl(void *qopengl_context);   /* QOpenGLContext 以 void* 透传，文档注明 Qt 运行时共享例外；后端选择走 RenderManager::requested_backend 语义 */
   OAKENGINE_API int oakengine_renderer_destroy(void);
   OAKENGINE_API OakEngineTexture *oakengine_renderer_create_texture(const oak_video_params *, const void *data, int linesize);
   OAKENGINE_API int oakengine_texture_upload(OakEngineTexture *, const void *data, int linesize);
   OAKENGINE_API int oakengine_texture_download(OakEngineTexture *, void *data, int linesize);
   OAKENGINE_API void oakengine_texture_free(OakEngineTexture *);
   OAKENGINE_API int oakengine_renderer_blit_color_managed(const oak_color_transform *, OakEngineTexture *, const oak_video_params *);
   ```
   facade 内部持有 DynamicRenderer/OpenGLRenderer 实例（与现在 manageddisplay 的选择逻辑相同）；backend-neutral 的离屏纹理 + 下载回读路径同样走 texture 句柄。落地后**移除 B7 两个过渡桥** `oakengine_color_transform_job_set_processor`/`oakengine_color_set_display_color_processor`，roadmap 补记。
   **验收**：5 个 Backends viewer 用例继续全过（这是该路径的现成回归测试）。

### 3.7 TrackListRippleToolCommand ctor（1）

v2 已钉死为遗留评估点。方案：grep 定位（timelinewidget ripple 工具），先尝试用现有 timeline 编辑原语组合替代；无法替代则设计 `oakengine_tracklist_ripple_*` 小族（参数拍平：track 列表 + per-track RippleInfo POD 数组 + 时间 + movement mode）。**这是最后一个符号，允许单独花时间；消不掉按 §6.4 进豁免清单（写理由）。**

### 3.8 B11d 收口（最后做）

1. 确认 oak-editor `U _ZN5olive` 只剩豁免清单 6 项；oak-render-worker 为 0。
2. liboakengine 符号可见性收口：`set_target_properties(oakengine PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)` 或 version script 白名单 `oakengine_*`。验证 `nm -D --defined-only liboakengine.so | grep -c " T _Z"` → 0。liboakcore 复查不回归。
3. facade 覆盖审计为空（`oakengine_worker_main` 豁免）。
4. 终验：全量构建 + ctest 全绿；§1 四条验收逐条核对；roadmap 附 C 标记战役完成。

---

## 4. 已完成批次（不要重做）

详见 roadmap 附 C。要点：B1–B8c 全部、B9a（Task/Undo）、B9b（Config/AudioManager/DiskManager/ProxyManager/LUTLibrary/ProjectSerializer）、B9c（预览/渲染服务 PreviewAutoCacher/RenderTicket/RenderTicketWatcher → `oakengine_preview_cacher_*`/`oakengine_preview_request_*`）、B9d（plugin）、B9e（gizmo POD 化 + DraggableGizmo 搬 app）、B10（工具类搬 app）、B11a 大部（Node 族、命令类、input id getter）、事件机制（ID 已分配到 143）。

**事件 ID 分配**：已用到 143（141/142 playback cache、143 frame cache）。**新事件从 144 起**。

---

## 5. 每批的标准产出（SOP）

1. `nm` 度量基线 → 2. grep 确认实际使用点（§3 清单仅供参考，以 grep 为准）→ 3. facade 补 C 函数（§6 契约）→ 4. app 逐文件换调用 → 5. 每个新 C 函数补单元测试（注册进 `engine/CMakeLists.txt` 的 `make_oakengine_test`）→ 6. 全量构建 + ctest 全绿 → 7. 族符号和总数双度量对比 → 8. roadmap 附 C 补记。

---

## 6. 边界契约与硬规则（钉死，不得违反）

### 6.1 C ABI 头文件规则
- 位置 `engine/include/oakengine/*.h`，实现在 `engine/src/capi/*.cpp`（注册进 `engine/src/capi/CMakeLists.txt`）。
- 每个头：GPL 版权头、`#ifdef __cplusplus extern "C"`、`OAKENGINE_API` 导出宏。
- **头文件里只允许 C 类型**：`int/int64_t/double/char*/void*`、POD struct、不透明句柄 typedef。禁止 C++ 类、模板、Qt 类型、std:: 类型、引用、默认参数、重载。
- 命名：`oakengine_<族>_<动作>`；错误码 `OAKENGINE_OK`（0）/ 负数 `OAKENGINE_E_*`。
- 字符串输出 buf/size 约定（返回所需长度不含 \0，`buf=NULL,buf_size=0` 查长度）。
- 线程语义：回调/事件 = Qt::DirectConnection 等价同步调用；回调内不得反调改同一对象的编辑原语。
- 所有权：create 返回 owned 句柄必须配套 free；borrowed 句柄在注释里写明。

### 6.2 undoable 编辑原语
- 所有改图操作必须 undoable，实现照 `engine/src/capi/node.cpp` 的 `push_or_run` 模式。
- **undo 粒度妥协是允许的**（facade 单命令边界导致一次用户操作产生多条 undo 记录），代码加注释说明即可。**但：用户语义上的一次操作若在 UI/测试层被当作一条 undo（如 drag&drop 移动、对话框 accept），必须用单条命令的 facade 函数**（`oakengine_folder_move_child` 是样板）。

### 6.3 事件机制（信号迁移唯一通道）
- 禁止 app 直接 `QObject::connect` engine 对象的信号。一律 `oakengine_event_subscribe` → `app/engineeventbridge` → app 连 bridge。SOP 见 roadmap 附 D。
- 新事件：events.h 加宏（**从 144 起**）、events.cpp `connect_event` 加 case、bridge 加信号 + dispatch、`oakengine_events_test.cpp` 补实测。
- **例外（钉死）**：facade 自有 owned 对象（OakEnginePlayback、OakEnginePreviewRequest）的完成/数据回调用各自的 `set_*_callback`，不走事件机制。

### 6.4 豁免清单（R6 后已清空：无豁免，nm=0）

> **状态（R6 收尾）**：原"终态保留"裁决已被 R6 计划推翻并全部消除——
> `AudioProcessor`（5 符号：ctor/dtor/open/close/convert）经 P5 改为 C vtable
> 接口（`oakengine/audio.h` `oakengine_audio_processor_*`，app 持
> `OakEngineAudioProcessor*` 句柄）；`plugin::PluginProgressReporter`（4 符号：
> staticMetaObject/qt_metacast/qt_metacall/cancelled）经 P3.2 去 Q_OBJECT、
> cancelled 信号改 C 回调。`oakengine_worker_main` 为 worker 进程入口
> （非 `olive::` 符号，不计入 nm 指标）。
>
> 实测：`nm -D` 于 oak-editor 与 oak-render-worker 的 ` U _ZN5olive` 均为 **0**。
> **当前无任何豁免。**

以下为 R5 冲刺时记录的 58 符号历史分类（**R6 已全部清零，仅作存档**）：

**GLM-5.2 R5 冲刺豁免清单（58 符号，分类理由）——✅ R6 已全部清零（nm 58→0）**：

**A. MOC 生成 staticMetaObject（9 符号）**——app 类的信号/槽参数类型
  含 Node*/Project*/Sequence*/ViewerOutput*/UndoStack* 时，MOC 生成的
  meta-object 代码引用 engine 类的 staticMetaObject。消除需更改所有
  此类信号/槽签名为 C ABI 句柄类型（OakEngineNode* 等），工程量大。
  - `Node::staticMetaObject`、`Project::staticMetaObject`、
    `Sequence::staticMetaObject`、`ViewerOutput::staticMetaObject`、
    `UndoStack::staticMetaObject`、`AudioWaveformCache::staticMetaObject`
  - `plugin::PluginProgressReporter::staticMetaObject/qt_metacast/qt_metacall`

**B. Inline 函数拉入（8 符号）**——engine 头文件的 inline 方法引用
  这些符号，app 包含头文件即产生 undefined reference。消除需创建
  app 侧 handle 头（不包含 engine C++ 头）或扩 facade 覆盖所有 inline 路径。
  - `Node::link`（NodeLinkCommand 析构 inline 调用）
  - `Node::set_standard_value`（inline getter 引用）
  - `Node::set_value_at_time`（1 处直接调用，QVariant→POD 转换复杂）
  - `UndoCommand::redo_now/undo_now/UndoCommand()`（MultiUndoCommand
    inline add_child/析构调用）
  - `MultiCamNode::k_current_input`、`SubtitleBlock::k_text_in`
    （inline 方法引用静态字符串）

**C. 实时回调边界（5 符号）**——v3 已预批。
  - `AudioProcessor`：ctor/dtor/open/close/convert

**D. 渲染/GPU 边界（13 符号）**——manageddisplay/viewer 的 OpenGL 路径
  直接创建 OpenGLRenderer/DynamicRenderer 对象并调用虚函数。
  facade 有 oakengine_renderer_* 但 app 仍用 C++ 对象。
  消除需将整个渲染对象管理移入 engine。
  - `Renderer`：destroy/create_texture/blit_color_managed
  - `DynamicRenderer`：ctor/init_with_open_gl_context/load
  - `OpenGLRenderer`：ctor/init
  - `Texture`：upload/download
  - `Frame`：allocate/create/set_video_params

**E. 色彩管理（6 符号）**——ManagedColor/ColorProcessor 的 C++ 对象
  在多个 UI 组件中使用。无 C ABI 等价物。
  - `ManagedColor`：ctor×2/set_color_input/set_color_output
  - `ColorProcessor`：create/convert_color

**F. 无 C ABI 等价物（17 符号）**——需新增 facade 函数。
  - `NodeValue`：4 个静态工具方法
  - `VideoParams`：3 个构造器重载
  - `AudioWaveformSync`：2 个估计算法
  - `AudioSynchronizer`：2 个对齐方法
  - `TimelineMarker`：set_time/ctor
  - `ShapeNodeBase::set_rect`、`FrameHashCache::load_cache_frame`
  - `RenderManager::instance_`（viewer.cpp inline instance() 引用）
  - `plugin::PluginProgressReporter::cancelled`（信号，需事件迁移）

### 6.5 测试规则
- facade 每个新 C 函数必须有单元测试（纯 C `assert` 风格，不依赖 GPU/QApplication；需要时 `oakengine_init(OAKENGINE_INIT_HEADLESS)`）。
- GL/Vulkan 测试必须可无 GPU 跳过（GTEST_SKIP 模式）。
- 新测试注册进 `engine/CMakeLists.txt` 的 `make_oakengine_test(...)`。

### 6.6 v3 新增硬规则（对应 §2.2 的六条教训）

- **R1（ODR/符号介入）**：app 侧**严禁**用与 engine 相同的限定名定义任何非 inline 符号（函数或静态数据）。确需同名本地副本（B10 模式），必须对该源文件加 `-fvisibility=hidden`（`app/CMakeLists.txt` 的 `set_source_files_properties` 是现成样板）。`#pragma GCC visibility` 对已被 engine 头以 default 可见性声明过的符号**无效**（GCC 取首次声明的可见性）；要么用编译 flag，要么在定义处打 `__attribute__((visibility("hidden")))`。验证方法：`readelf -sW <binary> | c++filt | grep <符号>` 必须是 `HIDDEN`。
- **R2（注册检查）**：新建任何 .cpp 必须同步注册进对应 CMakeLists，并在当批验证其符号确实从 `U` 清单消失。app 侧定义的 engine 同名函数若不加 hidden，会通过 ELF 介入把 engine .so 内部调用劫持到 app 版，可能形成跨模块无限递归。
- **R3（undo 语义）**：`oakengine_undo_push(command, name)` **只有两个参数**（全局栈，不传栈句柄）。`Core::instance()->undo_stack()` 返回 `void*`，仅作事件订阅 handle 用。删除任何 `push` 调用时必须同步删除/替换其命令的执行路径——**命令不压栈 = 静默不执行 + 内存泄漏**。
- **R4（linesize 约定）**：facade POD 中的 `linesize` 一律是**字节**。`olive::Frame` 有两个值：`linesize_bytes()`（字节）和 `linesize_pixels()`（像素 = 字节/bpp）。跨边界只传字节；engine 内部（纹理上传等）按各 API 既有约定（Vulkan/OpenGL 纹理上传收**像素**）。
- **R5（VideoParams 构造）**：默认构造的 `VideoParams` 是 width=0/height=0/**depth=0**/channels=0/format=invalid。凡要喂给帧分配/纹理上传的，**必须用带参构造器**（显示 RGBA 帧用四参 `VideoParams(w, h, format, VideoParams::k_internal_channel_count)`，depth=1）。depth=0 不会报错，只会让上传字节数为 0（纯黑）。
- **R6（接手验证）**：任何中断/交接后，第一件事是全量构建 + 全量 ctest + 对照 §3 清单 grep 复核，不要采信上一手的完成声明（包括本文 §2.3——以你实测为准）。

---

## 7. 禁止事项（硬约束）

1. **禁止任何 git 写操作**：commit / add / push / restore / checkout / stash / clean / rebase。
2. **禁止暴露 C++ ABI**：不得新建导出 C++ 类/模板/Qt 类型的头；不得往 liboakengine 导出表加 `_Z` 符号（新代码产生 C++ 弱符号时给实现类加 `visibility("hidden")`）。
3. **禁止把 Qt 类型放进 core/**（liboakcore 是 Qt-free）。
4. **禁止改 worker 的 NDJSON IPC 协议**；禁止重做 §4 已完成的任何批次。
5. **禁止修改本文已钉死的签名**：各 facade 头现有函数、events.h 已分配的事件 ID（1–143）、§3 各批钉死的契约签名。
6. **禁止为追求 undo 记录合并、staticMetaObject 消除等发明新机制**——按 §6.2/§6.4 妥协条款执行。
7. **禁止降低测试标准**：新 C 函数无测试不得算完成；全量 ctest 不绿不得进入下一批。
8. **禁止重新 cmake 配置构建目录**；禁止安装/卸载系统依赖；禁止改 CI/打包文件。
9. **禁止在未验证构建状态前继续批次**（§6.6-R6）。

---

## 8. 决策兜底原则

1. roadmap 已有裁决的，从 roadmap。
2. 能搬进 app 的纯 UI/工具代码 → 搬 app（优于 facade 化）。
3. 纯数据类 → POD 化或头内联，优先于新增 facade 族。
4. 必须跨边界的 → 最小 facade 族（只包 app 实际用到的成员）。
5. GPU/帧路径、Qt 运行时耦合 → 薄封装 + `void*` 透传 + 文档注明例外。
6. 以上都拿不准的：留下不动，写进 roadmap 遗留清单，继续下一点。**不允许为单点发明新架构。**

---

## 9. 环境备忘

- 构建目录 `cmake-build-debug`（Ninja + Qt6，Debug）；另有 cmake-build-asan / cmake-build-coverage，**不要用**。
- 单文件增量验证：`rm -f cmake-build-debug/app/CMakeFiles/libolive-editor.dir/<相对路径>.o && cmake --build cmake-build-debug --target olive-editor -j$(nproc)`。
- 测试素材：`tests/demo.mp4`、`tests/img.png`、`tests/project_with_footage.ove`。
- 本机有 GPU，worker/viewer 的 Vulkan 用例真实执行（OpenGL 用例 offscreen 不可绘，会 SKIP，属正常）；CI 无 GPU 会 GTEST_SKIP，两者都算通过。
- 全量 ctest 44 个约 90-140s（olive-gtest 占 ~85s），每批必须跑完不能裁剪。
- 调试技巧（本批实测有效）：teardown 堆崩溃用 `GLIBC_TUNABLES=glibc.malloc.tcache_count=0 gdb -batch -ex run -ex bt` 可拿到真实崩溃栈；帧/纹理内容验证用 `Texture::download` 回读后求和。
