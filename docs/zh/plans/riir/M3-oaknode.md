# M3 · oaknode 拆分手册

> 内容：`engine/node/`（Node/NodeGroup/NodeKeyframe/NodeInput/
> NodeValue/NodeFactory/NodeTraverser/NodeGroup/Project/Folder/
> Sequence/Track/TrackList/Block/ClipBlock/Footage/Serializer/
> ColorManager/各类节点实现）。
> 被依赖：timeline 32、task 38、render 38、plugin 6、codec 3、capi 110。
> 拆分顺序第 3 位（最大的模块；**链接级独立推迟到 M7 复核**，裁决
> 见 02 §4）。

## 1. 目标形态

```
oaknode/
  include/oaknode/   # node.h, project.h, track.h, block.h, footage.h,
                     # keyframe.h, factory.h, traverser.h, serializer.h,
                     # colormanager.h, types.h, export.h
  src/               # 现有 node/ 全量迁入（含全部具体节点实现子目录）
  tests/             # oaknode_gtest
```

## 2. 冻结 C API（跨界类清单）

跨界类 = 被其他模块引用的类（来源：02 依赖矩阵的热门头统计）。
每类按 01 §1 生成 `init/free/func`，此处冻结**函数族与特殊约定**
（逐函数签名照 oakengine 现有 facade 同族对齐——oakengine/node.h、
project.h、timeline.h 的对应函数就是模板，参数命名前缀换
`oaknode_`）：

| 类 | 主要消费方 | 函数族（每族含 init/free） |
|---|---|---|
| Node | 全部 | label/color/enabled 存取、input 列举与存取（`oak_node_value`）、connect/disconnect、output_connections 枚举、links、context positions、id() 字符串 |
| NodeGroup | nodeparamview 链 | passthrough add/remove/列举、resolve_input |
| NodeKeyframe | curve/keyframe | time/value/type/bezier 存取（live + undoable 变体） |
| NodeFactory | factorymenu、构造点 | id_count/id_at/name_from_id/create_from_id/node_at |
| NodeTraverser | nodevaluetree 等 | traverse db 创建/行枚举/释放 |
| Project | task/render | root、name/filename/cache_path、modified、is_modified、add/remove_node |
| Folder | projectexplorer | child 增删/列举/move_children |
| Sequence | timeline/render | track_list、workarea、markers、video/audio params、playhead |
| Track / TrackList | timeline | height/mute/lock/index/type、block 增删、split/ripple 原语 |
| Block / ClipBlock / GapBlock / TransitionBlock | timeline | in/out/length/media_in、speed/reverse/loop、links |
| Footage | task | filename、streams、proxy、duration |
| ProjectSerializer | task | save/load/copy/paste（clipboard 族，照 oakengine/serializer.h 模板） |
| ColorManager | render | config、default config、display transform |

**特殊约定**：
1. undoable 变体与 live 变体成对（`_live` 后缀或 `, void *command`
   尾参），与 oakengine 现状一致。
2. 事件：Node 族事件（label/input/keyframe/context 等）经
   `oaknode_subscribe(handle, event_id, fn, userdata)`——事件 ID 表
   直接沿用 `oakengine/events.h` 的 70-95 段（值不变，便于
   EngineEventBridge 逐步换绑）。
3. `Node *`、`Project *` 等句柄即 `OakNodeNode *`/`OakNodeProject *`，
   不透明。
4. 虚函数不出模块（01 §5）；具体节点类型经
   `oaknode_factory_create_from_id` 构造，消费侧不碰子类。

## 3. 切割点（node 的对下引用）

| 现状（次数） | 处理 |
|---|---|
| node → render/ 47（colorprocessor 8、videoparams 5、footagejob 4、rendermanager 3、pluginjob 3 等） | videoparams/colortransform 随 M3.5 下沉 oakcommon；其余 **M7 时**改经 oakrender C ABI（02 §4 裁决 A：M3 暂不断链，禁止新增） |
| node → codec/ 8（decoder 4、frame 2、encoder 1、proxymanager 1） | M5 时改经 oakcodec C ABI（M5 手册已含 frame/decoder 家族） |
| node → timeline/ 5（timelinecommon 2、marker 1、workarea 1、timelineundogeneral 1） | timelinecommon 的枚举/常量下沉 oakcommon/types.h；marker/workarea 引用（均在 node/project/ 序列化路径）M4 时改经 oaktimeline C ABI |
| node → audio/ 4 | M6 时改经 oakaudio C ABI |
| node → undo/ 4（undocommand.h） | M2 后改 include oakundo 公共头 + 适配类 |

M3 阶段判据（放宽版）：oaknode 目录就位、C API 实现、oaknode_gtest
全绿、对上述各向**无新增引用**（grep 快照对比）。M7 后复核
"oaknode 只经 C ABI 调下"。

## 4. 测试（映射 03 §2/§3）

- 重点：Node 增删连边、Project/Folder 层级、Track 属性、
  keyframe live/undoable 对称、serializer 剪贴板往返、
  factory 枚举与创建。
- 事件：每族至少 1 个 subscribe/trigger/unsubscribe 用例。
- 枚举序数：NodeValue::Type ⇄ oak_node_value_type 映射表（已在
  nodevaluehandle.h 钉过一次，oaknode 测试再钉一次，防两侧漂移）。
- `oaknode_debug_alive_count()` 泄漏断言。
