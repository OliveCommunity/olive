# oakplugin Rust crate（M11 第 1+2 期实现状态）

> 状态：**M11 第 1、2 期已实现**（第 2 期：GL 渲染路径 +
> ofxColour + pluginrenderer 渲染驱动收编）。原声明底稿（类型与
> 函数签名 + 文档注释即规约）已由实现方填充；`src/` 内已无
> `todo!()`。
> 计划：docs/zh/plans/riir/M11-ofx-host.md。

## 结构

```
src/
  lib.rs        crate 文档、模块图、FFI 纪律
  error.rs      错误码（与 include/plugin/error.h 一一对应）
  handle.rs     引用计数句柄脚手架（{ctx,addref,release,abi_version}）
  property.rs   PropertySet：OFX 属性集的存储与类型化读写
  suites/       插件调进来的 C 函数表（unsafe trampoline 层）
    mod.rs      fetchSuite 注册表 + 渲染/GL 上下文 TLS
    property.rs memory.rs image_effect.rs param.rs
    message.rs  progress.rs timeline.rs multithread.rs
    gl_render.rs OfxImageEffectOpenGLRenderSuiteV1（M11 §4 新增）
  host.rs       Host 单例：bundle 扫描、插件缓存、action 分发
  descriptor.rs EffectDescriptor/ClipDescriptor（describe 的产物）
  instance.rs   Instance：action 调用面（render/协商/RoD/RoI/isIdentity/
                render_gl/GetOutputColourspace）
  render_driver.rs pluginrenderer.cpp 渲染流程收编（M11 §4 新增）
  clip.rs       ClipInstance：clip↔oakrender 纹理桥（bridge::render）
  image.rs      Image：帧缓冲/纹理的 OFX 视图
  param.rs      12 种参数实例 + param↔oaknode 桥（bridge::node/undo）
  progress.rs   PluginProgressReporter
  bridge/       oak 其余模块的 C ABI 导入（extern "C" 声明）
    node.rs     render.rs  undo.rs
  ffi.rs        include/plugin/*.h 的全部导出（C ABI 出口层）
```

## 桥布局决策（M11 第 1 期冻结；第 2 期增补）

- **`bridge::node::Value`** = `include/node/node.h:93` 的 `oaknode_value`
  POD，字段逐字一致（`type`/`num`/`den`/`f[4]`；`type` 取值见
  `node_value_type`）。字符串族输入（k_file/k_text/k_font/
  k_str_combo）无 POD 表示，走 `*_input_string_*` 专用桥函数
  （`set_input_string_undoable`）。`ffi::OakNodeValue` 是同布局的
  出口层镜像（两处独立声明避免模块环）。
- **帧访问 C ABI**（`bridge::render`）= `oakrender_display_texture_*`
  （`get_frame`/`is_dummy`）+ `oakrender_codec_frame_*`
  （`get_params`/`data`/`free`），与 `include/render/renderer.h` 一致；
  `oakrender_display_texture_wrap_native` 是 C++ 专属符号（TexturePtr
  引用），Rust 不可调——**输出纹理由 oakrender 侧创建并经句柄传入**：
  `ClipInstance::set_output_texture` 挂入后，`store_output_image` 取
  其 CPU 帧整帧拷贝（全链路 F32，尺寸不符明确报错）。
- **GL 纹理桥**（M11 §4 增补）：`bridge::render` 增加渲染器族
  （`renderer_create_dynamic`/`init`/`is_open_gl`/`destroy`）与纹理族
  （`texture_create`/`upload`/`download`/`retain`/`free`/`id`/
  `get_params`/`renderer_download_from_texture`），以及
  `frame_linesize_bytes`（CPU 拷贝的行跨度感知修复）。行跨度单位
  统一为**字节**（renderer.h:206 明文契约；C++ 调用点传像素行跨度，
  由 oakrender 侧实现 C ABI 时换算）。
