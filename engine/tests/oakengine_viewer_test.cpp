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

// Pure C ABI test for the liboakengine viewer facade (oakengine/viewer.h)
// and the viewer events (oakengine/events.h ids 100-110). Exercises every
// function of the family on a Sequence (a ViewerOutput subclass): handle
// validation, input ids, playhead/length, stream params, enabled streams,
// workarea, parameter setup, waveform and the change notifications. No GL
// required (headless init, CPU only). Uses tests/demo.mp4 to give the
// sequence real content length.

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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_viewer_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_viewer_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
}

// A sequence handle is the same engine object pointer as its node handle
// (all facade handles are reinterpreted engine pointers; see the wrap()
// helpers in src/capi/timeline.cpp).
static OakEngineNode *as_node(OakEngineSequence *seq)
{
	return (OakEngineNode *)seq;
}

// ---- Handle validation / constants ----------------------------------------

static void test_from_node(OakEngineProject *project, OakEngineSequence *seq)
{
	OakEngineNode *seq_node = as_node(seq);
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);

	EXPECT_TRUE(oakengine_viewer_from_node(NULL) == NULL);
	EXPECT_TRUE(oakengine_viewer_from_node(solid) == NULL);
	EXPECT_TRUE(oakengine_viewer_from_node(seq_node) == seq_node);

	EXPECT_TRUE(oakengine_viewer_from_const_node(NULL) == NULL);
	EXPECT_TRUE(oakengine_viewer_from_const_node((const OakEngineNode *)solid) ==
		   NULL);
	EXPECT_TRUE(oakengine_viewer_from_const_node((const OakEngineNode *)seq_node) ==
		   (const OakEngineNode *)seq_node);

	// The input id constants are static, non-empty strings.
	EXPECT_TRUE(oakengine_viewer_video_params_input_id() != NULL);
	EXPECT_TRUE(oakengine_viewer_video_params_input_id()[0] != '\0');
	EXPECT_TRUE(oakengine_viewer_audio_params_input_id()[0] != '\0');
	EXPECT_TRUE(oakengine_viewer_subtitle_params_input_id()[0] != '\0');
	EXPECT_TRUE(oakengine_viewer_texture_input_id()[0] != '\0');
	EXPECT_TRUE(oakengine_viewer_samples_input_id()[0] != '\0');
	EXPECT_TRUE(oakengine_viewer_default_sample_format() >= 0);
}

// ---- Playhead / length ------------------------------------------------------

