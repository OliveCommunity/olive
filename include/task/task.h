/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_EDITOR_TASK_TASK_H
#define OAK_EDITOR_TASK_TASK_H

#include <stdint.h>

#include "task/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a background task (olive::Task).
 *
 * Tasks are created through the factories in task/project.h (and future
 * family headers) and must be released with oaktask_task_free().
 */
typedef struct OakTaskTask OakTaskTask;

/** @brief Lifecycle event ids for oaktask_task_subscribe(). */
enum OakTaskEvent {
	OAKTASK_EVENT_STARTED = 0,
	OAKTASK_EVENT_PROGRESS = 1,
	OAKTASK_EVENT_FINISHED = 2
};

/**
 * @brief Event callback (async command return channel, 01 §4 exception).
 *
 * For OAKTASK_EVENT_FINISHED, `value` is 1.0 on success / 0.0 on failure;
 * for OAKTASK_EVENT_PROGRESS it is 0..1; for OAKTASK_EVENT_STARTED it is
 * the start time in milliseconds.
 */
typedef void (*oaktask_event_fn)(int event_id, double value,
								 void *userdata);

/**
 * @brief Free a task. No-op on NULL. The task must not be running on the
 *        manager (oaktask_task_cancel + wait first if it is).
 */
void oaktask_task_free(OakTaskTask *t);

/** @brief Run synchronously in the calling thread. 1 = succeeded. */
int oaktask_task_start_sync(OakTaskTask *t);

/** @brief Run asynchronously on the task manager. */
int oaktask_task_start(OakTaskTask *t);

int oaktask_task_cancel(OakTaskTask *t);

/** @brief Wait for an asynchronously started task. */
int oaktask_task_wait(OakTaskTask *t);

int oaktask_task_is_finished(const OakTaskTask *t);

int oaktask_task_succeeded(const OakTaskTask *t);

/** @brief Two-stage string getters. */
int oaktask_task_title(OakTaskTask *t, char *buf, int buf_size);
int oaktask_task_error(OakTaskTask *t, char *buf, int buf_size);

/**
 * @brief Subscribe to lifecycle events (returns a subscription id >= 0,
 *        or a negative error code). One-shot per event stream: the
 *        subscription is dropped after OAKTASK_EVENT_FINISHED.
 */
int64_t oaktask_task_subscribe(OakTaskTask *t, oaktask_event_fn fn,
							   void *userdata);

/** @brief Alive-count for leak assertions in tests. */
int oaktask_debug_alive_count(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TASK_TASK_H
