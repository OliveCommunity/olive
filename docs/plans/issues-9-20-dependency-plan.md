# Issues 9–20 (#34–#45): Dependency Analysis & Two-Person Split

Scope: the twelve "Structure migrations" sub-issues of #26 (Eliminating
EventBridge), GitHub issues #34–#45. Each is independently shippable; this
document captures the real ordering constraints so two people can work in
parallel without stepping on each other.

## Hard dependencies

Only two kinds exist:

1. **The issue 7 signal (`Core::undo_index_changed`)** — required by issues
   9, 11, 15, 16 (and used by 10/12/13/17 for undo refresh).
   ✅ Already landed in `a030f2da2` (issue 7 / #55). **No longer blocks
   anything.**

2. **The issue 15 "project load finished" hook** (`Core` broadcasts after
   TaskDialog load success). Explicitly reused by:
   - **issue 13 (#38)** — "re-read uniformly after undo/**load**"
   - **issue 17 (#42)** — "undo and **load** rebuild uniformly"
   - **issue 19 (#44)** — "on load completion do a uniform model reset
     (**reuse the issue 15 hook**)"

Everything else has **no ordering constraints**.

## Dependency graph

```
issue 7 (done) ──┬─> 9, 10, 11, 12, 16        (undo refresh signal, available now)
                 └─> 15 ──┬─> 13              (project-load hook)
                          ├─> 17
                          └─> 19
14, 18, 20 — fully independent
```

## File-overlap (merge-conflict) risks

| Pair | Overlap | Severity |
|------|---------|----------|
| 15 / 16 | both in `app/widget/nodeview/` (+ `mainwindow.cpp` in 16) | low — different files |
| 17 / 18 | both in `app/widget/timelinewidget/` (17 heavily edits `timelinewidget.cpp`) | medium — keep in the same person's queue |
| 12 / 13 / 14 | all under `app/widget/nodeparamview/` | low — different files |
| 9 / 10 | marker/workarea pattern is identical; not the same files | none, but cheap to do together |

## Suggested split

**Person A — "signals already available" batch (start immediately):**

- issue 9 (#34) — seekablewidget marker/workarea
- issue 10 (#35) — resizabletimelinescrollbar marker/workarea (same pattern as 9)
- issue 11 (#36) — nodeviewitem label/color/message/array
- issue 12 (#37) — NodeParamViewItem / arraywidget / keyframecontrol
- issue 14 (#39) — NodeParamView group passthrough / context
- issue 20 (#45) — misc leftovers

**Person B — "project-load hook" chain (15 first, then its dependents):**

- issue 15 (#40) — nodeviewcontext structure core + **add the load hook**
- issue 16 (#41) — nodeview NODE_REMOVED_FROM_GRAPH (same area as 15)
- issue 13 (#38) — nodeparamviewwidgetbridge values (needs the hook)
- issue 17 (#42) — timelinewidget track/block structure (needs the hook; keeps 18's neighbor in one queue)
- issue 18 (#43) — trackviewitem index/muted
- issue 19 (#44) — projectviewmodel folder/label (needs the hook)

Rationale: B owns everything that consumes the issue 15 hook, so the hook's
API is designed and used by one person with no cross-team blocking. A's
batch only needs the already-landed issue 7 signal, so both can start today.
Workload is ~6 × 0.5 day each side.

Ground rules still apply per issue: `cmake --build cmake-build-debug -j8 &&
cd cmake-build-debug && ctest -j4` must be green (122/122) before shipping,
and remove the matching `bridge_->subscribe` / `oakengine_event_subscribe`
calls when done.
