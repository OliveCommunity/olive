# M8 · oaktask 拆分手册

> 内容：`engine/task/`（Task 基类、TaskManager、任务编排、cache 任务；
> **工程文件 IO——project/load/save/loadotio/saveotio 的落盘部分——
> 已划给 oakstorage（M10），本模块只做任务壳**）。
> 依赖：node 38、codec 4、render 4、common 4、config 2、timeline 1、
> coreengine 2。
> 拆分顺序第 8 位。

## 1. 目标形态

```
oaktask/
  include/oaktask/{task.h, manager.h, project.h, types.h, export.h}
  src/
  tests/  # oaktask_gtest
```

## 2. 冻结 C API

### 2.1 `oaktask/task.h`（单任务句柄，照 oakengine/task.h 模板对齐）

```c
typedef struct OakTaskTask OakTaskTask;
OAKTK_API void oaktask_task_free(OakTaskTask *t);
OAKTK_API int oaktask_task_start(OakTaskTask *t);       /* 异步 */
OAKTK_API int oaktask_task_start_sync(OakTaskTask *t);
OAKTK_API int oaktask_task_cancel(OakTaskTask *t);
OAKTK_API int oaktask_task_is_finished(const OakTaskTask *t);
OAKTK_API int oaktask_task_succeeded(const OakTaskTask *t);
OAKTK_API int oaktask_task_progress(const OakTaskTask *t, double *out);
OAKTK_API int oaktask_task_title(OakTaskTask *t, char *buf, int n);
OAKTK_API int oaktask_task_error(OakTaskTask *t, char *buf, int n);
/* 进度/完成回调：任务是异步命令，回调即其返回通道——04 §3 唯一
 * 例外情形（一次性语义，FINISHED 后自动失效） */
OAKTK_API int64_t oaktask_task_subscribe(OakTaskTask *t, int32_t event_id,
	oaktask_event_fn fn, void *userdata);
```

### 2.2 `oaktask/project.h`（任务工厂 + 结果访问器）

```c
OAKTK_API OakTaskTask *oaktask_create_project_load(const char *filename);
OAKTK_API OakTaskTask *oaktask_create_project_save(OakNodeProject *p,
	const char *filename_or_NULL, int use_compression,
	const void *layout_or_NULL);
/* 注：以上两个工厂是薄壳——文件 IO 全部委托 oakstorage（M10 §2.4），
 * 任务只保留进度/取消/事件编排；`use_compression` 等打包进
 * oakstorage_save 的 options 位掩码。 */
OAKTK_API OakTaskTask *oaktask_create_project_import(OakNodeNode *folder,
	const char *const *urls, int url_count);
OAKTK_API OakTaskTask *oaktask_create_project_load_otio(
	const char *filename, const int *sequence_indexes, int count);
OAKTK_API OakTaskTask *oaktask_create_project_save_otio(
	OakNodeProject *p, const char *filename, const int *sequence_indexes,
	int count);
/* import 结果（task 成功后读，borrowed） */
OAKTK_API OakUndoCommand *oaktask_import_take_command(OakTaskTask *t); /* 所有权转移（2026-08 修订：按 01 §0.1 由 void* 改为有类型句柄） */
OAKTK_API int oaktask_import_footage_count(OakTaskTask *t);
OAKTK_API OakNodeNode *oaktask_import_footage_at(OakTaskTask *t, int i);
OAKTK_API int oaktask_import_invalid_count(OakTaskTask *t);
OAKTK_API int oaktask_import_invalid_at(OakTaskTask *t, int i,
	char *buf, int n);
/* load 结果 */
OAKTK_API OakNodeProject *oaktask_load_take_project(OakTaskTask *t);
```

### 2.3 `oaktask/manager.h`

```c
OAKTK_API int oaktask_manager_count(void);
OAKTK_API OakTaskTask *oaktask_manager_at(int i);      /* borrowed */
OAKTK_API void oaktask_manager_delete_finished(void);
/* 无 manager 事件（2026-08 修订，04 §3）：任务的创建/删除都由调用方
 * 发起（facade 建任务即知 ADDED；delete_finished 的调用方知 REMOVED），
 * LIST_CHANGED 通知由调用方所在层发出。 */
```

## 3. 切割点

| 现状 | 处理 |
|---|---|
| task → node/ 38（footage 6、project 5、sequence 3、serializer/layout 3、colormanager 2 等） | 全部经 oaknode C ABI + 适配类（M3 已就位）——本手册工作量主体 |
| task → 工程文件 IO（load/save/loadotio/saveotio 落盘） | 划给 **oakstorage**（M3a/M10）：任务工厂保留签名，实现改为委托 `oakstorage_open/save` |
| task → codec/ 4 | 经 oakcodec C ABI（M5） |
| task → render/ 4 | 经 oakrender C ABI（M7） |
| task → timeline/ 1 | 经 oaktimeline C ABI（M4） |
| task → coreengine.h 2 | coreengine 的 Task 注册点改为 oaktask_manager 自管（manager 原本就是单例，注册调用内聚进 oaktask） |

## 4. 测试（映射 03 §2/§3）

- 每个工厂 1 个用例：demo.mp4 import（footage_count>0、command 可
  入栈）、save/load 往返（临时目录 .ove，load 后 project 非空、
  root 非空）。
- start_sync 成功/失败路径（不存在文件 → failed + error 非空）。
- 事件（异步任务例外）：STARTED→PROGRESS→FINISHED 序列（导入任务
  断言至少一次 PROGRESS 且 FINISHED.succeeded==1）。
- manager：count/at/delete_finished 行为（无事件，直接读状态断言）。
- `oaktask_debug_alive_count()` 泄漏断言。
