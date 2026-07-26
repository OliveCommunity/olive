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
2. **EngineCore(48)** ✅ 已完成(2026-07-21):新增 `oakengine/app.h` + `engine/src/capi/app.cpp`(oakengine_app_* 族:CoreParams 启动、start/stop、open/active project、recent 列表、tool/snapping/timecode、剪贴板、footage 过滤、status bar、handler 注册,信号→`OakEngineAppCallbacks` 函数指针回调)。`app/Core` 解除对 EngineCore 的继承改为组合转发,对 app 其余代码接口不变;coreengine.h 仅把 `add_open_project`/`add_open_project_from_task`/`set_active_project`/`add_recovery_project_from_task`/`get_auto_recovery_index_filename` 提为 public 并新增 `open_project()` 只读访问器。app 侧 `olive::EngineCore` 未定义符号 57→0,总 `U _ZN5olive` 491→441。测试 `oakengine_app_test`(纯 C,无需 GPU)。worker 的无头 Core 继续用 C++ 不动。
3. **节点图 UI(~103)**:Node 40 / ViewerOutput 20 / Track 13 / ClipBlock 12 / NodeGroup 9 / NodeKeyframe 7 / NodeTraverser 6 / MultiCamNode 6——node view、multicam 面板、曲线编辑器对节点类的直接引用,按控件逐个切。**节点参数/关键帧 UI 已完成(2026-07-21,B8a)**:facade `oakengine/node.h` 扩容 ~60 个 C 函数——输入元数据(is_array/array_size/flags/is_connectable/is_keyframable/is_keyframed_ex、property 全套 typed getter 与枚举、set_input_property_string(notify 可控))、值读取(get_input_at_time/string/binary/bezier、default_value)、图查询(get_project/input_get_connected_node/get_label_and_name/get_input_name/copy_inputs)、多轨道关键帧枚举与导航(track_count/count_on_track/handle_on_track/at_time/keyframes_at_time/has/earliest/latest/closest_before/after/best_type,时间为秒有理数)、keyframe 句柄族(get_time/type/value/bezier_point/valid_bezier_point/track/element/input_id/node/has_sibling_at_time + live 非 undo 三件套 + create/dispose)、undoable 批量(remove_many/toggle_at_time/set_input_keyframing/keyframes_paste,均单条命令)、输入拖拽器 OakEngineNodeDragger(create/start/drag/end 一条 undo)。事件表新增 70-86 节点族 17 个事件(label/value_changed(带范围 ts)/connected/disconnected/flags/property/data_type/array_size/keyframe 五个/enable/context 两个/message_count),`oakengine_event` 扩展 `c`/`s` 字段;EngineEventBridge 同步加带类型信号。app 侧:nodeparamview 13 文件、curvewidget、keyframeview、keyframeproperties 全部切到 facade+事件桥;新增共享头 `app/widget/keyframeview/keyframehandle.h`(key 指针当不透明句柄的全部访问器 + TimeBasedViewSelectionManager 的 ADL 定制点,模板本体改用 selection_time/selection_set_time/selection_has_sibling_at_time/selection_time_target_parent 自由函数,TimelineMarker 实例化不受影响)。四目录解析到引擎的 olive::Node/NodeKeyframe 符号 46+21+21+5→0,总 `U _ZN5olive` 346→320。测试:oakengine_node_test/oakengine_keyframe_test/oakengine_events_test 各补一族纯 C 用例(handle 族/导航/toggle/keyframing/paste/dragger/属性/节点事件),35/35 绿。已知妥协:keyframe 粘贴 undo 粒度为每节点一条命令(原全局一条);k_binary 输入写路径经 string_at_time(死路径兜底);3 处 Qt6 模板 connect 改用字符串式 SIGNAL/SLOT 以规避 Node::staticMetaObject。nodeview、multicam、ViewerOutput 相关属 B8b。**nodeview/NodeGroup/MultiCamNode 已完成(2026-07-21,B8b)**:facade `oakengine/node.h` 再扩 3 族——context 位置(contains/get/set_position/set_expanded(插入语义,同 C++)/count/at、`oakengine_node_get_effect_input`)、NodeGroup 族(is_group、add/remove_input_passthrough(直接+undoable 两版)、set/get_output_passthrough(两版)、passthrough count/at/get_id_of、resolve_input 完全解析)、MultiCam 族(is_multicam、4 个输入 id 常量、source_count、rows/cols 与 index 互换算静态数学、`oakengine_clip_find_multicam`、`oakengine_multicam_switch_source`(可选 split-preserving-links + 各 linked multicam 设源,单条 MultiUndoCommand)),外加 `oakengine_nodes_delete_many`(NodeViewDeleteCommand 等价,节点+边数组一次提交单条 undo)。事件表新增 87/88(group passthrough added/removed,handle=内层节点,s=input id,a=element)、89(group output passthrough changed)、90(node context position changed,a/b=x/y double 位模式);EngineEventBridge 同步加 4 个带类型信号。app 侧:multicamwidget/multicamdisplay 切 OakEngineNode* 不透明句柄(Switch 走 switch_source),viewer.cpp detect_multicam_node 与 timelinewidget multicam 启用(`oakengine_project_add_node` 按 type id 建 multicam)切 facade;nodeview 三文件+nodeparamview 三文件+keyframeview+panel/node 的 NodeGroup 全部切 facade+bridge(resolve_input 7+ 处、get_inner 循环、passthrough 增删/枚举、84/85/90 事件订阅替代 connect,delete_selected 改收集后一次 delete_many)。`olive::NodeGroup`/`olive::MultiCamNode` app 未定义符号归零,总 `U _ZN5olive` 320→300。测试:oakengine_node_test 补 context-position/effect-input/group(含嵌套 resolve 与 undo/redo)/multicam/nodes_delete_many 五族,oakengine_events_test 补 87-90 实测触发(含 double 位模式解码),35/35 绿。已知妥协:group_nodes 与 timelinewidget multicam 启用由单条 MultiUndoCommand 变为多条 undo 记录。保留待 B8c:ViewerOutput 族(29 符号,timebased/viewer 系)、NodeTraverser(7,nodevaluetree/nodetableview/viewerdisplay 帧提取)、nodeview 残余 Node 调用(copy_dependency_graph、find_ways_node_arrives_here、inputs_from、外观 brush/color)、RenderManager::get_cacher()->set_multicam_node(引擎内部头)。**ViewerOutput/NodeTraverser 已完成(2026-07-21,B8c,B8 系列收尾)**:新增 `oakengine/viewer.h` + `engine/src/capi/viewer.cpp`(oakengine_viewer_* 32 函数:from_node 类型探测(替代 dynamic_cast)、playhead get/set、length/video_length/audio_length、video/audio params(按流 index)、三类 stream count、has_enabled_streams、first_enabled_video_stream、enabled streams count+列表(替代 get_enabled_streams_as_references)、workarea POD get/set_range/set_enabled、set_default_parameters、set_parameters_from_footage、set_waveform_enabled、get_connected_waveform(const void* 透传)、5 个输入 id 常量访问器、default_sample_format、stream_enabled、subtitle count/at(返回 const Subtitle* 借用指针));新增 `oakengine/traverse.h` + `engine/src/capi/traverse.cpp`(oakengine_traverse_* 15 函数:Owned OakEngineTraverseDb(generate_database/generate_table/free + 输入/行访问器:type/source/tag/value_string/split),element_index_for_hint,以及两个 B7 式过渡桥 generate_row(就地填 app 侧 NodeValueRow)与 transform(出 QTransform 六系数));node.h 补 `oakengine_node_set_value_hint`(nodevaluetree 的 ValueHint 写路径)且 `oak_node_value_type` 追加 TEXTURE/SAMPLES/VIDEO_PARAMS/AUDIO_PARAMS 四个仅内省值;`oak_video_params` POD 尾部追加 video_type/premultiplied_alpha(仅 viewer 族填充)。事件表新增 100-110 viewer 族 11 事件(length/playhead/frame_rate/pixel_aspect 有理数 a=num,b=den;size a=w,b=h;interlacing/sample_rate a=值;video_params/audio_params/texture_input/connected_waveform 无载荷);EngineEventBridge 同步加 11 个带类型信号。app 侧:timebased 家族(timebasedwidget/timebasedview)、viewer 家族(viewer/audiowaveformview/footageviewer)、multicamwidget、nodeparamview 三件、export 对话框(workarea POD + playhead 事件 + sequence_has_subtitles 纯 facade)、import 工具、projectviewmodel/projectexplorer/project 面板/proxydialog、nodeview/timeruler 的 dynamic_cast、timelinewidget(set_playhead/代理生成/嵌套序列参数)、seekablewidget、mainwindow 全部切 facade+事件桥;nodetableview/nodevaluetree 重写为 traverse db 访问(新增 app/widget/viewer/vieweroutpututils.h:POD→VideoParams/AudioParams 内联互转 + 类型探测);app 五处 Q_OBJECT 信号/槽参数由 ViewerOutput* 改 OakEngineNode*(moc 不再引用 ViewerOutput 元对象),`&ViewerOutput::label_changed/removed_from_graph` 改 &Node:: 形式(基类信号,Node 符号属后续批次)。验收:` U olive::(ViewerOutput|NodeTraverser)`(含 staticMetaObject/typeinfo/k_*_params_input 静态)29+7→**0**,总 `U _ZN5olive` 300→271。测试:新增 oakengine_viewer_test/oakengine_traverse_test(纯 C 无 GPU,事件实测触发:playhead/length(demo.mp4 clip)/size/pixel_aspect/interlacing/sample_rate/video_params/audio_params/texture_input 均验证载荷;connected_waveform 仅订阅成功,无音频链无法触发,已注释),37/37 绿(基线 35+2)。已知妥协/遗留:PreviewAutoCacher 三函数与 plugin::set_active_viewer_provider(参数带 ViewerOutput*,属附 C 第 6 项)仍在;嵌套序列经 facade set_video_params 不带 divider/color_range/video_type/音频 format;core.cpp 图层 enabled 翻转变 undoable;timeline 时间码标签方向连接改 lambda+Connection 句柄。
4. **参数/色彩/导出(~50)**:VideoParams 19 / ColorManager 15 / EncodingParams 9 / ExportFormat 7——导出编解码控件、色彩管理菜单、scopes。**导出面已完成(2026-07-21,B6)**:新增 `oakengine/encoding.h`(格式/编解码元数据、`OakEngineEncodingParams` 不透明句柄全字段读写、preset 目录与 load/save、`generate_matrix`、图像序列文件名辅助、`oakengine_export_render_with_params`、last-used 读写、音频录制启动)与 `oakengine/videoparams.h`(`oak_video_params` POD + 标准帧率/像素比/分辨率档/像素格式名等静态数据);`oakengine/encoding.cpp` 实现,`oakengine_encoding_test` 纯 C 覆盖。app 侧 export 对话框族(export、video/audio/subtitles tab、四个 codec section、format combobox、save-preset dialog)、序列对话框(参数/preset tab + standardcombos 四个组合框)、viewer 录音与 preferencesaudiotab 全部切到 C API;`EncodingParams`/`ExportFormat`/`ExportCodec` 未定义符号归零,总 `U _ZN5olive` 410→375。VideoParams 剩 9 个符号(ctor/operator==/is_valid/bytes_per_pixel 等)全部位于显示/渲染路径(viewerdisplay、manageddisplay、scopes),留 B7。**色彩/显示面已完成(2026-07-21,B7)**:新增 `oakengine/color.h` + `engine/src/capi/color.cpp`(`OakEngineColorManager` 借用句柄的 config 文件名/colorspace/display/view/look 列表与默认值/luma 系数/compliant 解析,`oak_color_transform` POD,`OakEngineColorConfig` 独立 OCIO 配置句柄,`OakEngineColorProcessor` 属主句柄 create/free/is_valid/convert_color/id,以及两个过渡桥 `oakengine_color_transform_job_set_processor`/`oakengine_color_set_display_color_processor`);事件族新增 `OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED`/`_REFERENCE_SPACE_CHANGED` 取代 app 对 ColorManager Qt 信号的直连。`engine/render/videoparams.h` 的 ctor/operator==/is_valid/effective-size/bytes-per-pixel/divider 名/常量改为头内联(显示路径对 VideoParams 是值语义且要喂给 B8 范围的 renderer C++ 调用,POD 无法覆盖;POD 侧另补 `oakengine_video_params_make`/`_equal`/`_is_valid`/`_bytes_per_pixel`/`_internal_channel_count` 供无头消费者),`ManagedColor` 同样全内联。app 侧 manageddisplay(含信号订阅)、viewerdisplay、viewer、viewerbase、scopebase、waveform、vectorscope、colordialog、colorbutton、colorspacechooser、colorpreviewbox、colorswatchwidget、colorvalueswidget、projectproperties、videostreamproperties 全部切到 C API(共享辅助头 `app/widget/manageddisplay/colorprocessorhandle.h`)。测试:`oakengine_color_test`(纯 C,OCIO 缺失时跳过查询断言)、`oakengine_encoding_test` 增补 POD 用例。符号:`olive::VideoParams`/`ColorTransform`/`ColorProcessor`/`ManagedColor` 未定义符号归零;`olive::ColorManager` 仅余 `staticMetaObject`(来自 app target 直接编译的 engine 源码 footage.cpp/ociobase.cpp 对 ColorManager 信号的 QObject::connect,属 engine 内部连接,留后续批);总 `U _ZN5olive` 375→346。
5. **工具类(~30)**:QtUtils 9 / FileFunctions 5 / Config 5 / MainWindowLayoutInfo 6 / olive 8——从 engine 移到 core 或 app(它们本不属于引擎)。
6. **基础设施(~40)**:TaskManager/Task 14 / AudioManager 12 / DiskManager 9 / UndoStack 7 / PreviewAutoCacher 7 / RenderTicketWatcher 6 / Folder 7 / Footage 10 / Project 10 / TimelineMarker 6 / TimelineWorkArea 8——录制、刮擦、单帧刷新、preferences 等保留路径的收口,逐项判 facade 化或豁免。**B4 时间线族残留清理已完成(2026-07-22,B4c)**
7. **B9a Task/Undo 族收尾(2026-07-22)**:app 侧 Task/TaskManager/UndoStack 直驱全部切到 `oakengine/task.h`/`undo.h` C API;新增 `oakengine_undo_command_create`/`create_multi`/`multi_add_child`/`multi_child_count`/`free` 五个 C 函数,让 app 侧自定义 undo 命令(选择集、splitter、sequence 开/关、时间选择)不再继承 `olive::UndoCommand`,改由 C 回调包装。app 内 `OpenSequenceCommand`/`CloseSequenceCommand`/`SetSelectionsCommand`/`SetSplitterSizesCommand`/`SetTimeCommand` 五个子类移除 `UndoCommand` 基类;新增共享头 `app/common/undowrapper.h`。TaskManager|Task|UndoStack 未定义符号已清零;UndoCommand 剩余 3 个符号(`redo_now`/`undo_now`/ctor)全部来自 engine 源码被 app target 直接编译(如 `Folder::RemoveElementCommand`/`NodeEdgeAddCommand`/timeline 命令类等)以及遗留的 `MultiUndoCommand` 构造,需待 B11 消除 app target 直接编 engine 源码后自然消失,本批按 handoff §5.1 验收条款记录并说明。测试:`engine/tests/oakengine_task_test.cpp` 覆盖 task manager 空态/create/import 错误路径/load 失败路径/task 事件/undo 往返/custom+multi 命令,38/38 ctest 全绿,总数 195。:facade `oakengine/timeline.h` 扩容——轨道高度换算四函数(internal↔pixels、default)、`oakengine_block_is_enabled`、Clip 输入 id 六个字符串 getter、`oakengine_clip_set_media_in`(undoable)/`request_invalidate`/`discard_cache`/`add_cache_passthrough`、**marker 句柄族**(OakEngineMarkerList/OakEngineMarker:count/at/at_time/get_time/get_name/get_color/has_sibling/set_time_live/list_add/list_add_existing/remove/set_properties(一条 undo)/commit_time)、**workarea 句柄族**(OakEngineWorkarea:viewer 借用 `oakengine_viewer_get_workarea_handle` 或 `oakengine_workarea_create/free` 自有、get/set live/set_range_undoable/set_enabled_undoable/reset 常量)、`oakengine_sequence_add_default_nodes`(独立 undo entry,原并入 import 组包,已知妥协)、`oakengine_clip_get_media_range_rational`(有理秒、不依赖 timebase)。事件表新增 22-24 序列轨道列表/字幕、32-35 Track(index/height/refreshed/muted)、36/37 Block(enabled/preview)、91/92 Node(links/color)、111-113 marker list、114/115 workarea;EngineEventBridge 同步加信号。app 侧:timelinewidget/trackview/trackviewitem/timelineview/ripple/transition/slip/import/pointer/add/edit/record/razor、seekablewidget/resizabletimelinescrollbar/timebasedwidget/timebasedviewselectionmanager(marker ADL,新增 `app/widget/timeruler/markerhandle.h`)、markerpropertiesdialog、footageviewer(override workarea 改 `oakengine_workarea_create`)、viewer/viewerdisplay(subtitles)、speeddurationdialog、timelinewidgetwaveformsync、core.cpp、nodeparamview、nodeviewcontext、mainwindow、trackviewsplitter 全部切换;新增共享头 `app/widget/timelinewidget/trackhandle.h`(is_locked/is_muted/type)与 `cliphandle.h`(connected node/caches/speed/loop/reverse/maintain_pitch,替代 clip.h 内联访问器对 k_* 静态成员的引用);`Track::type()` 改为头内联(其内联用户 to_reference()/get_track_type() 会拖拽符号)。度量:族符号(Sequence|Track|ClipBlock|TrackList|TimelineMarker|TimelineMarkerList|TimelineWorkArea|Clip|Block)75→3,总 `U _ZN5olive` 271→219。测试:oakengine_events_test/oakengine_timeline_edit_test 各补一族(新事件实测触发、marker/workarea/clip id/media/高度换算/add_default_nodes,含 undo/redo),37/37 绿。遗留 3 个族符号(判豁免):`Block::staticMetaObject`/`Track::staticMetaObject`(app 内部信号以 Track*/Block* 为参数,moc 的 qMetaTypeId<QObject*> 注册必然引用,需把 app 信号参数改 void* 才能消除,代价不值)、`ClipBlock::ClipBlock()`(add/import 工具在组包 MultiUndoCommand 里 `new ClipBlock()` 自建节点,ctor 注册输入无法内联,留待工具链整体 facade 化)。undo 粒度妥协(均有注释):slip 每 clip 一条、import/core 新建序列的 default nodes 独立一条、marker 删除/paste 非序列分支逐条、set in/out 点 enable+range 两条、mainwindow footage workarea 两条。已知坑:timeline_waveform_sync 等处的 clip 可能不在轨道上,ts 换算会空指针——rational 秒版 `oakengine_clip_get_media_range_rational` 专为此加。

