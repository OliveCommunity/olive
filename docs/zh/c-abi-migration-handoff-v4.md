# liboakengine 纯 C ABI 迁移 — 重做交接执行计划（v4）

> 本文档是后续执行者（DeepSeek Flash 或任何接手代理）的**唯一权威执行依据**。
> v4 重写背景：2026-07-23 上一任执行代理误执行 `git checkout --`，把全部未提交的
> 迁移工作回滚到 HEAD。后经 JetBrains LocalHistory 部分恢复。
> **本文档面向没有此前对话记忆的执行者，自包含。**
>
> 契约细节（C ABI 头文件规则、事件机制 SOP、undo 规则、硬规则 R1–R6、各 facade 族
> 签名）未在本文重复的，均以同目录 `c-abi-migration-handoff.md`（v3，已随
> branch 提交保留）为准。两份文档冲突时，**本文（v4）优先**。

---

## 0. 事故记录与新的 git 铁律

### 0.1 发生了什么

- 迁移战役（B1–B11a）全部工作曾处于**未提交**状态。执行代理误执行
  `git checkout --`，所有已跟踪文件的修改被回滚到 HEAD（fcf717f6a）。
- 未跟踪新文件（约半数 facade 族、全部测试、部分 app 文件、v3 交接文档、
  RIIR 计划）未受影响；已跟踪文件的修改（node/timeline/project/preview 的
  facade 扩容、几乎全部 app 侧调用点迁移、CMake 注册、roadmap 记录）丢失。
- 用户随后从 JetBrains LocalHistory 导出恢复了一大部分（详见 §2 清单）。
- 当前工作全部在分支 **`c-abi-migration`** 上，已有 3 个抢救/修复提交
  （b11d91f56 → e0e51647d → d1779d74e）。

### 0.2 新 git 铁律（覆盖此前"禁止 git 写操作"的旧规则）

1. **所有工作只在 `c-abi-migration` 分支进行。**
2. **每完成一个小步立即提交**（一个族、一个文件、一个修复都算一步）。
   提交信息写明批次与内容。**绝不隔夜持有未提交工作。**
3. **严禁** `git checkout --` / `git restore` / `git clean` / `git reset --hard` /
   `git stash`（这些命令曾毁掉一次战役）。确需回滚某个文件时，用
   `git show HEAD~N:<path>` 读出内容后手工写回，并先经用户确认。
4. push 与否由用户决定；本地提交不需要再请示。

---

## 1. 目标与验收（不变）

1. `liboakengine.so` 动态符号表无 `olive::` C++ 符号（仅 `oakengine_*` + Qt/系统符号）。
2. `oak-editor`、`oak-render-worker` 不 import 任何 `olive::` C++ 符号（豁免见 §6.4）。
3. 全量测试通过；`engine/include/oakengine/*.h` 每个函数有测试覆盖。
4. worker 端到端 harness 保持通过（不重做）。

**度量命令**（统一口径）：

```bash
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"     # 总指标
nm -D cmake-build-debug/app/oak-editor | grep " U _ZN5olive" | c++filt | sed 's/.* U //;s/(.*//' | awk -F'::' '{print $1"::"$2}' | sort | uniq -c | sort -rn
nm -D --defined-only cmake-build-debug/engine/liboakengine.so | grep -c " T _Z"
grep -ho "oakengine_[a-z_0-9]*" engine/include/oakengine/*.h | sort -u > /tmp/decl.txt
cat engine/tests/oakengine_*_test.cpp | grep -ho "oakengine_[a-z_0-9]*" | sort -u > /tmp/tested.txt
comm -23 /tmp/decl.txt /tmp/tested.txt                              # 覆盖审计
cmake --build cmake-build-debug -j$(nproc)
cd cmake-build-debug && ctest --output-on-failure -j$(nproc)
```

已知 flaky：`oak_cli_transcode`、`oakengine_export_test`、`olive-gtest`（偶发 SEGFAULT，
单独重跑两次仍失败才算真失败）。

---

## 2. 当前状态（2026-07-23 实测）

