# R7 计划：从"nm=0"到 Rust-ready 纯 C ABI

> 面向执行者（Qwen 3.8 Max），自包含。工作分支：`c-abi-migration`。
> **背景**：R6 已使 oak-editor / oak-render-worker 的 `U _ZN5olive` = 0
> （`d13e4e800`）。但按 riir.md 的目标（engine 拆模块 → Rust 重写），
> 还差两层皮：
> 1. `oakengine/display.h` 的灰色契约——签名是 `void *`，语义却是 C++
>    （往调用方内存写 `std::shared_ptr`、参数实为 `olive::VideoParams*`）；
> 2. liboakengine.so 仍导出 3486 个 C++ 符号（`nm -D --defined-only |
>    grep " T _Z"`），Rust 模块化拆分要求导出面只剩 `oakengine_*`。
>
> 三条红线照旧（禁 inline 化 engine 实现、禁 no-op stub、禁 dlsym）。
> 每步闭环：全量构建 0 error → 全量 ctest 绿 → 立即提交。
> flaky 规则：`oak_cli_transcode`/`oakengine_export_test`/`olive-gtest`
> 单独重跑一次，连续两次失败才算回归（`oak_cli_transcode` 的间歇
> SEGFAULT 是预存问题，手动跑通过为准）。

---

## R7-A：display.h 灰色契约 POD 化

### A.1 现状问题（为什么 nm=0 还不够）

`engine/include/oakengine/display.h` 的 11 个函数签名全是 `void *`，
但注释约定：
- `out_texture/out_frame` 指向调用方内存中的 `TexturePtr/FramePtr`
  （`std::shared_ptr`），engine 在其上拷贝构造 shared_ptr；
- `video_params` 实为 `const olive::VideoParams*`；
- `color_job` 实为 `const olive::ColorTransformJob*`。

Rust 无法安全持有 shared_ptr、无法构造 C++ VideoParams。app 侧 47 处
`TexturePtr/FramePtr` 成员/变量（18 文件，见 A.4）同样要换。

### A.2 新契约（设计钉死，照此实现）

**所有权协议（核心决策，不许改）**：纹理/帧句柄是不透明指针，指向
engine 堆上的控制块（内部持有 `std::shared_ptr`，实现细节对 ABI 不可见）。
所有权经显式 retain/free 转移，禁止任何"写入调用方内存的 shared_ptr"。

```c
/* engine/include/oakengine/display.h —— 重写版 */

/* ---- 渲染器 ---- */
OAKENGINE_API void *oakengine_display_renderer_create_dynamic(
	const char *backend_id, void *parent_qobject);
OAKENGINE_API void *oakengine_display_renderer_create_opengl(
	void *parent_qobject);
OAKENGINE_API int oakengine_display_renderer_init(void *renderer,
	void *gl_context);
OAKENGINE_API void oakengine_display_renderer_destroy(void *renderer);

/* ---- 纹理（句柄 = OakEngineDisplayTexture*，不透明） ---- */
/* params 用 oak_video_params POD（oakengine/videoparams.h 已有），
 * 不再是 const void*。 */
OAKENGINE_API void *oakengine_display_texture_create(
	void *renderer, const oak_video_params *params,
	const void *pixels, int linesize);
/* retain：返回同一句柄并把内部引用计数 +1（跨线程移交用，
 * 见 A.3 协议）。free：-1，归零时释放。二者对 NULL 均为 no-op。 */
OAKENGINE_API void *oakengine_display_texture_retain(void *texture);
OAKENGINE_API void oakengine_display_texture_free(void *texture);
OAKENGINE_API int oakengine_display_texture_upload(
	void *texture, const void *pixels, int linesize);
OAKENGINE_API int oakengine_display_texture_download(
	void *texture, void *pixels, int linesize);
/* 只读属性查询（替代 texture->params()/width()/format() 等） */
OAKENGINE_API int oakengine_display_texture_get_params(
	const void *texture, oak_video_params *out);
OAKENGINE_API int oakengine_display_texture_id(const void *texture);

/* ---- 帧（句柄 = OakEngineCodecFrame*，不透明，同协议） ---- */
OAKENGINE_API void *oakengine_codec_frame_create(void);
OAKENGINE_API void *oakengine_codec_frame_retain(void *frame);
OAKENGINE_API void oakengine_codec_frame_free(void *frame);
OAKENGINE_API int oakengine_codec_frame_set_video_params(
	void *frame, const oak_video_params *params);
OAKENGINE_API int oakengine_codec_frame_get_params(
	const void *frame, oak_video_params *out);
OAKENGINE_API int oakengine_codec_frame_allocate(void *frame);
OAKENGINE_API void *oakengine_codec_frame_data(void *frame);
OAKENGINE_API int oakengine_codec_frame_linesize(const void *frame);

/* ---- 色彩管理 blit ---- */
/* oak_color_transform_job POD（新定义，字段以 engine
 *  ColorTransformJob 拍平：processor 句柄 + input/output 空间 id +
 *  各向异性等）。 */
OAKENGINE_API int oakengine_display_renderer_blit_color_managed(
	void *renderer, const oak_color_transform_job *job,
	void *dst_texture, const oak_video_params *params);

/* 跨后端纹理下载（viewerdisplay 的 download_from_texture 路径） */
OAKENGINE_API int oakengine_display_renderer_download_from_texture(
	void *renderer, int texture_id, const oak_video_params *params,
	void *dst_pixels, int linesize);
```

