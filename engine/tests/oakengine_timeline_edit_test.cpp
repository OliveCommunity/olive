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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_tl_edit_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_tl_edit_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
}

static void test_add_track(OakEngineProject *project, OakEngineSequence *seq)
{
	int video = -1, audio = -1, subtitle = -1;

	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0 && subtitle == 0);

	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) == 0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_SUBTITLE) ==
		   0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 1);

	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 2 && audio == 1 && subtitle == 1);

	// Invalid track types are rejected.
	EXPECT_TRUE(oakengine_sequence_add_track(seq, -1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, 3) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_add_track(NULL, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   OAKENGINE_E_INVALID);

	// Track adds are undoable: undo the second video track, then redo it.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	video = audio = subtitle = -1;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 1 && audio == 1 && subtitle == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	video = audio = subtitle = -1;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 2 && audio == 1 && subtitle == 1);
}

static void test_add_clip(OakEngineProject *project, OakEngineSequence *seq,
						  const char *media_path)
{
	char err[512];

	// Probe handles carry no project node and cannot be placed.
	OakEngineFootage *probed = oakengine_footage_probe(media_path);
	EXPECT_TRUE(probed != NULL);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, probed,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	oakengine_footage_free(probed);

	// Import the media into the project.
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);

	// No subtitle clips.
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_SUBTITLE,
											   0, 0, 30, 0) == NULL);

	// Out-of-range track indexes are rejected without creating tracks.
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 5,
											   0, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, -1,
											   0, 30, 0) == NULL);

	// Bad ranges: out <= in, negative in / media_in.
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   30, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   30, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   -1, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, -1) == NULL);

	// NULL safety.
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(NULL, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq, NULL,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);

	// clip_count reports OAKENGINE_E_NOT_FOUND for a missing track.
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 99) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_sequence_clip_count(NULL, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == OAKENGINE_E_INVALID);

	// Place a video clip on track 0: frames 10..40, media in-point 5.
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 40, 5);
	EXPECT_TRUE(clip != NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	OakEngineClip *at = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(at == clip);
	EXPECT_TRUE(oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  1) == NULL);
	EXPECT_TRUE(oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 9,
									  0) == NULL);

	int64_t in = -1, out = -1, media_in = -1;
	EXPECT_TRUE(oakengine_clip_get_range(clip, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 10 && out == 40 && media_in == 5);
	EXPECT_TRUE(oakengine_clip_get_range(NULL, &in, &out, &media_in) ==
		   OAKENGINE_E_INVALID);

	// And an audio clip on audio track 0 (same footage, whole range).
	OakEngineClip *aclip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, 30, 0);
	EXPECT_TRUE(aclip != NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 1);

	// Undo/redo both clip adds (the audio clip is on top of the undo stack,
	// the video clip right below it).
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_AUDIO,
										 0) == 1);

	oakengine_footage_free(footage); // wrapper only; node stays
	EXPECT_TRUE(oakengine_project_footage_count(project) == 1);
}

// Footage imported into one project must not be placed into another.
static void test_cross_project_rejected(const char *media_path)
{
	OakEngineProject *a = oakengine_project_create();
	OakEngineProject *b = oakengine_project_create();
	EXPECT_TRUE(a != NULL && b != NULL);
	EXPECT_TRUE(oakengine_project_new(a) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_new(b) == OAKENGINE_OK);

	OakEngineFootage *footage =
		oakengine_project_import_footage(a, media_path);
	EXPECT_TRUE(footage != NULL);

	OakEngineSequence *seq_b = oakengine_sequence_new(b, "B");
	EXPECT_TRUE(seq_b != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq_b, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);

	char err[512];
	EXPECT_TRUE(oakengine_sequence_add_footage_clip(seq_b, footage,
											   OAKENGINE_TRACK_TYPE_VIDEO, 0,
											   0, 30, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq_b, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);

	oakengine_footage_free(footage);
	oakengine_project_free(a);
	oakengine_project_free(b);
}

// Round-2 primitives: split, trim, move, ripple delete (plus undo/redo),
// over a fresh project with two clips on one video track.
static void test_edit_round2(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Round2");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);

	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);

	// Two clips back to back: A [0,30] and B [30,60], both from media 0.
	OakEngineClip *clip_a = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 30, 0);
	EXPECT_TRUE(clip_a != NULL);
	OakEngineClip *clip_b = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 30, 60, 0);
	EXPECT_TRUE(clip_b != NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	int64_t in = -1, out = -1, media_in = -1;
	char err[512];

	// ---- split -------------------------------------------------------------
	// Bad split points: on the edges, outside, missing clip.
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 30) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, -5) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 99) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 99, 15) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_sequence_split_clip(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 15) == OAKENGINE_E_INVALID);

	// Split A [0,30] at 15 -> [0,15] + [15,30] with aligned media in-points.
	EXPECT_TRUE(oakengine_sequence_split_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 15) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 3);
	OakEngineClip *left =
		oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	OakEngineClip *right =
		oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(left == clip_a);
	EXPECT_TRUE(oakengine_clip_get_range(left, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 15 && media_in == 0);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 15 && out == 30 && media_in == 15);

	// Undo merges them back, redo splits again.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);
	EXPECT_TRUE(oakengine_clip_get_range(clip_a, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 30 && media_in == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 3);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 15 && out == 30 && media_in == 15);

	// ---- trim --------------------------------------------------------------
	// Bad trim ranges.
	EXPECT_TRUE(oakengine_clip_trim(NULL, 0, 10) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_trim(right, 30, 20) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_trim(right, -1, 20) == OAKENGINE_E_INVALID);

	// Trim the right part's in-point 15 -> 20: media_in follows 15 -> 20.
	EXPECT_TRUE(oakengine_clip_trim(right, 20, 30) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30 && media_in == 20);
	// The leading clip is untouched (a gap absorbs the difference).
	EXPECT_TRUE(oakengine_clip_get_range(clip_a, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 15 && media_in == 0);

	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 15 && out == 30 && media_in == 15);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30 && media_in == 20);

	// Trim the out-point 30 -> 25 (media in-point stays).
	EXPECT_TRUE(oakengine_clip_trim(right, 20, 25) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 25 && media_in == 20);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30 && media_in == 20);

	// No-op trim is accepted and does nothing.
	EXPECT_TRUE(oakengine_clip_trim(right, 20, 30) == OAKENGINE_OK);

	// Both ends in one command: [20,30] -> [22,28], media_in follows.
	EXPECT_TRUE(oakengine_clip_trim(right, 22, 28) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 22 && out == 28 && media_in == 22);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30 && media_in == 20);

	// ---- move ---------------------------------------------------------------
	EXPECT_TRUE(oakengine_sequence_move_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1,
										-1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_move_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										99, 50) == OAKENGINE_E_NOT_FOUND);

	// Move the second clip [20,30] to 70 (past everything): position shifts,
	// length and media in-point are preserved, the other clips stay put.
	EXPECT_TRUE(oakengine_sequence_move_clip(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1,
										70) == OAKENGINE_OK);
	// After the move the moved clip is the LAST one on the track.
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 2);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 70 && out == 80 && media_in == 20);
	EXPECT_TRUE(oakengine_clip_get_range(clip_a, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 15 && media_in == 0);
	EXPECT_TRUE(oakengine_clip_get_range(clip_b, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 30 && out == 60 && media_in == 0);

	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30 && media_in == 20);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 2);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 70 && out == 80 && media_in == 20);

	// ---- ripple delete -------------------------------------------------------
	EXPECT_TRUE(oakengine_sequence_ripple_delete_clip(
			   seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 99) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_clip(NULL,
												 OAKENGINE_TRACK_TYPE_VIDEO, 0,
												 0) == OAKENGINE_E_INVALID);

	// Delete the first clip [0,15]: both following clips shift left by 15.
	EXPECT_TRUE(oakengine_sequence_ripple_delete_clip(
			   seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 15 && out == 45 && media_in == 0); // clip_b was [30,60]
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 55 && out == 65 && media_in == 20); // was [70,80]

	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 3);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 15 && media_in == 0);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 30 && out == 60 && media_in == 0);
	right = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 2);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 70 && out == 80 && media_in == 20);

	oakengine_footage_free(footage); // wrapper only; node stays
	EXPECT_TRUE(oakengine_project_footage_count(project) == 1);
}