- **undo 打包**（`bridge::undo`）= `oakundo_command_init_multi`/
  `redo_now`/`multi_add_child`/`free`；paramEditBegin/End 的编辑事务
  在 `Instance` 上维护深度与累积 multi（C++ `submit_undo_command` 语义：
  事务内子命令立即 redo 生效并并入 multi，editEnd 整体 redo+释放）。
- **param↔node 值转换**（`param.rs`）：`set_from_node`（节点→插件，
  按参数类型映射，维度截断/补零）、`to_node_value`（插件→节点，
  RGB 补 alpha=1，与 C++ `RGBInstance::set` 一致）。字符串参数经
  `oaknode_node_set_input_string_undoable`。
- **节点绑定**：`Instance::bind_node(identity)`（C++ `set_node_handle`
  的 Rust 侧）；回写只在 `ChangeReason::PluginEdited` 触发，未绑定/
  身份查无时 no-op。

## M11 第 2 期：GL 路径 + ofxColour + 渲染驱动

### GL 渲染路径

- `suites/gl_render.rs`：OfxImageEffectOpenGLRenderSuiteV1
  （clipLoadTexture / clipFreeTexture / flushResources），语义对照
  ofxGPURender.h（vendored）与 HostSupport 插件侧（ofxhImageEffect.cpp:
  2296-2367）。纹理句柄是属性集（12 个属性：OpenGLTextureIndex/
  OpenGLTextureTarget/PixelDepth/Components/PreMultiplication/
  RenderScale/PixelAspectRatio/Bounds/ROD/RowBytes/Field/
  UniqueIdentifier）；存活表持有 Box<PropertySet>（句柄地址稳定）。
  Output clip 的渲染目标绑定由调用方契约保证（等价 C++
  `attach_output_texture`）；clipFreeTexture 对 Output 不删纹理。
- GL render action：`Instance::render_gl`（与 CPU `render` 并存）——
  action 序列 kOfxActionOpenGLContextAttached → render（in args 带
  kOfxImageEffectPropOpenGLEnabled=1）→ OpenGLContextDetached；
  GL 模式无 CPU 输出回读，渲染结果留在附着纹理上。
- **GL 上下文规则**（ofxGPURender.h "OpenGL Current Context"）：宿主
  只在 Render/Begin/EndSequenceRender/Attach/Detach 期间要求上下文
  current——本实现的约定是调用方（oakrender PluginJob 路径）在
  进入 render_job 前把渲染器上下文置为 current 并附着输出纹理
  （文档见 include/plugin/instance.h 的新增声明）。
- 格式协商：插件描述符声明 kOfxImageEffectPropOpenGLRenderSupported
  （"false"/"true"/"needed"）与 kOfxOpenGLPropPixelDepth（可选位深
  列表）；宿主 `pick_gl_pixel_depth` 按管线 F32 约束选型（声明列表
  不含 Float → GL 模式不可行，回退 CPU）。use_opengl 决策在
  render_driver。

### ofxColour（OFX 1.4）

- 宿主描述符声明 OCIO 色彩管理（kOfxImageEffectPropColourManagementStyle
  = OCIO + AvailableConfigs = ofx-native-v1.5_aces-v1.3_ocio-v2.3）；
  实例期协商属性（style/config/OCIOConfig=ocio://default，工作空间
  ACEScg 经 clip 色彩空间属性传达）。
- 输入 clip 的 kOfxImageClipPropColourspace 宿主写为 ACEScg；
  GetOutputColourspace action（`Instance::get_output_colourspace`）：
  偏好列表采纳 + "OfxColourspace_<clip>" 交叉引用解析
  （`resolve_colourspace`），结果写回输出 clip
  （`set_output_colourspace`）；插件未实现 → 第一个输入 clip 的
  色彩空间（规范默认）。
- 描述符预定义 GL/colour 属性（ofxGPURender.h/ofxColour.h）：HostSupport
  的 propSet 不创建属性（返回 ErrUnknown），宿主预定义属性宇宙——
  第 1 期缺 GL/colour 声明，第 2 期补齐（见"第 1 期修复"）。

