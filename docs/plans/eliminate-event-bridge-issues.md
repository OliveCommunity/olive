# Eliminating EventBridge: Good First Issues

Goal: the app stops receiving engine events via `EngineEventBridge` /
`oakengine_event_subscribe`; the engine becomes a pure library (its internal
Qt signals are for its own use only).

Each issue is independent and can be claimed and shipped separately (do the
two infrastructure ones first — they simplify everything else).

---

## Ground rules

- **Run the tests before submitting, every time**:
  ```bash
  cmake --build cmake-build-debug -j8 && cd cmake-build-debug && ctest -j4
  ```
  Zero build errors and 122/122 tests passing are the definition of done,
  plus the manual acceptance checks listed in each issue.
- Only three migration patterns, copy them:
  - **A. app-internal Qt signal**: the event is app-initiated (playhead,
    edits, undo) — emit an app-internal signal at the origin and re-point
    subscribers to it.
  - **B. keep async**: truly async events (tasks, caches, audio beats) go
    through one dispatcher (issue 0b) that queues everything back to the
    GUI thread.
  - **C. delete**: the call site already knows — refresh inline and drop
    the subscription.
- When done: remove the matching `bridge_->subscribe` /
  `oakengine_event_subscribe` calls and check off the item here.
- Architecture background: `docs/zh/plans/eliminate-event-bridge.md`

---

## Infrastructure (do first)

### issue 0a — Fix the audio event ID collision (real bug, half a day)
`engine/include/oakengine/events.h:210`: `AUDIO_MANAGER_OUTPUT_NOTIFY = 141`
collides with `PLAYBACK_CACHE_INVALIDATED = 141` at :213; the case 141 in
events.cpp only performs the PlaybackCache conversion,
`AudioManager::output_notify` is never wired up, and the audio keep-alive
subscription in `viewer.cpp:1406` never actually fires.

In other words: `AUDIO_MANAGER_OUTPUT_NOTIFY` shares ID 141 with
`PLAYBACK_CACHE_INVALIDATED`; the audio notification is never wired up, so
the viewer's audio keep-alive subscription never fires.
- Change: assign OUTPUT_NOTIFY a fresh ID (>=144) and wire up AudioManager
  in events.cpp.
- Acceptance: audio keeps playing past 5 seconds on long footage without
  cutting out.
- **Required: build + ctest all green (see ground rules).**

### issue 0b — Single async event dispatcher (1 day)
Create `app/asyncengineevents.{h,cpp}` (a QObject held as a Core singleton)
subscribing only to the TASK family (120–127),
PLAYBACK_CACHE_VALIDATED/INVALIDATED, FRAME_CACHE_INVALIDATED, and
AUDIO_OUTPUT_PARAMS/NOTIFY. C callbacks just do
`QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` and then emit
typed Qt signals.

In other words: one dispatcher subscribing only to the truly async events;
C callbacks just queue to the GUI thread and re-emit typed Qt signals.
- Acceptance: transcode once — the status bar progress still works. All
  later (b) migrations must use it instead of their own subscriptions.
- **Required: build + ctest all green.**

### issue 0c — App-internal PlaybackController (1 day)
Create `app/playback/playbackcontroller.{h,cpp}` with a
`playhead_changed(oak::Node viewer, Rational)` signal. Route every
`oakengine_viewer_set_playhead` call site in the app (ViewerWidget playback
loop, timelinewidget, timeruler, multicam, export) through it and emit the
signal there.

In other words: one app-side controller that every
`oakengine_viewer_set_playhead` call site goes through; it re-broadcasts
`playhead_changed` as an app-internal Qt signal.
- Acceptance: all views keep updating during playback/drag/seek (bridge may
  coexist until later issues migrate each subscriber).
- **Required: build + ctest all green.**

---

## Playhead migrations (pattern A, about half a day each)

### issue 1 — timebasedview playhead subscription
`app/widget/timebased/timebasedview.cpp:168` (raw C callback). Reconnect to
`PlaybackController::playhead_changed`.
- Acceptance: the ruler playhead line moves during playback; no raw C
  subscription remains.
- **Required: build + ctest all green.**

### issue 2 — NodeParamViewWidgetBridge playhead
`app/widget/nodeparamview/nodeparamviewwidgetbridge.cpp:1140` (raw C).
- Acceptance: keyframe-interpolated slider values follow the playhead
  during playback.
