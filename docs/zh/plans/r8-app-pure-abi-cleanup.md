# R8 计划：app/ 纯 C ABI 头文件清理

> 目标：app/ 下的代码只 include engine/ 的 C 头文件（`oakengine/*.h`），
> 不再 include 任何 engine 内部 C++ 头。
> 前置：R7 已完成（nm U _ZN5olive = 0，liboakengine.so 导出收口）。

---

## 1. 纯头工具共享层（shared/）

### 1.1 新建目录

```
shared/
  include/
    oakutil/
      define.h          ← engine/common/define.h
      lerp.h            ← engine/common/lerp.h
      decibel.h         ← engine/common/decibel.h
      digit.h           ← engine/common/digit.h
      range.h           ← engine/common/range.h
      crashpadutils.h   ← engine/common/crashpadutils.h
      qtutils.h         ← engine/common/qtutils.h（声明）
      filefunctions.h   ← engine/common/filefunctions.h（声明）
      xmlutils.h        ← 精简版（XMLAttributeLoop 宏 + xml_read_next_start_element）
```

### 1.2 CMake 接入

顶层 CMakeLists.txt 添加：
```cmake
list(APPEND OLIVE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/shared/include)
```

### 1.3 迁移规则

- engine/common/ 中被移出的 .h：原位置改为 `#include "oakutil/xxx.h"` 的转发头
  （保持 engine 内部现有 include 路径不断）。
- app/ 中所有 `#include "common/xxx.h"`（指 engine 的）改为 `#include "oakutil/xxx.h"`。
- qtutils.h / filefunctions.h：声明移到 shared，.cpp 实现分别留在
  engine/common/（engine 用）和 app/common/（app 用，已有）。
- xmlutils.h 精简版：只保留 XMLAttributeLoop 宏 + 无 CancelAtom 的函数声明；
  engine 内部完整版保留在 engine/common/xmlutils.h（include 精简版 + 追加
  CancelAtom 重载）。

### 1.4 验证

```bash
grep -rn '#include "common/' app/ | grep -vE '"common/(colorcodingapp|configwrapper|nodevaluehandle|oakvaluehelper|undowrapper|debugapp|filefunctionsapp|hashstreamapp|htmlapp|qtutilsapp|xmlutilsapp)'
# 期望：0 结果
```

---

## 2. engine/ui/colorcoding.h → app 本地化

engine/ui/colorcoding.h 是 C++ 类（ColorCoding），被 app 5 处引用。
方案：在 app/common/ 下新建 `colorcoding.h`，通过 C ABI 或本地静态表实现。
engine 内部保留原文件。

---

## 3. engine C++ 类 → C ABI 替换（按子系统）

按影响面排序，每批一个 PR：

