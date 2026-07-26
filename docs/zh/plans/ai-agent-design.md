# AI Agent 设计文档（RIIR 拆分后长期规划）

> 本文是 Oak 引入 AI 能力的长期设计，**执行前提是 RIIR 绞杀者拆分完成**
> （见 [`riir.md`](riir.md)）。彼时 `liboakengine.so` 已不存在，取而代之的是
> 一组以纯 C ABI 为缝的小动态库。本文面向没有当前对话记忆的执行者，自包含。
>
> **一句话**：把多模态 LLM 当成引擎 C ABI 的**第三个一等消费者**
> （继 oak-cli、oak-render-worker 之后），用 MCP 暴露策展过的工具面，
> 用渲染管线把帧喂回给多模态模型，形成"编辑 → 看图 → 再编辑"的视觉闭环。

---

## 1. 定位与前提

### 1.1 前提（未满足不动工）

- RIIR 拆分战役完成：引擎已拆为 §2.1 的小库；各库导出仅 C 符号；
  `oakengine_*` facade 壳稳定且全量测试绿。
- Google Test 已是唯一测试框架；`gtest_discover_tests` 已接入。
- 本文不改动 RIIR 既定的模块划分，只在模块化树上**新增叶子**。

### 1.2 设计铁律（继承自迁移/拆分战役）

1. AI Agent **只经 C ABI** 访问引擎，一行 engine C++ 都不碰；不污染符号边界。
2. Agent 的一切编辑动作**必须可撤销**（undoable 原语），UI 默认"确认后执行"。
3. 不为 AI 发明新的引擎内部机制；工具面是现有 facade/小库的组合。
4. 引擎各模块**不得新增 Qt 依赖、不得新增 QObject 信号/moc 类**。

## 2. 总体架构

### 2.1 在模块化树上的位置

```
app / oak-cli / oak-render-worker / oak-agent / oak-mcp-server
                     │
            liboakengine-facade（壳：capi + 事件 + init）
                     │
   ┌────────┬────────┼─────────┬──────────┐
 oaktask  oakrender  oakplugin  oakaudio  oakserialize
   │        │         │          │          │
   └────────┴────┬───┴──────────┴──────────┘
                 │
        oakmodel（节点图 + 项目模型 + 时间线模型）
                 │
        ┌────────┼─────────┐
     oakcodec  liboakcore  oakbackend（GPU 插件）
        │
   ffmpeg_bridge
```

**新增三个叶子组件**（与 oak-cli、oak-render-worker 平级，都是纯消费者）：

- **`oak-mcp-server`**：把策展过的工具面经 MCP 暴露给任何 LLM 客户端。
- **`oak-agent`**：无头 Agent 运行时（对话编排 + 视觉闭环），供脚本/CI/本地使用。
- **editor AI 面板**：app 内的聊天/操作日志/确认界面，与 `oak-agent` 复用同一工具面。

AI 功能**不进入** oakmodel、oakrender 等引擎模块，引擎核心对 AI 无感知。

### 2.2 视觉闭环（本设计的核心）

```
多模态 LLM ──► oak-agent ──► facade/小库执行编辑 ──► 渲染取帧 ──► PNG ──► 回喂 LLM
   ▲                                                              │
   └──────────────── 看图判断（效果/切点/内容定位） ◄───────────────┘
```

- **验证式**：每次编辑后取一帧，LLM 判断"效果对不对"。
- **内容感知式**：沿时间线批量取缩略图拼 contact sheet，LLM 扫图定位
  （"人何时进画面""哪里该切"），Agent 据此下刀——自动粗剪/打点的雏形。
- **连续回放**：经 playback 族起范围播放，按间隔采样帧。

## 3. 工具面（策展，非全量 facade）

**不暴露全部 ~200+ facade 函数**，而是策展约 25 个高层工具，每个是 facade/小库
的组合。`oak-mcp-server` 内部就是一个薄模块，链接 facade 壳与相关小库。