### 2.1 构建状态

**当前构建是红的**，错误只集中在 4 个丢失的 facade 扩容族（§3 R1–R4）。
其余部分（含 app、worker、cli、liboakcore、liboakengine 既有 facade）编译通过。

### 2.2 幸存且已提交（不要重做）

- **完整 facade 族**（头 + 实现 + 测试）：`app`、`audio`、`color`、`config`、`disk`、
  `encoding`、`events`、`gizmo`、`lut`、`plugin`、`proxy`、`serializer`、`sync`、
  `task`、`traverse`、`undo`、`videoparams`、`viewer`、`worker`，以及更早期已入库的
  `init`/`ipc`/`export`/`exporter`/`playback`/`spscringbuffer`。
- **全部 facade 测试文件**（engine/tests/oakengine_*_test.cpp，含 node/keyframe/
  timeline_edit/footage/preview/renderer——**丢失族的测试还在，它们就是重做时的
  API 规格书**）。
- **app 侧**：`engineeventbridge.{h,cpp}`、`app/common/*`（configwrapper、undowrapper、
  colorcodingapp、filefunctionsapp、hashstreamapp、htmlapp、xmlutilsapp、debugapp）、
  各 handle 头（keyframehandle/markerhandle/cliphandle/trackhandle/colorprocessorhandle/
  vieweroutpututils）、`markerpainting.*`、`app/timeline/`、`app/ui/icons/`。
- **文档**：v3 交接文档（`c-abi-migration-handoff.md`）、RIIR 计划（`plans/riir.md`）、
  roadmap 批次记录（经 LocalHistory 恢复，`facade-migration-roadmap.md`）。
- **结构性改动**：B1 图标（engine 返回图标名 + app from_name 映射，已修复一致）、
  B2 布局 POD（SerializedLayoutInfo 全链路，已修复一致）、B3 coreengine.h、
  B7 managedcolor 删除与 VideoParams 头内联、CMake 全部注册（engine/capi/app/ui/
  timeruler/timeline/serializer/render）、`oak_proxy_params` POD（已补回 footage.h）、
  undostack B9a 访问器（已补回）、`engine/config/config.h` 的 `#ifndef OAK_CONFIG` 守卫、
  textv3.h 的 text_gizmo 访问器。

### 2.3 丢失（= 重做范围）

| # | 内容 | 批次 |
|---|---|---|
| R1 | `engine/include/oakengine/node.h`（545→~1500 行）+ `engine/src/capi/node.cpp`（1590→~3700 行）：OAK_NODE_VALUE_* 完整枚举、输入元数据/property、值读写、多轨关键帧、`OakEngineKeyframe` 句柄族、NodeDragger、undoable 批量原语、context 位置族、group passthrough 族、multicam 族 | B8a/B8b |
| R2 | `timeline.h`/`timeline.cpp`：track 高度换算、block_is_enabled、clip 输入 id 六 getter、`clip_set_media_in`/`request_invalidate`/`discard_cache`/`add_cache_passthrough`、marker 句柄族（OakEngineMarkerList/OakEngineMarker ~20 函数）、workarea 句柄族（~8）、`sequence_add_default_nodes`、`clip_get_media_range_rational`、块遍历族 | B4c |
| R3 | `project.h`/`project.cpp`：folder 族（create/has_child_recursive/index_of_child/child_input_key/add_child）+ **`oakengine_folder_move_child`**（v3 新增，单条 undo 移动） | B5 |
| R4 | `preview.h`/`preview.cpp`：cacher 四函数、`OakEnginePreviewRequest` 异步请求族（~10 函数）、playback cache 句柄 + `valid_ranges`/`indicator_height`、frame cache 句柄、waveform/audio analyze 两函数；事件 141/142/143 | B9c |
| R5 | app 侧全部已跟踪调用点迁移（viewer 簇已恢复到 B9c 前中间态——仍用 RenderTicketWatcher，需随 R4 再迁一次；timelinewidget/nodeview/nodeparamview/projectexplorer/keyframeview/timeruler/dialogs/panels 等数百处） | B1–B11a app 侧 |
| R6 | B11b GPU 收尾：renderer.h/cpp 已恢复 B11b 内容（texture/frame 族在），需验证 + 移除 B7 两过渡桥 | B11b |
| R7 | B11c staticMetaObject 清理 + B11d visibility 收口与终验 | B11c/B11d |

