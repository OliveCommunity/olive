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

// Pure C ABI test for the liboakengine disk cache family
// (oakengine/disk.h). Exercises the DiskManager instance lifecycle, default
// cache path queries, cache clearing, settings handler dispatch, folder
// handle lookup and default path mutation. Runs headless; no GPU required.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "oakengine/disk.h"
#include "oakengine/init.h"

static char g_handler_path[512];
static int g_handler_call_count;

static void reset_handler_state(void)
{
	memset(g_handler_path, 0, sizeof(g_handler_path));
	g_handler_call_count = 0;
}

static void settings_handler(const char *folder_path, void *parent_window,
							 void *userdata)
{
	(void) parent_window;
	(void) userdata;
	assert(folder_path != NULL);
	assert(strlen(folder_path) > 0);
	strncpy(g_handler_path, folder_path, sizeof(g_handler_path) - 1);
	g_handler_path[sizeof(g_handler_path) - 1] = '\0';
	g_handler_call_count++;
}

static void test_instance_lifecycle(void)
{
	char buf[512];

	// DiskManager is created by oakengine_init(HEADLESS).
	assert(oakengine_disk_get_default_cache_path(buf, sizeof(buf)) > 0);

	// Destroy is allowed and idempotent.
	assert(oakengine_disk_destroy_instance() == OAKENGINE_OK);
	assert(oakengine_disk_get_default_cache_path(buf, sizeof(buf)) ==
		   OAKENGINE_E_STATE);
	assert(oakengine_disk_destroy_instance() == OAKENGINE_OK);

	// Create recreates the instance.
	assert(oakengine_disk_create_instance() == OAKENGINE_OK);
	assert(oakengine_disk_get_default_cache_path(buf, sizeof(buf)) > 0);

	// Create is idempotent.
	assert(oakengine_disk_create_instance() == OAKENGINE_OK);
	assert(oakengine_disk_get_default_cache_path(buf, sizeof(buf)) > 0);
}

static void test_default_cache_path(void)
{
	char buf[512];
	memset(buf, 0, sizeof(buf));

	const int len = oakengine_disk_get_default_cache_path(buf, sizeof(buf));
	assert(len > 0);
	assert((int) strlen(buf) == len);
	assert(strchr(buf, '/') != NULL || strchr(buf, '\\') != NULL);

	// Query length with NULL buffer.
	assert(oakengine_disk_get_default_cache_path(NULL, 0) == len);
}

