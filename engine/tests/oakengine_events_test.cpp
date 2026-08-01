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

// Pure C ABI test for the liboakengine event subscription family
// (oakengine/events.h) and the track block traversal family
// (oakengine_track_nearest_block_* / oakengine_block_*). Every subscription
// is exercised by provoking a real engine change through the facade and
// asserting the callback fired with the documented payload. No GL required.

#include <assert.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "oakengine/events.h"
#include "oakengine/footage.h"
#include "oakengine/init.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"

#ifndef OAK_TEST_SOURCE_DIR
#define OAK_TEST_SOURCE_DIR "."
#endif

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_events_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_events_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
}

// Callback recorder: counts deliveries per event id and keeps the last
// payload of each.
#define MAX_TRACKED_EVENT 128

typedef struct {
	int count[MAX_TRACKED_EVENT];
	int64_t last_a[MAX_TRACKED_EVENT];
	int64_t last_b[MAX_TRACKED_EVENT];
	int64_t last_c[MAX_TRACKED_EVENT];
	void *last_source[MAX_TRACKED_EVENT];
	void *last_handle[MAX_TRACKED_EVENT];
	char last_s[MAX_TRACKED_EVENT][256];
} EventLog;

static void record_event(const oakengine_event *event, void *userdata)
{
	EventLog *log = (EventLog *)userdata;
	EXPECT_TRUE(event != NULL);
	EXPECT_TRUE(event->id > 0 && event->id < MAX_TRACKED_EVENT);
	log->count[event->id]++;
	log->last_a[event->id] = event->a;
	log->last_b[event->id] = event->b;
	log->last_c[event->id] = event->c;
	log->last_source[event->id] = event->source;
	log->last_handle[event->id] = event->handle;
	snprintf(log->last_s[event->id], sizeof(log->last_s[event->id]), "%s",
			 event->s ? event->s : "");
}

static void reset_event(EventLog *log, int id)
{
	log->count[id] = 0;
}

// ---- Subscription validation ----------------------------------------------

static void test_subscribe_validation(OakEngineProject *project,
									  OakEngineSequence *seq,
									  OakEngineTrack *track)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// NULL handle / NULL callback / unknown event id.
	EXPECT_TRUE(oakengine_event_subscribe(
			   NULL, OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED, record_event,
			   &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(project,
									 OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED,
									 NULL, &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(project, 999, record_event, &log) == 0);

	// Handle/event family mismatches.
	EXPECT_TRUE(oakengine_event_subscribe(
			   seq, OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED, record_event,
			   &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(project,
									 OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED,
									 record_event, &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(track,
									 OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM,
									 record_event, &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(project,
									 OAKENGINE_EVENT_TRACK_BLOCK_ADDED,
									 record_event, &log) == 0);

	// Bad unsubscribe arguments.
	EXPECT_TRUE(oakengine_event_unsubscribe(0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_event_unsubscribe(-5) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_event_unsubscribe(424242) == OAKENGINE_E_NOT_FOUND);
}

// ---- Project events ---------------------------------------------------------

static void test_project_events(OakEngineProject *project)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// Normalize to unmodified first: modified_changed only fires on an
	// actual flip, and the setup above already dirtied the project.
	oakengine_project_set_modified(project, 0);

	int64_t sub = oakengine_event_subscribe(
		project, OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED, record_event, &log);
	EXPECT_TRUE(sub > 0);

	oakengine_project_set_modified(project, 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] == 1);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] ==
		   (void *)project);

	oakengine_project_set_modified(project, 0);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] == 2);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] == 0);

	// After unsubscribing no further events arrive.
	EXPECT_TRUE(oakengine_event_unsubscribe(sub) == OAKENGINE_OK);
	oakengine_project_set_modified(project, 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED] == 2);

	// Unsubscribing twice is a not-found no-op.
	EXPECT_TRUE(oakengine_event_unsubscribe(sub) == OAKENGINE_E_NOT_FOUND);
}

// ---- Folder events ----------------------------------------------------------