### render_driver（pluginrenderer.cpp 收编）

- `render_driver.rs`：`PluginRenderer::render_plugin`（1857 行 C++
  的渲染流程部分）的 Rust 移植——实例锁、use_opengl 决策、多输入
  clip 收集（effect_input_id / inputs 表 / SimpleSource 回退）、
  getClipPreferences、RoI/RoD 设定、getRegionOfInterest（失败按默认
  整帧继续，C++ 对 BadHandle 即如此）、输出 clip 格式、isIdentity
  短路（透传帧直接拷贝，不调 render action）、参数覆盖
  （apply_param_overrides 移植）、CPU/GL 渲染与输出装配。
- 序列括号：`render_begin_sequence`/`render_end_sequence` 的 C ABI
  （oakrender 对同一实例的一批帧先 begin 后 end，中间逐帧
  `render_job`）。
- 目标形态：oakrender 的 PluginJob 退化为一次 C ABI 调用
  `oakplugin_instance_render_job`（instance.h 新增，见下）。

### include/plugin/instance.h 新增（M11 §4，既有签名不变）

- `oakplugin_instance_render_begin_sequence` / `render_end_sequence`：
  序列括号。
- `oakplugin_instance_render_job(instance, dst, time, clear, interactive,
  effect_input_id, src, inputs[], values[], renderer)`：一帧渲染的
  单一调用；`oakplugin_job_value`（参数覆盖）/`oakplugin_job_texture`
  （输入 clip 纹理）两个 POD 随附。GL 契约（上下文 current + 输出
  附着）见头文件文档注释。

## 与 M11 §3.5 验收的对照（第 1+2 期现状）

| 验收项 | 状态 |
| --- | --- |
| 0 期 golden master 全绿（描述符 diff 空、CPU 渲染 bit 一致） | **未达成**：0 期基建（`tests/ofx/` 的快照/帧库）尚未生成，`golden_test.rs` 对应用例 `#[ignore]`（原因见各用例 doc）；渲染 golden 另依赖真 liboakrender 的桥。 |
| 测试插件 round-trip + CImg 全量 describe/协商冒烟 | **部分**：测试插件 round-trip 绿（suites/ffi/lifecycle/negotiation/colour/gl/driver）；CImg 全量冒烟被本机 CImg bundle（Natron 分支，属性套件句柄语义不兼容）所阻——`#[ignore]`；`system_misc_ofx_bundle_smoke` 在本机 Misc bundle（同样为 Natron 分支，describe 返回 MissingHostFeature）上 skip，兼容 bundle 的机器照常断言。 |
| 生命周期：create/destroy 配对、重复 scan、render 中途 cancel、alive 无泄漏 | **达成**：lifecycle_test 全绿（含 256 次循环与 stub 渲染取消）。 |
| nm：liboaknode/liboakrender→liboakplugin C++ 符号为 0；HostSupport 移出构建 | 由外层 CMake 接线验证（本 crate 侧：ffi.rs 仅导出 C ABI）。 |
| cargo test（host 内部单测）+ ctest（C ABI 层）双绿 | **达成**（cargo test 两种模式全绿；ctest 属 standalone 树外层）。 |
| M11 §4：GL 路径 + ofxColour + pluginrenderer 收编 | **达成**（本机验证）：OpenGLRender suite v1 + render_gl（GL 上下文/格式协商规则按 ofxGPURender.h）；ofxColour 属性族 + GetOutputColourspace（ACEScg 工作空间）；render_driver 收编 + render_job C ABI（见上）。GL golden（EXR 容差）需 0 期基建 + 本机 GPU 人工确认（`OAK_GPU_TESTS` 门）。 |

## 实现纪律（实现方必读）

1. 所有 `extern "C"` 函数体必须包 `crate::handle::guard(..)`（
   catch_unwind + 错误码映射），禁止 panic 越过 FFI。
2. 句柄全部经 `handle.rs` 的 `RefBox<T>`；`ctx` 永不裸指针外露含义。
3. 共享状态（插件缓存、instance 注册表、线程表）一律 `Mutex`；
   插件可能在其自起线程回调任意 suite（MultiThread suite 存活期）。
