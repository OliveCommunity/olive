# 动态渲染后端拆分计划

## 目标

将当前强绑定 OpenGL 的渲染实现拆成可动态加载的后端库，使主程序只依赖一个轻量适配器：

- OpenGL 后端封装到私有动态库。
- Vulkan 后端封装到独立动态库。
- 后端库内部继续使用 C++ 实现。
- 后端库对外只导出 C ABI。
- C ABI 使用不透明 handle 表示 C++ 对象。
- 每个 C 函数对应一个后端类成员函数。
- 构造函数导出为特殊 create 函数，析构函数导出为特殊 destroy 函数。
- 主程序适配器构造时按配置显式加载后端库并调用 create/init，析构时调用 destroy 并卸载库。

## 命名约束

用户期望后端名为 `libgl.so` 和 `libvulkan.so`。Linux 系统上 `libGL.so`/`libgl.so` 容易和系统 OpenGL loader 混淆，因此工程实现应优先使用私有库名或私有目录，例如：

- `liboakgl.so`
- `liboakvulkan.so`
- 或 `render_backends/libgl.so`、`render_backends/libvulkan.so`

适配器只从 Oak 私有后端目录查找，避免加载到系统图形库。

## 阶段 1：OpenGL 动态后端骨架

- 新增稳定 C ABI 头：`app/render/backend/renderbackend_c.h`。
- 新增 `DynamicRenderer` 适配器，继承现有 `Renderer`，内部用 `QLibrary` 加载后端。
- 将现有 `OpenGLRenderer` 包装成 OpenGL 后端导出函数。
- `RenderManager` 按 `GraphicsBackend` 选择加载 OpenGL 或 Vulkan 后端。
- 在 Vulkan 后端未实现前，请求 Vulkan 时加载占位后端或回退 OpenGL，并记录明确 warning。

## 阶段 2：两层适配器 ABI

本计划不是把 OpenGL 代码用 C 重写。动态库一侧继续保留现有 C++ `OpenGLRenderer`/未来 `VulkanRenderer` 实现，只在导出边界增加一层 C wrapper；主程序和渲染进程一侧再用 `DynamicRenderer` 把 C 函数封回 C++ `Renderer` 接口。

第一阶段 C ABI 可以用 `void *` 承载现有 C++ 对象指针，例如 `QVariant`、`VideoParams`、`ShaderCode`、`Texture`、`AcceleratedJob`。C 函数内部只做类型转换并调用对应 C++ 成员函数。这样两侧代码都不用大改，但有一个前提：后端库和主程序必须用同一套头文件、编译器 ABI 和 Qt/FFmpeg/OpenFX 依赖构建。

长期要把 ABI 稳定下来时，再逐步引入更明确的 C 结构，避免跨库暴露 Qt/C++ 类型：

- texture handle：`OakBackendTextureHandle`
- shader handle：`OakBackendShaderHandle`
- video params C struct：宽、高、depth、pixel format、channel count、linesize
- shader code C struct：vertex/fragment 字符串
- blit job C struct：输入 texture handle、uniform 数组、输出 texture handle
- readback/upload 使用裸指针和 stride

这一步是 ABI 稳定化，不是把后端内部实现改成 C。

## 阶段 2.5：最小化 OpenGL/Vulkan 后端链接边界（已完成）

此前 `oakgl`/`oakvulkan` 通过 `$<TARGET_OBJECTS:libolive-editor>` 把整个 editor 对象库链进动态库，导致后端库包含项目、节点、任务、cache、UI 等大量 editor 代码和全局状态。

本次已完成链接边界收敛：

- 新增静态库 `libolive-rendercore`，仅包含渲染核心代码：
  - 渲染器基类与数据类型：`Renderer`、`Texture`、`VideoParams`、`ShaderCode`、`AcceleratedJob`、`ShaderJob`。
  - 动态适配器：`DynamicRenderer`、`renderbackend_c.h`。
  - 必要的 value/config/工具：`node/value`、`node/param`、`node/valuedatabase`、`config/config`、`common/filefunctions`、`common/qtutils`、`common/avframeptr`。
