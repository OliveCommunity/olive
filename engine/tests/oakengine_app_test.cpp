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

// Pure C ABI test for the liboakengine application facade
// (oakengine/app.h). Exercises the CoreParams startup, the start/stop state
// machine, the tool/snapping/timecode state with its change notifications,
// the recent-projects list, the status bar, the clipboard, the footage
// filter and the project lifecycle. No GPU: everything runs on the
// offscreen QGuiApplication created by the facade itself.
//
// Not covered (they require a running import/load task or the autorecovery
// timer, which need an event loop): the confirm_image_sequence,
// relink_footage, save_project and load_layout handler invocations.
// Registration of every handler field is exercised and the close_project
// handler is verified through oakengine_app_create_new_project().

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oakengine/app.h"
#include "oakengine/init.h"
#include "oakengine/project.h"

// Recording sink for every OakEngineAppCallbacks field.
typedef struct {
	int confirm_image_sequence_calls;
	int relink_calls;
	int save_project_calls;
	int close_project_calls;
	int close_project_ret;
	int load_layout_calls;
	int otio_import_calls;
	int status_show_calls;
	char last_status[256];
	int last_timeout;
	int status_clear_calls;
	int cache_full_calls;
	int active_project_calls;
	OakEngineProject *last_project;
	int tool_changed_calls;
	int last_tool;
	int addable_changed_calls;
	int last_addable;
	int snapping_changed_calls;
	int last_snapping;
	int timecode_changed_calls;
	int last_display;
	int recent_changed_calls;
	int color_picker_calls;
	int last_color_picker;
} Cb;

static Cb g_cb;

static int on_confirm_image_sequence(const char *filename, void *userdata)
{
	(void) filename;
	assert(userdata == &g_cb);
	g_cb.confirm_image_sequence_calls++;
	return 1;
}

static int on_relink_footage(OakEngineFootage **footage, int count,
							 void *userdata)
{
	(void) footage;
	assert(userdata == &g_cb);
	g_cb.relink_calls++;
	assert(count >= 0);
	return 1;
}

static void on_save_project(const char *override_filename, void *userdata)
{
	(void) override_filename;
	assert(userdata == &g_cb);
	g_cb.save_project_calls++;
}

static int on_close_project(void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.close_project_calls++;
	// Mirror the application's close: detach and delete the open project
	OakEngineProject *p = oakengine_app_open_project();
	if (p) {
		oakengine_app_set_active_project(NULL);
		oakengine_project_free(p);
	}
	return g_cb.close_project_ret;
}

static void on_load_layout(const void *layout, void *userdata)
{
	(void) layout;
	assert(userdata == &g_cb);
	g_cb.load_layout_calls++;
}

static int on_otio_import(OakEngineSequence **sequences, int count,
						  void *userdata)
{
	(void) sequences;
	(void) count;
	assert(userdata == &g_cb);
	g_cb.otio_import_calls++;
	return 1;
}

static void on_status_message_show(const char *message, int timeout,
								   void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.status_show_calls++;
	snprintf(g_cb.last_status, sizeof(g_cb.last_status), "%s", message);
	g_cb.last_timeout = timeout;
}

static void on_status_message_clear(void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.status_clear_calls++;
}

static void on_cache_full_warning(void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.cache_full_calls++;
}

static void on_active_project_changed(OakEngineProject *project,
									  void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.active_project_calls++;
	g_cb.last_project = project;
}

static void on_tool_changed(int tool, void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.tool_changed_calls++;
	g_cb.last_tool = tool;
}

static void on_addable_object_changed(int object, void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.addable_changed_calls++;
	g_cb.last_addable = object;
}

static void on_snapping_changed(int snapping, void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.snapping_changed_calls++;
	g_cb.last_snapping = snapping;
}

static void on_timecode_display_changed(int display, void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.timecode_changed_calls++;
	g_cb.last_display = display;
}

static void on_open_recent_list_changed(void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.recent_changed_calls++;
}

static void on_color_picker_enabled(int enabled, void *userdata)
{
	assert(userdata == &g_cb);
	g_cb.color_picker_calls++;
	g_cb.last_color_picker = enabled;
}

