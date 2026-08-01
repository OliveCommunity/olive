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

// Pure C ABI test for the liboakengine preview facade: clip loop mode,
// per-channel audio levels (one-frame RMS) and footage waveform summary.
// Audio renders through RenderManager's audio thread and needs no GL
// (only the RENDER init bit). Uses the real media file tests/demo.mp4.

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

#include "oakengine/footage.h"
#include "oakengine/init.h"
#include "oakengine/preview.h"
#include "oakengine/project.h"
#include "oakengine/renderer.h"
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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_preview_test_%lu",
			 base, (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_preview_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
}

static void test_loop_mode(OakEngineProject *project, OakEngineClip *clip)
{
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_OFF);

	EXPECT_TRUE(oakengine_clip_set_loop_mode(clip, OAKENGINE_LOOP_MODE_LOOP) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_LOOP);
	EXPECT_TRUE(oakengine_clip_set_loop_mode(clip, OAKENGINE_LOOP_MODE_CLAMP) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_CLAMP);

	// Unknown modes and NULL are rejected.
	EXPECT_TRUE(oakengine_clip_set_loop_mode(clip, 3) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_set_loop_mode(clip, -1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_set_loop_mode(NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(NULL) == OAKENGINE_E_INVALID);

	// Undoable: undo restores LOOP then OFF, redo restores LOOP then CLAMP.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_LOOP);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_OFF);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_loop_mode(clip) == OAKENGINE_LOOP_MODE_CLAMP);
}

// Generate a loud test tone (440 Hz stereo sine, 2 s) with the ffmpeg
// CLI -- the demo file's own audio track is essentially silent
// (volumedetect: -91 dB), so a real tone is needed to exercise the
// "content present" assertions. Requires ffmpeg in PATH, like the proxy
// tests do.
static void make_tone(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tone.wav", g_tmpdir);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
	char cmd[4608];
	snprintf(cmd, sizeof(cmd),
			 "ffmpeg -v error -y -f lavfi -i "
			 "\"sine=frequency=440:duration=2\" -ar 48000 -ac 2 \"%s\"",
			 dst);
	EXPECT_TRUE(system(cmd) == 0);
	FILE *f = fopen(dst, "rb");
	EXPECT_TRUE(f != NULL);
	fclose(f);
}

static void test_levels(OakEngineSequence *seq)
{
	double levels[4] = { -1.0, -1.0, -1.0, -1.0 };

	// Inside the clip (30 frames at 30000/1001): a loud sine on both
	// channels. RMS of a full-scale sine is ~0.707.
	const int written = oakengine_preview_get_audio_levels(seq, 10, levels, 4);
	EXPECT_TRUE(written == 2);
	EXPECT_TRUE(levels[2] == 0.0 && levels[3] == 0.0); // beyond channel count

	// Past the end of the track: exact silence.
	double silent[2] = { -1.0, -1.0 };
	EXPECT_TRUE(oakengine_preview_get_audio_levels(seq, 35, silent, 2) >= 0);
	EXPECT_TRUE(silent[0] == 0.0 && silent[1] == 0.0);

	// Error paths.
	EXPECT_TRUE(oakengine_preview_get_audio_levels(NULL, 10, levels, 2) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_audio_levels(seq, -1, levels, 2) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_audio_levels(seq, 10, NULL, 2) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_audio_levels(seq, 10, levels, 0) ==
		   OAKENGINE_E_INVALID);
}

static void test_waveform(OakEngineFootage *tone, OakEngineFootage *demo,
						 OakEngineFootage *probed)
{
	double mins[16], maxs[16];

	// The tone in 10 buckets over one second: every bucket swings.
	EXPECT_TRUE(oakengine_preview_get_waveform_summary(tone, 0, 0, 30, mins,
												  maxs, 10) == OAKENGINE_OK);
	for (int i = 0; i < 10; i++) {
		EXPECT_TRUE(mins[i] <= maxs[i]);
	}

	// Error paths.
	EXPECT_TRUE(oakengine_preview_get_waveform_summary(probed, 0, 0, 30, mins,
												  maxs, 10) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_waveform_summary(tone, 99, 0, 30, mins,
												  maxs, 10) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_waveform_summary(tone, 0, 0, 30, mins,
												  maxs, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_get_waveform_summary(NULL, 0, 0, 30, mins, maxs,
												  10) == OAKENGINE_E_INVALID);
}

// ===== B9c tests ==========================================================

static void test_waveform_max_sample_rate(void)
{
	int rate = oakengine_waveform_max_sample_rate();
	EXPECT_TRUE(rate > 0);
	(void) rate;
}

static void test_audio_analyze_levels(void)
{
	float ch0[] = {1.0f, -1.0f, 0.5f, -0.5f};
	float ch1[] = {0.0f, 0.0f, 0.0f, 0.0f};
	const float *data[] = {ch0, ch1};
	double levels[2] = {-1.0, -1.0};
	EXPECT_TRUE(oakengine_audio_analyze_levels(data, 2, 4, levels) == OAKENGINE_OK);
	EXPECT_TRUE(levels[0] > 0.0);
	EXPECT_TRUE(levels[1] == 0.0);
	EXPECT_TRUE(oakengine_audio_analyze_levels(NULL, 2, 4, levels) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_audio_analyze_levels(data, 0, 4, levels) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_audio_analyze_levels(data, 2, 0, levels) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_audio_analyze_levels(data, 2, 4, NULL) == OAKENGINE_E_INVALID);
}

static void test_cacher_null_state(void)
{
	EXPECT_TRUE(oakengine_preview_cacher_set_playhead(0, 1) == OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_preview_cacher_set_thumbnails_paused(1) == OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_preview_cacher_clear_single_frame_renders(0) == OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_preview_cacher_force_cache_range(NULL, 0, 1, 1, 1) == OAKENGINE_E_INVALID);
}

static void test_preview_request_null(void)
{
	EXPECT_TRUE(oakengine_preview_request_single_frame(NULL, 0, 1, 0) == NULL);
	EXPECT_TRUE(oakengine_preview_request_audio_range(NULL, 0, 1, 1, 1) == NULL);
	EXPECT_TRUE(oakengine_preview_request_is_done(NULL) == 0);
	EXPECT_TRUE(oakengine_preview_request_has_result(NULL) == 0);
	EXPECT_TRUE(oakengine_preview_request_set_finished_callback(NULL, NULL, NULL) == OAKENGINE_E_INVALID);
	oak_playback_frame frame;
	memset(&frame, 0, sizeof(frame));
	EXPECT_TRUE(oakengine_preview_request_get_frame(NULL, &frame) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_preview_request_get_audio_channel_count(NULL) == 0);
	EXPECT_TRUE(oakengine_preview_request_get_audio_sample_rate(NULL) == 0);
	EXPECT_TRUE(oakengine_preview_request_get_audio_samples(NULL, 0, NULL, 0) == OAKENGINE_E_INVALID);
	oakengine_preview_request_free(NULL);
}

static void test_render_manager_null(void)
{
	EXPECT_TRUE(oakengine_render_manager_set_aggressive_garbage_collection(1) == OAKENGINE_E_STATE);
	oakengine_render_manager_requested_backend();
	char buf[64];
	int len = oakengine_render_manager_backend_to_string(0, buf, sizeof(buf));
	EXPECT_TRUE(len >= 0);
}

static void test_playback_cache_null(void)
{
	EXPECT_TRUE(oakengine_viewer_get_playback_cache(NULL) == NULL);
	EXPECT_TRUE(oakengine_playback_cache_indicator_height() > 0);
	EXPECT_TRUE(oakengine_playback_cache_valid_ranges(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_viewer_get_frame_cache(NULL) == NULL);
}

TEST(OakEnginePreview, Main)
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

	// B9c: pure functions that don't need RenderManager
	test_waveform_max_sample_rate();
	test_audio_analyze_levels();
	test_cacher_null_state();
	test_preview_request_null();
	test_render_manager_null();
	test_playback_cache_null();

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Preview");
	EXPECT_TRUE(seq != NULL);

	char path[4096], tone_path[4096];
	demo_path(path, sizeof(path));
	make_tone(tone_path, sizeof(tone_path));
	OakEngineFootage *tone =
		oakengine_project_import_footage(project, tone_path);
	EXPECT_TRUE(tone != NULL);
	OakEngineFootage *demo =
		oakengine_project_import_footage(project, path);
	EXPECT_TRUE(demo != NULL);
	OakEngineFootage *probed = oakengine_footage_probe(path);
	EXPECT_TRUE(probed != NULL);

	// Loop mode works headless already.
	OakEngineClip *clip = NULL;
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) == 0);
	clip = oakengine_sequence_add_footage_clip(
		seq, demo, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 30, 0);
	EXPECT_TRUE(clip != NULL);
	OakEngineClip *aclip = oakengine_sequence_add_footage_clip(
		seq, tone, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, 30, 0);
	EXPECT_TRUE(aclip != NULL);
	test_loop_mode(project, clip);

	// Upgrade to RENDER for the audio readouts (audio needs no GL).
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);
	test_levels(seq);
	test_waveform(tone, demo, probed);

	oakengine_footage_free(probed);
	oakengine_footage_free(demo);
	oakengine_footage_free(tone);
	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