- `oakgl`/`oakvulkan` 现在只链接 `libolive-rendercore`，不再链接完整 `libolive-editor`。
- `liboakgl.so` / `liboakvulkan.so` 体积从约 21 MB 降至约 600 KB。
- 为隔离依赖做的头文件清理：
  - `renderer.h` 移除 `node/node.h`、`render/colorprocessor.h`、`render/job/colortransformjob.h`、`job/pluginjob.h`，改为前向声明。
  - `videoparams.h` 移除 `ofxImageEffect.h`，OFX 字符串 setter 实现下移到 `videoparams.cpp`。
  - `texture.h` 用新增的 `common/avframeptr.h` 替代 `common/ffmpegutils.h`，避免后端拉入大量 FFmpeg 工具代码。
  - `renderer.cpp` 的颜色管理（`GetColorContext` / `BlitColorManaged`）和隔行（`InterlaceTexture`）实现分别拆到 `render/colormanagement.cpp` 和 `render/interlacetexture.cpp`，这两个文件仍由 editor/worker 链接，但不进入后端库。
- 修复了拆分过程中暴露的 `StyleManager::kDefaultStyle` 跨库符号问题：改为 header 内 `inline static` 定义，使 `config.cpp` 在后端库中自包含。

剩余优化空间：
- 长远可将 `libolive-editor` 也改为依赖 `libolive-rendercore`，彻底消除渲染核心代码在主程序与后端库之间的重复编译/重复链接。当前阶段先保证后端边界干净、主程序保持兼容。

## 阶段 3：Vulkan 后端（原型实现，运行时验证待完成）

- 新增 Vulkan 后端库 `liboakvulkan.so`（当系统安装了 Vulkan 头文件/库时构建；无 Vulkan 环境时 CMake 自动跳过）。
- 新增 `VulkanRenderer` 类，继承 `Renderer`，使用原生 Vulkan API 实现 offscreen 渲染管线；代码已合入，并在本机 NVIDIA Vulkan 驱动上通过了基础端到端渲染测试。
- CMake 集成：根目录查找 `Vulkan` 和 `shaderc`（可选）；`oakvulkan` 目标链接 `Vulkan::Vulkan` 与 `shaderc_shared`；若 `Vulkan` 未找到则不构建该库，避免无 Vulkan 头文件时编译失败。
- 实现 Vulkan instance/device/queue/command pool 管理（代码层完成）。
- 实现 offscreen image/texture 管理（`CreateNativeTexture` / `DestroyNativeTexture`），支持 2D/3D、多种 pixel format（U8/U16/F16/F32 × 1/2/3/4 channel）；3-channel 格式会探测 `COLOR_ATTACHMENT` 支持并自动回退到 4-channel 等价格式。
- 实现 staging buffer 上传/下载（`UploadToTexture` / `DownloadFromTexture`）。
- 实现 `ClearDestination`（`vkCmdClearColorImage`）。
- 实现 `Flush`（`vkDeviceWaitIdle`）。
- 实现 GLSL → SPIR-V 运行时编译（通过 `shaderc`），支持顶点/片段共享 UBO、显式 sampler binding、顶点 uniform（如 `ove_mvpmat`）和常用 varyings。
- 实现基础 graphics pipeline 用于 `Blit`（全屏 quad、顶点缓冲、按格式缓存的 render pass、combined image sampler descriptor set、persistent linear/nearest sampler、per-texture framebuffer cache）。
- 提供 `GetPixelFromTexture`（基于 `DownloadFromTexture` 的简化实现）。
- `oak_renderer_is_available` 现在会在首次检查时尝试 `Init()`，成功后报告 Vulkan 可用。
- 测试更新：
  - `LoadsExperimentalVulkanBackendWhenAvailable`：验证 Vulkan 后端可加载、初始化、报告能力位。
  - `FallsBackWhenExperimentalVulkanUnavailable`：在 Vulkan 不可用的系统上验证回退 OpenGL；在 Vulkan 可用的系统上自动 SKIP。
  - `VulkanUploadBlitDownload`：创建 Vulkan backend，上传 U8 RGBA 纹理，经默认 pass-through shader Blit 到目标纹理，再下载并验证像素一致；该测试在当前开发环境的真实 Vulkan 驱动上通过。