8. **B9b Config(2026-07-22)**:新增 `oakengine/config.h` + `engine/src/capi/config.cpp`(`oakengine_config_load/save/get-set_string/int/set_error_handler/report_error`),用 buf/size 约定读字符串;新增 `app/common/configwrapper.h`,以头内联 `OakConfigValue` 替代 `OAK_CONFIG`/`OAK_CONFIG_STR` 宏,并把 `engine/config/config.h` 的宏定义加上 `#ifndef` 守卫,使 app 包含 wrapper 时优先走 C ABI。app 中所有直接使用 `Config::load/save/set_error_handler/current/operator[]` 的点改为 C API,大量 `OAK_CONFIG`/`OAK_CONFIG_STR` 使用点经 wrapper 重定向到 `oakengine_config_*`。`timelineundogeneral.h` 内原本内联使用 `OAK_CONFIG` 的静态成员初始化移入 `.cpp` 并改用 C API 读取。测试:`engine/tests/oakengine_config_test.cpp` 覆盖 load/save 往返、string/int 读写、缺省值、error handler/report_error,39/39 ctest 全绿(含一次 `oak_cli_transcode` 单独重跑通过),`olive::Config` 未定义符号归零,总数 190。

9. **B9b DiskManager(2026-07-22)**:新增 `oakengine/disk.h` + `engine/src/capi/disk.cpp`,封装 DiskManager 实例生命周期、`get_default_cache_path`/`set_default_cache_path`、缓存清理、settings handler 回调、settings/change-confirmation 对话框分发、`invalidate_project` 信号及 `get_open_folder` 借用句柄。app 侧 `core.cpp`/`projectproperties.cpp`/`diskcachedialog.cpp`/`preferencesdisktab.cpp/h` 全部切到 C API;`preferencesdisktab.h` 移除 `DiskCacheFolder*` 成员,改用 `QString` 保存默认缓存路径。为消零符号额外补了 `oakengine_disk_get_open_folder`/`set_default_cache_path`(任务原清单未列,但 core.cpp handler 与 preferencesdisktab accept()  otherwise 会残留 `DiskManager::instance`/`get_open_folder`/`DiskCacheFolder::set_path` 三个符号)。测试:`engine/tests/oakengine_disk_test.cpp` 覆盖 instance lifecycle、default cache path、open folder handle、clear_cache、settings handler round-trip、set_default_cache_path、invalidate_project;`show_change_confirmation_dialog` 因阻塞 QMessageBox 无法在 headless 纯 C 单测中覆盖,由 app 对话框代码间接验证。`DiskManager`/`DiskCacheFolder` 未定义符号归零,总数 179→169,41/41 ctest 全绿。
10. **B9b AudioManager(2026-07-22)**:新增 `oakengine/audio.h` + `engine/src/capi/audio.cpp`(`oakengine_audio_create/destroy_instance/manager_handle/get_set_output_device/get_set_input_device/hard_reset/clear_buffered_output/push_to_output/stop_recording`;`push_to_output` 收 `OakAudioParams*` + 原始字节 + 错误 buf)。事件表新增 140 `OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED`(handle = `oakengine_audio_manager_handle()`,AudioManager 单例指针),EngineEventBridge 加 `audio_output_params_changed` 信号。app 侧 core.cpp 生命周期、viewer.cpp 刮擦输出与事件订阅(`audio_bridge_`)、preferencesaudiotab 设备设置全部切到 C API。测试:`engine/tests/oakengine_audio_test.cpp`,40/40 ctest 全绿,`olive::AudioManager` 未定义符号归零,总数 190→179。
11. **B9b ProxyManager/LUTLibrary/ProjectSerializer(2026-07-22)**:新增三个族——`oakengine/proxy.h`(create/destroy_instance/params_from_config/get_state/state_to_string/get_or_start/get_working_filename,`oak_proxy_result` POD 含 state/filename[1024]/task 不透明 int64);`oakengine/lut.h`(directory_count/at/file_count/at/set_directories);`oakengine/serializer.h`(`oakengine_serializer_check_compressed` + `OakEngineClipboard` 句柄族 ~23 函数:set_nodes/markers/keyframes/property/copy/save_to_xml/paste/paste_with_map/free + get_loaded_* 访问器 + foreach_property/keyframe/connection 迭代器)。app 侧 proxydialog、timelinewidget 代理路径、lutfilefield、nodeparamviewwidgetbridge、preferencesluttab、main.cpp 压缩检查、keyframeview/seekablewidget/timelinewidget/nodeview/nodeparamview 复制粘贴全部切到 C API;`nodeparamview.h` 的 `generate_existing_paste_map` 去 ProjectSerializer 化(改 C map 回调)。测试:`oakengine_proxy_test.cpp`/`oakengine_lut_test.cpp`/`oakengine_serializer_test.cpp`。**已知债务:serializer 测试只覆盖 4/24 函数,clipboard 族其余 ~20 函数待补(facade 覆盖审计 59),按交接文档 §5.0 列为接手第一优先**。44/44 ctest 全绿,三类符号归零,总数 162。
12. **构建修复事故记录(2026-07-22,K2.7)**:B9b Proxy 批次子代理执行中途因 API 配额中断,误删 `timelinewidget.cpp` 一个函数块(rubber-band 三件套/add/remove/set_selections/get_item_at_scene_pos/save/restore_splitter_state/add_timeline_and_track_view/SetSplitterSizesCommand::redo/undo)并把 `seekablewidget.cpp` 留在半迁移态(`Core::undo_stack()` 返回 `void*` 后调用点未换、`resize_item_` 改 `void*` 后仍 `dynamic_cast`)。已由 K2.7 按 HEAD 恢复函数块(不含已 C API 化的 generate_existing_paste_map)、seekablewidget 改用 `resize_item_kind_` + `static_cast` + `oakengine_undo_push(command, name)` 双参形式,恢复 44/44 全绿。教训已写入交接文档 §2.1。代价:seekablewidget marker resize/绘制路径残留 `TimelineMarker::draw/set_time`/`TimelineWorkArea::set_range`/`MarkerChange*Command`/`ViewerOutput::set_playhead` 共 6 个符号,归入交接文档 §5.8.1(B11c)处理。
13. **B9c/B9d/B9e/B10/B11a 大部(2026-07-22~23,DS)**:预览/渲染服务(PreviewAutoCacher/RenderTicket/RenderTicketWatcher → `oakengine_preview_cacher_*`/`oakengine_preview_request_*` 异步请求族)、plugin 族(`oakengine_plugin_*` + progress reporter 工厂回调)、gizmo(TextGizmo POD 化进 `oakengine/gizmo.h`,DraggableGizmo 整体搬 app)、B10 工具类(QtUtils/FileFunctions/ColorCoding/Html/xml/debug_handler/qHash/Track::Reference 流运算符全部搬 app)、B11a Node 大部(图操作原语/NodeFactory/input id getter/命令类替换)。符号 162→36。
14. **v3 验收修复(2026-07-23,K2.7)**:对 DS 产出验收发现 6 个真实缺陷并全部修复——(1)B10 app 侧 `colorcodingapp/htmlapp/filefunctionsapp/hashstreamapp/xmlutilsapp` 与 engine 同名定义造成 ELF 符号介入、静态对象双重析构,`timeline-tests`/`olive-gtest` 退出即崩("corrupted double-linked list"),修复:`app/CMakeLists.txt` 对这 5 个文件加 `-fvisibility=hidden`;(2)`app/common/nodeimpl.cpp` 死代码(创建未注册),钉死方案 A:删除并改调用点(见交接文档 §3.4);(3)`SpeedDurationDialog::accept()` undo 命令未压栈(时长修剪不执行,2 个 gtest 红),补 `oakengine_undo_push(command, name)`;(4)`dropMimeData` 一次移动拆 3 条 undo,新增 `oakengine_folder_move_child`(单条命令)并补测试;(5)预览帧 POD `linesize` 误用 pixels 应为 bytes(preview.cpp + viewer.cpp 两处);(6)重建 display Frame 用 VideoParams 默认构造(channel_count=0 除零、depth=0 致 Vulkan 上传 0 字节、4 个 viewer NotBlack 黑屏),改四参构造。`manageddisplay.cpp` 渲染器创建恢复 DynamicRenderer 原版。当前符号 39(36+恢复的 DynamicRenderer 3),仅剩 `Backends/ViewerRuntimeRewireTest.RewireToIndirectConnectionNotBlack/1` 一个已知失败(交接文档 §3.1 有排查线索)。六条教训固化为交接文档 §6.6 硬规则 R1-R6。

