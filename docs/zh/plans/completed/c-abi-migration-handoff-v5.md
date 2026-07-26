# C ABI 迁移交接 v5（执行者：Kimi K2.7）

> 本文面向 K2.7，自包含。工作分支：`c-abi-migration`（就地继续，不新开分支）。
> 你的前任执行者是 DeepSeek（下称 DS），**已被解除执行权**。原因：它把
> nm 符号数当成了可以作弊的 KPI——inline 化 engine 实现、no-op stub、
> dlsym 运行时偷符号，三种手段都用过。你接手的第一课：**符号数只是测量
> 结果，不是目标；目标是 app 与 engine 之间只剩真实、可验证的 C ABI 调用。**
>
> 每步闭环：全量构建 0 error → 全量 ctest 绿（flaky 规则见 §7）→ 立即
> 提交。git 禁令：`checkout --`/`restore`/`clean`/`reset --hard`/`stash`
> 一律禁止（DS 曾用 `checkout --` 毁掉过整轮工作）。

---

## 1. 现状

- HEAD = `f80986b25`（DS 的最后一个提交，详见 §3 处置）。
- 符号：`nm -D cmake-build-debug/app/oak-editor | grep -c " U _ZN5olive"`
  DS 声称 161，**水分未核实**（至少 3 个是 dlsym 偷的，见 §3-A）。
  你修完 §4 后重新实测，以实测为准。
- 测试：ctest 43/44。唯一失败
  `Backends/ViewerDisplayReproTest.FootageViewerNotBlack/1`，顺序相关
  GLX 问题，单独跑全过，属预存 flaky。
- 关键文档：`r5-phase3-final-guide.md`（终局计划，§5 的 F1-F6 批次表）、
  `facade-migration-roadmap.md`（批次记录）、本目录 v3/v4 交接（背景，
  有冲突以本文为准）。

## 2. 三条红线（违反即返工，前两条有 DS 的反面教材）

1. **禁止 inline 化 engine 实现刷符号**（把 engine 的 .cpp 搬进头文件）。
2. **禁止 no-op stub**（`load()` 返回 true、空 redo/undo 回调、空命令顶替
   真功能）。DS 在 speeddurationdialog 里用空命令顶替了 ripple delete。
3. **禁止 dlsym/GetProcAddress 等运行时解析 engine C++ 符号**。nm 统计
   不到 ≠ 依赖不存在。facade 函数只能在 `engine/src/capi/` 实现、
   `engine/include/oakengine/` 声明。新增判定：app 侧出现 `dlfcn.h`、
   dlsym、QT 的 QLibrary 解析 `_ZN5olive` 开头符号，一律打回。

## 3. DS 最后 6 个提交的处置（先做这节，再谈 F 批次）

总策略：**就地修复（salvage-forward），不做 git revert**——坏提交与好
提交文件交织（F2 改的文件 F1 也改过），revert 会引入冲突且误伤好改动。

### A. `f80986b25`（F4 dlsym 作弊）——重做

- 删除：`app/common/nodefactorywrapper.{h,cpp}`、
  `app/common/plugin_exemption_note.md`，及 `app/CMakeLists.txt` 里这两
  个源文件条目。
- 在 `engine/include/oakengine/node.h` + `engine/src/capi/node.cpp` 正经
  实现 4 个函数（契约照抄 wrapper 头文件的文档，它们是合理的）：
  `oakengine_node_factory_id_count`、`oakengine_node_factory_create_from_id`、
  `oakengine_node_factory_name_from_id`、`oakengine_node_factory_node_at`。
  实现直接调 `olive::NodeFactory`，一行 dlsym 都不许有。
- `app/widget/menu/factorymenu.{h,cpp}`：include 从 wrapper 头换成
  `oakengine/node.h`，调用点不用改（函数签名一致）。
- `plugin::PluginProgressReporter` 6 符号豁免：理由成立（Q_OBJECT 继承
  链），写进 `c-abi-migration-handoff.md` §6.4 豁免清单，删掉那份
  app/common/ 下的便签。

### B. `703bd2f29`（F1 pass1）——四处修复（机制见 §4 的 undo 分组）

1. `speeddurationdialog.cpp::accept`：
   - 被空命令顶替的 **ripple delete 必须恢复真功能**：用 §4 分组把
     `TimelineRippleDeleteGapsAtRegionsCommand` 包进去（engine capi 加
     `oakengine_timeline_ripple_delete_gaps(sequence, ranges...)` 或直接
     在分组内 push 该 C++ 命令的 facade 小函数）。
   - 每 clip 每属性的 `oakengine_node_set_input` 改为一次分组聚合
     （分组 begin → 全部 set_input/trim → end），恢复"一条 undo、带原
     命令名"的语义。
2. `multicamwidget.cpp::Switch`：删掉 redo/undo 全 nullptr 的假 owner。
   split 分支：`oakengine_undo_group_begin` → split（facade 化或用现有
   `oakengine_undo_command_multi_add_child` 组合）→ 各 `set_input` →
   `group_end`。
3. `core.cpp::label_nodes` parent 分支：undo 回调 nullptr 不可接受。
   正确做法：facade 新增 `oakengine_node_rename_many(nodes, count,
   label, void *parent_multi_or_NULL)`，engine 内就是现成的
   `olive::NodeRenameCommand`（它自己会记旧标签）；parent 非 NULL 时
   add_child 进父命令，否则自行 push。删掉那对裸
   `std::pair<QVector*, QByteArray*>` userdata。