- **已修复的明显问题（代码层）**：
  - 初始化幂等性：`Init()` / `PostInit()` 可安全重复调用。
  - `Blit` 中的 descriptor/sampler 生命周期：sampler 与 descriptor set 在 `EndOneTimeCommands` 后统一释放。
  - sampler binding：从数组绑定改为显式 `layout(set=0, binding=N)`，避免跨驱动 array-of-samplers 行为不一致。
  - image layout 跟踪：输入纹理在绘制前被过渡到 `SHADER_READ_ONLY_OPTIMAL`。
  - viewport/scissor：改为 dynamic state，避免 pipeline 缓存 key 遗漏视口尺寸。
  - render pass clear：`clear_destination` 为 true 时 `loadOp` 设为 `CLEAR`。
  - 格式支持探测：通过 `vkGetPhysicalDeviceFormatProperties` 检查 `COLOR_ATTACHMENT` 能力，3-channel 不支持时回退到 4-channel（上传/下载的 CPU 侧通道对齐仍待完善）。
  - framebuffer / sampler 缓存：每张纹理延迟创建并复用 framebuffer；按插值模式复用 linear/nearest sampler。
  - 单通道纹理 swizzle：image view 组件映射为 R→RGB、A=1，匹配 OpenGL 灰度行为。
  - 纹理启用标志：为声明 `NAME_enabled` 的 shader 自动设置 0/1。
- **已知限制 / 待完善**：
  - 链接边界已最小化，`liboakvulkan.so` 现在只依赖 `libolive-rendercore`。
  - 单通道/3-channel 格式的上传/下载 CPU 侧对齐、回退格式与请求格式不一致时的数据转换仍待完善。
  - `Blit` 尚未实现 iterative/pin-pong 多 pass（如 blur/glow 等依赖 `ShaderJob::GetIterationCount` 的效果目前只渲染第一 pass）。
  - 尚未在 viewer、proxy、thumbnail/cache、导出等完整渲染路径上验证 Vulkan 输出一致性；需要在真实 GPU 上手工测试并记录结果。

## 阶段 4：Viewer 双后端（backend-neutral 路径已落地，Vulkan viewer 为原型）

- 当前 viewer display 基于 OpenGL widget 和 GL texture id。
- 默认构建下 Viewer 的 managed display 现在使用 `DynamicRenderer` 创建 renderer，并把现有 `QOpenGLContext` 传入动态后端；若动态后端加载失败则回退到 `OpenGLRenderer`。
- `OAK_ENABLE_DYNAMIC_RENDER_BACKEND` 默认改为 `ON`，保留 `OFF` 作为应急开关。
- 新增 backend-neutral viewer path 框架：
  - `ManagedDisplayWidget` 支持非 OpenGL inner widget（普通 `QWidget`），通过 `Renderer::IsOpenGL()` 判断。
  - `RenderManager` 不再在 `requested_backend_ == kVulkan` 时强制 fallback。
  - `ViewerDisplayWidget` 已移除 `glIsTexture()` 的直接 OpenGL 依赖，改为通用的跨 renderer texture 拷贝。
  - `ScopeBase` 在 backend-neutral 时安全跳过（TODO：完整 scope display 路径）。
- OpenGL 使用现有 `QOpenGLWidget/QOpenGLWindow`。
- Vulkan / backend-neutral viewer readback display 路径（offscreen texture → download → QImage → QPainter）已搭建：
  - 新增 `ManagedDisplayWidgetBackendNeutral`，在普通 `QWidget` 的 `paintEvent` 中转发到 `ManagedDisplayWidget::OnPaint`。
  - `ViewerDisplayWidget::OnPaint` 在 backend-neutral 模式下改用 `QPainter` 填充背景，将颜色管理后的画面渲染到 U8 RGBA offscreen texture，再 `Download` 到 CPU buffer，最后用 `QImage::Format_RGBA8888_Premultiplied` + `setDevicePixelRatio` 绘制到 inner widget。
  - OpenGL 路径保持原有 `BlitColorManaged` 直接到 widget 不变。