engine 实现（`engine/src/capi/display.cpp` 重写）：

```cpp
struct OakEngineDisplayTexture { olive::TexturePtr ptr; };
struct OakEngineCodecFrame { olive::FramePtr ptr; };
// create: new OakEngineDisplayTexture{renderer->create_texture(...)}
// retain/free: new/delete 控制块（引用计数即 shared_ptr 自身）
// POD↔C++：oak_video_params ↔ olive::VideoParams 的转换函数若
//   capi 已有（viewer.cpp 的 get 路径）就抽成内部共享 helper
//   （放 engine/src/capi/videoparamsinternal.h），不许复制粘贴第三份。
```

### A.3 跨线程移交协议（最容易写错的地方，钉死）

`viewerdisplay` 的 `load_frame_`/`load_texture_` 在解码线程生产、
显示线程消费。原语义靠 shared_ptr 引用计数保活。新协议：

1. 生产侧 `oakengine_display_texture_retain(t)` 后写入共享槽；
2. 消费侧取走句柄，旧句柄 `free`；
3. 槽清空时持有一方负责 `free`。
   **每个 retain 必须配对恰好一个 free**。写完后 grep 审计配对数。

### A.4 app 侧触点清单（47 处，按文件做，每文件一提交）

| 文件 | 处数 | 要点 |
|---|---|---|
| `app/widget/viewer/viewerdisplay.{h,cpp}` | 19+15 | 最大。`texture_`/`load_texture_`/gizmo 纹理全换句柄；析构与各 reset 路径补 free；A.3 协议主要在这里 |
| `app/widget/scope/scopebase/scopebase.{h,cpp}` | 6+9 | 同模式 |
| `app/widget/manageddisplay/manageddisplay.cpp` | 6 | create_texture/blit/download |
| `app/widget/viewer/viewer.{h,cpp}` | 1+9 | FramePtr 成员换句柄 |
| `app/widget/multicam/multicamdisplay.{h,cpp}` | 1+4 | |
| `app/widget/scope/histogram/histogram.{h,cpp}` | 2+1 | |
| `app/widget/scope/waveform/waveform.{h,cpp}` | 1+1 | |
| `app/widget/scope/vectorscope/vectorscope.{h,cpp}` | 1+1 | |
| `app/panel/viewer/viewerbase.h`、`app/panel/scope/scope.{h,cpp}` | 3 | |

完成判据：app 全仓库 grep `TexturePtr|FramePtr` = 0；
`oakengine/display.h` 全文无 `shared_ptr`、无 `olive::` 出现在**签名**
（注释里也不许写 "olive::TexturePtr storage" 这种约定）。

### A.5 验证

- 新增 `engine/tests/oakengine_display_test.cpp`（无 GL 环境测错误
  路径与 retain/free 配对；GL 相关断言用现有 backend 检测跳过模式）。
- `olive-gtest` 的 `ViewerDisplayReproTest` 三个可跑通用例**必须全过**
  （这是显示路径的回归网，挂一个就是真挂）。
- 手动验证（报告里写明）：打开素材 → 画面非黑；scope 面板渲染正常。

---

## R7-B：engine visibility 收口（3486 → 只导出 oakengine_*）

### B.1 原理（已具备的条件）

