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

#ifndef OAKENGINE_APP_H
#define OAKENGINE_APP_H

#include "export.h"
#include "footage.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file app.h
 * @brief C ABI for the application-level engine state (EngineCore facade)
 *
 * This family exposes the process-wide application state the editor UI needs
 * from the engine: the CoreParams-driven startup, the open/active project,
 * the recent-projects list, the global tool/snapping/timecode settings, the
 * status bar and the UI handler hooks the engine calls when it needs user
 * interaction (image-sequence confirmation, footage relink, project save /
 * close, main window layout restore, OTIO import).
 *
 * It wraps olive::EngineCore so that the UI layer no longer derives from or
 * links against that C++ class. The engine emits its change notifications
 * through the OakEngineAppCallbacks function pointers (registered with
 * oakengine_app_set_callbacks()) instead of Qt signals.
 *
 * Conventions (matching oakengine/project.h):
 *   - Booleans are int (1/0).
 *   - Return codes: 0 (OAKENGINE_OK) on success, negative OAKENGINE_E_* on
 *     failure. Functions documented as returning a value return
 *     OAKENGINE_E_INVALID when no application core exists.
 *   - String output uses the buf/size convention: the return value is the
 *     number of characters that would have been written excluding the NUL,
 *     so buf == NULL or a short buffer queries the required size. The output
 *     is NUL-terminated whenever buf_size > 0. A negative return value is an
 *     OAKENGINE_E_* error code.
 *   - Enum values mirror the engine enums (olive::Tool::Item,
 *     olive::Tool::AddableObject, olive::core::Timecode::Display) and are
 *     passed as plain int; the numeric values are identical.
 */

/**
 * @brief Application run modes (mirrors olive::EngineCore::CoreParams::RunMode).
 */
#define OAKENGINE_APP_RUN_NORMAL 0 /**< Normal GUI run. */
#define OAKENGINE_APP_RUN_HEADLESS_EXPORT 1 /**< Export without GUI. */
#define OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE 2 /**< Pre-cache without GUI. */

/**
 * @brief Startup parameters for oakengine_app_create().
 *
 * Strings may be NULL (treated as empty). The struct is copied by
 * oakengine_app_create(); the pointed-to strings are only read during the
 * call.
 */
typedef struct OakEngineAppParams {
	int run_mode; /**< OAKENGINE_APP_RUN_* value. */
	int fullscreen; /**< Start the main window fullscreen (bool). */
	const char *startup_project; /**< Project file to open on startup, or NULL. */
	const char *startup_language; /**< .qm file overriding the language, or NULL. */
	int crash_on_startup; /**< Trigger a manual crash shortly after start (bool). */
} OakEngineAppParams;

/**
 * @brief UI handler and notification callback set.
 *
 * Any field may be NULL. A NULL handler makes the engine fall back to its
 * headless default (accept the import, close without prompting, skip the
 * file write); a NULL notification simply drops the event.
 *
 * The callbacks are invoked synchronously on the thread that triggered the
 * engine call (usually the main thread). `userdata` is passed back verbatim.
 *
 * `load_layout` receives a `const olive::SerializedLayoutInfo *` (engine
 * data structure, only valid during the call). `otio_import` receives an
 * array of borrowed olive::Sequence pointers as OakEngineSequence handles.
 */
typedef struct OakEngineAppCallbacks {
	void *userdata;

	/* UI handlers (engine asks the application) */
	int (*confirm_image_sequence)(const char *filename, void *userdata);
	int (*relink_footage)(OakEngineFootage **footage, int count,
						  void *userdata);
	void (*save_project)(const char *override_filename, void *userdata);
	int (*close_project)(void *userdata);
	void (*load_layout)(const void *layout, void *userdata);
	int (*otio_import)(OakEngineSequence **sequences, int count,
					   void *userdata);

	/* Notifications (engine informs the application) */
	void (*status_message_show)(const char *message, int timeout,
								void *userdata);
	void (*status_message_clear)(void *userdata);
	void (*cache_full_warning)(void *userdata);
	void (*active_project_changed)(OakEngineProject *project, void *userdata);
	void (*tool_changed)(int tool, void *userdata);
	void (*addable_object_changed)(int object, void *userdata);
	void (*snapping_changed)(int snapping, void *userdata);
	void (*timecode_display_changed)(int display, void *userdata);
	void (*open_recent_list_changed)(void *userdata);
	void (*color_picker_enabled)(int enabled, void *userdata);
} OakEngineAppCallbacks;

/**
 * @brief Create the application engine core with the given startup params.
 *
 * `params` may be NULL for defaults (normal run, no startup project). Only
 * one application core may exist per process: if one already exists (either
 * from an earlier oakengine_app_create() or from the EngineCore shell that
 * oakengine_init() creates), OAKENGINE_E_STATE is returned.
 *
 * The core is never destroyed; it backs the process-wide engine singleton.
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_STATE if a core exists.
 */
OAKENGINE_API int oakengine_app_create(const OakEngineAppParams *params);

/**
 * @brief Start the engine services for the application (config, locale,
 * managers, autorecovery timer, recent projects list).
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_STATE if no core exists or
 * the application core was already started.
 */
