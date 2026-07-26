# Oak 主界面 UI 改版计划

> 本文是主界面重新设计的执行手册，面向 DeepSeek Flash（**不识字图，本文全部
> 用文字精确定义目标形态**）。详细程度对齐 `completed/r5-app-migration-guide.md`。
> 工作分支：`c-abi-migration`。**启动前提：R5（C ABI app 侧迁移）验收完成之
> 后**；启动后可与 Google Test 统一迁移并行——协调规则见 §2。
> 依据：`design/Oak-UI设计图-主界面-标注版.png`、`...-效果栈版.png`、
> `...-节点编辑器版.png`（共 10 项关键改动，本文逐项落地）。

---

## 1. 目标布局（文字定义，照此实现）

```
┌ 菜单栏：文件(F) 编辑(E) 视图(V) 回放(P) 序列(S) 窗口(W) 工具(T) 帮助(H)
├────────────────────────────────────────────────────────────────────
│ 素材查看器(源)        │ 序列查看器(节目) │  检查器│历史记录
│  ·适合/安全框         │  ·适合/安全框     │  ┌──────────────────┐
│  ·独立走带控制        │  ·节点编辑器(同组切换)│ 媒体 · xxx.mp4      │
│  ·1920×1080·25FPS    │  ·分辨率·帧率信息   │  ▼                 │
│                     │                │  ≡ 变换 ✓ ×(卡片)    │
│                     │  26px电平条(右缘) │  ≡ OCIO LUT ✓ ×(卡片)│
│                     │                │  [+ 添加效果]        │
│                     │                │  └──────────────────┘
├────────────────────────────────────────────────────────────────────
├ 工具条(31px)：14 个工具图标 + 吸附开关 + 缩放滑块 + 轨道高度滑块
│ 时间线(全宽贯通)                                                │
│ 轨道头180px │ 轨道区                                              │
│ V2 视频轨道1 [锁定][显示]  ████████                               │
│ V1 视频轨道0 [锁定][显示]  ████████████                           │
│ A1 音频轨道0 [锁定][静音][独奏] ▁▂▃▅▂▁                             │
│ A2 音频轨道1 [锁定][静音][独奏] ▁▂▁▃▂▁                             │
├────────────────────────────────────────────────────────────────────
│ 状态栏：就绪 | 缓存:已启用 | 代理:关 | 自动保存:3分钟前 || 时间码/时长 | 25FPS | 1920×1080
└────────────────────────────────────────────────────────────────────
```

节点编辑器视图（与序列查看器同组切换、占中央最大面板）：
```
┌ 节点编辑器（中央最大面板）
│  [+] [-] [适配]                                    ┌ 检查器(同效果栈) ┐
│  ┌ 第一稿.mp4[视频] · 00:00:00:00–00:04:18:18 ┐   │                  │
│  │ [媒体]→[变换]→[OCIO LUT]→[输出]           │   │                  │
│  └───────────────────────────────────────┘   │                  │
│  ┌ 第一稿.mp4[音频] · 00:00:00:00–00:04:18:18 ┐   │                  │
│  │ [媒体]→[音量]→[输出]                     │   │                  │
│  └───────────────────────────────────────┘   │                  │
│                                  ┌ 小地图 ┐  │                  │
└────────────────────────────────────────────────────────────────────
```

**核心原则：效果栈（检查器）与节点图是同一份节点数据的两种视图**——检查器按
「媒体 → 变换 → OCIO LUT → 输出」自上而下线性排卡片；节点编辑器把同一份数据
画成图。默认用户像传统软件一样在检查器里线性工作，需要分支合成时切到节点
编辑器。

## 2. 执行前提与并行协调

### 2.1 执行前提：R5 验收完成后启动

**本计划在 R5（C ABI app 侧迁移）验收完成之前不启动。** 这不是保守，是
依赖关系：

- WP4（检查器·效果栈）落在 `app/widget/nodeparamview/`、WP2（时间线轨道
  头）落在 `app/widget/timelinewidget/`、WP5（节点编辑器移位）落在
  `app/widget/nodeview/`——这些都是 R5 符号消除的主战场。R5 先把这些文件
  的 engine 调用点换到 facade（行为不变），本计划再在其上做 UI 重构，
  面对的才是干净的 facade 边界；提前动手只会和 R5 互相踩踏。
- R5 完成后不存在文件重叠问题，**全部 WP 无需错峰**，按 §4 顺序执行即可。

启动时仍需遵守的红线（R5 的成果，永久有效）：

- **禁止**：在本计划里改 `oakengine_*` 签名、新建 engine 命令类、或把
  engine 源码再编进 app。
- 全部改动限 **app 侧 UI 代码**（`app/`），不加 engine 符号。

### 2.2 与 Google Test 统一迁移（`gtest-migration-guide.md`）的并行协调

**结论：可以完全并行，无错峰要求。** 两份计划的文件域不相交：