// Footage imported into one project must not be placed into another.

// Track structure: move/height/mute/lock/remove over two video tracks,
// plus undo/redo of the undoable ones.
static void test_track_structure(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Tracks");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   1);

	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);
	// Distinctive clips per track so track order is observable.
	OakEngineClip *clip_a = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0);
	OakEngineClip *clip_b = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 1, 20, 30, 0);
	EXPECT_TRUE(clip_a != NULL && clip_b != NULL);

	int64_t in = -1, out = -1, media_in = -1;
	double height = 0.0;

	// move_track swaps their positions; the clips move with the tracks.
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 1) == OAKENGINE_OK);
	OakEngineClip *now = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(now == clip_b);
	EXPECT_TRUE(oakengine_clip_get_range(now, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 30);
	now = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 1, 0);
	EXPECT_TRUE(now == clip_a);
	EXPECT_TRUE(oakengine_clip_get_range(now, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 10);

	// Undo/redo restore the order both ways.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	now = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(now == clip_a);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	now = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(now == clip_b);

	// No-op move and out-of-range indexes.
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 1,
										 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 5) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_sequence_move_track(seq, OAKENGINE_TRACK_TYPE_VIDEO, 5,
										 0) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_sequence_move_track(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 1) == OAKENGINE_E_INVALID);

	// Height: default is positive, round-trip, direct (not undoable).
	EXPECT_TRUE(oakengine_track_get_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  &height) == OAKENGINE_OK);
	const double default_height = height;
	EXPECT_TRUE(default_height > 0.0);
	EXPECT_TRUE(oakengine_track_set_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  3.0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_track_get_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  &height) == OAKENGINE_OK);
	EXPECT_TRUE(height == 3.0);
	EXPECT_TRUE(oakengine_track_set_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  0.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_track_get_height(seq, OAKENGINE_TRACK_TYPE_VIDEO, 99,
									  &height) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_track_get_height(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  &height) == OAKENGINE_E_INVALID);

	// Mute and lock toggles, direct (not undoable).
	EXPECT_TRUE(oakengine_track_is_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0) == 0);
	EXPECT_TRUE(oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_track_is_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0) == 1);
	EXPECT_TRUE(oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_track_is_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0) == 0);
	EXPECT_TRUE(oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 99,
									 1) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_track_is_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0) ==
		   0);
	EXPECT_TRUE(oakengine_track_set_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_track_is_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0) ==
		   1);
	EXPECT_TRUE(oakengine_track_set_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_track_set_locked(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  1) == OAKENGINE_E_INVALID);

	// remove_track drops the track and its content; undo brings both back.
	int video = -1, audio = -1, subtitle = -1;
	EXPECT_TRUE(oakengine_sequence_remove_track(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										   1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 1);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_sequence_remove_track(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										   9) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 2);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 1) == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 1);

	oakengine_footage_free(footage);
	oakengine_project_free(project);
}