`OAKENGINE_API` 在 GCC/Clang 已是
`__attribute__((visibility("default")))`（export.h:40）。给
`oakengine` 目标加 `-fvisibility=hidden` 后，只有 `oakengine_*` 导出。
`oakgl`/`oakvulkan` 两个动态后端同理（已有 `*-cabi-check` OBJECT
目标，顺带确认它们导出面也只剩 C ABI）。

### B.2 唯一难点：测试链接

隐藏符号后，直接引用 engine C++ 内部的测试会断链：
- `olive-gtest`：**1006** 个 `U _ZN5olive`
- `timeline-tests`：29
- `oakengine_export_test`：9（`make_oakengine_test` 族里引用内部的）
- `compositing-tests`：0（纯 facade，无碍）

**解法（钉死）**：engine 源码改出 OBJECT 库，测试链对象文件而非
`.so`：

```cmake
# engine/CMakeLists.txt
add_library(oakengine-obj OBJECT ${OLIVE_SOURCES})
# （POSITION_INDEPENDENT_CODE ON）
add_library(oakengine SHARED $<TARGET_OBJECTS:oakengine-obj>)
target_compile_options(oakengine-obj PRIVATE -fvisibility=hidden)
# 引用 engine C++ 内部的测试目标：
target_link_libraries(<test> PRIVATE oakengine-obj)  # 替代 oakengine
```

- `olive-gtest`（tests/gtest/CMakeLists.txt）改链 `oakengine-obj` +
  `oakengine`（facade 符号从 .so 来，避免重复定义；若 ODR 冲突则只链
  oakengine-obj，先把 facade 函数符号在 object 里的重复问题解决——
  二选一，以链接通过且 ctest 全绿为准，把选择写进提交信息）。
- `timeline-tests`、引用内部的 `oakengine_*_test` 同法。
- **禁止**：为了让测试过而把 engine 内部符号加 visibility("default")
  白名单——那是开天窗。

### B.3 验证

```
nm -D --defined-only cmake-build-debug/engine/liboakengine.so | grep -c " T _Z"   # 目标 0
nm -D --defined-only cmake-build-debug/engine/liboakengine.so | grep -c " T "     # 应等于 oakengine_* 函数数
nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"                   # 必须仍为 0
```
全量构建 0 error + 全量 ctest 绿（45 个）才提交。

---

## R7-C（低优先级，时间够再做）：app 的 engine C++ 头清理

app 仍 include ~40 个 engine C++ 头（`grep -rn '#include "' app/ |
grep -E '"(node|render|timeline|undo|task|pluginSupport)/'`）。不产生
符号引用（nm=0 已证），但 RIIR 拆模块时这些 include 会全部失效。
逐个换 facade/句柄头（`cliphandle.h`、`nodevaluehandle.h` 模式）。
**本批不设完成判据**，收尾时把剩余清单写进 riir.md 附录即可。

## 验收（R7 完成判据）

1. `display.h` 全文无 C++ 类型签名/契约注释；app 无 TexturePtr/FramePtr。
2. liboakengine.so ` T _Z` = 0；oak-editor ` U _ZN5olive` 保持 0。
3. 全量构建 0 error；全量 ctest 绿。
4. 更新 `../riir.md` 状态（边界已纯 → 可进 Step 1 拆分）、
   `facade-migration-roadmap.md` R7 批次记录。
5. 向用户报告，由用户宣布进入 riir.md §4 的模块拆分阶段。

---

## 状态：R7 已完成（eb634b53e，2026-07-27）

- R7-A ✅ display.h 已按 §A.2 契约重写（POD + 句柄 retain/free），
  app TexturePtr/FramePtr 清零。
- R7-B ✅ liboakengine.so 导出 3486 → 19（version script；19 个为
  oakgl/oakvulkan dlopen 插件 ABI，ver 文件内注释钉死）。
- 复核修复 1 处：viewer.cpp 生产端推裸 `void *`、消费端解
  `OakSharedBufferPtr` 的类型不匹配（vulkan 显示路径全挂），
  已改为 oak_make_shared_frame/texture(retain)。
- R7-C（app 的 ~40 个 engine C++ 头清理）未做，转入 riir/ 模块
  拆分阶段顺带处理（M 系列手册的适配头天然覆盖）。
- 验收：构建 0 error、ctest 45/45、双二进制 nm U _ZN5olive = 0。
  **RIIR 模块拆分（../riir/ M1-M9）解锁。**