| 计划 | 动的文件 |
|---|---|
| UI 改版（本文） | `app/`（widget、panel、window、dialog、ts 翻译） |
| GTest 统一迁移 | `tests/`、`engine/tests/`、`core/tests/` 及三处测试 CMake |

唯一的接触点与规则：

- **`tests/gtest/`**：GTest 迁移明确"不动已有 gtest"；本计划 §5 要求新增
  UI 逻辑用例，新用例**直接加进 `tests/gtest/` 现有 `olive-gtest` 目标**，
  不新建测试二进制。两边若同时改 `tests/gtest/CMakeLists.txt`，后提交者
  普通三路合并即可（都是追加行，不会语义冲突）。
- **ctest 基线**：GTest 迁移的硬门槛是"用例总数只增不减"；本计划只**新增**
  用例、不删不改旧用例，天然满足。两边都以全量
  `ctest --output-on-failure` 绿为提交前提，谁先跑谁后跑无所谓。
- **共享入口约定**：ctest 仍是唯一运行入口（gtest 仅编写框架），本计划
  新增用例同样遵守，不引入别的测试框架或独立 runner。
- **已知 flaky**（`oak_cli_transcode`、`oakengine_export_test`、
  `olive-gtest` 单次失败需单独重跑）是两个计划共同的背景噪音，判定规则
  相同：单独重跑一次，连续两次失败才算回归。

## 3. 工作包（WP1–WP10，对应设计图 10 项）

### WP1 取消独立「工具」面板 → 31px 工具条
- **现状**：`app/panel/tool/tool.{h,cpp}` 是独立停靠面板，~8% 屏幕仅放 14 个图标。
- **目标**：删除该面板；在时间线（`app/widget/timelinewidget/`）上方加一条 31px
  工具条，承载 14 个工具图标 + 吸附开关 + 缩放滑块 + 轨道高度滑块。
- **文件**：删/改 `app/panel/tool/`；`app/widget/timelinewidget/timelinewidget.{h,cpp}`
  顶部加工具条；`app/panel/CMakeLists.txt`、`app/panel/panelmanager.cpp` 注册点。
- **验证**：工具条全部按钮功能与原面板一致；布局保存/恢复无该面板。

### WP2 时间线全宽贯通 + 轨道头 180px
- **现状**：时间线未全宽；轨道头窄，无统一的 显示/静音/独奏/锁定 与 V/A 编号命名。
- **目标**：时间线全宽；轨道头加宽至 180px 贴住轨道；视频轨「显示」、音频轨
  「静音/独奏」、统一「锁定」；轨道以 `V2/V1/A1/A2` 编号 + 用途名（与主流 NLE 一致）。
- **文件**：`app/widget/timelinewidget/`（timelinewidget、trackview、trackviewitem、
  timelineview）。**前提：R5 已完成 Track/ClipBlock facade 化，本 WP 在其上重构。**
- **验证**：轨道头控件改变 track 的 lock/mute/solo/show 状态；命名正确；全宽布局。

### WP3 双监看并列（源 + 节目）
- **现状**：`app/panel/footageviewer/`（源）与 `app/panel/sequenceviewer/`（节目）分开，
  审素材对位需切换标签。
- **目标**：左右并排常显；素材查看器带独立走带控制（transport）。
- **文件**：`app/window/mainwindow/mainwindow.cpp` 布局；`app/panel/footageviewer/`、
  `app/panel/sequenceviewer/`（KDDockWidgets 分组/dock 关系）。
- **验证**：两查看器同屏并列；源查看器独立走带；布局可保存/恢复。

### WP4 参数编辑器 → 「检查器·效果栈」
- **现状**：`app/widget/nodeparamview/` 是参数编辑器（item 列表 + 标题栏 + 关键帧控件）。
- **目标**：改为「检查器」面板（与「历史记录」同组标签）。自上而下线性排：
  「媒体 · xxx.mp4」源卡 + 效果卡（变换、OCIO LUT、音量…）。**卡片规范**：
  标题行 = ≡(拖拽排序) ▼(折叠) 名称 ✓(启停) ×(移除)；底部「+ 添加效果」即搜即加。
  每属性行右侧保留关键帧按钮（现 `nodeparamviewkeyframecontrol`）。
- **文件**：`app/widget/nodeparamview/`（nodeparamview、nodeparamviewitem、
  nodeparamviewitemtitlebar、nodeparamviewdockarea、nodeparamviewwidgetbridge）；
  新增「检查器」容器/卡片模型。**前提：R5 已完成 Node 大族 facade 化，本 WP 在其上重构。**
- **验证**：卡片折叠/拖拽排序/启停/移除全部生效且写入节点图（undoable）；
  与节点编辑器视图数据一致；「+ 添加效果」可搜可加。