static void test_folder_events(OakEngineProject *project)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// A fresh project's first node is its root folder (same fixture as
	// oakengine_footage_test).
	OakEngineNode *root = oakengine_project_node_at(project, 0);
	EXPECT_TRUE(root != NULL);

	int64_t sub_begin = oakengine_event_subscribe(
		root, OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM, record_event, &log);
	int64_t sub_end = oakengine_event_subscribe(
		root, OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM, record_event, &log);
	int64_t sub_rm_begin = oakengine_event_subscribe(
		root, OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM, record_event, &log);
	int64_t sub_rm_end = oakengine_event_subscribe(
		root, OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM, record_event, &log);
	EXPECT_TRUE(sub_begin > 0 && sub_end > 0 && sub_rm_begin > 0 &&
		   sub_rm_end > 0);

	OakEngineNode *folder = oakengine_folder_create(project, root, "Sub");
	EXPECT_TRUE(folder != NULL);

	EXPECT_TRUE(log.count[OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM] ==
		   (void *)folder);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM] >= 0);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM] == 1);

	// Undoing the folder creation removes it from the root again.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM] ==
		   (void *)folder);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM] == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_begin) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_end) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_rm_begin) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_rm_end) == OAKENGINE_OK);
}

// ---- Sequence / track events -------------------------------------------------

static void test_sequence_events(OakEngineProject *project,
								 OakEngineSequence *seq,
								 const char *media_path)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// Track added: subscribe, then append an audio track.
	int64_t sub_track = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED, record_event, &log);
	EXPECT_TRUE(sub_track > 0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   0);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED] ==
		   OAKENGINE_TRACK_TYPE_AUDIO);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED] != NULL);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED] ==
		   (void *)seq);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_track) == OAKENGINE_OK);

	// Block added on the video track (track 0 was created by the caller).
	OakEngineTrack *track = oakengine_sequence_track_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0);
	EXPECT_TRUE(track != NULL);
	int64_t sub_block = oakengine_event_subscribe(
		track, OAKENGINE_EVENT_TRACK_BLOCK_ADDED, record_event, &log);
	EXPECT_TRUE(sub_block > 0);

	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 40, 5);
	EXPECT_TRUE(clip != NULL);

	// Placing at in=10 on an empty track inserts a leading gap first, so the
	// event fires twice (gap 0..10, then the clip 10..40); the last
	// delivery is the clip itself.
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_BLOCK_ADDED] == 2);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_TRACK_BLOCK_ADDED] ==
		   (void *)clip);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_TRACK_BLOCK_ADDED] == 10);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_TRACK_BLOCK_ADDED] == 40);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_TRACK_BLOCK_ADDED] ==
		   (void *)track);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_block) == OAKENGINE_OK);
	oakengine_footage_free(footage);

	// Marker added / modified.
	int64_t sub_marker_add = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED, record_event, &log);
	int64_t sub_marker_mod = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED, record_event, &log);
	EXPECT_TRUE(sub_marker_add > 0 && sub_marker_mod > 0);

	EXPECT_TRUE(oakengine_sequence_marker_add(seq, 7, "Mark") == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED] == 7);

	EXPECT_TRUE(oakengine_sequence_marker_rename(seq, 7, "Renamed") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED] == 7);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_marker_add) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_marker_mod) == OAKENGINE_OK);

	// Workarea enabled + range changed.
	int64_t sub_range = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED, record_event,
		&log);
	int64_t sub_enabled = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED, record_event,
		&log);
	EXPECT_TRUE(sub_range > 0 && sub_enabled > 0);

	EXPECT_TRUE(oakengine_sequence_set_workarea(seq, 1, 3, 21) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED] ==
		   1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED] ==
		   1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED] == 3);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED] ==
		   21);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_range) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_enabled) == OAKENGINE_OK);
}

// ---- Track block traversal ---------------------------------------------------