- **Required: build + ctest all green.**

### issue 3 — NodeParamViewKeyframeControl playhead
`app/widget/nodeparamview/nodeparamviewkeyframecontrol.cpp:222` (raw C).
- Acceptance: prev/next/toggle keyframe buttons have the correct state as
  the playhead moves.
- **Required: build + ctest all green.**

### issue 4 — NodeParamViewConnectedLabel playhead
`app/widget/nodeparamview/nodeparamviewconnectedlabel.cpp:131` (raw C).
- Acceptance: the value tree refreshes with the playhead.
- **Required: build + ctest all green.**

### issue 5 — ExportDialog playhead
`app/dialog/export/export.cpp:311` (raw C).
- Acceptance: the export dialog's in/out times stay in sync with the
  playhead.
- **Required: build + ctest all green.**

### issue 6 — timelinewidget / viewerdisplay playhead family (bridge subscriptions)
`app/widget/timelinewidget/timelinewidget.cpp` (around :741-743) and the
related viewerdisplay subscriptions. Reconnect to PlaybackController.
- Acceptance: timeline timecode/playhead position display correctly.
- **Required: build + ctest all green.**

---

## Undo & modified migrations (pattern A, about half a day each)

### issue 7 — historywidget UNDO_INDEX_CHANGED
`app/widget/history/historywidget.cpp:33,127` (two raw C callbacks).
Emit an app-internal `undo_index_changed(int)` at Core's undo/redo/push
exit points and reconnect historywidget to it.
- Acceptance: after undo/redo the history list resets its model and selects
  the correct row.
- **Required: build + ctest all green.**

### issue 8 — core.cpp PROJECT_MODIFIED_CHANGED
`app/core.cpp:1108` (raw C). The modified flag is driven by the undo stack,
so the app can derive it at push/undo/redo/load; drive `setWindowModified`
from an app-internal signal instead.
- Acceptance: the title bar modified marker is correct after
  edit/undo/save.
- **Required: build + ctest all green.**

---

## Structure migrations (patterns C/A, 0.5–1 day each)

### issue 9 — seekablewidget marker/workarea subscriptions
`app/widget/seekable/seekablewidget.cpp:95-101,146-154`.
Edits go through undo commands: call `viewport()->update()` directly where
the command runs; also refresh after undo (reuse the issue 7 signal).
- Acceptance: the ruler refreshes immediately after adding/removing markers
  or changing the workarea; undo is equally correct.
- **Required: build + ctest all green.**

### issue 10 — resizabletimelinescrollbar marker/workarea
`app/widget/resizablescrollbar/resizabletimelinescrollbar.cpp:83-98,124-129`.
- Acceptance: markers render correctly on the scrollbar.
- **Required: build + ctest all green.**

### issue 11 — nodeviewitem label/color/message/array subscriptions
`app/widget/nodeview/nodeviewitem.cpp:85-119`.
label/color edits refresh in place; array size goes through the structure
command call sites; undo is covered by the issue 7 signal.
- Acceptance: node blocks refresh immediately after rename/recolor/array
  add/remove.
- **Required: build + ctest all green.**

### issue 12 — NodeParamViewItem / arraywidget / keyframecontrol bridge subscriptions
`app/widget/nodeparamview/nodeparamviewitem.cpp:95-296`,
`nodeparamviewarraywidget.cpp:43-45`, and the keyframe family in
keyframecontrol.
- Acceptance: parameter item label/flags/array/keyframe buttons refresh
  correctly with edits and undo.
- **Required: build + ctest all green.**

### issue 13 — nodeparamviewwidgetbridge parameter value subscriptions
`app/widget/nodeparamview/nodeparamviewwidgetbridge.cpp:161-178`.
The slider edit path sets values directly; re-read uniformly after
undo/load.
- Acceptance: slider values are correct after edit, undo, and load.
- **Required: build + ctest all green.**

### issue 14 — NodeParamView group passthrough / context subscriptions
`app/widget/nodeparamview/nodeparamview.cpp:189-205,350-352,871-874`.
- Acceptance: the parameter panel rebuilds correctly when groups open/close
  or nodes are added to/removed from contexts.
- **Required: build + ctest all green.**

