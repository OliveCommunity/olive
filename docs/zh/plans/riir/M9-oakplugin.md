# M9 · oakplugin 拆分手册 + facade 装配层裁决

> 内容：`engine/pluginSupport/`（OpenFX host：OliveHost、
> OlivePluginInstance、PluginNode、PluginProgressReporter）。
> 依赖：node 6、render 6、common 6、core 2、undo 2、coreengine 2。
> 拆分顺序第 9 位（最后拆的实体模块）。文末 §4 是 liboakengine
> 装配层的最终裁决。

## 1. 目标形态

```
oakplugin/
  include/oakplugin/{host.h, instance.h, progress.h, types.h, export.h}
  src/
  tests/  # oakplugin_gtest
```

## 2. 冻结 C API

### 2.1 `oakplugin/host.h`

```c
OAKPL_API int oakplugin_host_init(void);
OAKPL_API void oakplugin_host_shutdown(void);
OAKPL_API int oakplugin_host_scan(const char *const *bundle_dirs,
	int dir_count);
OAKPL_API int oakplugin_host_plugin_count(void);
OAKPL_API int oakplugin_host_plugin_id_at(int i, char *buf, int n);
OAKPL_API int oakplugin_host_plugin_label(const char *plugin_id,
	char *buf, int n);
```

### 2.2 `oakplugin/instance.h`

```c
typedef struct OakPluginInstance OakPluginInstance;
OAKPL_API OakPluginInstance *oakplugin_instance_create(
	const char *plugin_id);
OAKPL_API void oakplugin_instance_free(OakPluginInstance *i);
OAKPL_API int oakplugin_instance_set_param(OakPluginInstance *i,
	const char *param_id, const oak_node_value *v);
OAKPL_API int oakplugin_instance_get_param(OakPluginInstance *i,
	const char *param_id, oak_node_value *out);
OAKPL_API int oakplugin_instance_render(OakPluginInstance *i,
	OakCodecFrame *dst, const OakCodecFrame *src, int64_t ts);
/* 进度事件（R6 已把 cancelled 信号改成 C 回调，沿用该机制） */
OAKPL_API void oakplugin_instance_set_progress_cb(OakPluginInstance *i,
	oakplugin_progress_fn fn, void *userdata);
OAKPL_API void oakplugin_instance_cancel(OakPluginInstance *i);
```

### 2.3 `oakplugin/progress.h`

```c
typedef struct OakPluginProgress OakPluginProgress; /* 报告器句柄 */
OAKPL_API OakPluginProgress *oakplugin_progress_create_dialog(
	const char *message, const char *title);  /* UI 侧实现 */
OAKPL_API void oakplugin_progress_free(OakPluginProgress *p);
```

## 3. 切割点

| 现状 | 处理 |
|---|---|
| plugin → node/ 6（plugins/plugin.h 3 等） | 经 oaknode C ABI（PluginNode 是节点，留在 oaknode；oakplugin 只经 factory/host 接口交互）——**PluginNode 归属裁决：PluginNode 类随 oaknode 走**（它是节点体系成员），oakplugin 提供 host/instance |
| plugin → render/ 6（videoparams 3 等） | videoparams 已下沉；其余经 oakrender C ABI |
| plugin → undo/ 2 | oakundo 适配类 |
| plugin → coreengine.h 2 | 插件注册点内聚进 oakplugin_host_init |

## 4. facade 装配层最终裁决（liboakengine 的终态）

M1-M9 全部完成后，`engine/src/capi` + `coreengine` + `tool/` +
`ui/` 残余构成 liboakengine。裁决（选定）：

**facade 直链各模块，不绕 C ABI 自调**。即：facade 的 `oakengine_*`
实现可以继续直接调用各模块的 C++ 内部（链接 oaknode/oakrender 等
的静态或共享库），不要求 facade 经各模块的公共 C ABI 兜圈。
理由：facade 与 app 的边界（oakengine_*）已经纯 C 且 nm=0，模块间
边界是给"模块互相调用"用的；facade 是装配层，自家人不绕远路。
**但**：`coreengine.h` 被 node/render/task/plugin 引用的 5+2+2+2
处必须内聚（各模块的初始化改由各自的 `oak<mod>_init` 完成，
coreengine 只做编排调用）。

终态验证：

```
nm -D --defined-only liboakengine.so | grep -c " T _Z"      # 0（R7-B）
nm -D --defined-only liboaknode.so | grep -c " T _Z"        # 0（每模块同查）
ldd liboakengine.so | grep oak                               # 只见 oak* 模块库
全量构建 0 error；全量 ctest 绿
```

## 5. 测试（映射 03 §2/§3）

- host：init/scan/枚举（照现有 OliveHost 测试的 bundle fixture）。
- instance：Shadertoy 类插件（CI 里可用的）创建/参数/渲染一帧非空。
- progress：回调触发与 cancel。
- `oakplugin_debug_alive_count()` 泄漏断言。