static void test_block_traversal(OakEngineSequence *seq)
{
	OakEngineTrack *track = oakengine_sequence_track_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0);
	EXPECT_TRUE(track != NULL);

	// The caller placed one clip at 10..40; the track chain is
	// gap(0..10) -> clip(10..40).
	EXPECT_TRUE(oakengine_track_block_count(track) == 2);

	OakEngineBlock *gap =
		oakengine_track_nearest_block_before_or_at(track, 0);
	EXPECT_TRUE(gap != NULL);
	EXPECT_TRUE(oakengine_block_is_gap(gap) == 1);

	OakEngineBlock *clip = oakengine_block_next(gap);
	EXPECT_TRUE(clip != NULL);
	EXPECT_TRUE(oakengine_block_is_gap(clip) == 0);
	EXPECT_TRUE(oakengine_block_next(clip) == NULL);
	EXPECT_TRUE(oakengine_block_prev(clip) == gap);
	EXPECT_TRUE(oakengine_block_prev(gap) == NULL);

	int64_t in = -1, out = -1;
	EXPECT_TRUE(oakengine_block_get_range(gap, &in, &out) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 10);
	EXPECT_TRUE(oakengine_block_get_range(clip, &in, &out) == OAKENGINE_OK);
	EXPECT_TRUE(in == 10 && out == 40);

	// Time queries.
	EXPECT_TRUE(oakengine_track_block_at_time(track, 15) == clip);
	EXPECT_TRUE(oakengine_track_block_at_time(track, 5) == gap);
	EXPECT_TRUE(oakengine_track_block_at_time(track, 40) == NULL);
	EXPECT_TRUE(oakengine_track_nearest_block_before(track, 10) == gap);
	EXPECT_TRUE(oakengine_track_nearest_block_before_or_at(track, 10) == clip);
	EXPECT_TRUE(oakengine_track_nearest_block_after(track, 0) == clip);
	EXPECT_TRUE(oakengine_track_nearest_block_after_or_at(track, 10) == clip);
	EXPECT_TRUE(oakengine_track_nearest_block_after(track, 10) == NULL);

	// NULL safety.
	EXPECT_TRUE(oakengine_track_block_count(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_track_block_at_time(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_track_nearest_block_before(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_track_nearest_block_before_or_at(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_track_nearest_block_after(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_track_nearest_block_after_or_at(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_block_next(NULL) == NULL);
	EXPECT_TRUE(oakengine_block_prev(NULL) == NULL);
	EXPECT_TRUE(oakengine_block_is_gap(NULL) == 0);
	EXPECT_TRUE(oakengine_block_get_range(NULL, &in, &out) ==
		   OAKENGINE_E_INVALID);
}

// ---- Node events (B8a) ------------------------------------------------------

static void test_node_events(OakEngineProject *project)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	OakEngineNode *text = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	EXPECT_TRUE(solid != NULL && lut != NULL && text != NULL);

	// Family mismatch: node events need a node handle.
	EXPECT_TRUE(oakengine_event_subscribe(
			   project, OAKENGINE_EVENT_NODE_LABEL_CHANGED, record_event,
			   &log) == 0);

	int64_t subs[16];
	int nsubs = 0;
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_LABEL_CHANGED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		lut, OAKENGINE_EVENT_NODE_INPUT_CONNECTED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		lut, OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		text, OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_ADDED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED, record_event, &log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		solid, OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED, record_event,
		&log);
	for (int i = 0; i < nsubs; i++) {
		EXPECT_TRUE(subs[i] > 0);
	}

	// Label.
	EXPECT_TRUE(oakengine_node_set_label(solid, "EventSolid") == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_LABEL_CHANGED] == 1);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_NODE_LABEL_CHANGED],
				  "EventSolid") == 0);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_NODE_LABEL_CHANGED] == solid);

	// Value change on an input: element -1, a valid range, the input id.
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_COLOR;
	v.f[0] = 0.5;
	v.f[3] = 1.0;
	EXPECT_TRUE(oakengine_node_set_input(solid, "color_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED] >= 1);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED],
				  "color_in") == 0);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED] == -1);

	// Edge connect/disconnect: output node in the handle field.
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_CONNECTED] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_NODE_INPUT_CONNECTED] == solid);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_NODE_INPUT_CONNECTED],
				  "tex_in") == 0);
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED] == 1);

	// Property change (notified write only).
	EXPECT_TRUE(oakengine_node_set_input_property_string(solid, "color_in",
													"my_prop", "1", 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED] == 1);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED],
				  "color_in") == 0);
	EXPECT_TRUE(oakengine_node_set_input_property_string(solid, "color_in",
													"my_prop", "2", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED] == 1);

	// Array size change: old and new sizes.
	const int arr_before = oakengine_node_input_array_size(text, "args_in");
	EXPECT_TRUE(oakengine_node_array_insert_at(text, "args_in", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED] ==
		   arr_before);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED] ==
		   arr_before + 1);

	// Keyframe enable + add/remove/time/type/value.
	reset_event(&log, OAKENGINE_EVENT_NODE_KEYFRAME_ADDED);
	EXPECT_TRUE(oakengine_node_set_input_keyframing(solid, "color_in", -1, 1, 0,
											   1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED] == 1);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED] == 1);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED],
				  "color_in") == 0);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_ADDED] >= 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_NODE_KEYFRAME_ADDED] != NULL);
	// COLOR has four tracks; enabling keyframing adds one key per track.
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_NODE_KEYFRAME_ADDED] == 3);

	// Type change on the created key (ts 0 = time 0s).
	reset_event(&log, OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED);
	int64_t times[1] = { 0 };
	int tracks[1] = { 0 };
	// The engine only emits the type-changed signal on multi-key tracks,
	// so add a second key (1s = ts 30 with the default timebase) first.
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(solid, "color_in", -1, 1,
												   1, 1, NULL) ==
		   OAKENGINE_OK);
	// Default type is bezier; switch to hold (type 2) for a real change.
	EXPECT_TRUE(oakengine_node_keyframes_set_type_many(solid, "color_in", -1,
												  times, tracks, 1, 2) == 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED] == 1);

	// Value change.
	reset_event(&log, OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED);
	oak_node_value nv;
	memset(&nv, 0, sizeof(nv));
	nv.type = OAK_NODE_VALUE_COLOR;
	nv.f[0] = 0.75;
	EXPECT_TRUE(oakengine_node_keyframes_set_value_many(solid, "color_in", -1,
												   times, tracks, 1, &nv,
												   NULL) == 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED] == 1);

	// Time change.
	reset_event(&log, OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED);
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(solid, "color_in", -1,
												  times, tracks, 1,
												  30) == 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED] == 1);

	// Removal.
	reset_event(&log, OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED);
	EXPECT_TRUE(oakengine_node_set_input_keyframing(solid, "color_in", -1, 0, 0,
											   1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED] >= 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED] == 2);

	for (int i = 0; i < nsubs; i++) {
		EXPECT_TRUE(oakengine_event_unsubscribe(subs[i]) == OAKENGINE_OK);
	}

	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, text) == OAKENGINE_OK);
}

