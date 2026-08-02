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

#ifndef OAKENGINE_TASK_H
#define OAKENGINE_TASK_H

#include <stdint.h>

#include "encoding.h"
#include "init.h"
#include "node.h"
#include "project.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file task.h
 * @brief C ABI for the engine background-task system (olive::Task /
 * olive::TaskManager)
 *
 * Tasks are engine objects that run a job (project load/save, footage
 * import, proxy generation, export) on a worker thread. This family lets
 * C consumers create the concrete task they need, run it synchronously or
 * hand it to the global TaskManager queue, and observe its lifecycle
 * through the event mechanism (oakengine/events.h, task family
 * OAKENGINE_EVENT_TASK_* and manager family
 * OAKENGINE_EVENT_TASK_MANAGER_*), without ever seeing the C++ classes.
 *
 * Conventions (matching oakengine/project.h):
 *   - OakEngineTask is an opaque borrowed/owned pointer to an
 *     olive::Task subclass.
 *   - A task returned by an oakengine_task_create_*() function is OWNED by
 *     the caller until either oakengine_task_manager_add() (the manager
 *     takes ownership and deletes the task when done) or
 *     oakengine_task_free() (the caller deletes it). A task that ran via
 *     oakengine_task_start_sync() is still owned by the caller and must be
 *     released with oakengine_task_free() (or handed to the manager,
 *     though re-running is unusual).
 *   - Once a task was added to the manager its handle must be treated as
 *     borrowed: the manager may delete it at any time after the
 *     OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED notification.
 *   - Return codes: 0 (OAKENGINE_OK) on success, negative OAKENGINE_E_*
 *     on failure. String output uses the buf/size convention (return value
 *     is the length that would have been written excluding the NUL; a
 *     negative value is an OAKENGINE_E_* error).
 */

/**
 * @brief Opaque task handle (an olive::Task subclass instance).
 */
typedef struct OakEngineTask OakEngineTask;

/* ---- Global task manager ------------------------------------------------- */

/**
 * @brief Borrowed handle of the global TaskManager singleton, for use as
 * the subscription handle of the OAKENGINE_EVENT_TASK_MANAGER_* events.
 * Returns NULL when the engine is not initialized.
 */
OAKENGINE_API void *oakengine_task_manager_handle(void);

/**
 * @brief Number of tasks currently known to the manager (running plus
 * failed-but-kept), or OAKENGINE_E_INVALID when no manager exists.
 */
OAKENGINE_API int oakengine_task_manager_count(void);

/**
 * @brief Borrowed handle of an arbitrary running task (the manager's
 * "first" task, used by the status bar), or NULL when the queue is empty
 * or no manager exists.
 */
OAKENGINE_API OakEngineTask *oakengine_task_manager_first(void);

/**
 * @brief Hand `task` to the global manager queue (takes ownership). The
 * task starts as soon as a worker thread is available.
 *
 * @return OAKENGINE_OK, OAKENGINE_E_INVALID for NULL, OAKENGINE_E_STATE
 * when no manager exists.
 */
OAKENGINE_API int oakengine_task_manager_add(OakEngineTask *task);

/**
 * @brief Ask the manager to cancel `task` (TaskManager::cancel_task
 * semantics: a running task is signalled; a failed-but-kept task is
 * removed and deleted).
 */
OAKENGINE_API int oakengine_task_manager_cancel(OakEngineTask *task);

/* ---- Task accessors ------------------------------------------------------ */

/**
 * @brief Title of `task` (buf/size convention).
 */
OAKENGINE_API int oakengine_task_title(OakEngineTask *task, char *buf,
									   int buf_size);

/**
 * @brief Error message of `task` (buf/size convention). Meaningful after a
 * failed run.
 */
OAKENGINE_API int oakengine_task_error(OakEngineTask *task, char *buf,
									   int buf_size);

/**
 * @brief Start timestamp of `task` (milliseconds since epoch), 0 when the
 * task never started, OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int64_t oakengine_task_start_time(OakEngineTask *task);

/**
 * @brief 1 when `task` was asked to cancel, 0 otherwise,
 * OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int oakengine_task_is_cancelled(OakEngineTask *task);

/**
 * @brief Signal `task` to cancel as soon as possible (Task::Cancel).
 */
OAKENGINE_API int oakengine_task_cancel(OakEngineTask *task);

/**
 * @brief Run `task` synchronously on the CALLING thread (Task::start).
 * Emits the task events on this thread. Ownership stays with the caller.
 *
 * @return 1 when the task succeeded, 0 when it failed or was cancelled
 * (read oakengine_task_error()), OAKENGINE_E_INVALID for NULL.
 */
OAKENGINE_API int oakengine_task_start_sync(OakEngineTask *task);

/**
 * @brief Delete a task that was never added to the manager.
 */
OAKENGINE_API int oakengine_task_free(OakEngineTask *task);