static void test_markers(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Markers");
	EXPECT_TRUE(seq != NULL);

	int64_t ts = -1;
	char name[64];

	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_marker_add(seq, 30, "Chapter 1") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 1);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 0, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 30 && strcmp(name, "Chapter 1") == 0);

	// Duplicate time: the engine forbids two markers at one time.
	EXPECT_TRUE(oakengine_sequence_marker_add(seq, 30, "dup") ==
		   OAKENGINE_E_STATE);

	// Earlier marker sorts in front.
	EXPECT_TRUE(oakengine_sequence_marker_add(seq, 10, "Intro") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 2);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 0, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 10 && strcmp(name, "Intro") == 0);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 1, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 30 && strcmp(name, "Chapter 1") == 0);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 2, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_E_NOT_FOUND);

	// Rename with undo/redo.
	EXPECT_TRUE(oakengine_sequence_marker_rename(seq, 30, "Chapter 2") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 1, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(strcmp(name, "Chapter 2") == 0);
	EXPECT_TRUE(oakengine_sequence_marker_rename(seq, 77, "nope") ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 1, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(strcmp(name, "Chapter 1") == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 1, &ts, name, sizeof(name), NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(strcmp(name, "Chapter 2") == 0);

	// Remove with undo; removing again reports not found.
	EXPECT_TRUE(oakengine_sequence_marker_remove(seq, 10) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 1);
	EXPECT_TRUE(oakengine_sequence_marker_remove(seq, 10) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 2);

	// NULL safety.
	EXPECT_TRUE(oakengine_sequence_marker_add(NULL, 0, "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_marker_remove(NULL, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_marker_rename(NULL, 0, "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_remove_track(NULL, OAKENGINE_TRACK_TYPE_VIDEO,
										   0) == OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

static void test_sequence_params(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Params");
	EXPECT_TRUE(seq != NULL);

	int w = 0, h = 0, fn = 0, fd = 0, pn = 0, pd = 0, il = -1, fmt = -1,
		div = 0;
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	const int base_w = w, base_h = h, base_div = div;
	EXPECT_TRUE(base_w > 0 && base_h > 0 && fn > 0 && fd > 0 && div >= 1);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(NULL, &w, &h, &fn, &fd,
												  &pn, &pd, &il, &fmt,
												  &div) == OAKENGINE_E_INVALID);

	// Full video parameter write (f32 = 4, bottom-first = 2) + readback.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, 1280, 720, 24, 1, 1, 1, 2,
											   4, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == 1280 && h == 720 && fn == 24 && fd == 1);
	EXPECT_TRUE(pn == 1 && pd == 1 && il == 2 && fmt == 4 && div == base_div);
	int gn = 0, gd = 0;
	EXPECT_TRUE(oakengine_sequence_get_frame_rate(seq, &gn, &gd) == OAKENGINE_OK);
	EXPECT_TRUE(gn == 24 && gd == 1);

	// Partial update: -1 leaves the other fields unchanged.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, 640, -1, -1, -1, -1, -1,
											   -1, -1, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == 640 && h == 720 && fn == 24 && il == 2);

	// Invalid values: zero size, half a rational pair, out-of-range enum.
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, 0, 720, -1, -1, -1, -1,
											   -1, -1,
											   1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, -1, -1, 24, -1, -1, -1,
											   -1, -1,
											   1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, -1, -1, -1, -1, -1, -1, 3,
											   -1,
											   1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_video_params(seq, -1, -1, -1, -1, -1, -1,
											   -1, 5,
											   1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_video_params(NULL, 1, 1, -1, -1, -1, -1, -1,
											   -1,
											   1) == OAKENGINE_E_INVALID);

	// Undo restores step by step (partial write, then full write).
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == 1280 && h == 720);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == base_w && h == base_h);

	// Audio round trip; 0 / <= 0 leaves the field unchanged.
	int sr = 0;
	uint64_t layout = 0;
	EXPECT_TRUE(oakengine_sequence_get_audio_params(seq, &sr, &layout) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(sr > 0 && layout != 0);
	const int base_sr = sr;
	const uint64_t base_layout = layout;
	EXPECT_TRUE(oakengine_sequence_set_audio_params(seq, 44100, 3, 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_audio_params(seq, &sr, &layout) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(sr == 44100 && layout == 3);
	EXPECT_TRUE(oakengine_sequence_set_audio_params(seq, 48000, 0, 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_audio_params(seq, &sr, &layout) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(sr == 48000 && layout == 3);
	EXPECT_TRUE(oakengine_sequence_set_audio_params(NULL, 48000, 3, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_audio_params(NULL, &sr, &layout) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_audio_params(seq, &sr, &layout) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(sr == base_sr && layout == base_layout);

	// Preview divider; the other video params stay untouched.
	EXPECT_TRUE(oakengine_sequence_get_preview_divider(seq) == base_div);
	EXPECT_TRUE(oakengine_sequence_set_preview_divider(seq, 4, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_preview_divider(seq) == 4);
	EXPECT_TRUE(oakengine_sequence_set_preview_divider(seq, 0, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_preview_divider(NULL, 1, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_preview_divider(NULL) == 0);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(seq, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == base_w && h == base_h && div == 4);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_preview_divider(seq) == base_div);

	// Auto-cache flag: the engine accessors are stubs (read always 0, write
	// ignored), so there is nothing to undo here.
	EXPECT_TRUE(oakengine_sequence_get_video_auto_cache(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_set_video_auto_cache(seq, 1, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_auto_cache(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_get_video_auto_cache(NULL) == 0);
	EXPECT_TRUE(oakengine_sequence_set_video_auto_cache(NULL, 1, 1) ==
		   OAKENGINE_E_INVALID);

	// Label through the node family with the undoable flag.
	EXPECT_TRUE(oakengine_node_set_label_ex((OakEngineNode *)seq, "Renamed", 1) ==
		   OAKENGINE_OK);
	char name[64];
	EXPECT_TRUE(oakengine_sequence_name(seq, name, sizeof(name)) > 0);
	EXPECT_TRUE(strcmp(name, "Renamed") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_name(seq, name, sizeof(name)) > 0);
	EXPECT_TRUE(strcmp(name, "Params") == 0);

	oakengine_project_free(project);

	// undoable == 0 applies directly with no undo entry: on a fresh project
	// the creation is undone first so the stack is empty (the node stays
	// alive under its creation command).
	OakEngineProject *p2 = oakengine_project_create();
	EXPECT_TRUE(p2 != NULL);
	EXPECT_TRUE(oakengine_project_new(p2) == OAKENGINE_OK);
	OakEngineSequence *s2 = oakengine_sequence_new(p2, "Direct");
	EXPECT_TRUE(s2 != NULL);
	EXPECT_TRUE(oakengine_project_undo(p2) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_can_undo(p2) == 0);
	EXPECT_TRUE(oakengine_sequence_set_video_params(s2, 800, 600, -1, -1, -1, -1,
											   -1, -1,
											   0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_video_params_ex(s2, &w, &h, &fn, &fd, &pn,
												  &pd, &il, &fmt,
												  &div) == OAKENGINE_OK);
	EXPECT_TRUE(w == 800 && h == 600);
	EXPECT_TRUE(oakengine_project_can_undo(p2) == 0);
	EXPECT_TRUE(oakengine_sequence_set_audio_params(s2, 22050, 0, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_audio_params(s2, &sr, &layout) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(sr == 22050);
	EXPECT_TRUE(oakengine_project_can_undo(p2) == 0);
	EXPECT_TRUE(oakengine_sequence_set_preview_divider(s2, 2, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_get_preview_divider(s2) == 2);
	EXPECT_TRUE(oakengine_project_can_undo(p2) == 0);
	EXPECT_TRUE(oakengine_node_set_label_ex((OakEngineNode *)s2, "NoUndo", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_name(s2, name, sizeof(name)) > 0);
	EXPECT_TRUE(strcmp(name, "NoUndo") == 0);
	EXPECT_TRUE(oakengine_project_can_undo(p2) == 0);
	oakengine_project_free(p2);
}

static void test_batch_editing(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Batch");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);

	int64_t in = -1, out = -1;

	// Two clips on the video track: [0, 100) and [100, 160).
	OakEngineClip *c0 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 100, 0);
	OakEngineClip *c1 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 100, 160, 0);
	EXPECT_TRUE(c0 != NULL && c1 != NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	// split_clips at 50: the first clip splits in two.
	{
		OakEngineClip *arr[1] = { c0 };
		EXPECT_TRUE(oakengine_sequence_split_clips(seq, arr, 1, 50) ==
			   OAKENGINE_OK);
	}
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 3);
	OakEngineClip *left = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	OakEngineClip *right = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(left, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 50);
	EXPECT_TRUE(oakengine_clip_get_range(right, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 50 && out == 100);

	// Nothing spans 200; invalid arrays are rejected.
	{
		OakEngineClip *arr[2] = { left, right };
		EXPECT_TRUE(oakengine_sequence_split_clips(seq, arr, 2, 200) ==
			   OAKENGINE_E_NOT_FOUND);
		EXPECT_TRUE(oakengine_sequence_split_clips(seq, NULL, 1, 50) ==
			   OAKENGINE_E_INVALID);
		EXPECT_TRUE(oakengine_sequence_split_clips(seq, arr, 0, 50) ==
			   OAKENGINE_E_INVALID);
		EXPECT_TRUE(oakengine_sequence_split_clips(NULL, arr, 1, 50) ==
			   OAKENGINE_E_INVALID);
	}

	// Undo the split.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	// delete_clips without ripple: a gap is left, the second clip stays.
	int rippled = -1;
	{
		OakEngineClip *arr[1] = { c0 };
		EXPECT_TRUE(oakengine_sequence_delete_clips(seq, arr, 1, 0, NULL, 0,
											   &rippled) == OAKENGINE_OK);
	}
	EXPECT_TRUE(rippled == 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 100 && out == 160);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	// delete_clips with ripple (auto ranges): the second clip shifts left.
	rippled = -1;
	{
		OakEngineClip *arr[1] = { c0 };
		EXPECT_TRUE(oakengine_sequence_delete_clips(seq, arr, 1, 1, NULL, 0,
											   &rippled) == OAKENGINE_OK);
	}
	EXPECT_TRUE(rippled == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 60);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 100 && out == 160);

	// delete_clips with an explicit ripple range: same shift.
	rippled = -1;
	{
		OakEngineClip *arr[1] = { c0 };
		const int64_t ranges[4] = { OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 100 };
		EXPECT_TRUE(oakengine_sequence_delete_clips(seq, arr, 1, 1, ranges, 1,
											   &rippled) == OAKENGINE_OK);
	}
	EXPECT_TRUE(rippled == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 60);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	// A bad explicit range coordinate is rejected without side effects.
	{
		OakEngineClip *arr[1] = { c0 };
		const int64_t bad[4] = { OAKENGINE_TRACK_TYPE_VIDEO, 9, 0, 100 };
		EXPECT_TRUE(oakengine_sequence_delete_clips(seq, arr, 1, 1, bad, 1,
											   NULL) == OAKENGINE_E_NOT_FOUND);
	}
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);
	EXPECT_TRUE(oakengine_sequence_delete_clips(NULL, NULL, 0, 0, NULL, 0,
										   NULL) == OAKENGINE_E_INVALID);

	// ripple_delete_range over [0, 100): the area is removed from every
	// track and the following content shifts left.
	EXPECT_TRUE(oakengine_sequence_ripple_delete_range(seq, 0, 100) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	OakEngineClip *remaining = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_TRUE(oakengine_clip_get_range(remaining, &in, &out, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 60);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_range(seq, 100, 100) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_range(seq, -1, 5) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_range(NULL, 0, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);

	// Marker with an explicit color.
	EXPECT_TRUE(oakengine_sequence_marker_add_ex(seq, 10, "colored", 5) ==
		   OAKENGINE_OK);
	int color = -1;
	int64_t ts = -1;
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 0, &ts, NULL, 0, &color) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 10 && color == 5);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 0);

	oakengine_footage_free(footage);
	oakengine_project_free(project);
}

static void test_batch_editing_round2(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Batch2");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);

	int64_t in = -1, out = -1;

	OakEngineClip *c0 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 100, 0);
	EXPECT_TRUE(c0 != NULL);

	// trim_clips_to: out-edge to 60, then in-edge to 40 (one clip each).
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 1, 60) == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 60);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 100);
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 0, 40) == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 40 && out == 100);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);

	// Already at the point: nothing qualifies, 0 and no command.
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 1, 100) == 0);
	// Past the content there is no block under the point either (the
	// application's edit_to only shortens blocks containing the point).
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 1, 200) == 0);
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 0, 200) == 0);
	// Invalid arguments.
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 2, 60) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(seq, 1, -1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_trim_clips_to(NULL, 1, 60) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 100);

	// ripple_delete_in_to_out (ripple): area removed, workarea disabled,
	// one undo entry restores everything including the workarea.
	EXPECT_TRUE(oakengine_sequence_set_workarea(seq, 1, 20, 80) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 1);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_in_to_out(seq, 1, 20, 80) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);
	OakEngineClip *piece0 = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	OakEngineClip *piece1 = oakengine_sequence_clip_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 1);
	EXPECT_TRUE(oakengine_clip_get_range(piece0, &in, &out, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 20);
	EXPECT_TRUE(oakengine_clip_get_range(piece1, &in, &out, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 20 && out == 40);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 1);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 100);

	// ripple_delete_in_to_out (no ripple): a gap fills the area, later
	// content shifts right by the range length.
	EXPECT_TRUE(oakengine_sequence_ripple_delete_in_to_out(seq, 0, 20, 80) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 2);
	piece0 = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										0);
	piece1 = oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										1);
	EXPECT_TRUE(oakengine_clip_get_range(piece0, &in, &out, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 0 && out == 20);
	EXPECT_TRUE(oakengine_clip_get_range(piece1, &in, &out, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 80 && out == 100);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);

	// Workarea disabled: rejected; bad range: invalid (range is validated
	// before the workarea state).
	EXPECT_TRUE(oakengine_sequence_set_workarea(seq, 0, 20, 80) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_in_to_out(seq, 1, 20, 80) ==
		   OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_in_to_out(seq, 1, 80, 20) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_ripple_delete_in_to_out(NULL, 1, 20, 80) ==
		   OAKENGINE_E_INVALID);

	// delete_empty_tracks: add an empty audio and an empty subtitle track.
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_SUBTITLE) ==
		   0);
	int v = 0, a = 0, s = 0;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &v, &a, &s) == OAKENGINE_OK);
	EXPECT_TRUE(v == 1 && a == 1 && s == 1);
	// Type-filtered purge first: only the audio track goes.
	EXPECT_TRUE(oakengine_sequence_delete_empty_tracks(
			   seq, OAKENGINE_TRACK_TYPE_AUDIO) == 1);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &v, &a, &s) == OAKENGINE_OK);
	EXPECT_TRUE(v == 1 && a == 0 && s == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &v, &a, &s) == OAKENGINE_OK);
	EXPECT_TRUE(v == 1 && a == 1 && s == 1);
	// All types at once (the application's behavior).
	EXPECT_TRUE(oakengine_sequence_delete_empty_tracks(seq, -1) == 2);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &v, &a, &s) == OAKENGINE_OK);
	EXPECT_TRUE(v == 1 && a == 0 && s == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &v, &a, &s) == OAKENGINE_OK);
	EXPECT_TRUE(v == 1 && a == 1 && s == 1);
	// Nothing empty: 0, no command; invalid type rejected.
	EXPECT_TRUE(oakengine_sequence_delete_empty_tracks(
			   seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);
	EXPECT_TRUE(oakengine_sequence_delete_empty_tracks(seq, 3) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_delete_empty_tracks(NULL, -1) ==
		   OAKENGINE_E_INVALID);

	// marker_remove_many: batch delete by time, one undo entry.
	EXPECT_TRUE(oakengine_sequence_marker_add_ex(seq, 10, "m1", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_add_ex(seq, 20, "m2", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_add_ex(seq, 30, "m3", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 3);
	{
		const int64_t times[2] = { 10, 30 };
		EXPECT_TRUE(oakengine_sequence_marker_remove_many(seq, times, 2) == 2);
	}
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 3);
	{
		const int64_t bad[1] = { 99 };
		EXPECT_TRUE(oakengine_sequence_marker_remove_many(seq, bad, 1) ==
			   OAKENGINE_E_NOT_FOUND);
	}
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 3);
	EXPECT_TRUE(oakengine_sequence_marker_remove_many(seq, NULL, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_marker_remove_many(seq, NULL, 0) == 0);
	EXPECT_TRUE(oakengine_sequence_marker_remove_many(NULL, NULL, 0) ==
		   OAKENGINE_E_INVALID);

	oakengine_footage_free(footage);
	oakengine_project_free(project);
}

static void test_batch_editing_round3(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Batch3");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);

	int64_t in = -1, out = -1;

	// Two adjacent clips: [0, 60) and [60, 100).
	OakEngineClip *c0 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 60, 0);
	OakEngineClip *c1 = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 60, 100, 0);
	EXPECT_TRUE(c0 != NULL && c1 != NULL);
	OakEngineClip *two[2] = { c0, c1 };

	// toggle_enabled flips each clip in one undoable command.
	EXPECT_TRUE(oakengine_clip_is_enabled(c0) == 1 &&
		   oakengine_clip_is_enabled(c1) == 1);
	EXPECT_TRUE(oakengine_clip_toggle_enabled(two, 2) == 2);
	EXPECT_TRUE(oakengine_clip_is_enabled(c0) == 0 &&
		   oakengine_clip_is_enabled(c1) == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_is_enabled(c0) == 1 &&
		   oakengine_clip_is_enabled(c1) == 1);
	EXPECT_TRUE(oakengine_clip_toggle_enabled(NULL, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_toggle_enabled(two, 0) == 0);
	EXPECT_TRUE(oakengine_clip_is_enabled(NULL) == 0);

	// set_linked: link and unlink are one undoable command each.
	EXPECT_TRUE(oakengine_clip_are_linked(c0, c1) == 0);
	EXPECT_TRUE(oakengine_clip_set_linked(two, 2, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_are_linked(c0, c1) == 1);
	EXPECT_TRUE(oakengine_clip_set_linked(two, 2, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_are_linked(c0, c1) == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_are_linked(c0, c1) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_are_linked(c0, c1) == 0);
	EXPECT_TRUE(oakengine_clip_are_linked(c0, NULL) == 0);
	EXPECT_TRUE(oakengine_clip_set_linked(NULL, 1, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_clip_set_linked(two, 0, 1) == OAKENGINE_OK);

	// add_default_transition: an in-transition before c0, a dual between
	// the adjacent pair and an out-transition after c1 shrink both clips;
	// one undo restores the exact ranges.
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	const int64_t c0_in = in, c0_len = out - in;
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	const int64_t c1_len = out - in;
	EXPECT_TRUE(oakengine_sequence_add_default_transition(seq, two, 2) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in > c0_in && out - in < c0_len);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(out - in < c1_len);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(c0, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in == c0_in && out - in == c0_len);
	EXPECT_TRUE(oakengine_clip_get_range(c1, &in, &out, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(out - in == c1_len);
	EXPECT_TRUE(oakengine_sequence_add_default_transition(seq, NULL, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_add_default_transition(NULL, two, 2) ==
		   OAKENGINE_E_INVALID);

	oakengine_footage_free(footage);
	oakengine_project_free(project);
}

static void test_sequence_clip(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Outer");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	OakEngineSequence *nested = oakengine_sequence_new(project, "Nested");
	EXPECT_TRUE(nested != NULL);

	int64_t in = -1, out = -1, media_in = -1;

	// Place the nested sequence as a clip; undo/redo ride the stack.
	OakEngineClip *clip = oakengine_sequence_add_sequence_clip(
		seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 40, 5);
	EXPECT_TRUE(clip != NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_sequence_clip_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  0) == clip);
	EXPECT_TRUE(oakengine_clip_get_range(clip, &in, &out, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in == 10 && out == 40 && media_in == 5);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);

	// A sequence cannot nest into itself or into a sequence that
	// (indirectly) receives it: place Outer into Nested first, then
	// placing Nested into Outer must be refused, all without side
	// effects.
	EXPECT_TRUE(oakengine_sequence_add_track(nested, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   nested, seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) != NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) == NULL);
	char err[256];
	EXPECT_TRUE(oakengine_sequence_last_error(err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);

	// Validation: cross-project, bad track type/index and bad ranges are
	// all rejected without side effects.
	OakEngineProject *other = oakengine_project_create();
	EXPECT_TRUE(other != NULL);
	EXPECT_TRUE(oakengine_project_new(other) == OAKENGINE_OK);
	OakEngineSequence *foreign = oakengine_sequence_new(other, "Foreign");
	EXPECT_TRUE(foreign != NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, foreign, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) == NULL);
	oakengine_project_free(other);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_SUBTITLE, 0, 0, 10,
			   0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 5, 0, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, -1, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, -1) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   NULL, nested, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_add_sequence_clip(
			   seq, NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 10, 0) == NULL);
	EXPECT_TRUE(oakengine_sequence_clip_count(seq, OAKENGINE_TRACK_TYPE_VIDEO,
										 0) == 0);

	oakengine_project_free(project);
}

// Track queries: oakengine_track_type / oakengine_track_get_length /
// oakengine_track_is_range_free / oakengine_track_height_interval /
// oakengine_track_height_minimum.
static void test_track_queries(const char *media_path)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Queries");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   0);

	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	EXPECT_TRUE(footage != NULL);
	// Clip at [10, 20) on the video track.
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 10, 20, 0);
	EXPECT_TRUE(clip != NULL);

	// Type through the opaque handle.
	EXPECT_TRUE(oakengine_track_type(NULL) == -1);
	OakEngineTrack *vtrack = oakengine_sequence_track_at(
		seq, OAKENGINE_TRACK_TYPE_VIDEO, 0);
	OakEngineTrack *atrack = oakengine_sequence_track_at(
		seq, OAKENGINE_TRACK_TYPE_AUDIO, 0);
	EXPECT_TRUE(vtrack != NULL && atrack != NULL);
	EXPECT_TRUE(oakengine_track_type(vtrack) == OAKENGINE_TRACK_TYPE_VIDEO);
	EXPECT_TRUE(oakengine_track_type(atrack) == OAKENGINE_TRACK_TYPE_AUDIO);
	EXPECT_TRUE(oakengine_sequence_track_at(seq, OAKENGINE_TRACK_TYPE_VIDEO, 5) ==
		   NULL);
	EXPECT_TRUE(oakengine_sequence_track_at(NULL, OAKENGINE_TRACK_TYPE_VIDEO,
									   0) == NULL);

	// Length: the video track ends at 20, the empty audio track at 0.
	int64_t length = -1;
	EXPECT_TRUE(oakengine_track_get_length(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  &length) == OAKENGINE_OK);
	EXPECT_TRUE(length == 20);
	EXPECT_TRUE(oakengine_track_get_length(seq, OAKENGINE_TRACK_TYPE_AUDIO, 0,
									  &length) == OAKENGINE_OK);
	EXPECT_TRUE(length == 0);
	EXPECT_TRUE(oakengine_track_get_length(seq, OAKENGINE_TRACK_TYPE_VIDEO, 5,
									  &length) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_track_get_length(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
									  &length) == OAKENGINE_E_INVALID);

	// Range free: [0, 10) and [20, 30) are free, [15, 25) intersects the
	// clip, a zero-length probe at 15 also intersects.
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 10) == 1);
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 20, 30) == 1);
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 15, 25) == 0);
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_AUDIO, 0,
										 15, 25) == 1);
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_VIDEO, 5,
										 0, 10) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_track_is_range_free(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 10, 5) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_track_is_range_free(NULL, OAKENGINE_TRACK_TYPE_VIDEO, 0,
										 0, 10) == OAKENGINE_E_INVALID);

	// Height constants are positive (minimum 1.5, interval 0.5 in the
	// engine; only positivity is contract-level).
	EXPECT_TRUE(oakengine_track_height_interval() > 0.0);
	EXPECT_TRUE(oakengine_track_height_minimum() > 0.0);

	oakengine_project_free(project);
}