| 工具 | 落到哪个库 | 说明 |
|---|---|---|
| `create_project` / `open_project` / `save_project` | facade 壳 + oakmodel | 工程生命周期 |
| `probe_media` / `import_footage` / `get_media_info` | oakcodec + facade 壳 | 媒体探测与导入 |
| `add_track` / `add_clip` / `trim_clip` / `ripple` / `add_transition` / `add_marker` | oakmodel（经 facade 时间线族） | 时间线编辑 |
| `add_effect(effect_id)` / `set_param` / `set_keyframe` | oakmodel（经 facade node 族） | 节点与关键帧 |
| `apply_lut` / `set_color_transform` | oakmodel + facade color 族 | 调色 |
| `get_frame(time)` / `get_thumbnails(range,n)` / `get_audio_levels` | oakrender + oakbackend | **视觉闭环的取帧口** |
| `export_render(params)` | oaktask + oakcodec | 导出 |

**取帧→PNG 通路**（视觉闭环关键路径）：
`get_frame` 经 oakrender 的预览请求得到 RGBA 帧（POD：`宽/高/字节流`），
再经 oakcodec 的 OIIO 编码器出 PNG，base64 后作为图片消息发给 LLM。
缩略图用同一路径降采样，多张拼 contact sheet。

## 4. 协议：MCP（Model Context Protocol）

工具协议**定为 MCP**，理由：

- render-worker 已在用 **NDJSON over stdin/stdout 的 IPC**——MCP 本质是该模式
  的标准化，实现路径一致。
- 暴露成 `oak-mcp-server` 后，**外部 LLM 客户端（Claude Desktop、各类 agent
  框架）可直接连接复用**，无需自研对话编排。
- `oak-agent` 与 editor AI 面板都连同一个 MCP server，**一份工具面，多处消费**。

## 5. 模型层

抽象 `LLMProvider` 接口（输入：消息 + 图片；输出：文本 + tool_calls），两个后端：

- **云端**：Claude / GPT 多模态（效果优先）。
- **本地**：llama.cpp 跑 Qwen-VL / LLaVA 类多模态模型（隐私、离线优先）。

API key 只走环境变量，**绝不写入 config / 工程文件**。无 key 时优雅降级为
"仅本地工具"（仍可用 MCP，但不做对话编排）。

## 6. 安全

- **可撤销**：所有编辑走 undoable 原语；AI 面板提供"撤销整段会话"。
- **确认模式**：默认每次 Agent 动作需用户确认才 apply；可切换自动模式。
- **沙箱会话**：Agent 默认在临时工程中操作，用户接受后才落盘到真实工程。
- **资源**：取帧/扫描限帧率与分辨率上限，防止批量取帧拖垮渲染进程。

## 7. 可测试（与项目风格一致）

1. **Mock LLM server**：录制/回放 tool_call 序列与固定回复，让 Agent loop 在
   CI 无 key 无网络跑通（Google Test）。
2. **黄金帧校验**：复用 render-worker 端到端 harness（真实渲染 ≥2 帧 +
   像素非全黑 + 一致性断言），验证"Agent 的编辑确实改变了画面"。
3. **会话回放**：tool_call + 帧哈希落盘日志，可回放复现、可作测试夹具。

## 8. 里程碑（RIIR 完成后启动）

1. **M1 工具面**：`oak-mcp-server`（facade → MCP，~25 工具）+ 取帧→PNG 通路。
2. **M2 无头闭环**：Mock LLM + `oak-agent`，跑通"LLM→工具→取帧→回喂"，
   CI 可测（无网络）。
3. **M3 AI 面板**：editor 内聊天 + 操作日志 + 确认模式 + 撤销会话。
4. **M4 本地模型与内容感知**：llama.cpp provider、时间线扫描打点、自动粗剪。

## 9. 风险与边界（明确不做）

- **不**把 LLM/推理放进 oakmodel 或任何引擎模块（引擎对 AI 无感知）。
- **不**为 AI 绕过 C ABI 直接调 engine C++（边界不污染）。
- **不**把 API key 落盘到工程/config。
- **不**让取帧回路阻塞 GUI 线程（取帧走渲染/后台路径，UI marshal 回主线程）。
- 第三方大模型客户端的接入细节（OAuth、计费、配额）**超出本文范围**，按需另立文档。