| 批次 | 子系统 | 违规 include | 替换方案 |
|---|---|---|---|
| P2 | node/value.h + node/keyframe.h | ~10 处 | oakengine/node.h 的 oak_node_value_type 枚举 |
| P3 | node/node.h + node/param.h | ~20 处 | OakEngineNode* + oakengine_node_* 函数（执行手册：[r8-p3-node-param-abi.md](r8-p3-node-param-abi.md)） |
| P4 | node/project*.h | ~15 处 | OakEngineProject*/OakEngineSequence* |
| P5 | render/* | ~10 处 | oakengine/display.h + viewer.h |
| P6 | timeline/* | ~6 处 | oakengine/timeline.h |
| P7 | codec/* + audio/* | ~10 处 | oakengine/encoding.h + audio.h |
| P8 | tool/* + undo/* | ~20 处 | 新增 C ABI 或 app 本地枚举 |
| P9 | pluginSupport/* | ~6 处 | oakengine/plugin.h |

---

## 4. CMake 收口（最终步骤）

- 移除 `target_link_libraries(olive-editor PRIVATE oakengine-obj)`
- 从 `oakengine` 共享库 PUBLIC include 中移除 engine 根目录
- 只保留 `engine/include`（C ABI 头）

验证：
```bash
grep -rn '#include "' app/ | grep -E '"(node|render|timeline|codec|audio|task|undo|tool|pluginSupport|ui/colorcoding)/'
# 期望：0 结果
cmake --build build && ctest --test-dir build --output-on-failure
```

---

## 状态

- [x] Phase 1：shared/ 纯头工具层（define, lerp, decibel, digit, range, crashpadutils, qtutils, filefunctions, xmlutils, autoscroll）
- [x] Phase 2：node/value.h + node/keyframe.h 直接 include 已全部移除
  - app/common/nodevaluehandle.h 提供 AppNodeValueType 本地枚举
  - app/common/oakvaluehelper.h 提供 AppKeyframeType + 转换函数
  - NodeValue::Type → int, NodeValue::k_* → AppNodeValueType 常量
  - keyframeproperties.h 改为前置声明 NodeKeyframe
- [x] Phase 3：node/node.h + node/param.h 主体清理完成，**以双适配器（oak:: wrapper）形态落地**，
  消费侧统一经 `shared/include/oakutil/oaknode.h` 访问 engine，AppNodeInput 方案已废弃
  （落地细节与 WRAPPER-GAP 登记见 [r8-p3-node-param-abi.md](r8-p3-node-param-abi.md) 的"落地状态"一节）
- [x] Phase 4-9 + 补充批（2026-07-29 完成）：node/project*、render/*、timeline/*、codec/*、audio/*、
  tool/*、undo/*、pluginSupport/* 及 P3 遗留（block/track/clip/gizmo/factory 等）全部清理，
  同样以双适配器形态落地：
  - engine 侧新增 30 个 C ABI 函数（block/track/clip/transition 导航与谓词、链接、
    thumbnail/waveform/frame cache、playback cache、disk folder、sequence_track_list、
    visible_block_at_time、set_length_and_media_out、node_free、footage_is_valid、
    block_get_track 等）
  - 新增 app 侧构件：`shared/include/oakutil/oakvideo.h`（oak::VideoParams/ColorTransform）、
    `app/common/`（tooltypes.h、trackreferencehandle.h、keyframetypes.h、subtitleapp.h、
    serializedlayoutinfoapp.h、nodedatatypes.h、projecttypes.h、sliderdisplaytypeapp.h）、
    `app/timeline/timelinecommonapp.h`（TimelineApp 枚举镜像）、
    `app/widget/history/historywidget.{h,cpp}`（HistoryModel，C ABI 驱动）
  - `app/core.h` 不再 include engine `coreengine.h`（Tool/Timecode/Color 改经镜像与 olive/core）
- [x] 类型清洗（2026-07-29，Wave4/5）：app/ 内部接口不再持有 engine C++ 类型
  （olive::Node*/Block*/ClipBlock*/Track*/Sequence*/ViewerOutput*/TimelineMarker* 等全部
  换成 OakEngine* 句柄或 oak:: wrapper），~600 处双向 reinterpret_cast 消除；
  engine 侧配套新增 `oakengine_node_get_brush`（Qt QBrush 越界，照 QPainter* 先例）。
  全树 grep 终验：代码级 engine 类型使用仅剩 1 处论证例外（见下）。
  全量链接构建 0 error。
- [ ] Phase 10：CMake 收口

### Phase 4-9 已知遗留

- `app/widget/timebased/timebasedwidget.h` 仍 include engine `node/output/viewer/viewer.h`
  + `timebasedwidget.cpp:104` 的 `ViewerOutput*` 桥接：
  `QPointer<ViewerOutput>` 需要完整 QObject 类型，C ABI 无节点销毁事件与 QObject* 访问器，
  无法等价替代（文件内已论证并标 WRAPPER-GAP；是 app/ 唯一保留的 engine 类型使用）。
- WRAPPER-GAP 登记（app 代码内注释，后续 facade 扩充时清理）：
  undo 命令装配、group passthrough、traverse、clipboard、NodeValueRow create/free
  （viewerdisplay 的 gizmo_db_）、oak::Footage 的 proxy 家族方法、TrackList 枚举
  （oakengine_sequence_track_list 暂无查询函数）、VideoParams::stream_index、
  Node::get_context_positions、waveform validated ranges 专用 ABI、
  playback_cache_draw 缺 y 偏移参数、泛型 QVariant input property 读取
  （widgetbridge 以类型化 getter 重建）、engine keyframe has_sibling/closest 系
  "整秒"契约与 engine 语义不符（app 侧已用精确 rational 路径规避）、
  plugin CAdapter 的 is_cancelled 回调未接（取消不生效，engine 侧缺口）。


### Phase 1 已知遗留

- `app/widget/timebased/timebasedwidget.cpp` 仍引用 `common/current.h`（依赖 pluginSupport + render，需 Phase 2+ 处理）
- app/ 仍引用 3 个 engine 内部头（不在 Phase 1 清单内，待后续阶段处理）：
  `common/commandlineparser.h`（app/main.cpp）、`common/crashpadinterface.h`（app/main.cpp）、
  `common/dropworkflowbehavior.h`（app/widget/timelinewidget/tool/import.h）
- `engine/node/project.h` 原先经 `common/xmlutils.h` 间接获得 `NodeGroup` 前置声明，
  精简后已改为在 project.h 内显式前置声明

### Phase 2 已知遗留

- `node/node.h` 仍被 11 个 app/ 头文件直接引用（widget 层深度使用 NodeInput、
  NodeKeyframeTrackReference、Node::Position、Node* 方法调用）—— 待 Phase 3 处理
- `node/param.h` 仍被 4 个 app/ 头文件直接引用（NodeInput 类型）
- 其他 engine 内部头引用（render/, timeline/, codec/, audio/, node/project*,
  node/output/, pluginSupport/, tool/, undo/）—— 待 Phase 4-9