// ---- Group + context position events -----------------------------------------

static void test_group_events(OakEngineProject *project)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	OakEngineNode *group = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.group");
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(group != NULL && solid != NULL);

	int64_t subs[4];
	int nsubs = 0;
	subs[nsubs++] = oakengine_event_subscribe(
		group, OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		group, OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		group, OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED, record_event,
		&log);
	subs[nsubs++] = oakengine_event_subscribe(
		group, OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED, record_event,
		&log);
	for (int i = 0; i < nsubs; i++) {
		EXPECT_TRUE(subs[i] > 0);
	}

	// The group must contain the inner node before a passthrough can be
	// added (insertion itself fires the position-changed event).
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 0.0, 0.0) ==
		   OAKENGINE_OK);
	reset_event(&log, OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED);

	// Add passthrough: handle = inner node, s = input id, a = element.
	EXPECT_TRUE(oakengine_group_add_input_passthrough(group, solid, "color_in",
												 -1, NULL, NULL, 0) > 0);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED] == 1);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED] ==
		   group);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED] ==
		   solid);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED],
				  "color_in") == 0);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED] == -1);

	// Output passthrough: handle = the new output node.
	EXPECT_TRUE(oakengine_group_set_output_passthrough(group, solid) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED] ==
		   solid);

	// Remove passthrough.
	EXPECT_TRUE(oakengine_group_remove_input_passthrough(group, solid, "color_in",
													-1) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED] ==
		   solid);
	EXPECT_TRUE(strcmp(log.last_s[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED],
				  "color_in") == 0);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED] == -1);

	// Context position: handle = child node, a/b = x/y double bit patterns.
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 3.5, -2.25) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED] == 1);
	EXPECT_TRUE(log.last_source[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED] ==
		   group);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED] ==
		   solid);
	double px, py;
	memcpy(&px, &log.last_a[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED],
		   sizeof(px));
	memcpy(&py, &log.last_b[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED],
		   sizeof(py));
	EXPECT_TRUE(px == 3.5 && py == -2.25);

	// Moving again re-emits with the new coordinates.
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 0.0, 1.0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED] == 2);
	memcpy(&px, &log.last_a[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED],
		   sizeof(px));
	memcpy(&py, &log.last_b[OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED],
		   sizeof(py));
	EXPECT_TRUE(px == 0.0 && py == 1.0);

	for (int i = 0; i < nsubs; i++) {
		EXPECT_TRUE(oakengine_event_unsubscribe(subs[i]) == OAKENGINE_OK);
	}

	EXPECT_TRUE(oakengine_project_remove_node(project, group) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

// ---- Track / block state events (B4c) ---------------------------------------

// The caller left one clip at 10..40 on video track 0 (see
// test_sequence_events); `track` is that track.
static void test_track_extra_events(OakEngineSequence *seq,
									OakEngineTrack *track)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// Family mismatches: a track is not a sequence and vice versa.
	EXPECT_TRUE(oakengine_event_subscribe(
			   track, OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED,
			   record_event, &log) == 0);
	EXPECT_TRUE(oakengine_event_subscribe(seq, OAKENGINE_EVENT_TRACK_MUTED_CHANGED,
									 record_event, &log) == 0);

	// Muted changed.
	int64_t sub_mute = oakengine_event_subscribe(
		track, OAKENGINE_EVENT_TRACK_MUTED_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_mute > 0);
	EXPECT_TRUE(oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_MUTED_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_TRACK_MUTED_CHANGED] == 1);
	EXPECT_TRUE(oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_MUTED_CHANGED] == 2);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_TRACK_MUTED_CHANGED] == 0);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_mute) == OAKENGINE_OK);

	// Track height changed (track-level, double bit pattern) and the
	// sequence-level pixel variant.
	int64_t sub_h = oakengine_event_subscribe(
		track, OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED, record_event, &log);
	int64_t sub_sh = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED, record_event,
		&log);
	EXPECT_TRUE(sub_h > 0 && sub_sh > 0);
	EXPECT_TRUE(oakengine_track_set_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  2.5) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED] == 1);
	double h;
	memcpy(&h, &log.last_a[OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED], sizeof(h));
	EXPECT_TRUE(h == 2.5);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED] ==
		   OAKENGINE_TRACK_TYPE_VIDEO);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED] ==
		   oakengine_track_height_internal_to_pixels(2.5));
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED] ==
		   (void *)track);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_h) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_sh) == OAKENGINE_OK);

	// Track list changed + index changed: append a second video track,
	// then move track 0 to position 1.
	int64_t sub_list = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_list > 0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED] >= 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED] ==
		   OAKENGINE_TRACK_TYPE_VIDEO);

	int64_t sub_index = oakengine_event_subscribe(
		track, OAKENGINE_EVENT_TRACK_INDEX_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_index > 0);
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 1) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_INDEX_CHANGED] >= 1);
	EXPECT_TRUE(log.last_b[OAKENGINE_EVENT_TRACK_INDEX_CHANGED] == 1);
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 1,
										 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_index) == OAKENGINE_OK);

	// Clean up the extra track.
	EXPECT_TRUE(oakengine_sequence_remove_track(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										   1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_list) == OAKENGINE_OK);

	// Blocks refreshed: emitted when the track re-lays out its block chain
	// (e.g. moving a clip onto it).
	int64_t sub_refresh = oakengine_event_subscribe(
		track, OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED, record_event, &log);
	EXPECT_TRUE(sub_refresh > 0);
	EXPECT_TRUE(oakengine_sequence_move_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0,
										50) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED] >= 1);
	// Move it back to 10 to restore the original layout.
	EXPECT_TRUE(oakengine_sequence_move_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0,
										10) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_refresh) == OAKENGINE_OK);

	// Subtitles changed: subscription validates (no headless trigger --
	// the signal only fires on subtitle-track cache invalidation).
	int64_t sub_subs = oakengine_event_subscribe(
		seq, OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_subs > 0);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_subs) == OAKENGINE_OK);
}