### 2.4 事件 ID 与 facade 覆盖基线

- 事件 ID 已分配到 **143**（140 audio manager、141/142 playback cache、143 frame cache）。
  新事件从 **144** 起。
- facade 覆盖审计在重做期间必然有缺口（丢失族的函数还没回来），**R1–R4 完成后
  审计必须为空（仅 oakengine_worker_main 豁免）**。

---

## 3. 重做执行计划（按顺序，每步闭环：构建 + ctest + 符号度量 + 立即提交）

### R1 node 族扩容（最大单块，先做）

1. 以 `engine/tests/oakengine_node_test.cpp`、`oakengine_keyframe_test.cpp` 为
   **唯一 API 规格**：把测试引用但头文件缺失的函数逐个补回 `oakengine/node.h`
   （OAK_NODE_VALUE_* 完整枚举、输入元数据/property 全套、值读写、多轨关键帧、
   `OakEngineKeyframe` 句柄族、`OakEngineNodeDragger`、undoable 批量原语、
   context 位置、group passthrough、multicam）。
2. 实现补进 `engine/src/capi/node.cpp`，模式照现存的 `traverse.cpp`/`undo.cpp`
   （push_or_run、string_to_buf、impl() 转换）。
3. `events.cpp`/`traverse.cpp`（幸存）依赖这些枚举与类型，随 R1 自然恢复编译。
4. 验证：oakengine_node_test/keyframe_test/events_test 全过 + 全量 ctest 绿。
5. **立即提交。**

### R2 timeline 族扩容

1. 以 `oakengine_timeline_edit_test.cpp` 为规格，补 `timeline.h`/`timeline.cpp`
   （§2.3 R2 列出的全部族；marker/workarea 句柄定义在 timeline.h，
   `OakEngineMarkerList`/`OakEngineMarker`/`OakEngineWorkarea` typedef 一并补回）。
2. app 侧幸存文件（seekablewidget、timeruler、markerpainting、markerhandle）依赖
   这些类型，随 R2 恢复编译。
3. 验证 + 立即提交。

### R3 project 族 folder 补全

1. 以 `oakengine_footage_test.cpp`（含 folder 与 `oakengine_folder_move_child`
   用例）为规格，补 `project.h`/`project.cpp` 的 folder 族与 move_child
   （move_child 语义：detach 旧 folder + attach 新 folder 合成**一条**
   MultiUndoCommand；实现参照 v3 §2.2-4 与 footage_test 断言）。
2. 验证 + 立即提交。

### R4 preview 族扩容 + viewer 重迁

1. 以 `oakengine_preview_test.cpp` 为规格，补 `preview.h`/`preview.cpp`
   （§2.3 R4 全部；`OakEnginePreviewRequest` 内部 = RenderTicket + Watcher 封装，
   完成回调走 facade 自有 C 回调不占事件号；playback cache 事件 141/142、
   frame cache 143 已在 events.h/events.cpp 幸存，检查连通即可）。
2. **帧 POD 契约红线**：`oak_playback_frame.linesize` 是**字节**；
   app 重建 display Frame 用四参构造 `VideoParams(w,h,format,k_internal_channel_count)`
   （默认构造 depth=0 会导致 Vulkan 上传 0 字节纯黑——v3 §2.2-6 的事故，勿复现）。
3. viewer.cpp 随 R4 从 RenderTicketWatcher 中间态迁到 preview_request 流程
   （参照 v3 §5.2.2 契约；当前 viewer.cpp 是可编译的 B9c 前状态，能跑但符号多）。
4. 验证（含 Backends viewer 5 用例）+ 立即提交。

### R5 app 侧调用点迁移重做