static OakEngineAppCallbacks make_callbacks(void)
{
	OakEngineAppCallbacks cb = { 0 };
	cb.userdata = &g_cb;
	cb.confirm_image_sequence = on_confirm_image_sequence;
	cb.relink_footage = on_relink_footage;
	cb.save_project = on_save_project;
	cb.close_project = on_close_project;
	cb.load_layout = on_load_layout;
	cb.otio_import = on_otio_import;
	cb.status_message_show = on_status_message_show;
	cb.status_message_clear = on_status_message_clear;
	cb.cache_full_warning = on_cache_full_warning;
	cb.active_project_changed = on_active_project_changed;
	cb.tool_changed = on_tool_changed;
	cb.addable_object_changed = on_addable_object_changed;
	cb.snapping_changed = on_snapping_changed;
	cb.timecode_display_changed = on_timecode_display_changed;
	cb.open_recent_list_changed = on_open_recent_list_changed;
	cb.color_picker_enabled = on_color_picker_enabled;
	return cb;
}

// Query a buf/size string function into a heap buffer (caller frees).
static char *query0(int (*fn)(char *, int))
{
	const int needed = fn(NULL, 0);
	assert(needed >= 0);
	char *buf = (char *) malloc(size_t(needed) + 1);
	assert(fn(buf, needed + 1) == needed);
	buf[needed] = '\0';
	return buf;
}

static void test_create_and_params(void)
{
	OakEngineAppParams params = { 0 };
	params.run_mode = OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE;
	params.fullscreen = 1;
	params.startup_project = "/tmp/startup.ove";

	// NULL params would be valid too, but verify the values round-trip
	assert(oakengine_app_create(&params) == OAKENGINE_OK);
	assert(oakengine_app_run_mode() == OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE);
	assert(oakengine_app_fullscreen() == 1);
	char *startup = query0(oakengine_app_startup_project);
	assert(strcmp(startup, "/tmp/startup.ove") == 0);
	free(startup);

	// Only one application core may exist
	assert(oakengine_app_create(NULL) == OAKENGINE_E_STATE);
}

static void test_start_stop(void)
{
	// Not started yet
	assert(oakengine_app_stop() == OAKENGINE_E_STATE);

	// Full engine start (config, managers, autorecovery, recent list)
	assert(oakengine_app_start() == OAKENGINE_OK);
	assert(oakengine_app_start() == OAKENGINE_E_STATE);

	assert(oakengine_app_stop() == OAKENGINE_OK);
	assert(oakengine_app_stop() == OAKENGINE_E_STATE);
}

static void test_tool_state(void)
{
	OakEngineAppCallbacks cb = make_callbacks();
	assert(oakengine_app_set_callbacks(&cb) == OAKENGINE_OK);

	// Tool (k_none=0 .. k_track_select=13, k_count=14)
	assert(oakengine_app_set_tool(4) == OAKENGINE_OK);
	assert(oakengine_app_tool() == 4);
	assert(g_cb.tool_changed_calls == 1 && g_cb.last_tool == 4);
	assert(oakengine_app_set_tool(-1) == OAKENGINE_E_INVALID);
	assert(oakengine_app_set_tool(999) == OAKENGINE_E_INVALID);

	// Addable object
	assert(oakengine_app_set_addable_object(2) == OAKENGINE_OK);
	assert(oakengine_app_addable_object() == 2);
	assert(g_cb.addable_changed_calls == 1 && g_cb.last_addable == 2);
	assert(oakengine_app_set_addable_object(999) == OAKENGINE_E_INVALID);

	// Snapping
	assert(oakengine_app_set_snapping(0) == OAKENGINE_OK);
	assert(oakengine_app_snapping() == 0);
	assert(g_cb.snapping_changed_calls == 1 && g_cb.last_snapping == 0);
	assert(oakengine_app_set_snapping(1) == OAKENGINE_OK);
	assert(oakengine_app_snapping() == 1);
	assert(g_cb.snapping_changed_calls == 2 && g_cb.last_snapping == 1);

	// Timecode display (0..4)
	assert(oakengine_app_set_timecode_display(3) == OAKENGINE_OK);
	assert(oakengine_app_timecode_display() == 3);
	assert(g_cb.timecode_changed_calls == 1 && g_cb.last_display == 3);
	assert(oakengine_app_set_timecode_display(999) == OAKENGINE_E_INVALID);

	// Selected transition
	assert(oakengine_app_set_selected_transition("crossdissolve") ==
		   OAKENGINE_OK);
	char *transition = query0(oakengine_app_selected_transition);
	assert(strcmp(transition, "crossdissolve") == 0);
	free(transition);
	assert(oakengine_app_set_selected_transition(NULL) == OAKENGINE_OK);
	transition = query0(oakengine_app_selected_transition);
	assert(transition[0] == '\0');
	free(transition);

	// Magic flag
	assert(oakengine_app_set_magic(1) == OAKENGINE_OK);
	assert(oakengine_app_is_magic_enabled() == 1);
	assert(oakengine_app_set_magic(0) == OAKENGINE_OK);
	assert(oakengine_app_is_magic_enabled() == 0);
}

