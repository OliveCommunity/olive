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
- `ViewerPlaybackTimer`(`display_widget_->timer()` 的音频钟→时间戳换算)整体被 `get_position` 取代,`viewerplaybacktimer.{h,cpp}` 删除。
- `finish_play_preprocess` 中的 `reset_output_clock`/prequeued 音频推送/备份定时器(1385-1421)→ facade `start` 内部完成。

**复用不动(帧显示路径)**:`ViewerDisplayWidget` 的 `queue()->append_timewise` + 显示定时器整套保留——facade frame 回调 marshal 到主线程后走与今天 `renderer_generated_frame_for_queue`(viewer.cpp:1562)完全相同的路径;`dw->play/pause` 照旧。即只换帧的**来源**,不换帧的**去向**。

**留在 UI 侧(facade MVP 不覆盖):**
- 负倍速 / shuttle(`shuttle_left/right`、`playback_speed_` 变速路径)——后续再议。
- 音频刮擦(`audio_scrub_watchers_`、`push_scrubbed_audio`)——逐帧点播,非连续播放,保留现有 watcher 路径。
- 录制/capture(`arm_for_recording`/`disarm_recording` 等)。
- 纯 UI:gizmos、文本编辑、安全框、多屏 `ViewerWindow`、波形视图、右键菜单。
- multicam 检测(`detect_multicam_node`)——依赖 UI 选择状态。
- **边界与循环策略**(`playback_timer_update` 1959-2044 的 min/max 计算、workarea in-out、`StopPlaybackOnLastFrame`、`Loop` 配置、录制范围)——依赖 UI 配置与状态,保留在一个轮询 facade 位置的 UI 定时器里;到点由 UI 调 facade `pause`/`start`(循环时重锚)。

**facade 播放族需随迁移做的细化**:
- `pause` 时应停 AudioManager 输出(engine 内部,instance 判空),否则缓冲区余音继续播;app 不再直接碰 AudioManager。
- 音频监视器电平:facade audio 回调 → UI marshal → `AudioMonitor::push_sample_buffer_on_all`(AudioMonitor 是 app 侧控件)。
- 波形监视器:`AudioMonitor::start_waveform_on_all` 仍在 UI 于 start 时发起(依赖 connected node 的 waveform 元数据)。

**验收**:播放/暂停/停止下帧画面与声音同步;时间标尺播放头跟随;全量 gtest + facade 回归(固定 9 个 + playback)绿;viewer.h 中不再出现 `RenderTicketWatcher` 播放队列字段。

### 附 C:阶段 4 耦合消减战役(2026-07 测绘,基线 559)

按符号聚类的消减顺序(每步保持全绿+耦合计数下降):

1. **icon(56)**:注册表搬到 app(engine/ui/icons→app/ui/icons,~20 个 app 文件只改 include 路径,namespace 不变);engine 仅 4 处 data(Node::icon) 覆盖(folder/footage/sequence/node 默认),改为返回图标**名字符串**,projectviewmodel.cpp:185 单一消费点按名映射。**不要下沉 core**——liboakcore 实测 Qt-free(2026-07),QIcon 会污染它;图标本就是呈现资源,归 app。
2. **EngineCore(48)**:应用核心外观族(oakengine_app_*):项目生命周期(create/open/save/recent/autorecovery)、剪贴板、status bar、color picker、handler 注册;信号→回调。worker 的无头 Core 继续用 C++ 不动。
3. **节点图 UI(~103)**:Node 40 / ViewerOutput 20 / Track 13 / ClipBlock 12 / NodeGroup 9 / NodeKeyframe 7 / NodeTraverser 6 / MultiCamNode 6——node view、multicam 面板、曲线编辑器对节点类的直接引用,按控件逐个切。
4. **参数/色彩/导出(~50)**:VideoParams 19 / ColorManager 15 / EncodingParams 9 / ExportFormat 7——导出编解码控件、色彩管理菜单、scopes。
5. **工具类(~30)**:QtUtils 9 / FileFunctions 5 / Config 5 / MainWindowLayoutInfo 6 / olive 8——从 engine 移到 core 或 app(它们本不属于引擎)。
6. **基础设施(~40)**:TaskManager/Task 14 / AudioManager 12 / DiskManager 9 / UndoStack 7 / PreviewAutoCacher 7 / RenderTicketWatcher 6 / Folder 7 / Footage 10 / Project 10 / TimelineMarker 6 / TimelineWorkArea 8——录制、刮擦、单帧刷新、preferences 等保留路径的收口,逐项判 facade 化或豁免。

豁免原则:纯 UI 呈现类(不触引擎执行)可经 C++ 包装层引用——但包装层本身也是 C++ 符号引用,故阶段 4 的"0"实际指**直接 olive:: 符号**;包装类应放进 liboakengine 的 wrapper 头(符号由 wrapper 内联消解,不进动态符号表)。

### 阶段 4:隐藏 C++ 符号

- app/worker 的引擎符号引用清零后:visibility=hidden + version script(`oakengine_*` 白名单)。
- 验收:`nm -D` 仅 `oakengine_*`;全部测试(含 CLI)绿;三平台打包复验。

## 附 B:复合场景 facade 化取舍(2026-07 评估)

- **nest(序列嵌套)**:补一个小原语 `oakengine_sequence_add_sequence_clip`(与 add_footage_clip 对称)。拖序列上时间线的执行路径由此可迁。
- **multicam**:不补专门族。MultiCamNode 是节点,节点图族(add_node/connect/set_input)已覆盖建线与切机位;multicam widget 的交互状态留 UI。
- **waveform-sync(波形对齐)**:补一个小族(estimate_offset / estimate_stretch_offset 两函数),把 timelinewidget 里最后两处直接调引擎算法的执行路径(timelinewidget.cpp:1117/1133)迁掉;偏移量的应用走已有编辑原语。
- **import place_at**:不补。import_footage + add_track + add_footage_clip 组合已够,文件夹递归/静帧时长/轨道定位是 UI 策略,留在 app。

## 风险与对策

- **节点参数类型膨胀**:NodeValue 有 ~20 种类型。先支持编辑器最常用的 8 种(float/int/bool/string/rational/color/vec/combo),其余按面板需要逐个加。
- **性能**:参数级 facade 调用频率低(用户操作粒度),不构成问题;渲染/帧路径已有同步封装。
- **Qt 对象生命周期**:节点/序列/clip 均为 borrowed handle 随 project;文档已有约定,迁移时沿用。
- **undo 语义**:所有编辑原语必须经全局 UndoStack(facade 已强制),UI 侧不得再直接 new 命令。
- **icon 归属**(2026-07 实测):app→引擎剩余耦合中最大单类是 `olive::icon`(56 个符号,节点图标元数据留在引擎)。它不属于任何面板,阶段 4 隐藏符号前需要专门处理:补 icon 查询 facade 族(按节点 type id 取图标),或把图标注册表下沉到 core。倾向后者,icon 是纯资源元数据。
- **架构事实**(同次实测,防止误判):liboakengine 对 liboakcore 只经 C ABI(134 个 oakcore_* 全为 undefined 引用);EngineCore 不在 liboakcore 内,进程里只有 liboakengine 一份,无双单例问题。

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