OAKENGINE_API int oakengine_app_start(void);

/**
 * @brief Stop the engine services started by oakengine_app_start().
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_STATE if the application
 * core was not started.
 */
OAKENGINE_API int oakengine_app_stop(void);

/**
 * @brief Register the UI handler/notification callback set.
 *
 * The struct is copied; NULL clears all callbacks and restores the headless
 * default behavior.
 *
 * @return OAKENGINE_OK.
 */
OAKENGINE_API int
oakengine_app_set_callbacks(const OakEngineAppCallbacks *callbacks);

/**
 * @brief Startup parameter accessors (valid once a core exists).
 *
 * oakengine_app_run_mode() returns an OAKENGINE_APP_RUN_* value,
 * oakengine_app_fullscreen() a boolean; both return OAKENGINE_E_INVALID when
 * no core exists. oakengine_app_startup_project() uses the buf/size
 * convention.
 */
OAKENGINE_API int oakengine_app_run_mode(void);
OAKENGINE_API int oakengine_app_fullscreen(void);
OAKENGINE_API int oakengine_app_startup_project(char *buf, int buf_size);

/**
 * @brief Process-wide undo stack as an opaque pointer (an
 * olive::UndoStack *). Returns NULL when no core exists.
 */
OAKENGINE_API void *oakengine_app_undo_stack(void);

/**
 * @brief Current tool as an olive::Tool::Item value (int).
 */
OAKENGINE_API int oakengine_app_tool(void);

/**
 * @brief Set the current tool. Valid values are 0 <= tool < Tool::k_count.
 * Emits the tool_changed notification.
 */
OAKENGINE_API int oakengine_app_set_tool(int tool);

/**
 * @brief Currently selected addable object (olive::Tool::AddableObject).
 */
OAKENGINE_API int oakengine_app_addable_object(void);

/**
 * @brief Set the addable object. Valid values are 0 <= object <
 * Tool::k_addable_count. Emits addable_object_changed.
 */
OAKENGINE_API int oakengine_app_set_addable_object(int object);

/**
 * @brief Currently selected transition id (buf/size convention).
 */
OAKENGINE_API int oakengine_app_selected_transition(char *buf, int buf_size);

/**
 * @brief Set the selected transition id (NULL clears it).
 */
OAKENGINE_API int oakengine_app_set_selected_transition(const char *id);

/**
 * @brief Current snapping setting (boolean).
 */
OAKENGINE_API int oakengine_app_snapping(void);

/**
 * @brief Set snapping. Emits snapping_changed.
 */
OAKENGINE_API int oakengine_app_set_snapping(int enabled);

/**
 * @brief Current timecode display mode (olive::core::Timecode::Display).
 */
OAKENGINE_API int oakengine_app_timecode_display(void);

/**
 * @brief Set the timecode display mode (0 <= display <= 4). Emits
 * timecode_display_changed.
 */
OAKENGINE_API int oakengine_app_set_timecode_display(int display);

/**
 * @brief Number of entries in the recently opened/saved projects list.
 */
OAKENGINE_API int oakengine_app_recent_projects_count(void);

/**
 * @brief Path of the recent-project entry at `index` (buf/size convention).
 *
 * @return the string length, or OAKENGINE_E_NOT_FOUND for an invalid index.
 */
OAKENGINE_API int oakengine_app_recent_project_at(int index, char *buf,
												  int buf_size);

/**
 * @brief Remove the recent-project entry at `index`. Emits
 * open_recent_list_changed.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_NOT_FOUND for an invalid index.
 */
OAKENGINE_API int oakengine_app_remove_recent_project(int index);

/**
 * @brief Clear the recent projects list. Emits open_recent_list_changed.
 */
OAKENGINE_API int oakengine_app_clear_recent_projects(void);

/**
 * @brief Show a message in the status bar (delivered through the
 * status_message_show callback).
 */
OAKENGINE_API int oakengine_app_show_status_message(const char *message,
													int timeout);

/**
 * @brief Clear the status bar (delivered through the status_message_clear
 * callback).
 */
OAKENGINE_API int oakengine_app_clear_status_message(void);

/**
 * @brief Change the current language.
 *
 * @return 1 if a translation for `locale` was found and installed, 0 if
 * not, OAKENGINE_E_INVALID for NULL or when no core exists.
 */
OAKENGINE_API int oakengine_app_set_language(const char *locale);

/**
 * @brief Set how frequently an autorecovery is saved (minutes).
 */
OAKENGINE_API int oakengine_app_set_autorecovery_interval(int minutes);

/**
 * @brief Globally enable/disable decoding from proxy media.
 */
OAKENGINE_API int oakengine_app_set_use_proxy_media(int enabled);

/**
 * @brief Add/remove a pixel-sampling user. Emits color_picker_enabled when
 * the user count crosses 0.
 */
OAKENGINE_API int oakengine_app_request_pixel_sampling(int enable);

/**
 * @brief Debug "magic" flag accessors.
 */