### issue 15 — nodeviewcontext node/edge add-remove subscriptions (structure core)
`app/widget/nodeview/nodeviewcontext.cpp:99-138,180-183,427-430`.
Additions/removals come from: app edit commands (handled at call sites),
undo (rebuild covered by the issue 7 signal), and project load (add a new
"project load finished" hook — Core broadcasts it after TaskDialog success,
replacing the batch event-driven graph build).
- Acceptance: node/edge add/remove refreshes immediately; undo restores
  correctly; reopening a project shows the complete view.
- **Required: build + ctest all green.**

### issue 16 — nodeview NODE_REMOVED_FROM_GRAPH
`app/widget/nodeview/nodeview.cpp:91,1911` and `mainwindow.cpp:58,351`.
Handle at the delete-node command site plus the undo signal.
- Acceptance: after deleting a node its item/ViewerPanel closes; undo
  restores it.
- **Required: build + ctest all green.**

### issue 17 — timelinewidget track/block structure subscriptions
`app/widget/timelinewidget/timelinewidget.cpp:665-753,2177-2228`.
block/track add/remove creates/removes items directly at the timeline
command sites; undo and load rebuild uniformly.
- Acceptance: clip/track add/remove refreshes immediately; undo/load is
  correct.
- **Required: build + ctest all green.**

### issue 18 — trackviewitem index/muted
`app/widget/timelinewidget/trackview/trackviewitem.cpp:66,117`.
- Acceptance: track move/mute state refreshes immediately.
- **Required: build + ctest all green.**

### issue 19 — projectviewmodel folder/label subscriptions
`app/panel/project/projectviewmodel.cpp:45-61,505-512`.
Import/create-folder/rename go through the command sites; on load
completion do a uniform model reset (reuse the issue 15 hook).
- Acceptance: the project browser refreshes immediately on add/remove/
  rename.
- **Required: build + ctest all green.**

### issue 20 — misc leftovers
multicamwidget (:107/114 raw C), manageddisplay (OCIO, :143),
audiowaveformview (:57,76), viewerdisplay (subtitles, :100,264),
panel/timebased (label, :162).
- Acceptance: each UI refreshes with edits.
- **Required: build + ctest all green.**

---

## Async migrations (pattern B, about half a day each, depends on 0b)

### issue 21 — mainstatusbar / taskmanager / taskviewitem / task dialog
`app/window/mainwindow/mainstatusbar.cpp:88-146`,
`app/panel/taskmanager/taskmanager.cpp:41-56`,
`app/widget/taskview/taskviewitem.cpp:82-88`,
`app/dialog/task/task.cpp:47-48`. Reconnect all of them to the dispatcher's
task signals.
- Acceptance: transcode/export task progress bars and the task list update
  in real time.
- **Required: build + ctest all green.**

### issue 22 — timeruler cache bar + viewer cache invalidated
`app/widget/timeruler/timeruler.cpp:82-113`,
`app/widget/viewer/viewer.cpp:366-394`.
- Acceptance: the green cache bar grows with background rendering during
  playback; edits invalidate the cache bar correctly.
- **Required: build + ctest all green.**

### issue 23 — viewer audio keep-alive (AUDIO_OUTPUT_NOTIFY)
`app/widget/viewer/viewer.cpp:1406` (raw C, currently broken by the ID
collision). Depends on 0a+0b.
- Acceptance: audio keeps playing continuously on long footage.
- **Required: build + ctest all green.**

---

## Final teardown (do last)

### issue 24 — Delete EngineEventBridge
After every issue above is checked off:
`grep -rn "EngineEventBridge\|bridge_->subscribe" app` should find zero
references. Delete `app/engineeventbridge.{h,cpp}` and its CMake entries.
- Acceptance: build passes; full functional regression
  (playback/node graph/timeline/params/history/tasks).
- **Required: build + ctest all green + complete manual regression.**

### issue 25 — Narrow the engine event surface
`engine/src/capi/events.cpp` keeps only the async events the dispatcher
needs plus UNDO_INDEX_CHANGED / PROJECT_MODIFIED_CHANGED (the minimal
exception reserved for external writers such as a future AI assistant);
the synchronous event constants in `engine/include/oakengine/events.h` are
deleted or moved to an internal header; the engine's internal Qt signals
stay unchanged.
- Acceptance: ctest all green; nm confirms the C ABI no longer exposes the
  synchronous event subscription surface.
- **Required: build + ctest all green.**
