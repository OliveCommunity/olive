# 长期计划（plans）

本目录收纳 Oak 的**中长期规划文档与并行执行计划**。当前正在进行的
**C ABI 迁移战役**的文档在上一层（`docs/zh/`），见下方"当前进行中"。

## 目录

| 文档 | 内容 | 启动前提 |
|---|---|---|
| [`riir.md`](riir.md) | **RIIR 绞杀者模式执行计划**：C ABI 迁移完成后，把 liboakengine 安全拆成若干小模块，再逐个用 Rust 重写；含 API 冻结保证、模块图、六步流程与验证门禁 | C ABI 迁移战役验收完成 |
| [`riir/`](riir/) | **模块拆分执行手册**（riir.md 第一阶段落地）：00 总览 → 01 双层适配器规范 → 02 依赖矩阵与拆分顺序 → 03 测试规范 → M1-M9 逐模块手册（C API 冻结）；只拆分不重写，模块间 C ABI | R7 完成后启动（可与 R7 并行准备） |
| [`ai-agent-design.md`](ai-agent-design.md) | **AI Agent 设计**：多模态 LLM 经 MCP 调用策展工具面自动剪辑，渲染帧回喂形成"编辑→看图→再编辑"视觉闭环；含工具面、回放回路、安全与测试 | RIIR 拆分完成（面对一堆小库） |
| [`gtest-migration-guide.md`](gtest-migration-guide.md) | **测试统一到 Google Test**：把 OAK_ADD_TEST 宏框架、纯 C assert、已有 gtest 三套收敛为单一 Google Test，ctest 仅作运行器 | R5 验收完成后启动（可与 UI 改版并行） |
| [`ui-redesign-plan.md`](ui-redesign-plan.md) | **主界面 UI 改版**：依据 `design/` 三张设计图落地 10 个工作包（工具条、双监看、效果栈检查器、节点编辑器移位、电平条、状态栏等），全部文字精确定义 | R5 验收完成后启动（可与 GTest 迁移并行） |

## 当前进行中（不在本目录）

C ABI 迁移战役的执行文档在 `docs/zh/`：

- `c-abi-migration-handoff.md`（v3 交接）、`c-abi-migration-handoff-v4.md`（v4 重做计划）
- `facade-migration-roadmap.md`（批次记录）
- `r5-app-migration-guide.md`、`r5-phase2-detailed-guide.md`（R5 app 侧迁移指引）

## 其他参考

- 构建：`docs/zh/build.md`、`docs/zh/build_macos-zh.md`
- 工程文件：`docs/zh/project-file-reference.md`
- 代码风格与 Google Test 要求：`CONTRIBUTING.md`（仓库根）
