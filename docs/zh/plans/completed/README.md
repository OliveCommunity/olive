# 已完成战役归档（completed）

本目录归档 **C ABI 迁移战役（B1-R7）** 的全套文档。战役已结束：

- 成果：oak-editor / oak-render-worker / oak-cli 的 `U _ZN5olive` 从
  557 降为 **0**；liboakengine.so 导出 C++ 符号从 3486 收至 19
  （oakgl/oakvulkan dlopen 插件 ABI）；ctest 45/45。
- 合并：压合为 6 个主题提交合入 main（`6ea653e6d`）。

## 文档索引

- **战役记录**：`facade-migration-roadmap.md`（各批次完成记录）
- **交接文档**（执行过程的快照，记录历任执行者状态）：
  `c-abi-migration-handoff.md`（v3）、`-v4`、`-v5`、`-v6`
- **R5**（app 侧符号消除 557→58）：
  `r5-app-migration-guide.md`、`r5-phase2-detailed-guide.md`、
  `r5-phase3-final-guide.md`、`r5-final-sprint.md`
- **R6**（豁免清单清零 58→0）：`r6-cleanup-plan.md`
- **R7**（display.h POD 化 + visibility 收口）：
  `r7-pure-abi-plan.md`

这些文档**只作历史参考**，其中的"当前状态/进行中"描述均已是
过去时。后续工作在上一层：`riir/`（模块拆分）、
`gtest-migration-guide.md`、`ui-redesign-plan.md`。