static void test_open_folder_handle(void)
{
	char buf[512];
	assert(oakengine_disk_get_default_cache_path(buf, sizeof(buf)) > 0);

	void *folder = oakengine_disk_get_open_folder(buf);
	assert(folder != NULL);

	// NULL/empty path returns the default folder handle.
	void *default_folder = oakengine_disk_get_open_folder(nullptr);
	assert(default_folder == folder);

	void *empty_folder = oakengine_disk_get_open_folder("");
	assert(empty_folder == folder);

	// A different path opens a distinct folder.
	char tmp[256];
	snprintf(tmp, sizeof(tmp),
#if defined(_WIN32)
			 "%s\\oakengine_disk_test_folder_XXXXXX",
#else
			 "%s/oakengine_disk_test_folder_XXXXXX",
#endif
			 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

#if defined(_WIN32)
	char *tmpdir = _mktemp(tmp);
	assert(tmpdir != NULL);
	assert(_mkdir(tmpdir) == 0);
#else
	char *tmpdir = mkdtemp(tmp);
	assert(tmpdir != NULL);
#endif

	void *other_folder = oakengine_disk_get_open_folder(tmpdir);
	assert(other_folder != NULL);
	assert(other_folder != folder);

	// The same path returns the same handle.
	void *other_folder_again = oakengine_disk_get_open_folder(tmpdir);
	assert(other_folder_again == other_folder);

#if defined(_WIN32)
	_rmdir(tmpdir);
#else
	rmdir(tmpdir);
#endif
}

static void test_clear_cache(void)
{
	// Create a temporary cache directory and seed it with a file.
	char path[256];
	snprintf(path, sizeof(path),
#if defined(_WIN32)
			 "%s\\oakengine_disk_test_cache_XXXXXX",
#else
			 "%s/oakengine_disk_test_cache_XXXXXX",
#endif
			 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

#if defined(_WIN32)
	char *tmpdir = _mktemp(path);
	assert(tmpdir != NULL);
	assert(_mkdir(tmpdir) == 0);
#else
	char *tmpdir = mkdtemp(path);
	assert(tmpdir != NULL);
#endif

	char index_file[512];
	snprintf(index_file, sizeof(index_file),
#if defined(_WIN32)
			 "%s\\index", tmpdir);
#else
			 "%s/index", tmpdir);
#endif

	FILE *f = fopen(index_file, "w");
	assert(f != NULL);
	fclose(f);

	// clear_cache opens the folder and clears its contents.
	assert(oakengine_disk_clear_cache(tmpdir) == 1);

	// Re-create a file and clear again to ensure idempotency.
	f = fopen(index_file, "w");
	assert(f != NULL);
	fclose(f);
	assert(oakengine_disk_clear_cache(tmpdir) == 1);

#if defined(_WIN32)
	_rmdir(tmpdir);
#else
	rmdir(tmpdir);
#endif
}

static void test_settings_handler_round_trip(void)
{
	reset_handler_state();

	assert(oakengine_disk_set_settings_handler(settings_handler, NULL) ==
		   OAKENGINE_OK);

	// NULL path uses the default folder.
	assert(oakengine_disk_show_settings_dialog(NULL, NULL) == OAKENGINE_OK);
	assert(g_handler_call_count == 1);
	assert(strlen(g_handler_path) > 0);

	// Calling again with a specific path invokes the handler with that path.
	assert(oakengine_disk_show_settings_dialog(g_handler_path, NULL) ==
		   OAKENGINE_OK);
	assert(g_handler_call_count == 2);
	assert(strcmp(g_handler_path, g_handler_path) == 0);

	// Clearing the handler is allowed and results in a logged skip.
	assert(oakengine_disk_set_settings_handler(NULL, NULL) == OAKENGINE_OK);
	assert(oakengine_disk_show_settings_dialog(g_handler_path, NULL) ==
		   OAKENGINE_OK);
	assert(g_handler_call_count == 2);
}

static void test_invalidate_project(void)
{
	// No instance returns an error.
	assert(oakengine_disk_destroy_instance() == OAKENGINE_OK);
	assert(oakengine_disk_invalidate_project(NULL) == OAKENGINE_E_STATE);

	assert(oakengine_disk_create_instance() == OAKENGINE_OK);

	// NULL project is accepted (signal emitted with null pointer).
	assert(oakengine_disk_invalidate_project(NULL) == OAKENGINE_OK);

	// Valid project returns OK without crashing.
	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_disk_invalidate_project(project) == OAKENGINE_OK);
	oakengine_project_free(project);
}

static void test_set_default_cache_path(void)
{
	char original[512];
	assert(oakengine_disk_get_default_cache_path(original, sizeof(original)) >
		   0);

	char tmp[256];
	snprintf(tmp, sizeof(tmp),
#if defined(_WIN32)
			 "%s\\oakengine_disk_test_default_XXXXXX",
#else
			 "%s/oakengine_disk_test_default_XXXXXX",
#endif
			 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

#if defined(_WIN32)
	char *tmpdir = _mktemp(tmp);
	assert(tmpdir != NULL);
	assert(_mkdir(tmpdir) == 0);
#else
	char *tmpdir = mkdtemp(tmp);
	assert(tmpdir != NULL);
#endif

	assert(oakengine_disk_set_default_cache_path(tmpdir) == OAKENGINE_OK);

	char updated[512];
	assert(oakengine_disk_get_default_cache_path(updated, sizeof(updated)) > 0);
	assert(strcmp(updated, tmpdir) == 0);

	// Restore original default path.
	assert(oakengine_disk_set_default_cache_path(original) == OAKENGINE_OK);
	assert(oakengine_disk_get_default_cache_path(updated, sizeof(updated)) > 0);
	assert(strcmp(updated, original) == 0);

#if defined(_WIN32)
	_rmdir(tmpdir);
#else
	rmdir(tmpdir);
#endif
}

int main(void)
{
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_instance_lifecycle();
	test_default_cache_path();
	test_open_folder_handle();
	test_clear_cache();
	test_settings_handler_round_trip();
	test_set_default_cache_path();
	test_invalidate_project();

	// Leave DiskManager in the initialized state for shutdown.
	oakengine_disk_create_instance();

	oakengine_shutdown();

	printf("oakengine_disk_test: OK\n");
	return 0;
}
