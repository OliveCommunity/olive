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

#ifndef OAK_EDITOR_TASK_ERROR_H
#define OAK_EDITOR_TASK_ERROR_H

/**
 * @brief Status and error codes shared by all oaktask C API families.
 *
 * Return-code convention (mirrors engine/include/oakengine/init.h):
 * 0 (OAKTASK_OK) on success, a negative OAKTASK_E_* error code on
 * failure. String getters return the required buffer size in bytes
 * (including the terminating NUL) as a non-negative value instead.
 */
#define OAKTASK_ABI_VERSION 1

#define OAKTASK_OK 0 /**< Success. */
#define OAKTASK_E_INVALID (-80001) /**< NULL handle or invalid argument. */
#define OAKTASK_E_STATE (-80002) /**< Call not valid in the current state. */
#define OAKTASK_E_FAILED (-80003) /**< The underlying operation failed. */
#define OAKTASK_E_NOT_FOUND (-80004) /**< Index out of range / entry not found. */
#define OAKTASK_E_NOMEM (-80005) /**< Allocation failed. */
#define OAKTASK_E_CANCELLED (-80006) /**< The operation was cancelled. */

#endif //OAK_EDITOR_TASK_ERROR_H