static void test_block_state_events(OakEngineSequence *seq,
									const char *media_path)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// The clip is back at 10..40 (clip index 0; the leading gap is not
	// counted by the clip family).
	OakEngineClip *clip = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(clip != NULL);
	OakEngineBlock *block = (OakEngineBlock *)clip;

	// Family mismatch: a block is not a track.
	EXPECT_TRUE(oakengine_event_subscribe(
			   block, OAKENGINE_EVENT_TRACK_MUTED_CHANGED, record_event,
			   &log) == 0);

	int64_t sub_en = oakengine_event_subscribe(
		block, OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_en > 0);
	// The engine emits enabled_changed twice per flip (Block::set_enabled
	// and Block::InputValueChangedEvent), so each toggle delivers two.
	OakEngineClip *clips[1] = { clip };
	EXPECT_TRUE(oakengine_clip_toggle_enabled(clips, 1) == 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED] == 2);
	EXPECT_TRUE(oakengine_block_is_enabled(block) == 0);
	EXPECT_TRUE(oakengine_clip_toggle_enabled(clips, 1) == 1);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED] == 4);
	EXPECT_TRUE(oakengine_block_is_enabled(block) == 1);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_en) == OAKENGINE_OK);

	// Preview changed: writing the loop-mode input fires it.
	int64_t sub_prev = oakengine_event_subscribe(
		block, OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_prev > 0);
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_COMBO;
	v.num = 1;
	EXPECT_TRUE(oakengine_node_set_input((OakEngineNode *)clip,
									oakengine_clip_loop_mode_input_id(),
									&v) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED] == 1);
	v.num = 0;
	EXPECT_TRUE(oakengine_node_set_input((OakEngineNode *)clip,
									oakengine_clip_loop_mode_input_id(),
									&v) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED] == 2);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_prev) == OAKENGINE_OK);

	// Node links/color changed (Node signals on the block handle).
	int64_t sub_links = oakengine_event_subscribe(
		(OakEngineNode *)clip, OAKENGINE_EVENT_NODE_LINKS_CHANGED,
		record_event, &log);
	int64_t sub_color = oakengine_event_subscribe(
		(OakEngineNode *)clip, OAKENGINE_EVENT_NODE_COLOR_CHANGED,
		record_event, &log);
	EXPECT_TRUE(sub_links > 0 && sub_color > 0);
	OakEngineNode *one[1] = { (OakEngineNode *)clip };
	EXPECT_TRUE(oakengine_node_set_color_label(one, 1, 4) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_COLOR_CHANGED] == 1);
	OakEngineFootage *footage =
		oakengine_project_import_footage(oakengine_node_get_project(
										 (OakEngineNode *)seq),
									 media_path);
	EXPECT_TRUE(footage != NULL);
	OakEngineClip *clip2 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 50, 80, 0);
	EXPECT_TRUE(clip2 != NULL);
	OakEngineClip *pair[2] = { clip, clip2 };
	EXPECT_TRUE(oakengine_clip_set_linked(pair, 2, 1) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_NODE_LINKS_CHANGED] >= 1);
	EXPECT_TRUE(oakengine_clip_set_linked(pair, 2, 0) == OAKENGINE_OK);
	oakengine_footage_free(footage);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_links) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_color) == OAKENGINE_OK);
}

