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

// Pure C ABI test for the liboakengine footage facade. Probes the real
// media file tests/demo.mp4 (decoder, streams, durations, color tags),
// imports media into a project through the facade (including undo/redo),
// and covers the failure paths. No GL required.

#include <assert.h>
#include <math.h>
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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_footage_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_footage_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void demo_path(char *dst, size_t cap)
{
	const int n = snprintf(dst, cap, "%s/tests/demo.mp4", OAK_TEST_SOURCE_DIR);
	assert(n > 0 && (size_t)n < cap);
}

// tests/demo.mp4: 1920x1080 25 fps video (~17 s) + 48 kHz stereo audio,
// tagged BT.709 primaries and transfer (see codec_decoder_test.cpp).
static void test_probe(void)
{
	char path[4096];
	demo_path(path, sizeof(path));

	OakEngineFootage *f = oakengine_footage_probe(path);
	assert(f != NULL);

	char name[64];
	assert(oakengine_footage_get_decoder_name(f, name, sizeof(name)) > 0);
	assert(strcmp(name, "ffmpeg") == 0);

	double duration = 0.0;
	assert(oakengine_footage_get_duration(f, &duration) == OAKENGINE_OK);
	assert(fabs(duration - 17.0) < 0.5);

	assert(oakengine_footage_get_video_stream_count(f) == 1);
	assert(oakengine_footage_get_audio_stream_count(f) == 1);
	assert(oakengine_footage_get_subtitle_stream_count(f) == 0);

	oak_footage_video_info vi;
	memset(&vi, 0, sizeof(vi));
	assert(oakengine_footage_get_video_stream_info(f, 0, &vi) ==
		   OAKENGINE_OK);
	assert(vi.stream_index == 0);
	assert(vi.width == 1920 && vi.height == 1080);
	assert(vi.frame_rate_num == 25 && vi.frame_rate_den == 1);
	// Duration units make sense against the time base (17 s +/- 0.5).
	assert(vi.time_base_den > 0);
	const double vsecs =
		double(vi.duration_ts) * vi.time_base_num / vi.time_base_den;
	assert(fabs(vsecs - 17.0) < 0.5);
	assert(vi.color_primaries == 1); // BT.709
	assert(vi.color_trc == 1);
	assert(vi.interlaced == 0);

	oak_footage_audio_info ai;
	memset(&ai, 0, sizeof(ai));
	assert(oakengine_footage_get_audio_stream_info(f, 0, &ai) ==
		   OAKENGINE_OK);
	assert(ai.stream_index == 1);
	assert(ai.sample_rate == 48000);
	assert(ai.channel_count == 2);
	assert(ai.time_base_den > 0);
	const double asecs =
		double(ai.duration_ts) * ai.time_base_num / ai.time_base_den;
	assert(fabs(asecs - 17.0) < 0.5);

	assert(oakengine_footage_is_online(f) == 1);

	// demo.mp4 carries a timecode track reading 01:00:00:00 at 25 fps,
	// i.e. a source start time of 3600 seconds.
	int num = -1, den = -1;
	assert(oakengine_footage_get_source_start_time(f, &num, &den) == 1);
	assert(num == 3600 && den == 1);

	// Out-of-range stream indexes report OAKENGINE_E_NOT_FOUND.
	assert(oakengine_footage_get_video_stream_info(f, 1, &vi) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_get_audio_stream_info(f, 1, &ai) ==
		   OAKENGINE_E_NOT_FOUND);

	oakengine_footage_free(f);
}

static void test_import(void)
{
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	assert(oakengine_project_footage_count(project) == 0);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	// The import went into the project as an undoable command.
	assert(oakengine_project_footage_count(project) == 1);
	assert(oakengine_project_can_undo(project) == 1);
	assert(oakengine_project_is_modified(project) == 1);

	// The project's view of the footage matches the imported file.
	char filename[4096];
	assert(oakengine_project_footage_filename(project, 0, filename,
											  sizeof(filename)) > 0);
	assert(strcmp(filename, path) == 0);
	assert(oakengine_project_footage_is_online(project, 0) == 1);

	// The borrowed handle exposes the same probe information.
	assert(oakengine_footage_get_video_stream_count(f) == 1);
	assert(oakengine_footage_get_audio_stream_count(f) == 1);
	oak_footage_video_info vi;
	assert(oakengine_footage_get_video_stream_info(f, 0, &vi) ==
		   OAKENGINE_OK);
	assert(vi.width == 1920 && vi.height == 1080);
	char name[64];
	assert(oakengine_footage_get_decoder_name(f, name, sizeof(name)) > 0);
	assert(strcmp(name, "ffmpeg") == 0);
	assert(oakengine_footage_is_online(f) == 1);

	// Undo removes the footage from the project, redo brings it back.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_footage_count(project) == 0);
	assert(oakengine_project_can_redo(project) == 1);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_project_footage_count(project) == 1);

	// Releasing the borrowed handle frees only the wrapper.
	oakengine_footage_free(f);
	assert(oakengine_project_footage_count(project) == 1);

	oakengine_project_free(project);
}