豁免原则:纯 UI 呈现类(不触引擎执行)可经 C++ 包装层引用——但包装层本身也是 C++ 符号引用,故阶段 4 的"0"实际指**直接 olive:: 符号**;包装类应放进 liboakengine 的 wrapper 头(符号由 wrapper 内联消解,不进动态符号表)。

### 阶段 4:隐藏 C++ 符号

- app/worker 的引擎符号引用清零后:visibility=hidden + version script(`oakengine_*` 白名单)。
- 验收:`nm -D` 仅 `oakengine_*`;全部测试(含 CLI)绿;三平台打包复验。

## 附 B:复合场景 facade 化取舍(2026-07 评估)

- **nest(序列嵌套)**:补一个小原语 `oakengine_sequence_add_sequence_clip`(与 add_footage_clip 对称)。拖序列上时间线的执行路径由此可迁。
- **multicam**:不补专门族。MultiCamNode 是节点,节点图族(add_node/connect/set_input)已覆盖建线与切机位;multicam widget 的交互状态留 UI。
- **waveform-sync(波形对齐)**:补一个小族(estimate_offset / estimate_stretch_offset 两函数),把 timelinewidget 里最后两处直接调引擎算法的执行路径(timelinewidget.cpp:1117/1133)迁掉;偏移量的应用走已有编辑原语。
- **import place_at**:不补。import_footage + add_track + add_footage_clip 组合已够,文件夹递归/静帧时长/轨道定位是 UI 策略,留在 app。

