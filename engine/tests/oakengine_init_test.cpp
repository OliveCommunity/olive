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

// Pure C ABI test for the liboakengine init/project/timeline facade.
// Exercises engine init/shutdown, project create/new/save/load round-trips,
// the global undo stack, sequence inspection (length, frame rate, tracks,
// playhead, workarea, markers) and a fixture project with real footage
// (tests/demo.mp4). No Qt, no GL: oakengine_init() creates the
// QCoreApplication itself when needed.

#include <assert.h>
#include <gtest/gtest.h>
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
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_init_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_init_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void make_path(char *dst, size_t cap, const char *filename)
{
	const int n = snprintf(dst, cap, "%s/%s", g_tmpdir, filename);
	EXPECT_TRUE(n > 0 && (size_t)n < cap);
}

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		return 0;
	}
	fclose(f);
	return 1;
}

static void test_init(void)
{
	EXPECT_TRUE(oakengine_init(0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_init(4) == OAKENGINE_E_INVALID); // unknown bit

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() == OAKENGINE_INIT_HEADLESS);

	// Repeated init is idempotent.
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() == OAKENGINE_INIT_HEADLESS);
}

static void test_project_basics(void)
{
	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);

	// Fresh shell: untitled, unmodified, empty.
	EXPECT_TRUE(oakengine_project_is_modified(p) == 0);
	EXPECT_TRUE(oakengine_project_footage_count(p) == 0);
	EXPECT_TRUE(oakengine_project_sequence_count(p) == 0);
	EXPECT_TRUE(oakengine_project_can_undo(p) == 0);
	EXPECT_TRUE(oakengine_project_can_redo(p) == 0);

	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);
	// Content setup may only happen once.
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_E_STATE);

	// An untitled project reports the engine's "(untitled)" name and an
	// empty filename.
	char name[64];
	const int name_len = oakengine_project_name(p, NULL, 0);
	EXPECT_TRUE(name_len > 0);
	EXPECT_TRUE(oakengine_project_name(p, name, sizeof(name)) == name_len);
	EXPECT_TRUE((int)strlen(name) == name_len);
	EXPECT_TRUE(strcmp(name, "(untitled)") == 0);
	// buf/size truncation: reports the full size, writes what fits.
	char tiny[4];
	EXPECT_TRUE(oakengine_project_name(p, tiny, sizeof(tiny)) == name_len);
	EXPECT_TRUE(strlen(tiny) == sizeof(tiny) - 1);

	char filename[16];
	EXPECT_TRUE(oakengine_project_filename(p, filename, sizeof(filename)) == 0);
	EXPECT_TRUE(filename[0] == '\0');

	// Modified flag round-trips.
	EXPECT_TRUE(oakengine_project_set_modified(p, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_is_modified(p) == 1);
	EXPECT_TRUE(oakengine_project_set_modified(p, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_is_modified(p) == 0);

	// Saving an untitled project without a path fails.
	EXPECT_TRUE(oakengine_project_save(p, NULL) == OAKENGINE_E_INVALID);

	oakengine_project_free(p);
}

static void test_sequence_and_save_load(void)
{
	char path[4096];
	make_path(path, sizeof(path), "roundtrip.ove");

	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);

	// Create a sequence through the facade; it is owned by the project.
	OakEngineSequence *seq = oakengine_sequence_new(p, "Main");
	EXPECT_TRUE(seq != NULL);
	EXPECT_TRUE(oakengine_project_sequence_count(p) == 1);
	EXPECT_TRUE(oakengine_project_sequence_at(p, 0) == seq);
	EXPECT_TRUE(oakengine_project_sequence_at(p, 1) == NULL);
	EXPECT_TRUE(oakengine_project_sequence_at(p, -1) == NULL);

	// The creation went onto the global undo stack and marked the project.
	EXPECT_TRUE(oakengine_project_can_undo(p) == 1);
	EXPECT_TRUE(oakengine_project_is_modified(p) == 1);

	// Sequence name round-trips.
	char name[64];
	const int name_len = oakengine_sequence_name(seq, NULL, 0);
	EXPECT_TRUE(name_len == 4);
	EXPECT_TRUE(oakengine_sequence_name(seq, name, sizeof(name)) == 4);
	EXPECT_TRUE(strcmp(name, "Main") == 0);

	// Empty sequence: zero length.
	double seconds = -1.0;
	EXPECT_TRUE(oakengine_sequence_get_length(seq, &seconds) == OAKENGINE_OK);
	EXPECT_TRUE(seconds == 0.0);
	int num = -1, den = -1;
	EXPECT_TRUE(oakengine_sequence_get_length_rational(seq, &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 0);

	// Default frame rate from Config's DefaultSequenceFrameRate entry
	// (time base 1001/30000 -> frame rate 30000/1001).
	num = den = -1;
	EXPECT_TRUE(oakengine_sequence_get_frame_rate(seq, &num, &den) == OAKENGINE_OK);
	EXPECT_TRUE(num == 30000 && den == 1001);

	// Default video dimensions (Config's DefaultSequenceWidth/Height) and
	// square pixels
	int width = -1, height = -1, par_num = -1, par_den = -1;
	EXPECT_TRUE(oakengine_sequence_get_video_params(seq, &width, &height, &par_num,
											   &par_den) == OAKENGINE_OK);
	EXPECT_TRUE(width == 1920 && height == 1080);
	EXPECT_TRUE(par_num == 1 && par_den == 1);
	EXPECT_TRUE(oakengine_sequence_get_video_params(NULL, NULL, NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);

	// Fresh sequences have no tracks.
	int video = -1, audio = -1, subtitle = -1;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0 && subtitle == 0);

	// Playhead starts at zero and accepts frame timestamps.
	int64_t ts = -1;
	EXPECT_TRUE(oakengine_sequence_get_playhead(seq, &ts) == OAKENGINE_OK);
	EXPECT_TRUE(ts == 0);
	EXPECT_TRUE(oakengine_sequence_set_playhead(seq, 30) == OAKENGINE_OK);
	ts = -1;
	EXPECT_TRUE(oakengine_sequence_get_playhead(seq, &ts) == OAKENGINE_OK);
	EXPECT_TRUE(ts == 30);
	// 30 frames at 1001/30000 per frame = 1.001 seconds.
	seconds = -1.0;
	EXPECT_TRUE(oakengine_sequence_get_playhead_seconds(seq, &seconds) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(seconds - 1.001) < 1e-9);

	// Workarea: disabled by default, then set + read back.
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_set_workarea(seq, 1, 10, 40) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(seq) == 1);
	int64_t in = -1, out = -1;
	EXPECT_TRUE(oakengine_sequence_get_workarea(seq, &in, &out) == OAKENGINE_OK);
	EXPECT_TRUE(in == 10 && out == 40);

	// No markers on a fresh sequence.
	EXPECT_TRUE(oakengine_sequence_marker_count(seq) == 0);
	EXPECT_TRUE(oakengine_sequence_marker_at(seq, 0, NULL, NULL, 0, NULL) ==
		   OAKENGINE_E_NOT_FOUND);

	// Save; the modified flag clears and the filename is adopted.
	EXPECT_TRUE(oakengine_project_save(p, path) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_is_modified(p) == 0);
	EXPECT_TRUE(file_exists(path));
	char filename[4096];
	EXPECT_TRUE(oakengine_project_filename(p, filename, sizeof(filename)) ==
		   (int)strlen(path));
	EXPECT_TRUE(strcmp(filename, path) == 0);

	// Undo removes the sequence, redo brings the same object back.
	EXPECT_TRUE(oakengine_project_undo(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_sequence_count(p) == 0);
	EXPECT_TRUE(oakengine_project_can_redo(p) == 1);
	EXPECT_TRUE(oakengine_project_redo(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_sequence_count(p) == 1);
	EXPECT_TRUE(oakengine_project_sequence_at(p, 0) == seq);
	oakengine_project_free(p);

	// Load the saved file into a fresh project.
	OakEngineProject *q = oakengine_project_create();
	EXPECT_TRUE(q != NULL);
	char err[512];
	EXPECT_TRUE(oakengine_project_load(q, path, err, sizeof(err)) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_is_modified(q) == 0);
	EXPECT_TRUE(oakengine_project_can_undo(q) == 0); // load clears the undo stack
	EXPECT_TRUE(oakengine_project_can_redo(q) == 0);

	// Project name derives from the file's base name.
	const int qname_len = oakengine_project_name(q, NULL, 0);
	EXPECT_TRUE(qname_len == (int)strlen("roundtrip"));
	EXPECT_TRUE(oakengine_project_name(q, name, sizeof(name)) == qname_len);
	EXPECT_TRUE(strcmp(name, "roundtrip") == 0);
	EXPECT_TRUE(oakengine_project_filename(q, filename, sizeof(filename)) ==
		   (int)strlen(path));
	EXPECT_TRUE(strcmp(filename, path) == 0);

	// The sequence survived the round trip, workarea included (the workarea
	// is serialized by ViewerOutput::save_custom()).
	EXPECT_TRUE(oakengine_project_sequence_count(q) == 1);
	OakEngineSequence *loaded = oakengine_project_sequence_at(q, 0);
	EXPECT_TRUE(loaded != NULL);
	EXPECT_TRUE(oakengine_sequence_name(loaded, name, sizeof(name)) == 4);
	EXPECT_TRUE(strcmp(name, "Main") == 0);
	num = den = -1;
	EXPECT_TRUE(oakengine_sequence_get_frame_rate(loaded, &num, &den) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(num == 30000 && den == 1001);
	EXPECT_TRUE(oakengine_sequence_track_count(loaded, &video, &audio,
										  &subtitle) == OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0 && subtitle == 0);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(loaded) == 1);
	in = out = -1;
	EXPECT_TRUE(oakengine_sequence_get_workarea(loaded, &in, &out) == OAKENGINE_OK);
	EXPECT_TRUE(in == 10 && out == 40);
	EXPECT_TRUE(oakengine_project_footage_count(q) == 0);

	// Saving with a NULL path reuses the project's current filename.
	EXPECT_TRUE(oakengine_project_save(q, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(file_exists(path));
	oakengine_project_free(q);

	// Loading a missing file fails and reports a human-readable reason.
	OakEngineProject *r = oakengine_project_create();
	EXPECT_TRUE(r != NULL);
	char missing[4096];
	make_path(missing, sizeof(missing), "does-not-exist.ove");
	EXPECT_TRUE(oakengine_project_load(r, missing, err, sizeof(err)) ==
		   OAKENGINE_E_FAILED);
	EXPECT_TRUE(strlen(err) > 0);
	// Loading into a project that already has content is rejected.
	EXPECT_TRUE(oakengine_project_new(r) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_load(r, missing, err, sizeof(err)) ==
		   OAKENGINE_E_STATE);
	oakengine_project_free(r);
}

// tests/project_with_footage.ove is a small uncompressed project holding one
// footage item whose stored filename is the relative path "demo.mp4" (the
// real media file sits next to the fixture in tests/).
static void test_footage_fixture(void)
{
	char fixture[4096];
	const int n = snprintf(fixture, sizeof(fixture),
						   "%s/tests/project_with_footage.ove",
						   OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(n > 0 && (size_t)n < sizeof(fixture));
	EXPECT_TRUE(file_exists(fixture));

	// The fixture stores footage as the relative path "demo.mp4". Run the
	// load from the fixture directory so the engine does not treat the
	// project as "moved" and rewrite the stored filename to an absolute
	// path (see EngineCore footage relocation on load).
	char fixture_dir[4096];
	snprintf(fixture_dir, sizeof(fixture_dir), "%s/tests", OAK_TEST_SOURCE_DIR);
	EXPECT_TRUE(chdir(fixture_dir) == 0);

	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	char err[512];
	EXPECT_TRUE(oakengine_project_load(p, fixture, err, sizeof(err)) ==
		   OAKENGINE_OK);

	EXPECT_TRUE(oakengine_project_footage_count(p) == 1);
	char filename[256];
	EXPECT_TRUE(oakengine_project_footage_filename(p, 0, filename,
											  sizeof(filename)) > 0);
	EXPECT_TRUE(strcmp(filename, "demo.mp4") == 0);
	// The stored relative path resolves against the project file's
	// directory (tests/), where demo.mp4 exists.
	EXPECT_TRUE(oakengine_project_footage_is_online(p, 0) == 1);
	// Out-of-range footage indexes report OAKENGINE_E_NOT_FOUND.
	EXPECT_TRUE(oakengine_project_footage_filename(p, 1, filename,
											  sizeof(filename)) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_project_footage_is_online(p, -1) ==
		   OAKENGINE_E_NOT_FOUND);

	EXPECT_TRUE(oakengine_project_sequence_count(p) == 1);
	OakEngineSequence *seq = oakengine_project_sequence_at(p, 0);
	EXPECT_TRUE(seq != NULL);
	char name[64];
	EXPECT_TRUE(oakengine_sequence_name(seq, name, sizeof(name)) > 0);
	EXPECT_TRUE(strcmp(name, "Fixture Sequence") == 0);

	oakengine_project_free(p);
}

static void test_null_safety(void)
{
	oakengine_project_free(NULL);
	EXPECT_TRUE(oakengine_project_new(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_load(NULL, "/tmp/x.ove", NULL, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_save(NULL, "/tmp/x.ove") == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_is_modified(NULL) == 0);
	EXPECT_TRUE(oakengine_project_set_modified(NULL, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_name(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_filename(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_footage_count(NULL) == 0);
	EXPECT_TRUE(oakengine_project_footage_filename(NULL, 0, NULL, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_footage_is_online(NULL, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_can_undo(NULL) == 0);
	EXPECT_TRUE(oakengine_project_can_redo(NULL) == 0);
	EXPECT_TRUE(oakengine_project_undo(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_redo(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_sequence_count(NULL) == 0);
	EXPECT_TRUE(oakengine_project_sequence_at(NULL, 0) == NULL);

	EXPECT_TRUE(oakengine_sequence_new(NULL, "x") == NULL);
	EXPECT_TRUE(oakengine_sequence_name(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_length(NULL, NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_length_rational(NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_frame_rate(NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_track_count(NULL, NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_playhead(NULL, NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_playhead(NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_get_playhead_seconds(NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_workarea_is_enabled(NULL) == 0);
	EXPECT_TRUE(oakengine_sequence_get_workarea(NULL, NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_set_workarea(NULL, 0, 0, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_sequence_marker_count(NULL) == 0);
	EXPECT_TRUE(oakengine_sequence_marker_at(NULL, 0, NULL, NULL, 0, NULL) ==
		   OAKENGINE_E_INVALID);
}

static void test_shutdown_pairing(void)
{
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() == 0);

	// Shutdown is idempotent.
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

	// init may run again after a paired shutdown.
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() == OAKENGINE_INIT_HEADLESS);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() == 0);
}

TEST(OakEngineInit, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations so the engine never reads or
	// writes the real user configuration. The engine's config path comes
	// from QStandardPaths::AppDataLocation
	// (FileFunctions::get_configuration_location()), which follows
	// XDG_DATA_HOME on Linux; the other two are sandboxed for good measure.
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("OAK_CONFIG_DIR", g_tmpdir, 1) == 0);
#endif

	test_init();
	test_project_basics();
	test_sequence_and_save_load();
	test_footage_fixture();
	test_null_safety();
	test_shutdown_pairing();

}
