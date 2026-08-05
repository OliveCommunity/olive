# Issues 9–20（#34–#45）：依赖关系梳理与两人分工建议

范围：#26（Eliminating EventBridge）的 12 个 "Structure migrations"
子任务，对应 GitHub issue #34–#45。每个 issue 都可以独立交付，本文档
梳理真实的先后约束，让两个人可以并行推进而互不阻塞。

## 硬依赖

只有两类：

1. **issue 7 的信号（`Core::undo_index_changed`）** —— issue 9、11、
   15、16 明确依赖它（10/12/13/17 的 undo 刷新也要用）。
   ✅ 已在 `a030f2da2` 落地（issue 7 / #55）。**不再构成阻塞。**

2. **issue 15 新增的 "project load finished" 钩子**（Core 在 TaskDialog
   加载成功后广播）。明确复用它的有：
   - **issue 13（#38）** —— "re-read uniformly after undo/**load**"
   - **issue 17（#42）** —— "undo and **load** rebuild uniformly"
   - **issue 19（#44）** —— "on load completion do a uniform model
     reset（**复用 issue 15 的钩子**）"

其余 issue **没有任何先后约束**。

## 依赖图

```
issue 7（已完成） ──┬─> 9, 10, 11, 12, 16   （undo 刷新信号，现已可用）
                   └─> 15 ──┬─> 13          （项目加载钩子）
                            ├─> 17
                            └─> 19
14、18、20 —— 完全独立
```

## 文件重叠（合并冲突）风险

| 组合 | 重叠区域 | 程度 |
|------|---------|------|
| 15 / 16 | 都在 `app/widget/nodeview/`（16 还涉及 `mainwindow.cpp`） | 低——不同文件 |
| 17 / 18 | 都在 `app/widget/timelinewidget/`（17 会大改 `timelinewidget.cpp`） | 中——建议排在同一个人的队列里 |
| 12 / 13 / 14 | 都在 `app/widget/nodeparamview/` 下 | 低——不同文件 |
| 9 / 10 | marker/workarea 的处理模式完全相同，但不是同一批文件 | 无冲突，但连着做成本低 |

## 建议分工

**A 同学 —— "信号已就绪" 批次（今天就能开始）：**

- issue 9（#34）—— seekablewidget marker/workarea
- issue 10（#35）—— resizabletimelinescrollbar marker/workarea（与 9 同模式）
- issue 11（#36）—— nodeviewitem label/color/message/array
- issue 12（#37）—— NodeParamViewItem / arraywidget / keyframecontrol
- issue 14（#39）—— NodeParamView group passthrough / context
- issue 20（#45）—— 零散收尾

**B 同学 —— "项目加载钩子" 链（先做 15，再做它的下游）：**

- issue 15（#40）—— nodeviewcontext 结构核心 + **新增加载钩子**
- issue 16（#41）—— nodeview NODE_REMOVED_FROM_GRAPH（与 15 同区域）
- issue 13（#38）—— nodeparamviewwidgetbridge 参数值（依赖钩子）
- issue 17（#42）—— timelinewidget track/block 结构（依赖钩子；同时把 18 的邻域保持在同一队列）
- issue 18（#43）—— trackviewitem index/muted
- issue 19（#44）—— projectviewmodel folder/label（依赖钩子）

理由：所有消费 issue 15 钩子的 issue 都归 B，钩子的 API 由同一个人
设计并使用，零跨人阻塞；A 的批次只依赖已经落地的 issue 7 信号，
两人今天都能开工。工作量两侧均约为 6 × 0.5 天。

每个 issue 的通用要求不变：提交前
`cmake --build cmake-build-debug -j8 && cd cmake-build-debug && ctest -j4`
必须全绿（122/122），完成后移除对应的
`bridge_->subscribe` / `oakengine_event_subscribe` 调用。
