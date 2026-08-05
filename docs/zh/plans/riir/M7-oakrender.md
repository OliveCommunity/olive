# M7 · oakrender 拆分手册

> 内容：`engine/render/`（Renderer/OpenGLRenderer/DynamicRenderer/
> PlaybackCache/FrameHashCache/AudioWaveformCache/ColorProcessor/
> ColorManager/RenderManager/PreviewAutoCacher/DiskManager/job 族/
> shaders 加载）。
> 依赖：node 38、common 24、codec 9、core 8、config 6、plugin 5、
> audio 3、task 2、undo 1、**src 1（分层违规）**。
> 拆分顺序第 7 位。M7 完成后 oaknode 的链接级独立复核（02 §4）。

## 1. 目标形态

```
oakrender/
  include/oakrender/{renderer.h, cache.h, color.h, manager.h, display.h,
                     types.h, export.h}
  src/
  tests/  # oakrender_gtest
```

## 2. 冻结 C API

### 2.1 `oakrender/renderer.h`（渲染器/纹理/blit）

签名照 R7-A（`docs/zh/plans/completed/r7-pure-abi-plan.md` §A.2）的 display.h 重写版
**原样采用**——R7-A 先做的话，M7 直接把它从 facade 层搬进
oakrender 并改前缀 `oakrender_display_*`；本表不重复，以 R7-A 为准。
补充后端管理：

```c
OAKRD_API int oakrender_backend_count(void);
OAKRD_API int oakrender_backend_id_at(int i, char *buf, int n);
OAKRD_API int oakrender_set_backend(const char *backend_id);
OAKRD_API int oakrender_current_backend(char *buf, int n);
```

### 2.2 `oakrender/cache.h`（PlaybackCache/FrameHashCache/waveform 缓存）

```c
typedef struct OakRenderCache OakRenderCache;
OAKRD_API void oakrender_cache_invalidate(OakRenderCache *c,
	int64_t in_ts, int64_t out_ts);
OAKRD_API void oakrender_cache_validate(OakRenderCache *c,
	int64_t in_ts, int64_t out_ts);
OAKRD_API int oakrender_cache_has_validated_ranges(
	const OakRenderCache *c);
OAKRD_API int oakrender_cache_indicator_height(void);   /* 常量查询 */
/* 帧哈希缓存 */
OAKRD_API int oakrender_frame_cache_load(OakRenderCache *c,
	const char *path, const char *uuid, int64_t ts,
	OakCodecFrame **out_frame);
OAKRD_API void oakrender_frame_cache_save(OakRenderCache *c,
	const char *path, const char *uuid, const OakCodecFrame *f);
/* 无缓存事件（2026-08 修订，04 §3）：缓存的 invalidate/validate 由
 * 编辑命令的调用方触发并知情，通知由 facade 在命令后发出；
 * oakrender 不持有上层回调。渲染 ticket 的完成回调属异步命令
 * 返回通道，不在此限（见 renderer.h 族）。 */
```

### 2.3 `oakrender/color.h`

```c
typedef struct OakColorProcessor OakColorProcessor;
OAKRD_API OakColorProcessor *oakrender_color_processor_create(
	const char *src_space, const char *dst_transform, int direction);
OAKRD_API void oakrender_color_processor_free(OakColorProcessor *p);
OAKRD_API int oakrender_color_processor_convert(OakColorProcessor *p,
	double ir, double ig, double ib, double ia,
	double *or_, double *og, double *ob, double *oa);
/* ColorManager */
OAKRD_API int oakrender_color_manager_set_up_default_config(void);
OAKRD_API int oakrender_color_manager_get_config(char *buf, int n);
OAKRD_API int oakrender_color_manager_display_transform(
	const char *display, const char *view, char *buf, int n);
```

### 2.4 `oakrender/manager.h`（RenderManager/PreviewAutoCacher/
DiskManager/job 提交）

```c
OAKRD_API int oakrender_manager_init(void);
OAKRD_API void oakrender_manager_shutdown(void);
/* 异步帧请求：完成经回调送 OakCodecFrame（跨线程，retain 规则同 A.3） */
typedef void (*oakrender_frame_ready_fn)(OakCodecFrame *frame,
	int64_t ts, void *userdata);
OAKRD_API int64_t oakrender_request_frame(OakNodeNode *viewer,
	int64_t ts, oakrender_frame_ready_fn cb, void *userdata);
OAKRD_API void oakrender_cancel_request(int64_t request_id);
OAKRD_API int oakrender_set_cacher_multicam(OakNodeNode *multicam_or_NULL);
OAKRD_API int oakrender_set_display_color_processor(
	OakColorProcessor *p_or_NULL);
OAKRD_API int oakrender_disk_cache_path(char *buf, int n);
OAKRD_API int64_t oakrender_disk_cache_size(void);
OAKRD_API int oakrender_disk_cache_clear(void);
```

## 3. 切割点

| 现状 | 处理 |
|---|---|
| render → node/ 38（project 7、viewer 5、value 4、footage 4、traverser 3 等） | 全部经 oaknode C ABI + 适配类（M3 已就位）——**本手册的工作量主体** |
| render → codec/ 9（frame 7、decoder 1、conformmanager 1） | 经 oakcodec C ABI（M5） |
| render → task/ 2 | 回调注册（01 §4），task 句柄不传 |
| render → undo/ 1（undostack.h） | oakundo 适配类（M2） |
| render → audio/ 3 | 经 oakaudio C ABI（M6） |
| render → src/capi/displayinternal.h 1 次（违规） | R7-A 时随 display.h 重写消除；若 R7-A 未做，M7 先把该内部头的内容并入 oakrender |
| render → plugin/ 5 | pluginjob 等：M9 时经 oakplugin C ABI（M7 暂不断链，禁止新增） |

## 4. 测试（映射 03 §2/§3）

- cache：invalidate/validate 状态机、帧缓存存取往返（调用方触发后
  读状态断言——无事件）。
- color：默认 config 建置、processor convert 已知值（sRGB→Linear
  抽样点数值断言，容差 1e-3）。
- manager：request_frame 对 demo.mp4 + 最小 sequence 出帧非空
  （offscreen 可跑的部分）；cancel 路径。
- GL 相关：沿用 GTEST_SKIP 模式，不强求离屏可绘。
- `oakrender_debug_alive_count()` 泄漏断言。

## 5. 收尾复核（02 §4 裁决 A 的兑现）

M7 闭环时执行：
```
grep -rn '#include "render/' oaknode/src | wc -l     # 必须 0
nm -D oaknode 构建产物 | grep -c " U _ZN5olive.*render"   # 必须 0
```
node→render 的 47 次引用此时应全部经 oakrender C ABI（M3 遗留的
"暂不断链"在此结清）。