static void test_playhead(OakEngineSequence *seq)
{
	int64_t num = -1, den = -1;

	EXPECT_TRUE(oakengine_viewer_get_playhead(NULL, &num, &den) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_playhead(as_node(seq), &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 0);

	EXPECT_TRUE(oakengine_viewer_set_playhead(NULL, 1, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_playhead(as_node(seq), 2, 1) == OAKENGINE_OK);
	num = den = -1;
	EXPECT_TRUE(oakengine_viewer_get_playhead(as_node(seq), &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 2 && den == 1);
}

static void test_lengths(OakEngineSequence *seq)
{
	int64_t num = -1, den = -1;

	EXPECT_TRUE(oakengine_viewer_get_length(NULL, &num, &den) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_length(as_node(seq), &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 0);
	EXPECT_TRUE(oakengine_viewer_get_video_length(as_node(seq), &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 0);
	EXPECT_TRUE(oakengine_viewer_get_audio_length(as_node(seq), &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 0);
}

// ---- Stream parameters --------------------------------------------------------

static void test_stream_params(OakEngineSequence *seq)
{
	const OakEngineNode *node = (const OakEngineNode *)as_node(seq);
	oak_video_params vp;
	int sr = -1, format = -1;
	uint64_t layout = 1;

	EXPECT_TRUE(oakengine_viewer_get_video_params(NULL, 0, &vp) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_video_params(node, 0, NULL) ==
		   OAKENGINE_E_INVALID);

	// A fresh sequence has one video and one audio stream, no subtitles.
	EXPECT_TRUE(oakengine_viewer_get_video_stream_count(NULL) == 0);
	EXPECT_TRUE(oakengine_viewer_get_video_stream_count(node) == 1);
	EXPECT_TRUE(oakengine_viewer_get_audio_stream_count(node) == 1);
	EXPECT_TRUE(oakengine_viewer_get_subtitle_stream_count(node) == 0);

	// In-range video params come from the sequence defaults.
	EXPECT_TRUE(oakengine_viewer_get_video_params(node, 0, &vp) == OAKENGINE_OK);
	EXPECT_TRUE(vp.width > 0 && vp.height > 0);
	EXPECT_TRUE(vp.time_base_num > 0 && vp.time_base_den > 0);

	// Out-of-range yields a zeroed struct (documented in viewer.h).
	memset(&vp, 0xFF, sizeof(vp));
	EXPECT_TRUE(oakengine_viewer_get_video_params(node, 99, &vp) == OAKENGINE_OK);
	EXPECT_TRUE(vp.width == 0 && vp.height == 0);

	// Audio params; out-of-range yields 0/0/0.
	EXPECT_TRUE(oakengine_viewer_get_audio_params(NULL, 0, &sr, &layout,
											 &format) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_audio_params(node, 0, &sr, &layout,
											 &format) == OAKENGINE_OK);
	EXPECT_TRUE(sr > 0 && layout != 0);
	sr = -1;
	layout = 1;
	format = -1;
	EXPECT_TRUE(oakengine_viewer_get_audio_params(node, 99, &sr, &layout,
											 &format) == OAKENGINE_OK);
	EXPECT_TRUE(sr == 0 && layout == 0 && format == 0);

	// Per-stream enabled flags: video/audio stream 0 are enabled by
	// default; subtitle has no stream 0.
	EXPECT_TRUE(oakengine_viewer_get_stream_enabled(NULL, OAKENGINE_TRACK_TYPE_VIDEO,
											   0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_stream_enabled(node, OAKENGINE_TRACK_TYPE_VIDEO,
											   0) == 1);
	EXPECT_TRUE(oakengine_viewer_get_stream_enabled(node, OAKENGINE_TRACK_TYPE_AUDIO,
											   0) == 1);
	EXPECT_TRUE(oakengine_viewer_get_stream_enabled(
			   node, OAKENGINE_TRACK_TYPE_SUBTITLE, 0) == 0);
	EXPECT_TRUE(oakengine_viewer_get_stream_enabled(node, 99, 0) ==
		   OAKENGINE_E_INVALID);

	// Subtitle access: no subtitle streams on a fresh sequence.
	EXPECT_TRUE(oakengine_viewer_get_subtitle_count(NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_subtitle_count(node, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_subtitle_at(NULL, 0, 0) == NULL);
	EXPECT_TRUE(oakengine_viewer_get_subtitle_at(node, 0, 0) == NULL);
}

static void test_enabled_streams(OakEngineSequence *seq)
{
	const OakEngineNode *node = (const OakEngineNode *)as_node(seq);
	oak_video_params vp;

	EXPECT_TRUE(oakengine_viewer_has_enabled_streams(NULL,
												OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	EXPECT_TRUE(oakengine_viewer_has_enabled_streams(node,
												OAKENGINE_TRACK_TYPE_VIDEO) ==
		   1);
	EXPECT_TRUE(oakengine_viewer_has_enabled_streams(node,
												OAKENGINE_TRACK_TYPE_AUDIO) ==
		   1);
	EXPECT_TRUE(oakengine_viewer_has_enabled_streams(
			   node, OAKENGINE_TRACK_TYPE_SUBTITLE) == 0);
	EXPECT_TRUE(oakengine_viewer_has_enabled_streams(node, 99) == 0);

	EXPECT_TRUE(oakengine_viewer_get_first_enabled_video_stream(NULL, &vp) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_first_enabled_video_stream(node, &vp) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(vp.width > 0 && vp.height > 0);

	// Enabled stream references: video:0 and audio:0.
	EXPECT_TRUE(oakengine_viewer_get_enabled_stream_count(NULL) == 0);
	const int count = oakengine_viewer_get_enabled_stream_count(node);
	EXPECT_TRUE(count == 2);
	// Query form (max = 0, NULL arrays) returns the total count.
	EXPECT_TRUE(oakengine_viewer_get_enabled_streams(node, NULL, NULL, 0) == count);

	int types[8];
	int indices[8];
	memset(types, -1, sizeof(types));
	memset(indices, -1, sizeof(indices));
	EXPECT_TRUE(oakengine_viewer_get_enabled_streams(node, types, indices, 8) ==
		   count);
	int saw_video = 0, saw_audio = 0;
	for (int i = 0; i < count; i++) {
		EXPECT_TRUE(indices[i] == 0);
		if (types[i] == OAKENGINE_TRACK_TYPE_VIDEO) {
			saw_video = 1;
		} else if (types[i] == OAKENGINE_TRACK_TYPE_AUDIO) {
			saw_audio = 1;
		} else {
			EXPECT_TRUE(0); // unexpected stream type
		}
	}
	EXPECT_TRUE(saw_video && saw_audio);

	// A smaller max truncates the write but still returns the total.
	types[0] = types[1] = -1;
	indices[0] = indices[1] = -1;
	EXPECT_TRUE(oakengine_viewer_get_enabled_streams(node, types, indices, 1) ==
		   count);
	EXPECT_TRUE(types[0] != -1 && types[1] == -1);
}

// ---- Workarea ------------------------------------------------------------------

static void test_workarea(OakEngineSequence *seq)
{
	OakEngineNode *node = as_node(seq);
	oakengine_viewer_workarea wa;

	EXPECT_TRUE(oakengine_viewer_get_workarea(NULL, &wa) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_workarea(node, NULL) == OAKENGINE_E_INVALID);
	memset(&wa, 0xFF, sizeof(wa));
	EXPECT_TRUE(oakengine_viewer_get_workarea(node, &wa) == OAKENGINE_OK);
	EXPECT_TRUE(wa.enabled == 0);

	EXPECT_TRUE(oakengine_viewer_set_workarea_range(NULL, 0, 1, 1, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_workarea_range(node, 1, 1, 5, 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_viewer_set_workarea_enabled(NULL, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_workarea_enabled(node, 1) == OAKENGINE_OK);

	memset(&wa, 0, sizeof(wa));
	EXPECT_TRUE(oakengine_viewer_get_workarea(node, &wa) == OAKENGINE_OK);
	EXPECT_TRUE(wa.in_num == 1 && wa.in_den == 1);
	EXPECT_TRUE(wa.out_num == 5 && wa.out_den == 1);
	EXPECT_TRUE(wa.enabled == 1);

	EXPECT_TRUE(oakengine_viewer_set_workarea_enabled(node, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_viewer_get_workarea(node, &wa) == OAKENGINE_OK);
	EXPECT_TRUE(wa.enabled == 0);
}

// ---- Parameter setup / waveform -------------------------------------------------

static void test_parameter_setup(OakEngineProject *project,
								 OakEngineSequence *seq)
{
	OakEngineNode *node = as_node(seq);

	EXPECT_TRUE(oakengine_viewer_set_default_parameters(NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_default_parameters(node) == OAKENGINE_OK);

	// set_parameters_from_footage accepts any viewer handles; a second
	// sequence stands in for the footage array here.
	OakEngineSequence *other = oakengine_sequence_new(project, "Other");
	EXPECT_TRUE(other != NULL);
	OakEngineNode *other_node = as_node(other);

	EXPECT_TRUE(oakengine_viewer_set_parameters_from_footage(NULL, &other_node,
														1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_parameters_from_footage(node, NULL, 1) ==
		   OAKENGINE_E_INVALID);
	// An empty array is a valid no-op.
	EXPECT_TRUE(oakengine_viewer_set_parameters_from_footage(node, NULL, 0) ==
		   OAKENGINE_OK);
	// One invalid element rejects the whole call.
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);
	OakEngineNode *mixed[2] = { other_node, solid };
	EXPECT_TRUE(oakengine_viewer_set_parameters_from_footage(node, mixed, 2) ==
		   OAKENGINE_E_INVALID);
	// All viewers: OK, and the params are adopted.
	OakEngineNode *viewers[1] = { other_node };
	EXPECT_TRUE(oakengine_viewer_set_parameters_from_footage(node, viewers, 1) ==
		   OAKENGINE_OK);

	// Waveform toggle; nothing is connected to the samples input, so the
	// connected waveform is NULL.
	EXPECT_TRUE(oakengine_viewer_set_waveform_enabled(NULL, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_set_waveform_enabled(node, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_viewer_set_waveform_enabled(node, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_viewer_get_connected_waveform(NULL) == NULL);
	EXPECT_TRUE(oakengine_viewer_get_connected_waveform(
			   (const OakEngineNode *)node) == NULL);
}

// ---- Events ---------------------------------------------------------------------

struct EventLog {
	int playhead_events;
	int64_t playhead_num;
	int64_t playhead_den;
	int length_events;
	int64_t length_num;
	int64_t length_den;
	int size_events;
	int64_t size_w;
	int64_t size_h;
	int video_params_events;
	int audio_params_events;
	int sample_rate_events;
	int64_t sample_rate;
	int texture_events;
	int frame_rate_events;
	int pixel_aspect_events;
	int interlacing_events;
	int64_t interlacing_mode;
	int waveform_events;
};

static void record_event(const oakengine_event *event, void *userdata)
{
	struct EventLog *log = (struct EventLog *)userdata;
	EXPECT_TRUE(event != NULL);
	switch (event->id) {
	case OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED:
		log->playhead_events++;
		log->playhead_num = event->a;
		log->playhead_den = event->b;
		break;
	case OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED:
		log->length_events++;
		log->length_num = event->a;
		log->length_den = event->b;
		break;
	case OAKENGINE_EVENT_VIEWER_SIZE_CHANGED:
		log->size_events++;
		log->size_w = event->a;
		log->size_h = event->b;
		break;
	case OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED:
		log->video_params_events++;
		break;
	case OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED:
		log->audio_params_events++;
		break;
	case OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED:
		log->sample_rate_events++;
		log->sample_rate = event->a;
		break;
	case OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED:
		log->texture_events++;
		break;
	case OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED:
		log->frame_rate_events++;
		break;
	case OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED:
		log->pixel_aspect_events++;
		break;
	case OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED:
		log->interlacing_events++;
		log->interlacing_mode = event->a;
		break;
	case OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED:
		log->waveform_events++;
		break;
	default:
		EXPECT_TRUE(0); // unexpected event id on this subscription
	}
}

static int64_t subscribe_checked(OakEngineNode *node, int32_t id,
								 struct EventLog *log)
{
	const int64_t sub = oakengine_event_subscribe(node, id, record_event, log);
	EXPECT_TRUE(sub > 0);
	return sub;
}

static void test_events(OakEngineProject *project, OakEngineSequence *seq,
						const char *media_path)
{
	struct EventLog log;
	memset(&log, 0, sizeof(log));
	OakEngineNode *node = as_node(seq);

	// Family mismatch: a viewer event on a non-viewer node must fail.
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);
	EXPECT_TRUE(oakengine_event_subscribe(solid,
									 OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED,
									 record_event, &log) == 0);

	int64_t subs[16];
	int n = 0;
	subs[n++] = subscribe_checked(node, OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED,
								  &log);
	subs[n++] =
		subscribe_checked(node, OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED, &log);
	subs[n++] =
		subscribe_checked(node, OAKENGINE_EVENT_VIEWER_SIZE_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED, &log);
	subs[n++] = subscribe_checked(
		node, OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED, &log);

	// Playhead.
	EXPECT_TRUE(oakengine_viewer_set_playhead(node, 3, 1) == OAKENGINE_OK);
	EXPECT_TRUE(log.playhead_events == 1);
	EXPECT_TRUE(log.playhead_num == 3 && log.playhead_den == 1);

	// Length: placing a real clip makes verify_length() emit
	// length_changed with the new content length.
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 30, 0);
	EXPECT_TRUE(clip != NULL);
	EXPECT_TRUE(log.length_events >= 1);
	EXPECT_TRUE(log.length_num > 0 && log.length_den > 0);
	int64_t len_num = -1, len_den = -1;
	EXPECT_TRUE(oakengine_viewer_get_length(node, &len_num, &len_den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(len_num == log.length_num && len_den == log.length_den);

	// Video params: changing the size emits size_changed (with the new
	// dimensions as a/b) and video_params_changed.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, 1280, 720, -1, -1, -1, -1,
											   -1, -1, 0) == OAKENGINE_OK);
	EXPECT_TRUE(log.size_events == 1);
	EXPECT_TRUE(log.size_w == 1280 && log.size_h == 720);
	EXPECT_TRUE(log.video_params_events == 1);
	EXPECT_TRUE(log.frame_rate_events == 0);
	EXPECT_TRUE(log.pixel_aspect_events == 0);
	EXPECT_TRUE(log.interlacing_events == 0);

	// Pixel aspect and interlacing changes fire their own events.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, -1, -1, -1, -1, 4, 3, -1,
											   -1, 0) == OAKENGINE_OK);
	EXPECT_TRUE(log.pixel_aspect_events == 1);
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, -1, -1, -1, -1, -1, -1, 1,
											   -1, 0) == OAKENGINE_OK);
	EXPECT_TRUE(log.interlacing_events == 1);
	EXPECT_TRUE(log.interlacing_mode == 1);

	// Audio params: a new sample rate emits sample_rate_changed (a = rate)
	// and audio_params_changed.
	EXPECT_TRUE(oakengine_sequence_set_audio_params(seq, 44100, 0, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.sample_rate_events == 1);
	EXPECT_TRUE(log.sample_rate == 44100);
	EXPECT_TRUE(log.audio_params_events == 1);

	// Texture input: the placed clip's track auto-connected the viewer's
	// texture input, so disconnect it first, then connect a node and check
	// that texture_input_changed fired.
	EXPECT_TRUE(oakengine_node_disconnect(node,
									 oakengine_viewer_texture_input_id()) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_connect(solid, node,
								  oakengine_viewer_texture_input_id()) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(log.texture_events >= 1);
	EXPECT_TRUE(oakengine_node_disconnect(node,
									 oakengine_viewer_texture_input_id()) ==
		   OAKENGINE_OK);

	// connected_waveform_changed requires a connected sample output with a
	// waveform cache; there is no audio-producing node chain in this test,
	// so only the subscription itself is exercised above.

	while (n > 0) {
		EXPECT_TRUE(oakengine_event_unsubscribe(subs[--n]) == OAKENGINE_OK);
	}
}

TEST(OakEngineViewer, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations.
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

	OakEngineSequence *seq = oakengine_sequence_new(project, "ViewerSeq");
	EXPECT_TRUE(seq != NULL);

	test_from_node(project, seq);
	test_playhead(seq);
	test_lengths(seq);
	test_stream_params(seq);
	test_enabled_streams(seq);
	test_workarea(seq);
	test_parameter_setup(project, seq);

	char media[4096];
	demo_path(media, sizeof(media));

	// test_parameter_setup() called set_default_parameters(), which reads
	// the (empty, sandboxed) user config and may leave invalid params
	// behind (same hazard oakengine_sequence_new() backfills against).
	// Restore known-good params so the clip/timebase paths work.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, 1920, 1080, 30000, 1001,
											   1, 1, 0, -1, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_set_audio_params(seq, 48000, 3, 0) ==
		   OAKENGINE_OK);

	test_events(project, seq, media);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