OAKENGINE_API int oakengine_app_set_magic(int enabled);
OAKENGINE_API int oakengine_app_is_magic_enabled(void);

/**
 * @brief Copy a string to the system clipboard.
 */
OAKENGINE_API int oakengine_app_copy_to_clipboard(const char *text);

/**
 * @brief Paste a string from the system clipboard (buf/size convention).
 */
OAKENGINE_API int oakengine_app_paste_from_clipboard(char *buf, int buf_size);

/**
 * @brief File filter for footage import dialogs (buf/size convention).
 */
OAKENGINE_API int oakengine_app_footage_file_dialog_filter(char *buf,
														   int buf_size);

/**
 * @brief Whether `path` has an extension allowed for footage import.
 *
 * @return 1/0, or OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int oakengine_app_is_footage_extension_allowed(const char *path);

/**
 * @brief Create a new sequence named appropriately for `project`.
 *
 * `name_format` is a QString::arg() pattern (e.g. "Sequence %1"); NULL uses
 * the default "Sequence %1". The returned handle is owned by the caller
 * (it is not yet added to the project). Returns NULL on invalid input.
 */
OAKENGINE_API OakEngineSequence *
oakengine_app_create_sequence(OakEngineProject *project,
							  const char *name_format);

/**
 * @brief Path of the autorecovery index file (buf/size convention).
 */
OAKENGINE_API int oakengine_app_auto_recovery_index_filename(char *buf,
															 int buf_size);

/**
 * @brief Currently open project (borrowed handle, may be NULL).
 */
OAKENGINE_API OakEngineProject *oakengine_app_open_project(void);

/**
 * @brief Close the current project (through the close_project handler) and
 * open a new empty one.
 */
OAKENGINE_API int oakengine_app_create_new_project(void);

/**
 * @brief Open an already-loaded project, closing the current one first.
 * Pushes it to the recent list when `add_to_recents` is set and the project
 * has a filename.
 */
OAKENGINE_API int oakengine_app_add_open_project(OakEngineProject *project,
												 int add_to_recents);

/**
 * @brief Adopt the project loaded by a project-load task (an olive::Task *
 * as an opaque pointer).
 *
 * @return 1 if the project was opened, 0 if the load was cancelled or the
 * footage validation was rejected, OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int oakengine_app_add_open_project_from_task(void *task,
														   int add_to_recents);

/**
 * @brief Adopt an autorecovery project loaded by a project-load task (an
 * olive::Task * as an opaque pointer).
 *
 * @return 1 on success, 0 otherwise, OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int oakengine_app_add_recovery_project_from_task(void *task);

/**
 * @brief Update engine state after `project` was successfully saved (recent
 * list, modified flag, unrecovered list).
 */
OAKENGINE_API int oakengine_app_on_project_saved(OakEngineProject *project);

/**
 * @brief Set the active (open) project. Emits active_project_changed.
 * `project` may be NULL.
 */
OAKENGINE_API int oakengine_app_set_active_project(OakEngineProject *project);

/**
 * @brief Convenience wrapper: set just the confirm-image-sequence handler
 * (same as setting cb.confirm_image_sequence in oakengine_app_set_callbacks).
 * Replaces both fn and userdata.
 */
OAKENGINE_API int oakengine_app_set_confirm_image_sequence_handler(
    int (*fn)(const char *filename, void *userdata), void *userdata);

/**
 * @brief Convenience wrapper: set just the relink handler.
 */
OAKENGINE_API int oakengine_app_set_relink_handler(
    int (*fn)(OakEngineFootage **footage, int count, void *userdata),
    void *userdata);

/**
 * @brief Convenience wrapper: set just the save-project handler.
 */
OAKENGINE_API int oakengine_app_set_save_project_handler(
    void (*fn)(const char *override_filename, void *userdata), void *userdata);

/**
 * @brief Convenience wrapper: set just the close-project handler.
 */
OAKENGINE_API int oakengine_app_set_close_project_handler(
    int (*fn)(void *userdata), void *userdata);

/**
 * @brief Convenience wrapper: set just the load-layout handler.
 */
OAKENGINE_API int oakengine_app_set_load_layout_handler(
    void (*fn)(const void *layout, void *userdata), void *userdata);

/**
 * @brief Alias for oakengine_app_auto_recovery_index_filename().
 */
OAKENGINE_API int oakengine_app_get_auto_recovery_index_filename(char *buf,
                                                                 int buf_size);

/**
 * @brief Alias for oakengine_app_remove_recent_project().
 */
OAKENGINE_API int oakengine_app_remove_recently_opened_project(int index);

/**
 * @brief void*-based overload of oakengine_app_on_project_saved() for use
 * from app code that holds a opaque QObject pointer.
 */
OAKENGINE_API int oakengine_app_on_project_saved_vp(void *project);

/**
 * @brief void*-based overload of oakengine_app_set_active_project().
 */
OAKENGINE_API int oakengine_app_set_active_project_vp(void *project);

/**
 * @brief void*-based overload of oakengine_app_add_open_project().
 */
OAKENGINE_API int oakengine_app_add_open_project_vp(void *project,
                                                    int add_to_recents);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_APP_H */
