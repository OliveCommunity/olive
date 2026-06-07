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

## 阶段 2.5：最小化 OpenGL 后端链接边界

当前工程大量代码和静态依赖被编译进 `libolive-editor` object 库。直接把整个 object 库塞进 `liboakgl.so` 会带来两个问题：

- 非 PIC 静态依赖会阻塞共享库链接，例如 `KDDockWidgets`。
- 主程序和后端库会复制全局状态，增加配置、cache、单例和 Qt meta-object 的一致性风险。

当前实验构建已经可以通过 `OAK_ENABLE_DYNAMIC_RENDER_BACKEND=ON` 生成 `liboakgl.so`，做法是把相关 object/static 依赖切到 PIC 后链接进 OpenGL 后端库。这满足“动态库一侧 C++ 实现 + C ABI 导出”的第一步，但它仍不是最终边界：后端库暂时会带入较多 editor 代码和全局状态。

已新增 `DynamicRenderBackend.LoadsExperimentalOpenGLBackend` smoke test：默认构建跳过，实验构建会实际加载 `liboakgl` 并验证 create/destroy C ABI 路径。

已新增可选 `oak_renderer_is_available` C ABI：后端库可以在被加载和 create 后报告自身是否可用。OpenGL 后端返回可用；Vulkan 占位后端返回不可用，适配器随后卸载它并回退 OpenGL。这把“后端存在”和“后端可用于渲染”分开，避免后续最小化链接边界时把不可用实现误接入渲染路径。

已新增 `oak_renderer_get_info` C ABI：后端库可以报告 ABI 版本、后端类型、能力位和状态字符串。默认构建也会编译检查 OpenGL/Vulkan 两个 C wrapper，避免只在实验构建中发现 C ABI 破损。

因此动态后端成为默认路径前，仍需要把 OpenGL 后端库的链接边界收敛到最小集合：

- 后端库只拥有 OpenGL/Vulkan native 操作和必要的后端私有状态。
- 主程序侧保留项目、节点、任务、cache、OpenFX host 等 editor 状态。
- 如果某个后端函数需要主程序创建 `Texture` 或访问 cache，用 C callback table 从后端回调主程序，而不是把完整 editor 链进后端库。
- `OAK_ENABLE_DYNAMIC_RENDER_BACKEND` 可用于实验构建和加载验证；默认启用前必须完成最小链接边界拆分并通过 CI。

## 阶段 3：Vulkan 后端

- 新增 Vulkan 后端库。
- 已新增 `liboakvulkan.so` 实验占位库：内部使用 C++，对外导出与 OpenGL 后端一致的 C ABI；当前 `oak_renderer_is_available` 返回 `false`，请求 Vulkan 时由 `DynamicRenderer` 自动回退 OpenGL。
- 已新增 `DynamicRenderBackend.FallsBackWhenExperimentalVulkanUnavailable` smoke test：默认构建跳过，实验构建验证 Vulkan 占位后端可加载且不会被误用为可渲染后端。
- 实现 Vulkan instance/device/swapchain 或 offscreen image 管理。
- 实现 texture 创建、上传、下载、shader 编译/缓存、blit、clear、flush。
- 提供 OpenGL 辅助格式转换函数的 Vulkan 替代版本。
- 确认 CPU readback、proxy preview、scope、thumbnail/cache 输出一致。

## 阶段 4：Viewer 双后端

- 当前 viewer display 基于 OpenGL widget 和 GL texture id。
- 已在 `OAK_ENABLE_DYNAMIC_RENDER_BACKEND=ON` 实验构建下让 Viewer 的 managed display 使用 `DynamicRenderer` 创建 renderer，并把现有 `QOpenGLContext` 传入动态后端；默认构建仍直接使用 `OpenGLRenderer`。
- 新增 backend-neutral viewer path。
- OpenGL 使用现有 `QOpenGLWidget/QOpenGLWindow`。
- Vulkan 使用 `QVulkanWindow` 或 Qt RHI/QRhi 显示路径。
- Viewer 只消费后端 texture handle 或 readback frame，不直接假设 GL texture id。

## 阶段 5：OpenFX 处理边界

- OpenFX 插件 OpenGL 渲染路径保留 OpenGL 依赖，不强行改写。
- 格式转换、readback、upload 等辅助函数移入后端库。
- Vulkan 后端提供等价辅助函数。
- 如果某个 OFX 插件必须走 OpenGL，则在 Vulkan 项目里明确使用 OpenGL 兼容路径或回退。

## 完成标准

- 主程序不直接 new `OpenGLRenderer` 作为默认渲染路径，而是通过动态后端适配器创建。
- OpenGL 后端库可单独构建、加载、初始化、销毁。
- 用户能在配置中选择 OpenGL/Vulkan。
- Vulkan 未实现完整 renderer 前，请求 Vulkan 不崩溃，并有明确回退。
- 手工测试计划覆盖 OpenGL/Vulkan 选择、重启持久化、回退、viewer、proxy、scope、导出。