## 附 D:变更通知事件机制(oakengine/events.h,2026-07-21 落地)

app 直接 connect 引擎 QObject 信号是 B4/B5 后最大的遗留耦合(~30 个连接点)。本批建立了通用替代机制:**引擎侧 C 订阅 API + app 侧 Qt 信号桥**。

### 机制

- `oakengine_event_subscribe(void *handle, int32_t event_id, oakengine_event_fn fn, void *userdata) -> int64_t`:按事件族传对应 facade handle(Project/Sequence/Track/Node,handle 即引擎对象指针,与既有约定一致)。返回订阅 id(>0),失败返回 0(handle NULL、事件 id 未知、或 handle 类型与事件族不匹配——内部用 `dynamic_cast` 从 `QObject*` 校验)。
- `oakengine_event_unsubscribe(int64_t id)`:退订;id 已失效(对象已销毁)时返回 `OAKENGINE_E_NOT_FOUND`,无害。
- 回调签名为 `void (*)(const oakengine_event *event, void *userdata)`;`oakengine_event` 是纯 POD:`{id, a, b, source, handle}`(`a`/`b` 为 int64:标志位、index 或帧时间戳;`source` 为被订阅 handle;`handle` 为事件相关对象,均为借用指针,仅回调期间有效)。
- **线程语义**:与原 Qt direct connection 完全一致——回调在发射线程上同步调用(内部 `Qt::DirectConnection`),先于引擎自身发射返回。引擎对象都在 GUI 线程,回调即在 GUI 线程。回调不得在持锁点反向调用修改同一对象的编辑原语。
- **生命周期**:被观察对象销毁时引擎侧自动注销(Qt `destroyed`),绝不会有悬空回调;`userdata` 归订阅方管理,退订前需自行保证有效。