### WP5 节点编辑器移至中央最大面板 + 缩放/小地图
- **现状**：`app/widget/nodeview/` 节点编辑器非中央主区，大图易迷路。
- **目标**：移到中央最大面板（与序列查看器同组切换）；新增缩放控件（+/−/适配）
  与右下角小地图。
- **文件**：`app/widget/nodeview/`（nodeview、nodeviewcontext、nodeviewminimap）、
  `app/panel/node/`、`app/window/mainwindow/mainwindow.cpp`。**前提：R5 已完成 Node 大族 facade 化，本 WP 在其上重构。**
- **验证**：节点编辑器占中央；缩放/适配/小地图可用；大图导航不迷路。

### WP6 音频监视器 → 26px 电平条
- **现状**：`app/widget/audiomonitor/`、`app/panel/audiomonitor/` 占一个完整面板。
- **目标**：改为 26px 超薄电平条，贴附在节目查看器右侧常显，释放一个面板。
- **文件**：`app/widget/audiomonitor/`、`app/panel/sequenceviewer/`（宿主）、
  `app/panel/audiomonitor/`（去面板化）。
- **验证**：电平条 26px 常显、随播放电平跳动；原面板释放；布局可恢复。

### WP7 新增全局状态栏
- **现状**：`app/window/mainwindow/mainstatusbar.{h,cpp}` 仅显示 TaskManager 摘要。
- **目标**：全局状态栏显示：就绪状态、缓存、代理、自动保存时间（左）；
  当前时间码/时长、帧率、分辨率（右）。
- **文件**：`app/window/mainwindow/mainstatusbar.{h,cpp}`、`app/window/mainwindow/
  mainwindow.cpp`。
- **验证**：各项信息实时刷新；与序列状态/缓存/代理/自动保存一致。

### WP8 查看器细节
- **现状**：填充条为蓝色；缩放/安全框入口不全；信息芯片不全；有与时间线重复的标尺。
- **目标**：填充条改黑；查看器右上角提供缩放与安全框按钮；信息芯片标注
  分辨率与帧率；移除与时间线重复的标尺。
- **文件**：`app/panel/footageviewer/`、`app/panel/sequenceviewer/`、
  `app/widget/viewer/`（viewer、viewerdisplay）。
- **验证**：外观与信息符合 §1 描述；无重复标尺。

### WP9 菜单访问键补全
- **现状**：`app/window/mainwindow/mainmenu.{h,cpp}` 的「窗口」无访问键 (W)。
- **目标**：「窗口」加 `(W)`，与系统及其他菜单项一致。
- **文件**：`app/window/mainwindow/mainmenu.{h,cpp}`。
- **验证**：菜单访问键完整一致。

### WP10 文案与格式统一
- **现状**：History 未汉化；项目面板日期为英文格式；首选项有英文残留。
- **目标**：History → 历史记录；项目面板日期改 `YYYY-MM-DD HH:mm`（如
  `2026-06-03 20:25`）；首选项英文残留（Behavior、Enable hover focus、
  `1 minute(s)` 等）全部汉化。
- **文件**：`app/panel/history/`、项目面板（`app/widget/projectexplorer/`）、
  首选项（`app/dialog/preferences/`）、`app/ts/*.ts` 翻译。
- **验证**：文案全部汉化、日期格式统一。

## 4. 执行顺序

R5 已验收完成（§2.1），无错峰约束。建议先做无依赖的布局项热身，再做
重构量大的三个 WP：

```
第一波（布局类，互相独立）: WP1 → WP3 → WP6 → WP7 → WP8 → WP9 → WP10
第二波（重构类，建议在 R5 facade 化后的干净边界上做）:
  WP4（检查器）→ WP5（节点编辑器）→ WP2（时间线轨道头）
```

**每 WP 闭环**：现状 grep → 实现 → 全量构建 0 error → 全量 ctest 44/44 绿
（UI 改动不得引入回归）→ 立即提交 → roadmap 补记。

## 5. 测试要求（沿用项目规则）

- 所有测试用 **Google Test**（`CONTRIBUTING.md` 已立规）。
- 检查器卡片模型、效果栈↔节点图数据一致性、轨道头控件状态、状态栏信息、
  工具条功能：补 Google Test 用例（`tests/gtest/`，`gtest_discover_tests`）。
- UI 行为改动以现有 `olive-gtest` 不回归为底线；新增可测逻辑（卡片模型、
  视图模型）必须有单测。
- 需要显示的用例沿用 offscreen QPA；渲染相关用例沿用 `GTEST_SKIP` 模式。

## 6. 不做

- 不改 `oakengine_*` 公共 API（见 `riir.md` 的 API 冻结保证）。
- 不动 engine 内部实现、不动 R5 的 facade 工作。
- 不重写底层渲染/播放路径；本计划只改 UI 布局、容器与交互。
- 不做 AI 相关 UI（属 `ai-agent-design.md` 范围，另行）。
