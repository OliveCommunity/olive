# M2 · oakundo 拆分手册

> 内容：`engine/undo/`（UndoCommand、MultiUndoCommand、UndoStack）。
> 依赖：common 2、node/project.h **1 处**（undocommand.cpp）。
> 被依赖：node 4、timeline 1、render 1、plugin 2、capi 14。
> 拆分顺序第 2 位（近叶子）。

## 1. 目标形态

```
oakundo/
  include/oakundo/{undocommand.h, undostack.h, types.h, export.h}
  src/
  tests/  # oakundo_gtest
```

## 2. 冻结 C API

### 2.1 `oakundo/undocommand.h`

```c
typedef struct OakUndoCommand OakUndoCommand;

/* 回调式命令（app 与跨模块命令的统一载体） */
typedef void (*oakundo_command_fn)(void *userdata);
OAKUNDO_API OakUndoCommand *oakundo_command_create(
	const char *name, oakundo_command_fn redo, oakundo_command_fn undo,
	oakundo_command_fn free_fn, void *userdata);
OAKUNDO_API OakUndoCommand *oakundo_command_create_multi(void);
OAKUNDO_API int  oakundo_command_multi_add_child(OakUndoCommand *multi,
	OakUndoCommand *child);
OAKUNDO_API int  oakundo_command_multi_child_count(
	const OakUndoCommand *multi);
OAKUNDO_API void oakundo_command_redo_now(OakUndoCommand *cmd);
OAKUNDO_API void oakundo_command_undo_now(OakUndoCommand *cmd);
OAKUNDO_API void oakundo_command_free(OakUndoCommand *cmd);  /* NULL no-op */
```

### 2.2 `oakundo/undostack.h`

```c
typedef struct OakUndoStack OakUndoStack;
OAKUNDO_API OakUndoStack *oakundo_undostack_init(void *parent_qobject);
OAKUNDO_API void oakundo_undostack_free(OakUndoStack *self);

OAKUNDO_API void oakundo_undostack_push(OakUndoStack *self,
	OakUndoCommand *cmd, const char *name);
/* push_pre_executed：子命令已 redo 过，入栈不重复 redo（undo 分组用） */
OAKUNDO_API void oakundo_undostack_push_pre_executed(OakUndoStack *self,
	OakUndoCommand *cmd, const char *name);

OAKUNDO_API int  oakundo_undostack_can_undo(const OakUndoStack *self);
OAKUNDO_API int  oakundo_undostack_can_redo(const OakUndoStack *self);
OAKUNDO_API void oakundo_undostack_undo(OakUndoStack *self);
OAKUNDO_API void oakundo_undostack_redo(OakUndoStack *self);
OAKUNDO_API void oakundo_undostack_jump(OakUndoStack *self, int64_t index);
OAKUNDO_API void oakundo_undostack_clear(OakUndoStack *self);
OAKUNDO_API int64_t oakundo_undostack_count(const OakUndoStack *self);
OAKUNDO_API int64_t oakundo_undostack_index(const OakUndoStack *self);
OAKUNDO_API int  oakundo_undostack_command_text(OakUndoStack *self,
	int64_t row, char *buf, int buf_size);
OAKUNDO_API int  oakundo_undostack_command_is_done(OakUndoStack *self,
	int64_t row);
OAKUNDO_API void oakundo_undostack_update_actions(OakUndoStack *self);
/* QAction* 句柄（GUI 菜单绑定用，borrowed） */
OAKUNDO_API void *oakundo_undostack_undo_action(OakUndoStack *self);
OAKUNDO_API void *oakundo_undostack_redo_action(OakUndoStack *self);

/* 事件（index_changed） */
#define OAKUNDO_EVENT_INDEX_CHANGED 1
typedef void (*oakundo_event_fn)(int event_id, int64_t a, int64_t b,
	void *userdata);
OAKUNDO_API int64_t oakundo_undostack_subscribe(OakUndoStack *self,
	int event_id, oakundo_event_fn fn, void *userdata);
OAKUNDO_API void oakundo_unsubscribe(int64_t subscription_id);
```

## 3. 切割点（1 处）

`undocommand.cpp` include `node/project.h`（`get_relevant_project()`
虚函数的 Project 类型）。
处理：`UndoCommand::get_relevant_project()` 的返回类型在 oakundo
内部改为不透明 `void *`（oakundo 不解释它）；node 侧（M3）在自己的
适配层把 `void *` 与 `olive::Project *` 互转。**不改语义**——
Project* 本来就只是作为不透明身份被使用（修改标记归属）。

## 4. 消费侧适配（按 01 §2）

- `node/`（4 处）、`timeline/`（1）、`render/`（1）、`plugin/`（2）：
  各自放 `adapter/undocommand.h`，转发用到的方法子集。
- `src/capi/undo.cpp`（14 处）：facade 的 undo 族实现改为转发
  oakundo C API（或保持 facade 直链 oakundo——facade 是装配层，
  裁决见 M9 §4，默认直链不绕圈）。

## 5. 测试（映射 03 §2/§3）

- 每条 API 正常+错误路径（NULL self、空 push 不入栈——
  空 MultiUndoCommand 被删除的既有行为必须有 TEST 钉死）。
- push/undo/redo/jump/clear 全序列；command_text/is_done 边界行。
- 事件：push 后 index_changed 触发且 a=新 index；unsubscribe 后不再触发。
- 往返测试：C API 与适配类各做一遍 push-undo-redo，状态一致。
- `oakundo_debug_alive_count()`：init/free 配对无泄漏。
