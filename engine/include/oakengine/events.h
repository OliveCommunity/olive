/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAKENGINE_EVENTS_H
#define OAKENGINE_EVENTS_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "project.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file events.h
 * @brief C ABI for engine change notifications (the signal/slot replacement)
 *
 * The engine's C++ API notifies observers through Qt signals (Project::
 * modified_changed, Folder::begin_insert_item, Track::block_added, the
 * sequence's track/marker/workarea notifications, ...). This family exposes
 * the same notifications to C consumers as a subscription/callback
 * mechanism, so the application never needs to connect() to an engine
 * QObject directly.
 *
 * Usage:
 *
 *   int64_t sub = oakengine_event_subscribe(handle, OAKENGINE_EVENT_..., fn,
 *                                           userdata);
 *   ...
 *   oakengine_event_unsubscribe(sub);
 *
 * `handle` is a borrowed facade handle whose static type depends on the
 * event family (see the table below); a mismatch or NULL handle fails with
 * 0 (an invalid subscription id). Subscribing the same (handle, event)
 * twice is allowed and returns two independent subscription ids.
 *
 * Thread semantics: callbacks are invoked SYNCHRONOUSLY on the thread that
 * emits the change (the equivalent of Qt::DirectConnection) before the
 * engine's own emission returns, exactly like the C++ connections they
 * replace. The callback runs under whatever locks the engine holds at the
 * emission site; it must not call back into editing primitives that mutate
 * the same object. All engine objects live on the GUI thread, so callbacks
 * normally fire there.
 *
 * Lifetime: the registry drops the subscription automatically when the
 * observed engine object is destroyed, so a stale subscription id is never
 * a use-after-free; oakengine_event_unsubscribe() on an id whose object
 * died is a harmless no-op returning OAKENGINE_E_NOT_FOUND. The inverse is
 * NOT tracked: `userdata` ownership stays with the subscriber, which must
 * unsubscribe (or tolerate callbacks) until its own teardown.
 *
 * Event payloads use POD fields only. Timestamps are frame numbers in the
 * owning sequence's frame-rate timebase (same convention as timeline.h);
 * `handle`/`source` are borrowed pointers the callee may use during the
 * callback only.
 */

/**
 * @brief Event ids for oakengine_event_subscribe().
 *
 * handle column: the facade handle to pass for that event.
 * payload column: oakengine_event field contents on delivery.
 */
#define OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED 1 /**< handle: OakEngineProject*. a = new modified flag (0/1). */
#define OAKENGINE_EVENT_PROJECT_NAME_CHANGED 2 /**< handle: OakEngineProject*. no payload. */

#define OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM 10 /**< handle: OakEngineNode* (a folder). handle field = child OakEngineNode*, a = insertion index. */
#define OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM 11 /**< handle: OakEngineNode* (a folder). no payload. */
#define OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM 12 /**< handle: OakEngineNode* (a folder). handle field = child OakEngineNode*, a = child index. */
#define OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM 13 /**< handle: OakEngineNode* (a folder). no payload. */

#define OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED 20 /**< handle: OakEngineSequence*. handle field = OakEngineTrack*, a = track type (OAKENGINE_TRACK_TYPE_*). */
#define OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED 21 /**< handle: OakEngineSequence*. handle field = OakEngineTrack*, a = track type. */
#define OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED 22 /**< handle: OakEngineSequence*. a = track type. Fired on TrackList::track_list_changed (order/label-affecting changes). */
#define OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED 23 /**< handle: OakEngineSequence*. handle field = OakEngineTrack*, a = track type, b = new height in PIXELS (TrackList::track_height_changed). */
#define OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED 24 /**< handle: OakEngineSequence*. a/b = changed range in/out (ts). */

#define OAKENGINE_EVENT_TRACK_BLOCK_ADDED 30 /**< handle: OakEngineTrack*. handle field = OakEngineBlock*, a = block in-point (ts), b = block out-point (ts). */
#define OAKENGINE_EVENT_TRACK_BLOCK_REMOVED 31 /**< handle: OakEngineTrack*. handle field = OakEngineBlock*, a = in (ts), b = out (ts) at removal time. */
#define OAKENGINE_EVENT_TRACK_INDEX_CHANGED 32 /**< handle: OakEngineTrack*. a = old index, b = new index. */
#define OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED 33 /**< handle: OakEngineTrack*. a = int64 bit-cast of the new height (double, internal units; memcpy to decode). */
#define OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED 34 /**< handle: OakEngineTrack*. no payload (Track::blocks_refreshed). */
#define OAKENGINE_EVENT_TRACK_MUTED_CHANGED 35 /**< handle: OakEngineTrack*. a = muted 0/1. */

