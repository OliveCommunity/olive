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
typedef struct OakUndoObjectParent OakUndoObjectParent;  /* borrowed QObject 挂载点 */
OAKUNDO_API OakUndoStack *oakundo_undostack_init(
	const OakUndoObjectParent *parent);
/* OakUndoObjectParent：QObject 父子树挂载点的 borrowed 不透明句柄
 * （2026-08 修订：按 01 §0.1 由 void* 改为有类型句柄；
 * 提供侧内部即 QObject*，消费侧不可解引用；可传 NULL 表无父） */
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
```

**无事件接口（2026-08 修订）**：上层对下层只有命令。push/undo/redo/jump
的调用方知道栈索引的变化（`oakundo_undostack_index` 调用后即可读），
`index_changed` 通知改由**调用方所在层**（facade 的 undo 适配层）在
每次变更命令后自行发出——oakundo 不持有任何上层回调。

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
- push/undo/redo/jump/clear 全序列；command_text/is_done 边界行；
  每步后 `oakundo_undostack_index` 读数与预期一致（替代原事件断言——
  调用方知道影响，直接读状态）。
- 往返测试：C API 与适配类各做一遍 push-undo-redo，状态一致。
- `oakundo_debug_alive_count()`：init/free 配对无泄漏。