/**
 * @brief Run `task` through the engine's CLI modal progress dialog and return
 * 1 when it succeeds, 0 when it fails or is cancelled.
 *
 * The dialog shows the task's title and progress on the terminal. `parent`
 * is an optional QObject parent (may be NULL). The task is started
 * synchronously; the caller retains ownership and must free it with
 * oakengine_task_free() when done.
 */
OAKENGINE_API int oakengine_cli_task_dialog_run(OakEngineTask *task,
                                                void *parent_or_NULL);

/* ---- Task creators --------------------------------------------------------
 *
 * All creators return an OWNED task (NULL on invalid input). The task is
 * not started by creation.
 */

/**
 * @brief Task that loads an OVE project from `filename`.
 */
OAKENGINE_API OakEngineTask *
oakengine_task_create_project_load(const char *filename);

/**
 * @brief Task that loads an OpenTimelineIO project from `filename`.
 * Returns NULL when the engine was built without OTIO support.
 */
OAKENGINE_API OakEngineTask *
oakengine_task_create_project_load_otio(const char *filename);

/**
 * @brief Task that saves `project` (ProjectSaveTask semantics).
 *
 * `use_compression` selects the compressed .ove writer (0 writes the
 * uncompressed .ovexml form). `override_filename` may be NULL to save to
 * the project's own filename. `layout` is an opaque
 * `const olive::SerializedLayoutInfo *` (may be NULL) whose contents are
 * copied into the saved file.
 */
OAKENGINE_API OakEngineTask *oakengine_task_create_project_save(
	OakEngineProject *project, int use_compression,
	const char *override_filename, const void *layout);

/**
 * @brief Task that saves `project` in OpenTimelineIO format. Returns NULL
 * when the engine was built without OTIO support.
 */
OAKENGINE_API OakEngineTask *
oakengine_task_create_project_save_otio(OakEngineProject *project);

/**
 * @brief Task that imports `url_count` media files into `folder` (a folder
 * node of the target project; use oakengine_project_root() for the top
 * level). The URL array is copied during the call.
 */
OAKENGINE_API OakEngineTask *oakengine_task_create_project_import(
	OakEngineNode *folder, const char **urls, int url_count);

/**
 * @brief Task that generates the proxy media for `footage` (a footage node
 * handle, as accepted by oakengine_footage_borrow(); the task keeps the
 * underlying node).
 */
OAKENGINE_API OakEngineTask *
oakengine_task_create_proxy(OakEngineNode *footage);

/**
 * @brief Task that renders an export of `sequence` with `params`.
 *
 * Takes ownership of `params` (destroyed with the task). Progress is
 * reported through the OAKENGINE_EVENT_TASK_PROGRESS event; cancelling
 * the task cancels the engine export render.
 */
OAKENGINE_API OakEngineTask *oakengine_task_create_export(
	OakEngineSequence *sequence, OakEngineEncodingParams *params);

/* ---- Import task results --------------------------------------------------
 *
 * Valid on a task created by oakengine_task_create_project_import() after
 * it ran; all return OAKENGINE_E_INVALID (or 0/NULL) for other tasks.
 */

/**
 * @brief Number of files the import task will process (valid right after
 * creation; 0 means "nothing to import" and the task should be freed
 * instead of run).
 */
OAKENGINE_API int oakengine_task_import_file_count(OakEngineTask *task);

/**
 * @brief The undo command built by a successful import run as an opaque
 * `olive::MultiUndoCommand *` (NULL before the run, after a cancelled
 * run, or on a second call). Ownership is DETACHED from the task and
 * passes to the caller: push it with oakengine_undo_push() or delete it.
 */
OAKENGINE_API void *oakengine_task_import_get_command(OakEngineTask *task);

/**
 * @brief Number of footage items a successful import run created.
 */
OAKENGINE_API int oakengine_task_import_footage_count(OakEngineTask *task);

/**
 * @brief Borrowed node handle of the imported footage item at `index`
 * (NULL when out of range).
 */
OAKENGINE_API OakEngineNode *
oakengine_task_import_footage_at(OakEngineTask *task, int index);

/**
 * @brief Number of files the import task rejected.
 */
OAKENGINE_API int
oakengine_task_import_invalid_files_count(OakEngineTask *task);

/**
 * @brief Rejected file path at `index` (buf/size convention).
 */
OAKENGINE_API int oakengine_task_import_invalid_file_at(OakEngineTask *task,
														int index, char *buf,
														int buf_size);

/* ---- Save task results ---------------------------------------------------- */

/**
 * @brief Borrowed handle of the project a save task wrote (NULL for other
 * tasks).
 */
OAKENGINE_API OakEngineProject *
oakengine_task_save_get_project(OakEngineTask *task);

#ifdef __cplusplus
}
#include <QtCore/qmetatype.h>
Q_DECLARE_OPAQUE_POINTER(OakEngineTask *)
#endif

#endif /* OAKENGINE_TASK_H */