4. OFX 语义以 HostSupport 的行为为参照系；每个协商/时序实现点
   必须注释对应 HostSupport 文件行号。
5. 错误码、句柄布局、字符串两段式与 `include/plugin/*.h` 逐字一致。
6. 依赖政策：**优先使用成熟第三方 crate**（serde/quick-xml/thiserror
   等），不重复造轮子。选型要求：crates.io 有维护、许可
   MIT/Apache-2.0/BSD（GPL 兼容）；新增依赖在 README 登记名称与
   理由。OTIO 等大型既有 C++ 库绝不重写——继续经其 C ABI/桥接层
   使用。
7. 跨期句柄（suite 返回的纹理/图像句柄）必须指向**堆上稳定对象**
   （Box/Arc）——栈上临时对象在 suite 返回后悬垂（M11 第 2 期
   gl_render 初版踩过，见"第 1 期修复"）。

## 已知技术点（实现方注意）

- **C 变长参数**：`paramGetValue`/`paramSetValue` 等是 variadic。
  stable Rust 不能 *定义* C-variadic 函数（`c_variadic` 仍不稳定）。
  两个选项：(a) 这几个函数用 nightly 的 `c_variadic`；(b) 用
  build.rs 编一个几十行的 C shim 只承载 variadic 入口再转发。
  选 (b) 保持 stable toolchain。`core::ffi::VaList` 类型本身稳定，
  仅"定义 variadic fn"受限。
  **已落地**：message suite 的 v1/v2 入口即 C shim
  （cbits/ofx_message_shim.c，build.rs 经 `cc` 编译；`cc` 仅 build
  依赖，理由见下）。param suite 的 variadic 同理（届时同方案）。
- **依赖登记**：`cc`（build-dependencies）——编译 C shim 的唯一
  稳妥方式（手写 `Command::new("cc")` 无法处理跨平台 flag/交叉编译；
  零运行时依赖不变）。M11 第 2 期未新增依赖。
- suite 函数表经 `suite_v1()` 等 accessor 暴露（`static` 初始化
  放 lazy/OnceLock 里），`fetch_suite` 只查表。
- **GL 上下文归属**：oakplugin 不持有 GL 上下文（C ABI 无
  make-current 函数）；render_job 的 GL 契约要求调用方（oakrender）
  置上下文 current 并附着输出纹理。GL 纹理上传/回读经 oakrender C
  ABI（其内部确保上下文）。

## 第 1 期修复（M11 第 2 期发现并修复的 phase-1 缺陷）

1. **ffi::oakplugin_instance_render 泄漏帧句柄**：render 失败路径
   `?` 提前返回时不释放 `frame`（texture_get_frame 的保留引用）。
   修复：统一走 `render_driver::write_output_frame`，所有路径释放。
2. **CPU 拷贝假设紧凑行**：fetch_image / store_output_image / ffi
   render 用紧凑行宽拷贝——真实 oakrender 帧可有行填充。修复：
   新增 `frame_linesize_bytes` 导入，行优先拷贝按 linesize。
3. **描述符未预定义 GL/colour 属性**：属性 suite 的 propSet 对未
   定义属性返回 ErrUnknown（与 HostSupport 一致：宿主预定义属性
   宇宙）；第 1 期描述符表缺 kOfxImageEffectPropOpenGLRenderSupported/
   kOfxOpenGLPropPixelDepth/ofxColour 族 → 插件无法声明 GL/colour
   能力。修复：`init_descriptor_props` 补齐预定义（默认
   "false"/空数组/None）。

## 测试

运行（全量，含渲染像素路径的 oakrender 测试桩）：

```sh
cargo test --features test-stubs
cargo tarpaulin --out stdout --features test-stubs   # 覆盖率门槛
```

- 默认 `cargo test` 亦可：桥经 dlsym 解析 oakrender/oaknode/oakundo
  符号（缺失时渲染/回写边界给出可解释错误），桩相关用例不编译；