static void test_marker_list_events(OakEngineSequence *seq)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	OakEngineMarkerList *list =
		oakengine_viewer_get_marker_list((OakEngineNode *)seq);
	EXPECT_TRUE(list != NULL);

	// Family mismatch: a marker list is not a workarea.
	EXPECT_TRUE(oakengine_event_subscribe(
			   list, OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED, record_event,
			   &log) == 0);

	int64_t sub_add = oakengine_event_subscribe(
		list, OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED, record_event, &log);
	int64_t sub_mod = oakengine_event_subscribe(
		list, OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED, record_event,
		&log);
	int64_t sub_rm = oakengine_event_subscribe(
		list, OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED, record_event,
		&log);
	EXPECT_TRUE(sub_add > 0 && sub_mod > 0 && sub_rm > 0);

	// Add a marker at 2 seconds (rational seconds, not timestamps).
	EXPECT_TRUE(oakengine_marker_list_add(list, 2, 1, 2, 1, "ListMark", 3) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED] == 1);
	OakEngineMarker *marker = (OakEngineMarker *)log.last_handle
		[OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED];
	EXPECT_TRUE(marker != NULL);
	EXPECT_TRUE(oakengine_marker_list_at(list, 0) != NULL);

	// Modify: recolor through the properties batch.
	OakEngineMarker *one[1] = { marker };
	EXPECT_TRUE(oakengine_marker_set_properties(one, 1, 5, NULL, 0, 0, 0, 0, 0,
										   NULL) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED] == 1);
	EXPECT_TRUE(log.last_handle[OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED] ==
		   (void *)marker);

	// Remove.
	EXPECT_TRUE(oakengine_marker_remove(marker) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED] == 1);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_add) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_mod) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_rm) == OAKENGINE_OK);
}

