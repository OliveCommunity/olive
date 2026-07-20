# liboakengine Facade 迁移路线图

本文把"编辑器 UI 迁移到 liboakengine C ABI(facade)"这一独立工程拆解为可执行的阶段。背景与已完成的基础见各提交:`28c442623`(引擎拆分)、`bff06e00e`(Core 拆分)、以及 facade 各族提交(ipc/init/project/timeline/renderer/footage/export + 两轮编辑原语)。

## 终态定义

1. `liboakengine.so` 只导出 `oakengine_*` C 符号(`nm -D --defined-only` 除 `oakengine_*` 外为零,与 liboakcore 相同标准)。
2. oak-editor 与 oak-render-worker 不再引用任何引擎 C++ 符号,全部经 facade(或其 C++ 包装层,模式同 liboakcore 的 wrapper)访问引擎。
3. 全部现有测试(gtest + CLI ctest + 各 C ABI 测试)持续全绿,GL/Vulkan 相关用例可跳过。

## 现状盘点(已完成)

- 引擎物理拆分完毕:`engine/` 树 + liboakengine.so;worker 脱 UI(336MB→2.9MB)。
- facade 13 族约 160+ C 函数:ipc/init/project/timeline/renderer/footage/export/node/keyframe/序列结构/编辑原语×2/preview/playback(在建);oak-cli 是现成的纯 C ABI 消费者(info/probe/render/transcode,已进三平台打包)。
- 面板迁移(阶段 3)已切:Export 对话框、素材属性/项目浏览器、序列参数、时间线(三轮,含遗留命令)、节点参数面板、关键帧区;app 侧约定为直接调 C 函数。
- 编辑器与引擎的 UI 依赖已全部反转(Core/EngineCore 拆分、handler 钩子体系、枚举下沉、布局数据纯净化)。
- 测试基建:make_oakengine_test、GL 门控 SKIP 模式、CLI ctest、沙箱 config。

## API 缺口(按 UI 需求倒推)

facade 现状覆盖:项目/序列读写、素材探测与导入、时间线查询与片段编辑、渲染、导出、IPC。缺口按优先级:

1. **节点图族(oakengine_node_*)**——最大缺口,编辑器操作的实体。
   - 节点枚举/查询:type id、label、category、输入定义(id/类型/默认值/属性)。
   - 参数读写:get/set_input(类型化值,参照 NodeValue::Type 到 C 类型映射:float/int/rational(num,den)/color(rgba)/string/bool/vec234)。
   - 图操作:add_node(factory id)、remove_node、connect(input,output)/disconnect。
   - 效果链读取:某 sequence/clip 的效果栈遍历。
2. **关键帧族(oakengine_keyframe_*)**:增删改、时间/值/缓动(bezier 四点)、按参数遍历。
3. **序列结构族**:轨道高度、静音/锁定、轨道重命名/重排/删除;marker 增删改;播放头 scrub 状态。
4. **选择/命令族**:undo 命令描述(label)、按命名命令打包的编辑操作(对齐 app 命令名,便于 UI 直接映射)。
5. **素材管理族**:relink、代理状态查询/生成(ProxyManager 已有引擎实现,直接包)、footage 参数覆盖。
6. **预览控制族**:播放/暂停/倍速、音频表读数(AudioMonitor)、scrub、工作区循环。

## 阶段计划

### 阶段 0:冻结契约(1 周)
- 将 facade 头文件与语义写进 `docs/facade-api-reference.md`(从各头 Javadoc 生成骨架)。
- C++ 包装层生成器/模板定稿(照 liboakcore wrapper 模式,按族手写而非生成,规模可控)。
- CI 增加 facade 测试目标(make_oakengine_test 已全部进 ctest,确认 Win/mac 也跑)。

### 阶段 1:worker 与 CLI 对齐(1-2 周)
- oak-render-worker 内部一律改用 oliveimpl 内部头(当前已是),确认其**不依赖** facade; facade 专为外部消费者。
- oak-cli 覆盖到 proxy 生成/媒体信息全命令,作为 facade 回归基准。

### 阶段 2:节点图族(2-4 周)
- oakengine_node_* 设计与落地(参数值映射表先行)。
- 测试:程序化建图(Solid→LUT→渲染)、参数往返、连接校验、undo。
- **里程碑**:oak-cli 能 `oak-cli graph <ove>` 打印效果栈,`oak-cli lut <ove> <lut>` 给指定 clip 加 LUT。