- `--features test-stubs`：库内 oakrender/oaknode/oakundo 桩
  （`bridge::*::stub`），ffi/clip/param 的桥调用（含像素读写、
  节点回写、undo 打包）全链路可跑——**覆盖率以该模式为准**
  （M11 第 1 期实测 82.93% 行覆盖；第 2 期门槛 ≥80%，见 COVERAGE）；
- 最小测试插件（cbits/oak_test_plugin.c，三个入口：
  org.oak.test-plugin / org.oak.test-plugin.gl / 
  org.oak.test-plugin.identity）由 build.rs 编译为共享库，
  `common::test_plugin_dir` 运行时装配成 bundle；不可用时相关用例
  skip；
- 宿主单例无锁：触碰宿主面的用例经 `common::with_host` 串行化。
- **系统级冒烟**：`bridge_test::system_misc_ofx_bundle_smoke` 在
  `/Library/OFX/Plugins/Misc.ofx.bundle` 存在时扫描→describe→
  create_instance→协商；本机 bundle 为 Natron 分支（describe 返回
  MissingHostFeature）时 skip（README 已记录），兼容 bundle 的机器
  照常断言。
- **GL 用例策略**（`gl_render_test.rs`）：`test-stubs` 模式下桩 GL
  渲染器（renderer_is_open_gl/texture_create/texture_id 等）模拟
  GPU——suite 往返、GL render 路径（attach/detach 配对、插件经
  message 上报纹理索引）、错误路径全链路可跑；默认模式（无
  liboakrender）断言优雅降级（GL suite 无上下文 →
  MissingHostFeature、render 回退 CPU）。真实 GPU golden 为
  `OAK_GPU_TESTS` 门（`common::gpu_available`），CI 跳过。
- **golden 用例**（`golden_test.rs`）因 M11 0 期基建（tests/ofx/ 的
  快照与帧库）尚未生成而 `#[ignore]`（原因写在各用例 doc）；其中
  CImg 全量冒烟另受本机 CImg bundle 的 Natron 分支句柄语义所阻
  （详见用例注释），真实插件路径由 Misc 系统冒烟覆盖。

TDD：测试声明与实现声明同步冻结（tests/，函数体 `todo!()`）：

- `handle_test.rs` / `property_test.rs` — 基础设施契约（引用计数、
  free 容错、Registry、属性集语义、并发）。
- `suites_test.rs` — 八张 suite 的 round-trip（经最小测试插件，
  "插件视角"的 HostSupport 兼容性背书）。
- `ffi_host_test.rs` / `ffi_instance_test.rs` — C ABI 出口契约
  （每函数一正常一错误路径；两段式字符串边界）。
- `lifecycle_test.rs` — 创建/销毁配对、重复扫描、渲染取消原子性、
  256 次循环无泄漏、并发实例。
- `negotiation_test.rs` — 协商重灾区专项（分量/位深矩阵、RoD/RoI、
  isIdentity、field 透传、sequence 括号）。
- `colour_test.rs` — ofxColour 属性族 + GetOutputColourspace 往返
  （偏好采纳、交叉引用解析、输出写回；ACEScg 工作空间）。
- `gl_render_test.rs` — GL suite 往返/错误路径/像素深度协商矩阵 +
  GL render 路径端到端（无 GPU 优雅跳过策略见上）。
- `render_driver_test.rs` — render_job CPU 路径（序列括号、多输入、
  参数覆盖、isIdentity 透传像素断言、无桩降级）。
- `bridge_test.rs` — node/render/undo 三桥（`--features test-stubs`
  全链路；默认模式走 dlsym 缺失的降级路径）+ Misc 系统冒烟。
- `golden_test.rs` — 描述符快照 diff + CPU bit 级/GL 容差渲染
  golden + F32+ACEScg 链路断言（0 期基建未落地，见上）。
- `common/mod.rs` — 测试插件定位、golden 目录、GPU 门、skip 约定。
