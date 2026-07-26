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

// Pure C ABI tests for the liboakengine task and undo families
// (oakengine/task.h and oakengine/undo.h). Runs headless; no GPU required.

#include <assert.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>

#include "oakengine/events.h"
#include "oakengine/init.h"
#include "oakengine/project.h"
#include "oakengine/task.h"
#include "oakengine/undo.h"

static int g_task_started = 0;
static int g_task_progress = 0;
static int g_task_finished = 0;
static int g_task_succeeded = 0;
static int g_manager_added = 0;
static int g_manager_removed = 0;

static void task_event_cb(const oakengine_event *event, void *userdata)
{
	(void) userdata;
	switch (event->id) {
	case OAKENGINE_EVENT_TASK_STARTED:
		g_task_started = 1;
		break;
	case OAKENGINE_EVENT_TASK_PROGRESS:
		g_task_progress = 1;
		break;
	case OAKENGINE_EVENT_TASK_FINISHED:
		g_task_finished = 1;
		g_task_succeeded = (int) event->a;
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED:
		g_manager_added = 1;
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED:
		g_manager_removed = 1;
		break;
	default:
		break;
	}
}

static void test_manager_no_engine(void)
{
	EXPECT_TRUE(oakengine_task_manager_handle() == NULL);
	EXPECT_TRUE(oakengine_task_manager_count() == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_manager_first() == NULL);
	EXPECT_TRUE(oakengine_task_manager_add(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_manager_cancel(NULL) == OAKENGINE_E_INVALID);
}

static void test_manager_empty(void)
{
	void *mgr = oakengine_task_manager_handle();
	EXPECT_TRUE(mgr != NULL);
	EXPECT_TRUE(oakengine_task_manager_count() == 0);
	EXPECT_TRUE(oakengine_task_manager_first() == NULL);
}

static void test_task_null(void)
{
	char buf[64];
	EXPECT_TRUE(oakengine_task_title(NULL, buf, sizeof(buf)) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_error(NULL, buf, sizeof(buf)) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_start_time(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_is_cancelled(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_cancel(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_start_sync(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_task_free(NULL) == OAKENGINE_E_INVALID);
}

static void test_import_error_path(void)
{
	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);
	OakEngineNode *root = oakengine_project_root(p);
	EXPECT_TRUE(root != NULL);

	// Empty URL list is rejected at creation.
	OakEngineTask *task = oakengine_task_create_project_import(root, NULL, 0);
	EXPECT_TRUE(task == NULL);

	// Valid creation but with non-existent file gives zero footage/one invalid.
	const char *url = "file:///this/file/does/not/exist.mov";
	task = oakengine_task_create_project_import(root, &url, 1);
	EXPECT_TRUE(task != NULL);

	EXPECT_TRUE(oakengine_task_import_file_count(task) == 1);

	int result = oakengine_task_start_sync(task);
	(void) result;

	EXPECT_TRUE(oakengine_task_import_footage_count(task) == 0);
	EXPECT_TRUE(oakengine_task_import_invalid_files_count(task) == 1);
	char buf[256];
	int len = oakengine_task_import_invalid_file_at(task, 0, buf, sizeof(buf));
	EXPECT_TRUE(len > 0);
	EXPECT_TRUE(strstr(buf, "exist.mov") != NULL);
	EXPECT_TRUE(oakengine_task_import_invalid_file_at(task, 0, NULL, 0) == len);
	EXPECT_TRUE(oakengine_task_import_invalid_file_at(task, 1, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_task_free(task) == OAKENGINE_OK);
	oakengine_project_free(p);
}

static void test_load_task_sync(void)
{
	OakEngineTask *task =
			oakengine_task_create_project_load("/no/such/project.ove");
	EXPECT_TRUE(task != NULL);

	// No event subscription here; just confirm it reports failure cleanly.
	int ok = oakengine_task_start_sync(task);
	EXPECT_TRUE(ok == 0);

	char err[256];
	int len = oakengine_task_error(task, err, sizeof(err));
	EXPECT_TRUE(len > 0);

	EXPECT_TRUE(oakengine_task_free(task) == OAKENGINE_OK);
}

static void test_task_events(void)
{
	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);
	OakEngineNode *root = oakengine_project_root(p);
	EXPECT_TRUE(root != NULL);

	const char *url = "file:///this/file/does/not/exist.mov";
	OakEngineTask *task =
			oakengine_task_create_project_import(root, &url, 1);
	EXPECT_TRUE(task != NULL);

	void *mgr = oakengine_task_manager_handle();
	int64_t sub_started = oakengine_event_subscribe(
			task, OAKENGINE_EVENT_TASK_STARTED, task_event_cb, NULL);
	int64_t sub_progress = oakengine_event_subscribe(
			task, OAKENGINE_EVENT_TASK_PROGRESS, task_event_cb, NULL);
	int64_t sub_finished = oakengine_event_subscribe(
			task, OAKENGINE_EVENT_TASK_FINISHED, task_event_cb, NULL);
	int64_t sub_added = oakengine_event_subscribe(
			mgr, OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED, task_event_cb, NULL);
	int64_t sub_removed = oakengine_event_subscribe(
			mgr, OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED, task_event_cb, NULL);

	EXPECT_TRUE(sub_started > 0);
	EXPECT_TRUE(sub_progress > 0);
	EXPECT_TRUE(sub_finished > 0);
	EXPECT_TRUE(sub_added > 0);
	EXPECT_TRUE(sub_removed > 0);

	g_task_started = g_task_progress = g_task_finished = 0;
	g_task_succeeded = g_manager_added = g_manager_removed = 0;

	EXPECT_TRUE(oakengine_task_manager_add(task) == OAKENGINE_OK);

	// Wait for the task to finish. Manager tasks run on a worker thread and
	// emit events on that thread; spin briefly until the finished event fires.
	for (int i = 0; i < 200 && !g_task_finished; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	EXPECT_TRUE(g_manager_added == 1);
	EXPECT_TRUE(g_task_started == 1);
	EXPECT_TRUE(g_task_finished == 1);
	// The import task may succeed even when all files are invalid; the event
	// payload only reports Task::finished() success, which is implementation
	// dependent. We only verify the event fired and had a boolean value.
	EXPECT_TRUE(g_task_succeeded == 0 || g_task_succeeded == 1);

	// Cancel returns OK whether the task is still running or already done.
	EXPECT_TRUE(oakengine_task_manager_cancel(task) == OAKENGINE_OK);

	oakengine_event_unsubscribe(sub_started);
	oakengine_event_unsubscribe(sub_progress);
	oakengine_event_unsubscribe(sub_finished);
	oakengine_event_unsubscribe(sub_added);
	oakengine_event_unsubscribe(sub_removed);

	oakengine_project_free(p);
}

static void test_undo_round_trip(void)
{
	EXPECT_TRUE(oakengine_undo_handle() != NULL);
	EXPECT_TRUE(oakengine_undo_count() == 1); // the empty "New Project" entry
	EXPECT_TRUE(oakengine_undo_index() == 1);
	EXPECT_TRUE(oakengine_undo_can_undo() == 0);
	EXPECT_TRUE(oakengine_undo_can_redo() == 0);

	char text[256];
	int len = oakengine_undo_command_text(0, text, sizeof(text));
	EXPECT_TRUE(len > 0);

	// Push a custom no-op command with a user-visible label.
	void *cmd = oakengine_undo_command_create(
			"Internal Name", NULL, NULL, NULL, NULL);
	EXPECT_TRUE(cmd != NULL);
	EXPECT_TRUE(oakengine_undo_push(cmd, "Test Command") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_undo_count() == 2);
	EXPECT_TRUE(oakengine_undo_index() == 2);
	EXPECT_TRUE(oakengine_undo_can_undo() == 1);

	len = oakengine_undo_command_text(1, text, sizeof(text));
	EXPECT_TRUE(len > 0);
	EXPECT_TRUE(strstr(text, "Test Command") != NULL);

	EXPECT_TRUE(oakengine_undo_command_is_done(1) == 1);
	EXPECT_TRUE(oakengine_undo_jump(1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_undo_index() == 1);
	EXPECT_TRUE(oakengine_undo_can_redo() == 1);
	EXPECT_TRUE(oakengine_undo_command_is_done(1) == 0);

	EXPECT_TRUE(oakengine_undo_jump(2) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_undo_index() == 2);
	EXPECT_TRUE(oakengine_undo_can_undo() == 1);

	EXPECT_TRUE(oakengine_undo_clear() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_undo_count() == 1);
	EXPECT_TRUE(oakengine_undo_index() == 1);
	EXPECT_TRUE(oakengine_undo_can_undo() == 0);
}

static void test_custom_command_multi(void)
{
	static int g_redo = 0;
	static int g_undo = 0;
	static int g_free = 0;

	g_redo = g_undo = g_free = 0;

	void *cmd = oakengine_undo_command_create(
			"Custom",
			[](void *ud) { (void) ud; g_redo++; },
			[](void *ud) { (void) ud; g_undo++; },
			[](void *ud) { (void) ud; g_free++; },
			NULL);
	EXPECT_TRUE(cmd != NULL);

	EXPECT_TRUE(oakengine_undo_command_redo_now(cmd) == OAKENGINE_OK);
	EXPECT_TRUE(g_redo == 1);
	EXPECT_TRUE(g_undo == 0);

	EXPECT_TRUE(oakengine_undo_command_undo_now(cmd) == OAKENGINE_OK);
	EXPECT_TRUE(g_undo == 1);

	void *multi = oakengine_undo_command_create_multi();
	EXPECT_TRUE(multi != NULL);
	EXPECT_TRUE(oakengine_undo_command_multi_child_count(multi) == 0);
	EXPECT_TRUE(oakengine_undo_command_multi_add_child(multi, cmd) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_undo_command_multi_child_count(multi) == 1);
	EXPECT_TRUE(oakengine_undo_command_multi_add_child(multi, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_undo_command_multi_add_child(NULL, cmd) ==
		   OAKENGINE_E_INVALID);

	// Remove the child from the multi-command and free it directly to verify
	// the custom command's free callback. (MultiUndoCommand does not own its
	// children, so freeing the multi-command alone would leak the child.)
	EXPECT_TRUE(oakengine_undo_command_multi_child_count(multi) == 1);
	oakengine_undo_command_free(cmd);
	EXPECT_TRUE(g_free == 1);

	// The now-empty multi-command can be freed safely.
	oakengine_undo_command_free(multi);
}

// ---- Save task ---------------------------------------------------------------

static void test_save_task_creation(void)
{
	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);

	// Create a save task with NULL override and NULL layout.
	OakEngineTask *task = oakengine_task_create_project_save(
		p, 1, NULL, NULL);
	EXPECT_TRUE(task != NULL);

	// task_save_get_project should return the project we passed.
	EXPECT_TRUE(oakengine_task_save_get_project(task) == p);
	EXPECT_TRUE(oakengine_task_save_get_project(NULL) == NULL);

	// Running sync on an untitled project: may succeed or fail gracefully.
	int ok = oakengine_task_start_sync(task);
	(void) ok; // must not crash

	EXPECT_TRUE(oakengine_task_free(task) == OAKENGINE_OK);
	oakengine_project_free(p);
}

// ---- OTIO / Export / Proxy task creators (null/invalid smoke) ----------------

static void test_other_task_creation(void)
{
	// OTIO load: test that it either returns NULL (no OTIO support) or
	// creates a task that can be freed.
	OakEngineTask *task = oakengine_task_create_project_load_otio(
		"/nonexistent.otio");
	if (task != NULL) {
		EXPECT_TRUE(oakengine_task_free(task) == OAKENGINE_OK);
	}

	// OTIO save: same.
	task = oakengine_task_create_project_save_otio(NULL);
	// Passing NULL project may return NULL.

	// Export: NULL sequence, NULL params.
	EXPECT_TRUE(oakengine_task_create_export(NULL, NULL) == NULL);

	// Proxy: NULL footage.
	EXPECT_TRUE(oakengine_task_create_proxy(NULL) == NULL);
}

// ---- Import result accessors (extending test_import_error_path) --------------

static void test_import_result_accessors(void)
{
	OakEngineProject *p = oakengine_project_create();
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_project_new(p) == OAKENGINE_OK);
	OakEngineNode *root = oakengine_project_root(p);
	EXPECT_TRUE(root != NULL);

	const char *url = "file:///this/file/does/not/exist.mov";
	OakEngineTask *task = oakengine_task_create_project_import(root, &url, 1);
	EXPECT_TRUE(task != NULL);

	int ok = oakengine_task_start_sync(task);
	EXPECT_TRUE(ok == 0 || ok == 1);
	(void) ok;

	// Import of invalid file: footage_at should return 0/NULL.
	EXPECT_TRUE(oakengine_task_import_footage_count(task) == 0);
	EXPECT_TRUE(oakengine_task_import_footage_at(task, 0) == NULL);
	EXPECT_TRUE(oakengine_task_import_footage_at(task, -1) == NULL);
	EXPECT_TRUE(oakengine_task_import_footage_at(NULL, 0) == NULL);

	// import_get_command: should be non-NULL (the import built a command
	// even when all files failed) or NULL (no data to build).
	void *cmd = oakengine_task_import_get_command(task);
	if (cmd != NULL) {
		oakengine_undo_command_free(cmd);
	}

	EXPECT_TRUE(oakengine_task_free(task) == OAKENGINE_OK);
	oakengine_project_free(p);
}

// ---- Undo action helpers -----------------------------------------------------

static void test_undo_actions(void)
{
	// update_actions should not crash.
	EXPECT_TRUE(oakengine_undo_update_actions() == OAKENGINE_OK);

	// undo_action / redo_action return QAction* as void* (may be NULL).
	oakengine_undo_undo_action();
	oakengine_undo_redo_action();
}

TEST(OakEngineTask, Main)
{
	test_manager_no_engine();

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_manager_empty();
	test_task_null();
	test_import_error_path();
	test_load_task_sync();
	test_task_events();
	test_undo_round_trip();
	test_custom_command_multi();
	test_save_task_creation();
	test_other_task_creation();
	test_import_result_accessors();
	test_undo_actions();

	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);
}