- Viewer 只消费后端 texture handle 或 readback frame，不直接假设 GL texture id。
- **状态说明**：backend-neutral 代码已合并；VulkanRenderer 现在可完成单 pass Blit，Viewer 的 backend-neutral readback 路径在代码层面可工作，但尚未在完整 UI 播放/导出流程中验证。

## 阶段 5：OpenFX 处理边界（边界框架已完成，Vulkan 路径待验证）

- OpenFX 插件 OpenGL 渲染路径保留 OpenGL 依赖，不强行改写。
- `PluginRenderer` 不再继承 `OpenGLRenderer`，改为持有通用的 `Renderer *`：
  - OpenGL 渲染路径仅在 `renderer_->IsOpenGL()` 为 true 时启用，并正确调用 `OlivePluginInstance::setOpenGLEnabled(use_opengl)`。
  - 非 OpenGL 渲染器（Vulkan、DynamicRenderer 加载的任意后端）自动回退到 CPU readback/upload 路径，不再因缺少 OpenGL context 而直接跳过插件渲染。
- 将 OFX 输出纹理绑定/解绑抽象为 `Renderer::AttachOutputTexture` / `DetachOutputTexture`：
  - `OpenGLRenderer` 实现为 `AttachTextureAsDestination` / `DetachTextureAsDestination`。
  - C ABI 新增 `oak_renderer_attach_output_texture` / `oak_renderer_detach_output_texture`。
  - `DynamicRenderer` 通过 C ABI 转发，使动态 OpenGL 后端也能支持 OFX OpenGL 渲染。
  - `VulkanRenderer` 默认 no-op，Vulkan 项目中的 OFX 插件回退到 CPU 路径。
- 格式转换（`ConvertFrameIfNeeded`、`ConvertTextureForParams`）、readback（`ReadbackTextureToFrame`）、upload 等辅助函数保持后端无关，通过 `Renderer` 接口调用，无需移入后端库。
- `RenderProcessor::ProcessPluginJob` 不再要求 `render_ctx_` 实现 `OpenGLContextProvider`，任何 `Renderer` 都能驱动插件渲染。
- 更新相关 gtest：`PluginRenderer` 构造函数现在需要传入 renderer 指针，测试传入 `nullptr` 验证纯 CPU 路径。
- **状态说明**：后端无关的边界框架和 OpenGL 动态路径已可编译并通过现有测试；Vulkan 下的 OFX CPU 回退路径代码已就位，并在 Vulkan 可完成基础 Blit 的当前版本上具备验证条件。

## 完成标准

- [x] 主程序默认不再直接 new `OpenGLRenderer`，而是通过 `DynamicRenderer` 动态加载 OpenGL/Vulkan 后端；加载失败时保留回退到 `OpenGLRenderer` 的安全路径。
- [x] `OAK_ENABLE_DYNAMIC_RENDER_BACKEND` 默认 `ON`，`liboakgl.so` 默认构建并安装；`liboakvulkan.so` 在检测到 Vulkan 开发库时构建并安装。
- [x] OpenGL 后端库可单独构建、加载、初始化、销毁。
- [x] 用户能在配置中选择 OpenGL/Vulkan。
- [x] Vulkan 不可用时自动回退到 OpenGL，不崩溃；`RenderManager::backend()` 会在 `DynamicRenderer` 内部回退后同步为实际运行后端。
- [x] 链接边界已最小化：`oakgl` / `oakvulkan` 现在只链接独立的 `libolive-rendercore`，不再拉入完整 editor 代码；库体积从约 21 MB 降至约 600 KB。
- [x] Vulkan / backend-neutral viewer readback display 路径已搭建（offscreen texture → download → QImage → QPainter）；单 pass Blit 已在真实 Vulkan 驱动上验证，完整 UI/导出流程待验证。
- [x] OpenFX 插件渲染边界已处理：`PluginRenderer` 后端无关化，非 OpenGL 渲染器自动回退 CPU 路径，动态 OpenGL 后端通过 C ABI 支持 OFX OpenGL 输出绑定。
- [ ] 手工测试计划覆盖 viewer、proxy、scope、导出等完整路径。