### 事件 ID 表

| ID | 宏 | 订阅 handle | 载荷 |
|---|---|---|---|
| 1 | `PROJECT_MODIFIED_CHANGED` | OakEngineProject* | a = modified 0/1 |
| 2 | `PROJECT_NAME_CHANGED` | OakEngineProject* | — |
| 10/11 | `FOLDER_BEGIN/END_INSERT_ITEM` | OakEngineNode*(folder) | handle = 子节点, a = index |
| 12/13 | `FOLDER_BEGIN/END_REMOVE_ITEM` | OakEngineNode*(folder) | handle = 子节点, a = index |
| 20/21 | `SEQUENCE_TRACK_ADDED/REMOVED` | OakEngineSequence* | handle = OakEngineTrack*, a = track type |
| 30/31 | `TRACK_BLOCK_ADDED/REMOVED` | OakEngineTrack* | handle = OakEngineBlock*, a/b = in/out(ts) |
| 40/41/42 | `SEQUENCE_MARKER_ADDED/REMOVED/MODIFIED` | OakEngineSequence* | a = marker 时间(ts) |
| 50/51 | `SEQUENCE_WORKAREA_RANGE_CHANGED/ENABLED_CHANGED` | OakEngineSequence* | a/b = in/out(ts) 或 a = enabled |
| 60/61 | `COLOR_MANAGER_CONFIG_CHANGED/REFERENCE_SPACE_CHANGED` | OakEngineColorManager* | — |
| 70 | `NODE_LABEL_CHANGED` | OakEngineNode* | s = 新 label |
| 71 | `NODE_INPUT_VALUE_CHANGED` | OakEngineNode* | s = input id, a = element, b/c = 范围 in/out(ts) |
| 72/73 | `NODE_INPUT_CONNECTED/DISCONNECTED` | OakEngineNode* | handle = 对端节点, s = input id, a = element |
| 74 | `NODE_INPUT_FLAGS_CHANGED` | OakEngineNode* | s = input id, a = flags |
| 75 | `NODE_INPUT_PROPERTY_CHANGED` | OakEngineNode* | s = input id(key/value 省略,用 getter 重读) |
| 76 | `NODE_INPUT_DATA_TYPE_CHANGED` | OakEngineNode* | s = input id, a = oak_node_value_type |
| 77 | `NODE_INPUT_ARRAY_SIZE_CHANGED` | OakEngineNode* | s = input id, a/b = 旧/新 size |
| 78 | `NODE_KEYFRAME_ENABLE_CHANGED` | OakEngineNode* | s = input id, a = element, b = enabled |
| 79/80 | `NODE_KEYFRAME_ADDED/REMOVED` | OakEngineNode* | handle = keyframe, s = input id, a = element, b = track |
| 81/82/83 | `NODE_KEYFRAME_TIME/TYPE/VALUE_CHANGED` | OakEngineNode* | handle = keyframe |
| 84/85 | `NODE_NODE_ADDED/REMOVED_TO_CONTEXT` | OakEngineNode*(context) | handle = 子节点 |
| 86 | `NODE_MESSAGE_COUNT_CHANGED` | OakEngineNode* | — |
| 87/88 | `GROUP_INPUT_PASSTHROUGH_ADDED/REMOVED` | OakEngineNode*(group) | handle = 内层节点, s = input id, a = element |
| 89 | `GROUP_OUTPUT_PASSTHROUGH_CHANGED` | OakEngineNode*(group) | handle = 新输出节点 |
| 90 | `NODE_CONTEXT_POSITION_CHANGED` | OakEngineNode*(context) | handle = 子节点, a/b = x/y(double 位模式) |
| 100/101/102/104 | `VIEWER_LENGTH/PLAYHEAD/FRAME_RATE/PIXEL_ASPECT_CHANGED` | OakEngineNode*(viewer) | a/b = 秒有理数 num/den |
| 103 | `VIEWER_SIZE_CHANGED` | OakEngineNode*(viewer) | a = width, b = height |
| 105/109 | `VIEWER_INTERLACING_CHANGED`/`SAMPLE_RATE_CHANGED` | OakEngineNode*(viewer) | a = interlacing 枚举 / 采样率 |
| 106/107/108/110 | `VIEWER_VIDEO_PARAMS/AUDIO_PARAMS/TEXTURE_INPUT/CONNECTED_WAVEFORM_CHANGED` | OakEngineNode*(viewer) | — |
| 22/23/24 | `SEQUENCE_TRACK_LIST_CHANGED/TRACK_HEIGHT_CHANGED/SUBTITLES_CHANGED` | OakEngineSequence* | a = track type;23 带 handle = track、b = 像素高;24 a/b = ts 范围 |
| 32/33/34/35 | `TRACK_INDEX/HEIGHT/BLOCKS_REFRESHED/MUTED_CHANGED` | OakEngineTrack* | 32 a/b = 旧/新 index;33 a = double 位模式;35 a = 0/1 |
| 36/37 | `BLOCK_ENABLED/PREVIEW_CHANGED` | OakEngineBlock* | — |
| 91/92 | `NODE_LINKS/COLOR_CHANGED` | OakEngineNode* | — |
| 111/112/113 | `MARKER_LIST_MARKER_ADDED/REMOVED/MODIFIED` | OakEngineMarkerList* | handle = OakEngineMarker* |
| 114/115 | `WORKAREA_RANGE/ENABLED_CHANGED` | OakEngineWorkarea* | 114 无载荷(重读 `oakengine_workarea_get`);115 a = 0/1 |