static void test_status_and_pixel_sampling(void)
{
	assert(oakengine_app_show_status_message("hello", 250) == OAKENGINE_OK);
	assert(g_cb.status_show_calls == 1);
	assert(strcmp(g_cb.last_status, "hello") == 0);
	assert(g_cb.last_timeout == 250);
	assert(oakengine_app_show_status_message(NULL, 0) == OAKENGINE_E_INVALID);

	assert(oakengine_app_clear_status_message() == OAKENGINE_OK);
	assert(g_cb.status_clear_calls == 1);

	// Pixel sampling ref-count emits only when crossing zero
	assert(oakengine_app_request_pixel_sampling(1) == OAKENGINE_OK);
	assert(oakengine_app_request_pixel_sampling(1) == OAKENGINE_OK);
	assert(g_cb.color_picker_calls == 1 && g_cb.last_color_picker == 1);
	assert(oakengine_app_request_pixel_sampling(0) == OAKENGINE_OK);
	assert(g_cb.color_picker_calls == 1);
	assert(oakengine_app_request_pixel_sampling(0) == OAKENGINE_OK);
	assert(g_cb.color_picker_calls == 2 && g_cb.last_color_picker == 0);
}

static void test_project_lifecycle(void)
{
	g_cb.close_project_ret = 1;
	const int active_before = g_cb.active_project_calls;

	// New project goes through the close handler and becomes active
	assert(oakengine_app_create_new_project() == OAKENGINE_OK);
	assert(g_cb.close_project_calls == 1);
	OakEngineProject *p = oakengine_app_open_project();
	assert(p != NULL);
	assert(g_cb.active_project_calls > active_before);
	assert(g_cb.last_project == p);

	// Replacing it invokes the close handler again
	assert(oakengine_app_create_new_project() == OAKENGINE_OK);
	assert(g_cb.close_project_calls == 2);
	p = oakengine_app_open_project();
	assert(p != NULL);

	// Saving a project without a filename keeps the recent list unchanged
	assert(oakengine_app_clear_recent_projects() == OAKENGINE_OK);
	assert(oakengine_app_on_project_saved(p) == OAKENGINE_OK);
	assert(oakengine_app_recent_projects_count() == 0);

	// Detach and free it again
	assert(oakengine_app_set_active_project(NULL) == OAKENGINE_OK);
	assert(oakengine_app_open_project() == NULL);
	assert(g_cb.last_project == NULL);
	oakengine_project_free(p);

	// add_open_project adopts an externally created project
	p = oakengine_project_create();
	assert(p != NULL);
	assert(oakengine_project_new(p) == OAKENGINE_OK);
	assert(oakengine_app_add_open_project(p, 0) == OAKENGINE_OK);
	assert(oakengine_app_open_project() == p);
	assert(oakengine_app_set_active_project(NULL) == OAKENGINE_OK);
	oakengine_project_free(p);

	// NULL tolerance
	assert(oakengine_app_add_open_project(NULL, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_app_on_project_saved(NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_app_add_open_project_from_task(NULL, 0) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_app_add_recovery_project_from_task(NULL) ==
		   OAKENGINE_E_INVALID);
}

static void test_recent_projects(void)
{
	// Start from a clean list
	assert(oakengine_app_clear_recent_projects() == OAKENGINE_OK);
	assert(oakengine_app_recent_projects_count() == 0);
	const int changes_before = g_cb.recent_changed_calls;

	// A saved project lands in the recent list through on_project_saved
	OakEngineProject *p = oakengine_project_create();
	assert(p != NULL);
	assert(oakengine_project_new(p) == OAKENGINE_OK);
	assert(oakengine_project_save(p, "oakengine_app_test_recent.ove") ==
		   OAKENGINE_OK);
	assert(oakengine_app_on_project_saved(p) == OAKENGINE_OK);
	assert(oakengine_app_recent_projects_count() == 1);
	assert(g_cb.recent_changed_calls > changes_before);

	const int needed = oakengine_app_recent_project_at(0, NULL, 0);
	assert(needed > 0);
	char *buf = (char *) malloc(size_t(needed) + 1);
	assert(oakengine_app_recent_project_at(0, buf, needed + 1) == needed);
	buf[needed] = '\0';
	assert(strstr(buf, "oakengine_app_test_recent.ove") != NULL);
	free(buf);

	// Out-of-range access is rejected (the engine would assert otherwise)
	assert(oakengine_app_recent_project_at(5, NULL, 0) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_app_remove_recent_project(5) == OAKENGINE_E_NOT_FOUND);

	assert(oakengine_app_remove_recent_project(0) == OAKENGINE_OK);
	assert(oakengine_app_recent_projects_count() == 0);

	remove("oakengine_app_test_recent.ove");
	oakengine_project_free(p);
}

static void test_clipboard(void)
{
	assert(oakengine_app_copy_to_clipboard("hello clipboard") ==
		   OAKENGINE_OK);
	char *text = query0(oakengine_app_paste_from_clipboard);
	assert(strcmp(text, "hello clipboard") == 0);
	free(text);
	assert(oakengine_app_copy_to_clipboard(NULL) == OAKENGINE_E_INVALID);
}

static void test_footage_filter(void)
{
	assert(oakengine_app_is_footage_extension_allowed("movie.mp4") == 1);
	assert(oakengine_app_is_footage_extension_allowed("IMAGE.PNG") == 1);
	assert(oakengine_app_is_footage_extension_allowed("doc.txt") == 0);
	assert(oakengine_app_is_footage_extension_allowed(NULL) ==
		   OAKENGINE_E_INVALID);

	char *filter = query0(oakengine_app_footage_file_dialog_filter);
	assert(strstr(filter, "*.mp4") != NULL);
	assert(strstr(filter, ";;") != NULL);
	free(filter);
}

static void test_misc(void)
{
	// Unknown locale is reported as "not found" without failing
	assert(oakengine_app_set_language("definitely_not_a_locale_xx") == 0);
	assert(oakengine_app_set_language(NULL) == OAKENGINE_E_INVALID);

	assert(oakengine_app_set_autorecovery_interval(5) == OAKENGINE_OK);
	assert(oakengine_app_set_use_proxy_media(1) == OAKENGINE_OK);

	char *index = query0(oakengine_app_auto_recovery_index_filename);
	assert(strstr(index, "unrecovered") != NULL);
	free(index);

	assert(oakengine_app_undo_stack() != NULL);
}

static void test_create_sequence(void)
{
	OakEngineProject *p = oakengine_project_create();
	assert(p != NULL);
	assert(oakengine_project_new(p) == OAKENGINE_OK);

	OakEngineSequence *s = oakengine_app_create_sequence(p, "Seq %1");
	assert(s != NULL);

	assert(oakengine_app_create_sequence(NULL, NULL) == NULL);

	// The returned sequence is not yet part of the project (owned by the
	// caller); this test intentionally leaves it unparented.
	oakengine_project_free(p);
}

static void test_callbacks_clear(void)
{
	assert(oakengine_app_set_callbacks(NULL) == OAKENGINE_OK);

	// State changes still work, but nothing is delivered anymore
	const int calls = g_cb.tool_changed_calls;
	assert(oakengine_app_set_tool(2) == OAKENGINE_OK);
	assert(oakengine_app_tool() == 2);
	assert(g_cb.tool_changed_calls == calls);
}

int main(void)
{
	// The facade brings up its own offscreen application object
	test_create_and_params();
	test_start_stop();

	// Engine services (incl. renderer manager for set_active_project)
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);

	test_tool_state();
	test_status_and_pixel_sampling();
	test_project_lifecycle();
	test_recent_projects();
	test_clipboard();
	test_footage_filter();
	test_misc();
	test_create_sequence();
	test_callbacks_clear();

	assert(oakengine_shutdown() == OAKENGINE_OK);
	return 0;
}