static void test_workarea_events(OakEngineSequence *seq)
{
	EventLog log;
	memset(&log, 0, sizeof(log));

	// Viewer-owned (borrowed) workarea.
	OakEngineWorkarea *wa =
		oakengine_viewer_get_workarea_handle((OakEngineNode *)seq);
	EXPECT_TRUE(wa != NULL);

	int64_t sub_range = oakengine_event_subscribe(
		wa, OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED, record_event, &log);
	int64_t sub_en = oakengine_event_subscribe(
		wa, OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_range > 0 && sub_en > 0);

	EXPECT_TRUE(oakengine_workarea_set_range(wa, 1, 1, 4, 1) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED] == 1);
	int64_t in_num = 0, in_den = 0, out_num = 0, out_den = 0;
	EXPECT_TRUE(oakengine_workarea_get(wa, &in_num, &in_den, &out_num, &out_den,
								  NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in_num == 1 && in_den == 1 && out_num == 4 && out_den == 1);

	EXPECT_TRUE(oakengine_workarea_set_enabled(wa, 1) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED] == 1);
	EXPECT_TRUE(log.last_a[OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED] == 1);
	EXPECT_TRUE(oakengine_workarea_set_enabled(wa, 0) == OAKENGINE_OK);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_range) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_en) == OAKENGINE_OK);

	// Standalone owned workarea (the footage viewer override pattern).
	OakEngineWorkarea *over = oakengine_workarea_create();
	EXPECT_TRUE(over != NULL);
	int64_t sub_over = oakengine_event_subscribe(
		over, OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED, record_event, &log);
	EXPECT_TRUE(sub_over > 0);
	EXPECT_TRUE(oakengine_workarea_set_range(over, 0, 1, 7, 2) == OAKENGINE_OK);
	EXPECT_TRUE(log.count[OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED] == 2);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_over) == OAKENGINE_OK);
	oakengine_workarea_free(over);
	oakengine_workarea_free(NULL);
}

TEST(OakEngineEvents, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("OAK_CONFIG_DIR", g_tmpdir, 1) == 0);
#endif

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Events");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);

	char path[4096];
	demo_path(path, sizeof(path));

	OakEngineTrack *track = oakengine_sequence_track_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0);
	EXPECT_TRUE(track != NULL);

	test_subscribe_validation(project, seq, track);
	test_project_events(project);
	test_folder_events(project);
	test_sequence_events(project, seq, path);
	test_block_traversal(seq);
	test_track_extra_events(seq, track);
	test_block_state_events(seq, path);
	test_marker_list_events(seq);
	test_workarea_events(seq);
	test_node_events(project);
	test_group_events(project);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