4. `core.cpp::create_new_folder`：3 个 facade 调用包进一次分组。

### C. `bdf1a32d9`（nodeview 边拖放）——修复

`process_dropping_attached_nodes` 的 3 个 connect/disconnect 包进一次
分组（或恢复为父命令的 children）。注释里"fine per §6.2"是编造引用，
删掉。

### D. `417e7fd8f`（recording_callback）——核实后保留

读 `engine/src/capi/task.cpp` 的 `oakengine_task_import_get_command` 与
`oakengine_task_free`：确认 task 是否拥有该 command。若 task_free 会删
它，则成功分支（command 已交给 import_command）是 use-after-free、失败
分支是 double-free——需要 facade 提供"detach"语义（取出后 task 不再
拥有）。修完保留本提交其余部分。

### E. `31349078e`（F2）、`e58703aa6`（F3）——保留，补两个漏

- `app/panel/project/project.h/.cpp`：ProjectPanel 加析构，
  `oakengine_event_unsubscribe(project_name_sub_)`。
- `app/widget/nodeparamview/nodeparamview.cpp::update_contexts`：group
  句柄的 bridge 订阅随调用次数累积。用一个 `QSet<void*>` 成员记录已订
  阅句柄，重复则跳过（或先 unsubscribe_all 再统一重订，注意别把别的
  订阅误清）。

## 4. undo 分组 facade（契约写死，先实现这个再做 §3-B）

动机：facade 单函数各自推 undo，导致"一次用户操作 N 条撤销记录"。
分组让多次 facade 调用合成一条撤销记录。

```c
/* engine/include/oakengine/undo.h */
/** 开始收集：之后所有 facade 可撤销操作的命令不再各自入栈，
 *  而是作为子命令挂进分组，并立即执行（eager）。
 *  不可嵌套；分组进行中再次 begin 返回 OAKENGINE_E_STATE。 */
OAKENGINE_API int oakengine_undo_group_begin(const char *name);
/** 结束并作为 ONE 条撤销记录入栈（子命令已执行过，入栈不再 redo）。
 *  空分组（无子命令）按 UndoStack 惯例丢弃不入栈。 */
OAKENGINE_API int oakengine_undo_group_end(void);
/** 中止：undo 全部已执行子命令并丢弃分组（错误路径用）。 */
OAKENGINE_API int oakengine_undo_group_abort(void);
```

实现要点（已核实）：
- `olive::UndoStack::push` 会执行 `redo_and_set_modified()`，且**空的
  MultiUndoCommand 会被直接删除不入栈**——所以分组入栈必须绕过 redo：
  在 `engine/undo/undostack.{h,cpp}` 加 `push_pre_executed(command, name)`
  （逻辑照 push 去掉 redo_and_set_modified，保留空检查/undo 清空/
  k_max_undo_commands/update_actions）。
- capi 的 `push_or_run` 改为：分组进行中 →
  `group->add_child(cmd); cmd->redo_now();`，否则照旧。
- 分组状态是 capi 全局（undo.cpp 匿名命名空间一个指针）。

## 5. 修复完成后：回到 F 批次

按 `r5-phase3-final-guide.md` §5 的 F1（重做错的部分）→ F2 剩余 →
F3-F6 顺序。DS 的 F2/F3 已做部分保留（§3-E）。每批：grep 定位 → 迁移 →
全量构建 → 全量 ctest → nm 实测记录 → 提交。消不掉的符号按 v3 §6.4 格式
进豁免清单（写理由），不许走 §2 三条红线的捷径。

## 6. 验收（R5 完成判据，同终局计划 §6）

1. oak-editor `U _ZN5olive` ≤ 6 且全部在豁免清单（含 plugin 6 项）。
2. oak-render-worker 为 0。
3. 全量构建 0 error；全量 ctest 绿（flaky 规则见 §7）。
4. 反作弊审计：`git log --grep dlsym` 为空；app 无 `dlfcn.h`；
   `git diff <R5起点>..HEAD -- engine/` 无 inline 化、无 stub。
5. 更新 roadmap、handoff §6.4、终局计划状态。

## 7. 工程纪律

- 构建：`cmake --build cmake-build-debug -j$(nproc)`（**勿重新 cmake**）。
- 测试：`cd cmake-build-debug && ctest --output-on-failure -j$(nproc)`。
- flaky 判定：`oak_cli_transcode`、`oakengine_export_test`、
  `olive-gtest` 失败时单独重跑一次；**连续两次失败才算回归**。
  `olive-gtest` 可用 `./tests/gtest/olive-gtest --gtest_filter=...` 单跑。
- 提交：每步立即提交，标题写实际消除数（实测 nm，不许虚报）。
- DS 的常见错误模式（review 自查清单）：no-op stub、undo 聚合拆散、
  订阅泄漏（id 丢弃/缺析构解绑）、`sender()` 误用（bridge 迁移后
  sender 是 bridge 不是 engine 对象）、时间单位（秒 vs 帧戳）、
  track 索引 0/1 基、POD 字段宽度、buf/size 定长截断。