按 v3 §3 的 36 符号清单逐项消灭（清单以你重做时的 nm 实测为准）：
- 优先顺序同 v3 §5：杂项小点（Project::name_changed、SubtitleBlock::k_text_in、
  RenderManager、AudioWaveformCache）→ UndoCommand 3 → Node 5 + NodeFactory 1
  （**方案 A 钉死：删 nodeimpl.cpp，改调用点走 facade**）→ staticMetaObject 清理。
- app 侧纯换调用不加新测试；每族符号归零后立即提交。

### R6 B11b GPU 收尾

renderer.h/cpp 已含 texture/frame 族（恢复版）。验证其编译与测试
（oakengine_renderer_test），然后按 v3 §3.6 完成显示路径句柄化并移除 B7 两过渡桥
（`oakengine_color_transform_job_set_processor`/`oakengine_color_set_display_color_processor`）。
验收：Backends viewer 5 用例全过。

### R7 B11c/B11d 收口

按 v3 §3.7/§3.8：TrackListRippleToolCommand 遗留评估 → 豁免清单确认
（AudioProcessor 4 + Block/Track::staticMetaObject = 6）→ visibility 收口
（`CXX_VISIBILITY_PRESET hidden` 或 version script 白名单）→
`nm -D --defined-only liboakengine.so | grep -c " T _Z"` = 0 →
全量终验 + roadmap 附 C 补记战役完成。

---

## 4. 边界契约（沿用 v3，要点重申）

- C ABI 头只允许 C 类型；buf/size 字符串约定；owned/borrowed 注释；错误码
  `OAKENGINE_OK`/负数 `OAKENGINE_E_*`。
- 改图操作必须 undoable（push_or_run 模式）；用户语义上的单次操作必须单条 undo
  （`oakengine_folder_move_child` 是样板）。
- 信号迁移唯一通道 = 事件机制（`oakengine_event_subscribe` + EngineEventBridge，
  SOP 见 roadmap 附 D）；facade 自有 owned 对象的完成回调例外（playback/preview
  request 先例）。
- **v3 §6.6 硬规则 R1–R6 全部继续有效**（ODR/hidden visibility、注册检查、
  undo 双参、linesize 字节、VideoParams 构造、接手先验证）。
- 新 C 函数必须有单元测试；GL/Vulkan 用例可无 GPU 跳过；测试注册进
  `engine/CMakeLists.txt` 的 `make_oakengine_test`。

## 5. 禁止事项

1. **严禁 `git checkout --` / `restore` / `clean` / `reset --hard` / `stash`**（§0.2-3）。
2. 禁止暴露 C++ ABI；禁止往 liboakengine 加 `_Z` 导出；禁止 Qt 类型进 core/。
3. 禁止改 worker NDJSON 协议；禁止重做 §2.2 已列的幸存部分。
4. 禁止修改已钉死签名：各 facade 头现有函数、事件 ID 1–143、v3/v4 契约。
5. 禁止降低测试标准；禁止重新 cmake 配置构建目录；禁止改 CI/打包文件。
6. 禁止在未验证构建状态前继续批次（R6 规则）。

## 6. 环境备忘

- 分支：`c-abi-migration`（已含 3 个抢救/修复提交）。
- 构建目录 `cmake-build-debug`（Ninja + Qt6，Debug）；asan/coverage 目录不要用。
- 测试素材 `tests/demo.mp4`、`tests/img.png`、`tests/project_with_footage.ove`。
- 本机有 GPU，Vulkan 用例真实执行；OpenGL offscreen 用例 SKIP 属正常。
- 全量 ctest 44+ 个约 90–140s。
- 单文件增量验证：`rm -f cmake-build-debug/app/CMakeFiles/libolive-editor.dir/<相对路径>.o && cmake --build cmake-build-debug --target olive-editor -j$(nproc)`。
- 恢复工具备忘：JetBrains LocalHistory（`~/.cache/JetBrains/CLion*/LocalHistory`）
  在 IDE 里按目录 Show History 可再挖；git fsck 悬空对象已查无可用内容。