static void test_failures(void)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/tests/definitely-not-there.mp4",
			 OAK_TEST_SOURCE_DIR);

	char err[512];
	assert(oakengine_footage_probe(path) == NULL);
	assert(oakengine_footage_last_error(err, sizeof(err)) > 0);

	// A non-media file is rejected by the probe.
	snprintf(path, sizeof(path), "%s/not-media.txt", g_tmpdir);
	FILE *txt = fopen(path, "w");
	assert(txt != NULL);
	fputs("this is not a media file\n", txt);
	fclose(txt);
	assert(oakengine_footage_probe(path) == NULL);
	assert(oakengine_footage_last_error(err, sizeof(err)) > 0);

	// Importing a missing file fails with a reason.
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	snprintf(path, sizeof(path), "%s/tests/definitely-not-there.mp4",
			 OAK_TEST_SOURCE_DIR);
	assert(oakengine_project_import_footage(project, path) == NULL);
	assert(oakengine_footage_last_error(err, sizeof(err)) > 0);
	assert(oakengine_project_footage_count(project) == 0);
	assert(oakengine_project_import_footage(NULL, path) == NULL);
	oakengine_project_free(project);

	// Borrowing nothing yields no handle.
	assert(oakengine_footage_borrow(NULL) == NULL);
	assert(oakengine_footage_last_error(err, sizeof(err)) > 0);

	// NULL safety.
	oakengine_footage_free(NULL);
	assert(oakengine_footage_get_decoder_name(NULL, err, sizeof(err)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_get_video_stream_count(NULL) == 0);
	assert(oakengine_footage_get_audio_stream_count(NULL) == 0);
	assert(oakengine_footage_get_subtitle_stream_count(NULL) == 0);
	oak_footage_video_info vi;
	assert(oakengine_footage_get_video_stream_info(NULL, 0, &vi) ==
		   OAKENGINE_E_INVALID);
	oak_footage_audio_info ai;
	assert(oakengine_footage_get_audio_stream_info(NULL, 0, &ai) ==
		   OAKENGINE_E_INVALID);
	double duration = 0.0;
	assert(oakengine_footage_get_duration(NULL, &duration) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_is_online(NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_footage_get_source_start_time(NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
}

// Copy a file in C (used to stage a deletable media copy).
static void copy_file(const char *src, const char *dst)
{
	FILE *in = fopen(src, "rb");
	assert(in != NULL);
	FILE *out = fopen(dst, "wb");
	assert(out != NULL);
	char chunk[65536];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		assert(fwrite(chunk, 1, n, out) == n);
	}
	fclose(out);
	fclose(in);
}

static void test_relink(void)
{
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char path[4096], img[4096], missing[4096], txt[4096];
	demo_path(path, sizeof(path));
	snprintf(img, sizeof(img), "%s/tests/img.png", OAK_TEST_SOURCE_DIR);
	snprintf(missing, sizeof(missing), "%s/tests/not-there.mp4",
			 OAK_TEST_SOURCE_DIR);
	snprintf(txt, sizeof(txt), "%s/not-media.txt", g_tmpdir);
	{
		FILE *f = fopen(txt, "w");
		assert(f != NULL);
		fputs("not media\n", f);
		fclose(f);
	}

	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	// Missing target, non-media target, probe handles, NULL.
	assert(oakengine_footage_relink(f, missing) == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_relink(f, txt) == OAKENGINE_E_FAILED);
	assert(oakengine_footage_relink(NULL, path) == OAKENGINE_E_INVALID);
	OakEngineFootage *probed = oakengine_footage_probe(path);
	assert(probed != NULL);
	assert(oakengine_footage_relink(probed, img) == OAKENGINE_E_INVALID);
	oakengine_footage_free(probed);

	// Relink to the same file: still valid, still online.
	assert(oakengine_footage_relink(f, path) == OAKENGINE_OK);
	assert(oakengine_footage_is_online(f) == 1);
	assert(oakengine_footage_get_video_stream_count(f) == 1);

	// Relink to a still image: decoder/filename update.
	assert(oakengine_footage_relink(f, img) == OAKENGINE_OK);
	char filename[4096];
	assert(oakengine_project_footage_filename(project, 0, filename,
											  sizeof(filename)) > 0);
	assert(strcmp(filename, img) == 0);
	char decoder[64];
	assert(oakengine_footage_get_decoder_name(f, decoder,
											  sizeof(decoder)) > 0);
	assert(strlen(decoder) > 0);
	assert(oakengine_footage_get_video_stream_count(f) >= 1);
	assert(oakengine_footage_is_online(f) == 1);

	// And back to the demo file.
	assert(oakengine_footage_relink(f, path) == OAKENGINE_OK);
	assert(oakengine_project_footage_filename(project, 0, filename,
											  sizeof(filename)) > 0);
	assert(strcmp(filename, path) == 0);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

static void test_find_offline(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	// Import a staged copy, then delete it so the footage goes offline.
	char staged[4096], tests_dir[4096];
	snprintf(staged, sizeof(staged), "%s/demo.mp4", g_tmpdir);
	snprintf(tests_dir, sizeof(tests_dir), "%s/tests", OAK_TEST_SOURCE_DIR);
	char path[4096];
	demo_path(path, sizeof(path));
	copy_file(path, staged);

	OakEngineFootage *f = oakengine_project_import_footage(project, staged);
	assert(f != NULL);
	assert(oakengine_project_footage_is_online(project, 0) == 1);
	assert(remove(staged) == 0);
	assert(oakengine_project_footage_is_online(project, 0) == 0);

	// The search directory has a file with the same name: relinked.
	assert(oakengine_project_find_offline_footage(project, tests_dir) == 1);
	assert(oakengine_project_footage_is_online(project, 0) == 1);
	char filename[4096];
	assert(oakengine_project_footage_filename(project, 0, filename,
											  sizeof(filename)) > 0);
	assert(strcmp(filename, path) == 0);

	// Nothing left to do; a missing search directory is an error.
	assert(oakengine_project_find_offline_footage(project, tests_dir) == 0);
	assert(oakengine_project_find_offline_footage(project,
												  "/definitely/not/here") ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_find_offline_footage(NULL, tests_dir) ==
		   OAKENGINE_E_INVALID);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

static void test_proxy(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	assert(oakengine_footage_proxy_get_state(f) == 0); // missing
	assert(oakengine_footage_proxy_get_state(NULL) == OAKENGINE_E_INVALID);

	// Generate synchronously (CPU transcode of the 17 s demo file).
	assert(oakengine_footage_proxy_generate(f) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_get_state(f) == 2); // ready
	char proxy_path[4096];
	assert(oakengine_footage_proxy_get_path(f, proxy_path,
											sizeof(proxy_path)) > 0);
	FILE *pf = fopen(proxy_path, "rb");
	assert(pf != NULL);
	fclose(pf);
	assert(oakengine_footage_proxy_is_enabled(f) == 1);

	// Enable/disable round-trip.
	assert(oakengine_footage_proxy_set_enabled(f, 0) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_is_enabled(f) == 0);
	assert(oakengine_footage_proxy_set_enabled(f, 1) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_is_enabled(f) == 1);

	// Delete: state and file are gone.
	assert(oakengine_footage_proxy_delete(f) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_get_state(f) == 0);
	assert(fopen(proxy_path, "rb") == NULL);

	// Error paths.
	assert(oakengine_footage_proxy_generate(NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_footage_proxy_delete(NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_footage_proxy_set_enabled(NULL, 1) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_proxy_get_path(NULL, proxy_path,
											sizeof(proxy_path)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_proxy_is_enabled(NULL) == OAKENGINE_E_INVALID);
	OakEngineFootage *probed = oakengine_footage_probe(path);
	assert(probed != NULL);
	assert(oakengine_footage_proxy_generate(probed) == OAKENGINE_E_INVALID);
	oakengine_footage_free(probed);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

static void test_stream_overrides(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	char cs[128];
	int range = -1, interlace = -1, premult = -1;

	// Defaults from the probe.
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_OK);
	assert(range == 0); // limited
	assert(interlace == 0); // progressive
	assert(premult == 0);

	// Full override write + read-back + undo/redo.
	assert(oakengine_footage_set_video_stream_overrides(
			   f, 0, "Rec.709 OETF", 1, 1, 1) == OAKENGINE_OK);
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_OK);
	assert(strcmp(cs, "Rec.709 OETF") == 0);
	assert(range == 1 && interlace == 1 && premult == 1);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_OK);
	assert(range == 0 && interlace == 0 && premult == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_OK);
	assert(range == 1 && interlace == 1 && premult == 1);

	// Partial write: NULL/-1 leaves fields alone.
	assert(oakengine_footage_set_video_stream_overrides(f, 0, NULL, 0, -1,
														-1) == OAKENGINE_OK);
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_OK);
	assert(range == 0 && interlace == 1 && premult == 1);

	// Pixel aspect ratio.
	int num = 0, den = 0;
	assert(oakengine_footage_get_pixel_aspect(f, 0, &num, &den) ==
		   OAKENGINE_OK);
	assert(num == 1 && den == 1);
	assert(oakengine_footage_set_pixel_aspect(f, 0, 4, 3) == OAKENGINE_OK);
	assert(oakengine_footage_get_pixel_aspect(f, 0, &num, &den) ==
		   OAKENGINE_OK);
	assert(num == 4 && den == 3);
	assert(oakengine_footage_set_pixel_aspect(f, 0, 0, 3) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_footage_get_pixel_aspect(f, 0, &num, &den) ==
		   OAKENGINE_OK);
	assert(num == 1 && den == 1);

	// Image-sequence parameters round-trip (stored even for non-sequence
	// footage; the dialog shows them only for image sequences).
	int64_t start = -1, dur = -1;
	assert(oakengine_footage_get_image_sequence_params(f, 0, &start, &dur,
													   &num, &den) ==
		   OAKENGINE_OK);
	assert(oakengine_footage_set_image_sequence_params(f, 0, 10, 100, 25,
													   1) == OAKENGINE_OK);
	assert(oakengine_footage_get_image_sequence_params(f, 0, &start, &dur,
													   &num, &den) ==
		   OAKENGINE_OK);
	assert(start == 10 && dur == 100 && num == 25 && den == 1);
	assert(oakengine_footage_set_image_sequence_params(f, 0, 0, 0, 25, 1) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);

	// Stream enable toggles.
	assert(oakengine_footage_get_stream_enabled(f, 0, 0) == 1);
	assert(oakengine_footage_set_stream_enabled(f, 0, 0, 0) == OAKENGINE_OK);
	assert(oakengine_footage_get_stream_enabled(f, 0, 0) == 0);
	assert(oakengine_footage_set_stream_enabled(f, 0, 0, 1) == OAKENGINE_OK);
	assert(oakengine_footage_get_stream_enabled(f, 0, 0) == 1);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_footage_get_stream_enabled(f, 0, 0) == 0);
	assert(oakengine_footage_get_stream_enabled(f, 1, 0) == 1);
	assert(oakengine_footage_get_stream_enabled(f, 0, 99) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_set_stream_enabled(f, 9, 0, 1) ==
		   OAKENGINE_E_INVALID);

	// Source start time set/clear with source label.
	int snum = 0, sden = 0;
	assert(oakengine_footage_get_source_start_time(f, &snum, &sden) == 1);
	char source[64];
	assert(oakengine_footage_set_source_start_time(f, 1, 42, 1) ==
		   OAKENGINE_OK);
	assert(oakengine_footage_get_source_start_time(f, &snum, &sden) == 1);
	assert(snum == 42 && sden == 1);
	assert(oakengine_footage_get_source_start_time_source(f, source,
														  sizeof(source)) > 0);
	assert(strcmp(source, "manual") == 0);
	assert(oakengine_footage_set_source_start_time(f, 0, 0, 1) ==
		   OAKENGINE_OK);
	assert(oakengine_footage_get_source_start_time(f, &snum, &sden) == 0);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_footage_get_source_start_time(f, &snum, &sden) == 1);
	assert(snum == 42);

	// Out-of-range and NULL.
	assert(oakengine_footage_get_video_stream_overrides(
			   f, 9, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_set_video_stream_overrides(f, 9, "x", 0, 0,
														0) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_get_pixel_aspect(f, 9, &num, &den) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_get_image_sequence_params(f, 9, &start, &dur,
													   &num, &den) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_get_video_stream_overrides(
			   NULL, 0, cs, sizeof(cs), &range, &interlace, &premult) ==
		   OAKENGINE_E_INVALID);
	OakEngineFootage *probed = oakengine_footage_probe(path);
	assert(probed != NULL);
	assert(oakengine_footage_set_video_stream_overrides(probed, 0, "x", 0,
														0, 0) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_colorspace_count(probed) == 0);
	oakengine_footage_free(probed);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

static void test_colorspace_candidates(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	const int count = oakengine_footage_colorspace_count(f);
	assert(count > 0);
	char name[128];
	int found_rec709 = 0;
	for (int i = 0; i < count; i++) {
		assert(oakengine_footage_colorspace_at(f, i, name, sizeof(name)) > 0);
		assert(strlen(name) > 0);
		if (strcmp(name, "Rec.709 OETF") == 0) {
			found_rec709 = 1;
		}
	}
	assert(found_rec709 == 1);
	assert(oakengine_footage_colorspace_at(f, count, name, sizeof(name)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_colorspace_at(f, -1, name, sizeof(name)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_colorspace_at(NULL, 0, name, sizeof(name)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_colorspace_count(NULL) == 0);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

// Project extras: filenames, cache paths, settings, MIME type, from_object.
static void test_project_extras(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char buf[4096];

	// Untitled project: pretty filename is the "(untitled)" placeholder.
	assert(oakengine_project_pretty_filename(project, buf, sizeof(buf)) > 0);
	assert(strlen(buf) > 0);

	// set_filename round-trips through the plain filename getter.
	char target[4096];
	snprintf(target, sizeof(target), "%s/roundtrip.ove", g_tmpdir);
	assert(oakengine_project_set_filename(project, target) == OAKENGINE_OK);
	assert(oakengine_project_filename(project, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, target) == 0);
	assert(oakengine_project_set_filename(project, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_set_filename(NULL, target) ==
		   OAKENGINE_E_INVALID);

	// With a filename set, the cache paths are derivable and non-empty.
	assert(oakengine_project_cache_path(project, buf, sizeof(buf)) > 0);
	assert(strlen(buf) > 0);
	assert(oakengine_project_cache_alongside_path(project, buf, sizeof(buf)) >
		   0);
	assert(strlen(buf) > 0);

	// Custom cache path setting round-trip (NULL clears).
	assert(oakengine_project_set_custom_cache_path(project, "/tmp/oakcache") ==
		   OAKENGINE_OK);
	assert(oakengine_project_get_custom_cache_path(project, buf,
												   sizeof(buf)) > 0);
	assert(strcmp(buf, "/tmp/oakcache") == 0);
	assert(oakengine_project_set_custom_cache_path(project, NULL) ==
		   OAKENGINE_OK);
	assert(oakengine_project_get_custom_cache_path(project, buf,
												   sizeof(buf)) == 0);

	// Color reference space setting round-trip.
	assert(oakengine_project_set_color_reference_space(
			   project, "Rec.709 OETF") == OAKENGINE_OK);
	assert(oakengine_project_get_color_reference_space(project, buf,
													   sizeof(buf)) > 0);
	assert(strcmp(buf, "Rec.709 OETF") == 0);
	assert(oakengine_project_set_color_reference_space(NULL, "x") ==
		   OAKENGINE_E_INVALID);

	// Cache location setting defaults to a valid enum value; NULL is invalid.
	assert(oakengine_project_get_cache_location_setting(project) >= 0);
	assert(oakengine_project_get_cache_location_setting(NULL) < 0);

	// The project item MIME type is a non-empty static string.
	const char *mime = oakengine_project_item_mime_type();
	assert(mime != NULL && strlen(mime) > 0);

	// from_object: the root node resolves back to its owning project.
	OakEngineNode *root = oakengine_project_node_at(project, 0);
	assert(root != NULL);
	assert(oakengine_project_from_object(root) == project);
	assert(oakengine_project_from_object(NULL) == NULL);

	// NULL safety.
	assert(oakengine_project_pretty_filename(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_cache_path(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_get_custom_cache_path(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_get_color_reference_space(NULL, buf,
													   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	oakengine_project_free(project);
}

// Folder creation and child queries.
static void test_folder(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	// A fresh project's first node is its root folder.
	OakEngineNode *root = oakengine_project_node_at(project, 0);
	assert(root != NULL);

	OakEngineNode *folder =
		oakengine_folder_create(project, root, "My Folder");
	assert(folder != NULL);
	assert(oakengine_folder_has_child_recursive(root, folder) == 1);
	assert(oakengine_folder_index_of_child(root, folder) >= 0);

	// A subfolder is found recursively from the root.
	OakEngineNode *sub = oakengine_folder_create(project, folder, "Sub");
	assert(sub != NULL);
	assert(oakengine_folder_has_child_recursive(root, sub) == 1);
	assert(oakengine_folder_has_child_recursive(folder, sub) == 1);
	assert(oakengine_folder_has_child_recursive(sub, folder) == 0);

	// A folder from another project is not a child here.
	OakEngineProject *other = oakengine_project_create();
	assert(other != NULL);
	assert(oakengine_project_new(other) == OAKENGINE_OK);
	OakEngineNode *other_root = oakengine_project_node_at(other, 0);
	assert(other_root != NULL);
	OakEngineNode *alien = oakengine_folder_create(other, other_root, "Alien");
	assert(alien != NULL);
	assert(oakengine_folder_has_child_recursive(root, alien) == 0);
	assert(oakengine_folder_index_of_child(root, alien) ==
		   OAKENGINE_E_NOT_FOUND);
	oakengine_project_free(other);

	// The child input key is a non-empty static string.
	const char *key = oakengine_folder_child_input_key();
	assert(key != NULL && strlen(key) > 0);

	// Error paths: non-folder parents, non-folder queries, NULL.
	assert(oakengine_folder_create(project, folder, NULL) != NULL);
	OakEngineNode *footage_node = NULL;
	{
		char path[4096];
		demo_path(path, sizeof(path));
		OakEngineFootage *f = oakengine_project_import_footage(project, path);
		assert(f != NULL);
		oakengine_footage_free(f);
		// The imported footage is a non-folder project node.
		for (int i = 0; i < oakengine_project_node_count(project); i++) {
			OakEngineNode *n = oakengine_project_node_at(project, i);
			char id[128];
			assert(oakengine_node_get_type_id(n, id, sizeof(id)) > 0);
			if (strcmp(id, "org.olivevideoeditor.Olive.folder") != 0) {
				footage_node = n;
				break;
			}
		}
		assert(footage_node != NULL);
	}
	assert(oakengine_folder_create(project, footage_node, "Nope") == NULL);
	assert(oakengine_folder_has_child_recursive(footage_node, folder) == 0);
	assert(oakengine_folder_index_of_child(footage_node, folder) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_folder_has_child_recursive(NULL, folder) == 0);
	assert(oakengine_folder_has_child_recursive(root, NULL) == 0);
	assert(oakengine_folder_index_of_child(NULL, folder) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_folder_index_of_child(root, NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_folder_create(NULL, root, "Nope") == NULL);

	oakengine_project_free(project);
}

// Footage extras: filename, stream references, descriptions, proxy params,
// manual proxy state and invalidation.
static void test_footage_extras(void)
{
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);

	char path[4096];
	demo_path(path, sizeof(path));
	OakEngineFootage *f = oakengine_project_import_footage(project, path);
	assert(f != NULL);

	char buf[4096];

	// Filename of the imported footage.
	assert(oakengine_footage_get_filename(f, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, path) == 0);
	assert(oakengine_footage_get_filename(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// Real stream index 0 is the video stream, 1 the audio stream.
	int track_type = -1, stream_index = -1;
	assert(oakengine_footage_get_stream_reference(f, 0, &track_type,
												  &stream_index) == OAKENGINE_OK);
	assert(track_type == OAKENGINE_TRACK_TYPE_VIDEO && stream_index == 0);
	assert(oakengine_footage_get_stream_reference(f, 1, &track_type,
												  &stream_index) == OAKENGINE_OK);
	assert(track_type == OAKENGINE_TRACK_TYPE_AUDIO && stream_index == 0);
	assert(oakengine_footage_get_stream_reference(f, 99, &track_type,
												  &stream_index) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_get_stream_reference(NULL, 0, &track_type,
												  &stream_index) ==
		   OAKENGINE_E_INVALID);

	// Stream descriptions.
	assert(oakengine_footage_describe_video_stream(f, 0, buf, sizeof(buf)) >
		   0);
	assert(strlen(buf) > 0);
	assert(oakengine_footage_describe_audio_stream(f, 0, buf, sizeof(buf)) >
		   0);
	assert(strlen(buf) > 0);
	assert(oakengine_footage_describe_video_stream(f, 9, buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_describe_audio_stream(f, 9, buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_footage_describe_video_stream(NULL, 0, buf,
												   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// Static stream type names (no handle needed).
	assert(oakengine_footage_stream_type_name(OAKENGINE_TRACK_TYPE_VIDEO, buf,
											  sizeof(buf)) > 0);
	assert(strlen(buf) > 0);
	assert(oakengine_footage_stream_type_name(OAKENGINE_TRACK_TYPE_AUDIO, buf,
											  sizeof(buf)) > 0);
	assert(strlen(buf) > 0);

	// Proxy params: effective defaults first, then a custom round-trip.
	assert(oakengine_footage_has_custom_proxy_params(f) == 0);
	oak_proxy_params params;
	memset(&params, 0, sizeof(params));
	assert(oakengine_footage_get_effective_proxy_params(f, &params) ==
		   OAKENGINE_OK);
	assert(params.width > 0 && params.height > 0);
	params.width = 640;
	params.height = 360;
	params.divider = 1;
	params.version = 1;
	params.crf = 30;
	params.include_audio = 0;
	strcpy(params.extension, "mkv");
	strcpy(params.preset, "slow");
	assert(oakengine_footage_set_custom_proxy_params(f, &params) ==
		   OAKENGINE_OK);
	assert(oakengine_footage_has_custom_proxy_params(f) == 1);
	oak_proxy_params back;
	memset(&back, 0, sizeof(back));
	assert(oakengine_footage_get_effective_proxy_params(f, &back) ==
		   OAKENGINE_OK);
	assert(back.width == 640 && back.height == 360 && back.crf == 30);
	assert(back.include_audio == 0);
	assert(strcmp(back.extension, "mkv") == 0);
	assert(strcmp(back.preset, "slow") == 0);
	assert(oakengine_footage_clear_custom_proxy_params(f) == OAKENGINE_OK);
	assert(oakengine_footage_has_custom_proxy_params(f) == 0);
	assert(oakengine_footage_set_custom_proxy_params(f, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_get_effective_proxy_params(f, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_has_custom_proxy_params(NULL) ==
		   OAKENGINE_E_INVALID);

	// Manual proxy state: set then clear (no file is created here).
	assert(oakengine_footage_set_proxy(f, "/tmp/fake_proxy.mp4", 2, 0, 1,
									   1) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_get_state(f) == 2);
	assert(oakengine_footage_proxy_get_path(f, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "/tmp/fake_proxy.mp4") == 0);
	assert(oakengine_footage_proxy_is_enabled(f) == 1);
	assert(oakengine_footage_clear_proxy(f) == OAKENGINE_OK);
	assert(oakengine_footage_proxy_get_state(f) == 0);
	assert(oakengine_footage_proxy_get_path(f, buf, sizeof(buf)) == 0);
	assert(oakengine_footage_set_proxy(NULL, "x", 2, 0, 1, 1) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_clear_proxy(NULL) == OAKENGINE_E_INVALID);

	// Cache invalidation after proxy/relink changes.
	assert(oakengine_footage_invalidate(f) == OAKENGINE_OK);
	assert(oakengine_footage_invalidate(NULL) == OAKENGINE_E_INVALID);

	// Probe handles carry no project node: the whole section rejects them.
	OakEngineFootage *probed = oakengine_footage_probe(path);
	assert(probed != NULL);
	assert(oakengine_footage_get_filename(probed, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_get_stream_reference(probed, 0, &track_type,
												  &stream_index) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_describe_video_stream(probed, 0, buf,
												   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_get_effective_proxy_params(probed, &params) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_footage_invalidate(probed) == OAKENGINE_E_INVALID);
	oakengine_footage_free(probed);

	oakengine_footage_free(f);
	oakengine_project_free(project);
}

int main(void)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test);
	// the probe metadata cache then lives in the temp dir.
#if !defined(_WIN32)
	assert(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_probe();
	test_import();
	test_failures();
	test_relink();
	test_find_offline();
	test_proxy();
	test_stream_overrides();
	test_colorspace_candidates();
	test_project_extras();
	test_folder();
	test_footage_extras();

	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_footage_test: all assertions passed\n");
	return 0;
}