// ---- Marker handle family (B4c) ----------------------------------------------

static void test_marker_handle_family(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "MarkerHandles");
	EXPECT_TRUE(seq != NULL);

	OakEngineMarkerList *list =
		oakengine_viewer_get_marker_list((OakEngineNode *)seq);
	EXPECT_TRUE(list != NULL);
	EXPECT_TRUE(oakengine_viewer_get_marker_list(NULL) == NULL);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 0);
	EXPECT_TRUE(oakengine_marker_list_count(NULL) == 0);

	// Add two markers (rational seconds) through the list family.
	EXPECT_TRUE(oakengine_marker_list_add(list, 4, 1, 6, 1, "Out", 2) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_add(list, 1, 1, 2, 1, "In", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 2);

	// Sorted by time: index 0 is the 1s marker.
	OakEngineMarker *m0 = oakengine_marker_list_at(list, 0);
	OakEngineMarker *m1 = oakengine_marker_list_at(list, 1);
	EXPECT_TRUE(m0 != NULL && m1 != NULL && m0 != m1);
	EXPECT_TRUE(oakengine_marker_list_at(list, 2) == NULL);
	EXPECT_TRUE(oakengine_marker_list_at(list, -1) == NULL);

	int64_t in_num = 0, in_den = 0, out_num = 0, out_den = 0;
	EXPECT_TRUE(oakengine_marker_get_time(m0, &in_num, &in_den, &out_num,
									 &out_den) == OAKENGINE_OK);
	EXPECT_TRUE(in_num == 1 && in_den == 1 && out_num == 2 && out_den == 1);
	char name[64];
	EXPECT_TRUE(oakengine_marker_get_name(m0, name, sizeof(name)) == 2);
	EXPECT_TRUE(strcmp(name, "In") == 0);
	EXPECT_TRUE(oakengine_marker_get_color(m0) == 0);
	EXPECT_TRUE(oakengine_marker_get_color(m1) == 2);

	// Lookup by exact in-point.
	EXPECT_TRUE(oakengine_marker_list_marker_at_time(list, 4, 1) == m1);
	EXPECT_TRUE(oakengine_marker_list_marker_at_time(list, 5, 1) == NULL);

	// Sibling check: m0 has a sibling at 4s (m1), none at 3s.
	EXPECT_TRUE(oakengine_marker_has_sibling_at_time(m0, 4, 1) == 1);
	EXPECT_TRUE(oakengine_marker_has_sibling_at_time(m0, 3, 1) == 0);

	// Live (non-undo) resize, then the undoable commit with the old range.
	EXPECT_TRUE(oakengine_marker_set_time_live(m0, 1, 1, 3, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_time(m0, NULL, NULL, &out_num, &out_den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(out_num == 3 && out_den == 1);
	EXPECT_TRUE(oakengine_marker_commit_time(m0, 1, 1, 3, 1, 1, 1, 2, 1,
										NULL) == OAKENGINE_OK);
	// Undo restores the pre-commit (live) state is NOT reverted (the live
	// edit was already applied; undo goes back to the old range).
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_time(m0, NULL, NULL, &out_num, &out_den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(out_num == 2 && out_den == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_time(m0, NULL, NULL, &out_num, &out_den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(out_num == 3 && out_den == 1);

	// Detached marker creation (used by the UI before adding to a list).
	OakEngineMarker *detached = oakengine_marker_create(3, 5, 1, 7, 1,
													"Detached");
	EXPECT_TRUE(detached != NULL);
	EXPECT_TRUE(oakengine_marker_get_color(detached) == 3);
	EXPECT_TRUE(oakengine_marker_get_name(detached, name, sizeof(name)) == 8);
	EXPECT_TRUE(strcmp(name, "Detached") == 0);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 2);
	oakengine_marker_free(detached);

	// Batch properties: recolor + rename both markers as ONE undo entry.
	OakEngineMarker *both[2] = { m0, m1 };
	EXPECT_TRUE(oakengine_marker_set_properties(both, 2, 7, "Same", 0, 0, 0, 0,
										   0, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_color(m0) == 7);
	EXPECT_TRUE(oakengine_marker_get_color(m1) == 7);
	EXPECT_TRUE(oakengine_marker_get_name(m0, name, sizeof(name)) >= 0);
	EXPECT_TRUE(strcmp(name, "Same") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_color(m0) == 0);
	EXPECT_TRUE(oakengine_marker_get_color(m1) == 2);
	EXPECT_TRUE(oakengine_marker_get_name(m0, name, sizeof(name)) >= 0);
	EXPECT_TRUE(strcmp(name, "In") == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	// Single-marker time move through the same batch call.
	EXPECT_TRUE(oakengine_marker_set_properties(both, 1, -1, NULL, 1, 10, 1, 12,
										   1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_get_time(m0, &in_num, NULL, &out_num, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in_num == 10 && out_num == 12);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);

	// Remove with undo.
	EXPECT_TRUE(oakengine_marker_remove(m1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 2);

	// NULL safety.
	EXPECT_TRUE(oakengine_marker_get_time(NULL, &in_num, NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_get_name(NULL, name, sizeof(name)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_get_color(NULL) == -1);
	EXPECT_TRUE(oakengine_marker_has_sibling_at_time(NULL, 1, 1) == 0);
	EXPECT_TRUE(oakengine_marker_set_time_live(NULL, 0, 1, 1, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_list_add(NULL, 0, 1, 1, 1, "x", 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_remove(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_set_properties(NULL, 1, 0, NULL, 0, 0, 0, 0, 0,
										   NULL) == OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

// ---- Workarea handle family (B4c) ----------------------------------------------

static void test_workarea_handle_family(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Workarea");
	EXPECT_TRUE(seq != NULL);

	// Reset sentinels: k_reset_in is 0, k_reset_out is RATIONAL_MAX.
	int64_t ri_num = 0, ri_den = 0, ro_num = 0, ro_den = 0;
	oakengine_workarea_reset_in_out(&ri_num, &ri_den, &ro_num, &ro_den);
	EXPECT_TRUE(ri_num == 0 && ri_den > 0);
	EXPECT_TRUE(ro_num > 0 && ro_den > 0);

	OakEngineWorkarea *wa =
		oakengine_viewer_get_workarea_handle((OakEngineNode *)seq);
	EXPECT_TRUE(wa != NULL);
	EXPECT_TRUE(oakengine_viewer_get_workarea_handle(NULL) == NULL);

	// A fresh workarea is disabled.
	int enabled = -1;
	EXPECT_TRUE(oakengine_workarea_get(wa, NULL, NULL, NULL, NULL, &enabled) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(enabled == 0);

	// Undoable enable + range change.
	EXPECT_TRUE(oakengine_workarea_set_enabled_undoable(wa, 1, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_workarea_get(wa, NULL, NULL, NULL, NULL, &enabled) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(enabled == 1);
	EXPECT_TRUE(oakengine_workarea_set_range_undoable(wa, 1, 2, 3, 2, ri_num,
												 ri_den, ro_num, ro_den,
												 NULL) == OAKENGINE_OK);
	int64_t in_num = 0, in_den = 0, out_num = 0, out_den = 0;
	EXPECT_TRUE(oakengine_workarea_get(wa, &in_num, &in_den, &out_num, &out_den,
								  NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in_num == 1 && in_den == 2 && out_num == 3 && out_den == 2);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_workarea_get(wa, &in_num, NULL, &out_num, NULL,
								  NULL) == OAKENGINE_OK);
	EXPECT_TRUE(in_num == ri_num && out_num == ro_num);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_workarea_get(wa, NULL, NULL, NULL, NULL, &enabled) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(enabled == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	// Standalone workarea: enabled-undoable degrades to a direct apply
	// (no project owns it).
	OakEngineWorkarea *over = oakengine_workarea_create();
	EXPECT_TRUE(over != NULL);
	EXPECT_TRUE(oakengine_workarea_set_enabled_undoable(over, 1, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_workarea_get(over, NULL, NULL, NULL, NULL, &enabled) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(enabled == 1);
	EXPECT_TRUE(oakengine_workarea_set_range(over, 0, 1, 5, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_workarea_get(over, NULL, NULL, &out_num, &out_den,
								  NULL) == OAKENGINE_OK);
	EXPECT_TRUE(out_num == 5 && out_den == 1);
	oakengine_workarea_free(over);

	// NULL safety.
	EXPECT_TRUE(oakengine_workarea_get(NULL, &in_num, NULL, NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_workarea_set_range(NULL, 0, 1, 1, 1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_workarea_set_enabled(NULL, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_workarea_set_range_undoable(NULL, 0, 1, 1, 1, 0, 1, 1,
												 1, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_workarea_set_enabled_undoable(NULL, 1, NULL) ==
		   OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

// ---- Clip input ids / media in / cache (B4c) -----------------------------------

static void test_clip_input_ids_and_media(void)
{
	// Input id statics: non-null, distinct, and stable across calls.
	const char *ids[] = { oakengine_clip_buffer_input_id(),
						  oakengine_clip_speed_input_id(),
						  oakengine_clip_reverse_input_id(),
						  oakengine_clip_maintain_audio_pitch_input_id(),
						  oakengine_clip_loop_mode_input_id(),
						  oakengine_clip_auto_cache_input_id() };
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		EXPECT_TRUE(ids[i] != NULL && ids[i][0] != '\0');
		for (size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); j++) {
			EXPECT_TRUE(strcmp(ids[i], ids[j]) != 0);
		}
	}
	EXPECT_TRUE(strcmp(oakengine_clip_speed_input_id(), ids[1]) == 0);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "ClipMedia");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, path);
	EXPECT_TRUE(footage != NULL);
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 30, 0);
	EXPECT_TRUE(clip != NULL);

	// Media in-point: read via the range getter, write undoably.
	int64_t media_in = -1;
	EXPECT_TRUE(oakengine_clip_get_range(clip, NULL, NULL, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(media_in == 0);
	EXPECT_TRUE(oakengine_clip_set_media_in(clip, 5, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(clip, NULL, NULL, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(media_in == 5);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(clip, NULL, NULL, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(media_in == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	// Non-undoable mode applies directly.
	EXPECT_TRUE(oakengine_clip_set_media_in(clip, 2, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_range(clip, NULL, NULL, &media_in) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(media_in == 2);

	// Cache entry points: smoke calls (headless, no caches to speak of).
	oakengine_clip_request_invalidate(clip, 0, 0, 0);
	oakengine_clip_request_invalidate(clip, 1, 0, 30);
	oakengine_clip_add_cache_passthrough(clip, clip);
	oakengine_clip_discard_cache(clip);

	// NULL safety.
	EXPECT_TRUE(oakengine_clip_set_media_in(NULL, 0, 1) == OAKENGINE_E_INVALID);
	oakengine_clip_request_invalidate(NULL, 0, 0, 0);
	oakengine_clip_add_cache_passthrough(NULL, clip);
	oakengine_clip_add_cache_passthrough(clip, NULL);
	oakengine_clip_discard_cache(NULL);
	EXPECT_TRUE(oakengine_block_is_enabled(NULL) == 0);
	EXPECT_TRUE(oakengine_block_is_enabled((OakEngineBlock *)clip) == 1);

	oakengine_footage_free(footage);
	oakengine_project_free(project);
}

// ---- Track height helpers / default nodes (B4c) --------------------------------

static void test_track_height_helpers(void)
{
	EXPECT_TRUE(oakengine_track_height_default() > 0.0);
	// Round-trip through the pixel conversion.
	const int px = oakengine_track_default_height_in_pixels();
	EXPECT_TRUE(px > 0);
	EXPECT_TRUE(oakengine_track_height_internal_to_pixels(
			   oakengine_track_height_pixels_to_internal(px)) == px);
	EXPECT_TRUE(oakengine_track_height_internal_to_pixels(
			   oakengine_track_height_default()) == px);
}

static void test_add_default_nodes(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Defaults");
	EXPECT_TRUE(seq != NULL);

	int video = -1, audio = -1;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0);

	// Adds one video + one audio track as ONE undo entry.
	EXPECT_TRUE(oakengine_sequence_add_default_nodes(seq) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 1 && audio == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 1 && audio == 1);

	EXPECT_TRUE(oakengine_sequence_add_default_nodes(NULL) ==
		   OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

// ---- clip_get_media_range_rational ------------------------------------------

static void test_clip_get_media_range_rational(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "MediaRange");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) == 0);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, path);
	EXPECT_TRUE(footage != NULL);

	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 30, 0);
	EXPECT_TRUE(clip != NULL);

	// NULL handle.
	EXPECT_TRUE(oakengine_clip_get_media_range_rational(NULL, NULL, NULL, NULL,
												   NULL) ==
		   OAKENGINE_E_INVALID);

	// Valid clip: media range should have a non-zero duration.
	int64_t in_num = -1, in_den = -1, out_num = -1, out_den = -1;
	EXPECT_TRUE(oakengine_clip_get_media_range_rational(clip, &in_num, &in_den,
												   &out_num, &out_den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(in_num >= 0 && in_den > 0 && out_num > in_num);

	// Partial output pointers (any may be NULL).
	EXPECT_TRUE(oakengine_clip_get_media_range_rational(clip, NULL, &in_den,
												   &out_num, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_clip_get_media_range_rational(clip, NULL, NULL, NULL,
												   NULL) == OAKENGINE_OK);

	oakengine_project_free(project);
}

// ---- clip_find_multicam / multicam_switch_source (basic) --------------------

static void test_multicam_basic(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);

	// clip_find_multicam: NULL clip returns NULL.
	EXPECT_TRUE(oakengine_clip_find_multicam(NULL) == NULL);

	// A non-clip node (Solid) returns NULL.
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);
	EXPECT_TRUE(oakengine_clip_find_multicam(solid) == NULL);

	// multicam_switch_source: NULL args.
	EXPECT_TRUE(oakengine_multicam_switch_source(NULL, NULL, 0, 0, 0.0, NULL) ==
		   OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	oakengine_project_free(project);
}

// ---- marker_list_add_existing -----------------------------------------------

static void test_marker_list_add_existing(void)
{
	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "MarkerAdopt");
	EXPECT_TRUE(seq != NULL);

	OakEngineMarkerList *list =
		oakengine_viewer_get_marker_list((OakEngineNode *)seq);
	EXPECT_TRUE(list != NULL);

	// Add a marker to the list, get its handle.
	EXPECT_TRUE(oakengine_marker_list_add(list, 0, 1, 2, 1, "Test", 0) ==
		   OAKENGINE_OK);
	OakEngineMarker *marker = oakengine_marker_list_at(list, 0);
	EXPECT_TRUE(marker != NULL);

	// Remove it from the list.
	EXPECT_TRUE(oakengine_marker_remove(marker) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 0);

	// add_existing to re-add it.
	EXPECT_TRUE(oakengine_marker_list_add_existing(list, marker) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_marker_list_count(list) == 1);

	// NULL list or marker.
	EXPECT_TRUE(oakengine_marker_list_add_existing(NULL, marker) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_marker_list_add_existing(list, NULL) ==
		   OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

TEST(OakEngineTimelineEdit, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Edit");
	EXPECT_TRUE(seq != NULL);

	char path[4096];
	demo_path(path, sizeof(path));

	test_add_track(project, seq);
	test_add_clip(project, seq, path);
	test_cross_project_rejected(path);
	test_edit_round2(path);
	test_track_structure(path);
	test_markers();
	test_sequence_params();
	test_batch_editing(path);
	test_batch_editing_round2(path);
	test_batch_editing_round3(path);
	test_sequence_clip();
	test_track_queries(path);
	test_marker_handle_family();
	test_workarea_handle_family();
	test_clip_input_ids_and_media();
	test_track_height_helpers();
	test_add_default_nodes();
	test_clip_get_media_range_rational();
	test_multicam_basic();
	test_marker_list_add_existing();

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