#define OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED 36 /**< handle: OakEngineBlock*. no payload (Block::enabled_changed; re-read via oakengine_block_is_enabled). */
#define OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED 37 /**< handle: OakEngineBlock*. no payload (Block::preview_changed). */

#define OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED 40 /**< handle: OakEngineSequence*. a = marker in-point (ts). */
#define OAKENGINE_EVENT_SEQUENCE_MARKER_REMOVED 41 /**< handle: OakEngineSequence*. a = marker in-point (ts). */
#define OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED 42 /**< handle: OakEngineSequence*. a = marker in-point (ts). */

#define OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED 50 /**< handle: OakEngineSequence*. a = in (ts), b = out (ts). */
#define OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED 51 /**< handle: OakEngineSequence*. a = enabled flag (0/1). */

#define OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED 60 /**< handle: OakEngineColorManager*. no payload. Fired when the OCIO config changes (ColorManager::config_changed). */
#define OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED 61 /**< handle: OakEngineColorManager*. no payload (ColorManager::reference_space_changed). */

/* Node family (handle: OakEngineNode*). `s` carries the input id where
 * noted; frame timestamps use the same timebase as oakengine_node_frame_
 * time_base() (the project's first sequence's frame rate). */
#define OAKENGINE_EVENT_NODE_LABEL_CHANGED 70 /**< s = new label. */
#define OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED 71 /**< s = input id, a = element, b = range in (ts), c = range out (ts). */
#define OAKENGINE_EVENT_NODE_INPUT_CONNECTED 72 /**< handle = connected output OakEngineNode*, s = input id, a = element. */
#define OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED 73 /**< handle = former output OakEngineNode*, s = input id, a = element. */
#define OAKENGINE_EVENT_NODE_INPUT_FLAGS_CHANGED 74 /**< s = input id, a = new flags (OAKENGINE_NODE_INPUT_FLAG_*). */
#define OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED 75 /**< s = input id (property key/value intentionally omitted; re-read through the node family getters). */
#define OAKENGINE_EVENT_NODE_INPUT_DATA_TYPE_CHANGED 76 /**< s = input id, a = new oak_node_value_type. */
#define OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED 77 /**< s = input id, a = old size, b = new size. */
#define OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED 78 /**< s = input id, a = element, b = enabled (0/1). */
#define OAKENGINE_EVENT_NODE_KEYFRAME_ADDED 79 /**< handle = OakEngineKeyframe*, s = input id, a = element, b = track. */
#define OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED 80 /**< handle = OakEngineKeyframe* (about to die; use the s/a/b fields, do not dereference), s = input id, a = element, b = track. */
#define OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED 81 /**< handle = OakEngineKeyframe*. */
#define OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED 82 /**< handle = OakEngineKeyframe*. */
#define OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED 83 /**< handle = OakEngineKeyframe*. */
#define OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT 84 /**< handle = OakEngineNode* added to this context. */
#define OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT 85 /**< handle = OakEngineNode* removed from this context. */
#define OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED 86 /**< no payload. */

/* Group family (handle: OakEngineNode*, must be a group). For 87/88 the
 * handle field carries the passthrough's inner node, `s` its input id and
 * `a` its element. */
#define OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED 87 /**< handle = inner OakEngineNode*, s = input id, a = element. */
#define OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED 88 /**< handle = inner OakEngineNode*, s = input id, a = element. */
#define OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED 89 /**< handle = new output OakEngineNode*. */

/**
 * handle = OakEngineNode* whose position in this context changed; `a`/`b`
 * carry the new x/y scene coordinates as int64 bit-casts of double (use
 * memcpy to decode). */
#define OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED 90

#define OAKENGINE_EVENT_NODE_LINKS_CHANGED 91 /**< no payload (Node::links_changed). */
#define OAKENGINE_EVENT_NODE_COLOR_CHANGED 92 /**< no payload (Node::color_changed). */
#define OAKENGINE_EVENT_NODE_INPUT_ADDED 93 /**< s = input id (Node::input_added). */
#define OAKENGINE_EVENT_NODE_INPUT_REMOVED 94 /**< s = input id (Node::input_removed). */
#define OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH 95 /**< handle = project OakEngineNode* (Node::removed_from_graph). */

/* Viewer family (handle: OakEngineNode*, must be a viewer -- validate with
 * oakengine_viewer_from_node()). Rational payloads (seconds) are carried
 * as a = numerator, b = denominator. */
