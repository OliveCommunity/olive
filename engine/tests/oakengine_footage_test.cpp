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
#include "oakengine/project.h"

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

	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_footage_test: all assertions passed\n");
	return 0;
}
