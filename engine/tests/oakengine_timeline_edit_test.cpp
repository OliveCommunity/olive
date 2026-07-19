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

// Pure C ABI test for the liboakengine timeline editing primitives:
// add_track, add_footage_clip and the clip accessors, including their
// undo/redo behavior and failure paths. Uses the real media file
// tests/demo.mp4. No GL required.

#include <assert.h>
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
#include "oakengine/project.h"
#include "oakengine/timeline.h"

#ifndef OAK_TEST_SOURCE_DIR
#define OAK_TEST_SOURCE_DIR "."
#endif

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	assert(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_tl_edit_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_tl_edit_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	assert(n > 0 && (size_t)n < cap);
}

static void test_add_track(OakEngineProject *project, OakEngineSequence *seq)
{
	int video = -1, audio = -1, subtitle = -1;

	assert(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	assert(video == 0 && audio == 0 && subtitle == 0);

	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);
	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) == 0);
	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_SUBTITLE) ==
		   0);
	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 1);

	assert(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	assert(video == 2 && audio == 1 && subtitle == 1);

	// Invalid track types are rejected.
	assert(oakengine_sequence_add_track(seq, -1) == OAKENGINE_E_INVALID);
	assert(oakengine_sequence_add_track(seq, 3) == OAKENGINE_E_INVALID);
	assert(oakengine_sequence_add_track(NULL, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   OAKENGINE_E_INVALID);

	// Track adds are undoable: undo the second video track, then redo it.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	video = audio = subtitle = -1;
	assert(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	assert(video == 1 && audio == 1 && subtitle == 1);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	video = audio = subtitle = -1;
	assert(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	assert(video == 2 && audio == 1 && subtitle == 1);
}

static void test_add_clip(OakEngineProject *project, OakEngineSequence *seq,
						  const char *media_path)
{
	char err[512];

	// Probe handles carry no project node and cannot be placed.
	OakEngineFootage *probed = oakengine_footage_probe(media_path);
	assert(probed != NULL);
	assert(oakengine_sequence_add_footage_clip(seq, probed,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	assert(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	oakengine_footage_free(probed);

	// Import the media into the project.
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	assert(footage != NULL);

	// No subtitle clips.
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_SUBTITLE,
											   0, 0, 30, 0) == NULL);

	// Out-of-range track indexes are rejected without creating tracks.
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 5,
											   0, 30, 0) == NULL);
	assert(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, -1,
											   0, 30, 0) == NULL);

	// Bad ranges: out <= in, negative in / media_in.
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   30, 30, 0) == NULL);
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   30, 10, 0) == NULL);
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   -1, 30, 0) == NULL);
	assert(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, -1) == NULL);

	// NULL safety.
	assert(oakengine_sequence_add_footage_clip(NULL, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	assert(oakengine_sequence_add_footage_clip(seq, NULL,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);

	// clip_count reports OAKENGINE_E_NOT_FOUND for a missing track.
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 99) == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_sequence_clip_count(NULL, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == OAKENGINE_E_INVALID);

	// Place a video clip on track 0: frames 10..40, media in-point 5.
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 40, 5);
	assert(clip != NULL);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	OakEngineClip *at = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	assert(at == clip);
	assert(oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  1) == NULL);
	assert(oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 9,
									  0) == NULL);

	int64_t in = -1, out = -1, media_in = -1;
	assert(oakengine_clip_get_range(clip, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	assert(in == 10 && out == 40 && media_in == 5);
	assert(oakengine_clip_get_range(NULL, &in, &out, &media_in) ==
		   OAKENGINE_E_INVALID);

	// And an audio clip on audio track 0 (same footage, whole range).
	OakEngineClip *aclip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, 30, 0);
	assert(aclip != NULL);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 1);

	// Undo/redo both clip adds (the audio clip is on top of the undo stack,
	// the video clip right below it).
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 0);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	assert(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 1);

	oakengine_footage_free(footage); // wrapper only; node stays
	assert(oakengine_project_footage_count(project) == 1);
}

// Footage imported into one project must not be placed into another.
static void test_cross_project_rejected(const char *media_path)
{
	OakEngineProject *a = oakengine_project_create();
	OakEngineProject *b = oakengine_project_create();
	assert(a != NULL && b != NULL);
	assert(oakengine_project_new(a) == OAKENGINE_OK);
	assert(oakengine_project_new(b) == OAKENGINE_OK);

	OakEngineFootage *footage =
		oakengine_project_import_footage(a, media_path);
	assert(footage != NULL);

	OakEngineSequence *seq_b = oakengine_sequence_new(b, "B");
	assert(seq_b != NULL);
	assert(oakengine_sequence_add_track(seq_b, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);

	char err[512];
	assert(oakengine_sequence_add_footage_clip(seq_b, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	assert(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	assert(oakengine_sequence_clip_count(seq_b, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);

	oakengine_footage_free(footage);
	oakengine_project_free(a);
	oakengine_project_free(b);
}

int main(void)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	assert(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Edit");
	assert(seq != NULL);

	char path[4096];
	demo_path(path, sizeof(path));

	test_add_track(project, seq);
	test_add_clip(project, seq, path);
	test_cross_project_rejected(path);

	oakengine_project_free(project);
	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_timeline_edit_test: all assertions passed\n");
	return 0;
}