### 阶段 3:编辑面迁移(4-8 周,按面板逐个)
顺序(依赖递增):
1. **Export 对话框**—— facade export 族已就绪,最先切,删 UI 侧 ExportTask 直驱。
2. **Footage 属性/项目浏览器**—— footage 族 + 素材管理族。
3. **序列参数/时间标尺/工作区**—— timeline 族已就绪。
4. **时间线面板**—— 需要选择族+编辑原语全集;UI 动作→facade 命令一一映射(参照 timelineundogeneral 的命名)。
5. **节点参数面板/效果控件**—— 需要节点图族+关键帧族。
6. **预览/播放**—— 预览控制族。
7. **多机位/标记/其余面板**。

每切一个面板:该面板的 UI 代码改为只调 facade(或 wrapper),对应引擎类从 UI 侧 include 中移除;全量测试必须保持绿。

### 附 A:viewer 面板迁移映射(阶段 3.6 细化)

地基:facade 播放族(`oakengine_playback_*`,无头异步播放引擎)。`ViewerWidget` 当前自管的整条播放管线替换为 facade 播放会话;消费方式与其余面板一致(app 侧直接调 C 函数)。

**迁入 facade(从 ViewerWidget 删除):**
- `playback_timer_update` / `playback_backup_timer_` → facade 拉取线程 + frame 回调,UI 在回调里 marshal 回主线程调 `set_display_image`。
- 视频 prequeue(`prequeue_length_`/`prequeue_count_`/`queue_watchers_`/`request_next_frame_for_queue`/`renderer_generated_frame_for_queue`)→ facade 内部 8 帧 prequeue。
- 音频播放队列(`audio_playback_queue_`/`audio_processor_`/`prequeued_audio_`/`k_audio_playback_interval`/`queue_next_audio_buffer`/`received_audio_buffer_for_playback`/`decrement_prequeued_audio`)→ facade audio 回调(1/4 秒 planar float 块)。
- 音频主时钟:facade 内部直接用 `AudioManager`(已在 `engine/audio/`)的秒时钟;UI 侧改为轮询 `oakengine_playback_get_position` 更新播放头。

**留在 UI 侧(facade MVP 不覆盖):**
- 负倍速 / shuttle(`shuttle_left/right`、`playback_speed_` 变速路径)——后续再议。
- 音频刮擦(`audio_scrub_watchers_`、`push_scrubbed_audio`)——逐帧点播,非连续播放,保留现有 watcher 路径。
- 录制/capture(`arm_for_recording`/`disarm_recording` 等)。
- 纯 UI:gizmos、文本编辑、安全框、多屏 `ViewerWindow`、波形视图、右键菜单。
- multicam 检测(`detect_multicam_node`)——依赖 UI 选择状态。
- in/out 区间播放(`play_in_to_out_only`)——UI 在 frame 回调里判断到点自行 pause,无需 facade 支持。

**验收**:播放/暂停/停止下帧画面与声音同步;时间标尺播放头跟随;全量 gtest + facade 回归(固定 9 个 + playback)绿;viewer.h 中不再出现 `RenderTicketWatcher` 播放队列字段。

### 阶段 4:隐藏 C++ 符号(1 周)
- app/worker 的引擎符号引用清零后:visibility=hidden + version script(`oakengine_*` 白名单)。
- 验收:`nm -D` 仅 `oakengine_*`;全部测试(含 CLI)绿;三平台打包复验。

## 风险与对策

- **节点参数类型膨胀**:NodeValue 有 ~20 种类型。先支持编辑器最常用的 8 种(float/int/bool/string/rational/color/vec/combo),其余按面板需要逐个加。
- **性能**:参数级 facade 调用频率低(用户操作粒度),不构成问题;渲染/帧路径已有同步封装。
- **Qt 对象生命周期**:节点/序列/clip 均为 borrowed handle 随 project;文档已有约定,迁移时沿用。
- **undo 语义**:所有编辑原语必须经全局 UndoStack(facade 已强制),UI 侧不得再直接 new 命令。

## 验收清单(阶段 4 完成时)

剩余耦合度可量化复测(在构建目录下执行;阶段 3 推进期间应单调下降,2026-07 基线为 565):

```sh
nm -u app/oak-editor | awk '$1=="U"{print $2}' | grep '^_ZN5olive' | sort -u > /tmp/u.txt
comm -12 <(nm -D --defined-only engine/liboakengine.so | awk '{print $3}' | sort -u) /tmp/u.txt | wc -l
```

- [ ] 上述耦合计数降为 0
- [ ] `nm -D --defined-only liboakengine.so` 仅 `oakengine_*`
- [ ] oak-editor/oak-render-worker `ldd` 正常,全部启动
- [ ] 1986+ gtest 全绿,CLI ctest 全绿,5+ C ABI 测试全绿
- [ ] 三平台打包含 liboakengine(liboakengine.so/dylib/oakengine.dll),Linux 位于标准 libdir