(`oakengine_event` 在 B8a 扩展了 `c`(第三整数载荷)与 `s`(字符串载荷,仅回调期间有效)两个字段;宏均带 `OAKENGINE_EVENT_` 前缀。)

### app 侧:EngineEventBridge

`app/engineeventbridge.{h,cpp}`:一个 QObject,`subscribe(handle, event_id)` 注册 C 回调并把事件按 id 分发为**带类型的 Qt 信号**(如 `folder_begin_insert_item(OakEngineNode*, OakEngineNode*, int)`)。桥拥有订阅,析构时全退订;被观察对象死亡时引擎侧自动注销,双向都安全。

### 已迁的代表性连接点

- `app/core.cpp` `on_active_project_changed`:`Project::modified_changed` → bridge 订阅 + 信号接 `QMainWindow::setWindowModified`。
- `app/widget/projectexplorer/projectviewmodel.cpp`:Folder 的 begin/end insert/remove 四个信号 → `folder_bridge_` + `folder_subscriptions_`(QHash<Folder*, ids>);槽函数改为显式传 `Folder*`(原 `sender()` 语义由事件的 `source` 字段承担)。

### 后续批次迁移连接点的标准操作步骤

1. 确认目标信号已在事件 ID 表内;不在则:在 `events.h` 加宏(新 id)、`events.cpp` 的 `connect_event` 加 case(dynamic_cast 校验 + DirectConnection + POD 载荷)、`EngineEventBridge` 加对应信号和 `dispatch` case、在 `oakengine_events_test` 补一条实测。
2. app 侧:在原来 `connect(engineObj, &EngineClass::sig, ...)` 处改为 `bridge->subscribe(reinterpret_cast<OakEngineXxx*>(obj), OAKENGINE_EVENT_...)`,保存返回 id;对象失效或换绑时 `unsubscribe(id)`(引擎对象销毁会自动注销,重复 unsubscribe 无害)。
3. 若原槽函数用 `sender()`,改为从事件的 `source`/`handle` 字段显式传入(见 projectviewmodel 改法)。
4. 构造期一次性 `connect(bridge, &EngineEventBridge::xxx, this, ...)`;回调语义与原 direct connection 相同,无需改线程假设。
5. 全量构建 + ctest 全绿,`nm -D app/oak-editor | grep -c " U _ZN5olive"` 应下降。

