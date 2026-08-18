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

#ifndef OAK_EDITOR_TASK_MANAGER_H
#define OAK_EDITOR_TASK_MANAGER_H

#include "task/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task manager singleton lifecycle.
 */
int oaktask_manager_init(void);
void oaktask_manager_shutdown(void);

/**
 * @brief Register oaktask as oakcodec's background task submitter
 *        (olive::register_codec_task_submitter()). Called by
 *        oaktask_manager_init(); exposed for manual control.
 */
int oaktask_register_codec_submitter(void);

int oaktask_manager_count(void);

/** @brief Borrowed task at index (release only frees the box), empty
 *        handle when out of range or no manager. */
OakTaskTask oaktask_manager_at(int i);

void oaktask_manager_delete_finished(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TASK_MANAGER_H