#define OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED 100 /**< a/b = new length. */
#define OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED 101 /**< a/b = new playhead. */
#define OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED 102 /**< a/b = new frame rate (NOT flipped). */
#define OAKENGINE_EVENT_VIEWER_SIZE_CHANGED 103 /**< a = width, b = height. */
#define OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED 104 /**< a/b = new pixel aspect. */
#define OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED 105 /**< a = olive::VideoParams::Interlacing. */
#define OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED 106 /**< no payload. */
#define OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED 107 /**< no payload. */
#define OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED 108 /**< no payload. */
#define OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED 109 /**< a = new sample rate. */
#define OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED 110 /**< no payload. */

/* Marker list family (handle: OakEngineMarkerList*, from
 * oakengine_viewer_get_marker_list()). handle field = the OakEngineMarker*
 * (for REMOVED it is about to die; do not dereference). */
#define OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED 111
#define OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED 112
#define OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED 113

/* Workarea family (handle: OakEngineWorkarea*, borrowed from
 * oakengine_viewer_get_workarea_handle() or owned from
 * oakengine_workarea_create()). */
#define OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED 114 /**< no payload; re-read via oakengine_workarea_get(). */
#define OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED 115 /**< a = enabled 0/1. */

/* Task manager family (handle: oakengine_task_manager_handle(), see
 * oakengine/task.h). The handle field carries the OakEngineTask* (for
 * REMOVED it is about to die; do not dereference). */
#define OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED 120 /**< handle = OakEngineTask*, s = task title. */
#define OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED 121 /**< handle = OakEngineTask* (about to die; do not dereference). */
#define OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED 122 /**< handle = OakEngineTask*. */
#define OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED 123 /**< no payload. */

/* Task family (handle: OakEngineTask*, see oakengine/task.h). Delivered
 * synchronously on the thread the task runs on. */
#define OAKENGINE_EVENT_TASK_STARTED 125 /**< a = start time (msecs since epoch). */
#define OAKENGINE_EVENT_TASK_PROGRESS 126 /**< a = int64 bit-cast of the progress double 0..1 (memcpy to decode). */
#define OAKENGINE_EVENT_TASK_FINISHED 127 /**< a = succeeded 0/1. */

/* Undo stack family (handle: oakengine_undo_handle(), see
 * oakengine/undo.h). Fires after every stack mutation (push/undo/redo/
 * jump/clear); re-read the command list through the oakengine_undo_*
 * accessors. */
#define OAKENGINE_EVENT_UNDO_INDEX_CHANGED 130 /**< a = new index (done-command count). */

/* AudioManager family (handle: oakengine_audio_manager_handle(), see
 * oakengine/audio.h). Fired when the output device or format changes. */
#define OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED 140 /**< no payload. */
#define OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY 144 /**< no payload; emitted after each notify interval of audio has been consumed. */

/* ---- Playback cache / frame cache (B9c) ----------------------------------- */
#define OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED 141
#define OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED 142
#define OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED 143

/**
 * @brief POD event payload delivered to oakengine_event_fn.
 */
typedef struct oakengine_event {
	int32_t id; /**< Event id (OAKENGINE_EVENT_*). */
	int32_t reserved; /**< Alignment padding; 0. */
	int64_t a; /**< Event-specific integer payload (see the event table). */
	int64_t b; /**< Event-specific second integer payload. */
	int64_t c; /**< Event-specific third integer payload. */
	void *source; /**< The subscribed handle the event was delivered for (borrowed). */
	void *handle; /**< Related object, event-specific (borrowed; NULL when none). */
	const char *s; /**< Event-specific string payload (valid only during the callback; NULL when none). */
} oakengine_event;

/**
 * @brief Change-notification callback. Invoked synchronously on the
 * emitting thread; `event` is valid only for the duration of the call.
 */
typedef void (*oakengine_event_fn)(const oakengine_event *event,
								   void *userdata);

/**
 * @brief Subscribe to `event_id` on `handle` (an OakEngineProject*,
 * OakEngineSequence*, OakEngineTrack* or OakEngineNode* per the event
 * table) and return a subscription id (> 0).
 *
 * Returns 0 on failure: NULL handle/callback, unknown event id, or a
 * handle whose engine object does not match the event's family. The
 * callback starts firing with the next matching change; there is no
 * replay of past state.
 */
OAKENGINE_API int64_t oakengine_event_subscribe(void *handle, int32_t event_id,
												oakengine_event_fn fn,
												void *userdata);

/**
 * @brief Cancel a subscription. OAKENGINE_OK on success,
 * OAKENGINE_E_INVALID for `id` <= 0, OAKENGINE_E_NOT_FOUND for an id that
 * was never registered or whose engine object has since been destroyed.
 */
OAKENGINE_API int oakengine_event_unsubscribe(int64_t id);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_EVENTS_H */