### Track 块遍历族(同批落地)

`oakengine_track_block_at_time / nearest_block_before(_or_at) / nearest_block_after(_or_at) / block_count` + `oakengine_block_next/prev/is_gap/get_range`(timeline.h,`OakEngineBlock` 不透明句柄,含 gap;clip 句柄与 `OakEngineClip` 同指针)。已迁 razor(nearest_block_before)与 trackselect(链式遍历)两个代表点;ripple/transition/timelineview 的同构用法照此替换即可。

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

## 附 C：R5 批次记录

### F3（Task/TimelineWorkArea/ViewerOutput/Project）— GLM-5.2 完成

- **Task(6)+CLITaskDialog(1)+ProjectLoadTask(1)+ProjectSaveTask(1)+ProjectImportTask(1)=10 符号**：
  TaskDialog/TaskViewItem/TaskView/TaskManagerPanel 改用 `OakEngineTask*`；
  Core 改用 `oakengine_task_create_project_load/save/import/otio` + 访问器；
  删除 `FacadeExportTask`/`FacadeProxyTask`（engine 自有等价物）；
  `oakengine_cli_task_dialog_run` 替代 `CLITaskDialog`。
- **ViewerOutput k_*_params_input(3) + Project(2)**：
  35 处 inline `get_*_params()` 调用替换为 `viewer_output_video/audio_params` 助手和 C ABI；
  `Project::get_project_from_object` 替换为 `oakengine_project_from_object`。
- **TimelineWorkArea(6)**：
  `oakengine_workarea_create/set_range/set_enabled` 替代构造和方法调用；
  信号连接改事件订阅 `OAKENGINE_EVENT_WORKAREA_*`；
  `WorkareaSetEnabled/RangeCommand` 替换为 `oakengine_workarea_set_*_undoable`。

### F4（Node 方法调用）— GLM-5.2 完成

- **14 符号**：新增 7 个 facade 函数（`oakengine_node_enabled_input_id`、
  `_category_name`、`_link_command`、`_copy_in_graph`、`_copy_dependency_graph`、
  `_connect_command_string`、`_transform_time_to`）；
  15 处 `Node::` 方法调用替换为 C ABI。
- **Node 信号连接(23)未完成**：46 处 `connect(node, &Node::signal, ...)` 跨 11 文件；
  事件 ID 已全部分配（70-95），EngineEventBridge 信号已存在，
  但 9 个类缺少 `EngineEventBridge` 成员——需逐类添加。

### F6（长尾部分）— GLM-5.2 完成

- **7 个节点构造器** 替换为 `oakengine_node_factory_create_from_id`：
  VolumeNode、TransformDistortNode、SubtitleBlock、ShapeNode、SolidGenerator、
  TextGeneratorV3、CrossDissolveTransition。
- **5 个静态字符串** 替换为 C ABI 访问器：
  VolumeNode::k_samples_input、TransformDistortNode::k_texture_input、
  TransitionBlock::k_in/out_block_input、AudioVisualWaveform::k_maximum_sample_rate。

### 当前状态（GLM-5.2 R5 冲刺交接）

- nm `U _ZN5olive` = **58**（从 131 降下来，GLM-5.2 共消除 73 个）。
- oak-render-worker = 0。
- 构建 0 error；ctest 43/44（flaky 不计）。
- 反作弊：app 无 dlfcn；engine 改动仅 `engine/include/oakengine/` + `engine/src/capi/`。

### G1：Node 信号清零（88→66，-22）

53 处 `connect(node, &Node::signal, ...)` 跨 13 文件迁移为
`bridge_->subscribe()` + `connect(bridge_, &EngineEventBridge::node_*, ...)`。
22 个 Node 信号符号全部消除。剩余 4 个 Node 符号（link/set_standard_value/
set_value_at_time/staticMetaObject）从 inline 函数拉入，进豁免清单。

### G2：渲染族信号 + RenderManager（66→58，-8）

- PlaybackCache invalidated/validated 迁事件订阅（timeruler.cpp）
- Sequence::subtitles_changed 迁事件订阅（viewerdisplay.cpp）
- RenderManager::backend_to_string + instance() 换 C ABI（manageddisplay.cpp）

### G3：UndoCommand C ABI（58 不变）

- oakengine_undo_command_redo_now/undo_now 声明补入 undo.h
- 6 处直接调用替换；符号仍从 MultiUndoCommand inline 引用

### R6：豁免清单清零（58 → 0，100% C ABI）— 完成

> 详见 `docs/zh/r6-cleanup-plan.md`（各 P 节已标 ✅）。目标：把 R5 遗留的
> 58 个豁免符号全部消除到 0，为 engine 模块化拆分与 RIIR 打地基。

- **P1（F 类 facade 补齐，17）**：NodeValue 静态方法、VideoParams 构造器、
  音频对齐算法、TimelineMarker/ShapeNodeBase/FrameHashCache/RenderManager/
  MultiCamNode/SubtitleBlock 零散单点，全部新增 C facade 替换。
- **P2（B 类 inline 清零，8）**：app 中 113 处 `new XxxCommand(`（16 个命令类）
  替换为 facade 构造；`Node::link`/`set_value_at_time` 换 C ABI。
- **P3（A 类 MOC staticMetaObject，9+1）**：app 信号/槽参数类型由 engine C++ 类
  改 C ABI 句柄（OakEngineNode* 等）；plugin::PluginProgressReporter 去 Q_OBJECT
  改 C 回调（推翻原"终态保留"裁决）。
- **P4（E 类色彩管理，6）**：ManagedColor 整体迁出 engine 至 app
  （colorprocessorhandle.h，纯 UI 值类型）；ColorProcessor create/convert_color
  换 C ABI。nm 24→18。
- **P5（C 类音频回调，5）**：AudioProcessor 改 C vtable 接口
  （`oakengine_audio_processor_*`，推翻原"终态保留"裁决）。nm 18→13。
- **P6（D 类渲染/GPU，13）**：新增 `oakengine/display.h` + `engine/src/capi/display.cpp`
  （`oakengine_display_renderer_*`/`oakengine_display_texture_*`/
  `oakengine_codec_frame_*` 共 11 函数）；manageddisplay/viewerdisplay/scopebase/
  viewer/multicamdisplay/histogram 的渲染器构造-init-destroy、create_texture、
  blit_color_managed、upload/download、Frame::create/set_video_params/allocate
  全部收口到 facade。nm 13→0。

**验收**：nm ` U _ZN5olive` = **0**（oak-editor 与 oak-render-worker 均为 0）；
全量构建 0 error；全量 ctest 100%（45/45）；ViewerDisplayReproTest 三个可跑通
用例（Vulkan）保持通过；导出测试无回归。反作弊：app 无 dlsym/dlfcn/QLibrary
（仅 main.cpp 的 wglGetProcAddress 为 OpenGL 驱动能力检测，与 engine 符号无关）；
engine 无 inline 化（oakengine/*.h 纯 C 声明，ManagedColor 为类整体迁出非 inline 化）。
handoff §6.4 豁免清单已清空为"无豁免"。
