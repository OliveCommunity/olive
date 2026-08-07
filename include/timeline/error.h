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

#ifndef OAK_EDITOR_TIMELINE_ERROR_H
#define OAK_EDITOR_TIMELINE_ERROR_H

/**
 * @brief Status and error codes shared by all oaktimeline C API families.
 *
 * Return-code convention (mirrors engine/include/oakengine/init.h):
 * 0 (OAKTIMELINE_OK) on success, a negative OAKTIMELINE_E_* error code on
 * failure. String getters return the required buffer size in bytes
 * (including the terminating NUL) as a non-negative value instead.
 */
#define OAKTIMELINE_ABI_VERSION 1

#define OAKTIMELINE_OK 0 /**< Success. */
#define OAKTIMELINE_E_INVALID (-1) /**< NULL handle or invalid argument. */
#define OAKTIMELINE_E_STATE (-2) /**< Call not valid in the current state. */
#define OAKTIMELINE_E_FAILED (-3) /**< The underlying operation failed. */
#define OAKTIMELINE_E_NOT_FOUND (-4) /**< Index out of range / entry not found. */
#define OAKTIMELINE_E_NOMEM (-5) /**< Allocation failed. */

#endif //OAK_EDITOR_TIMELINE_ERROR_H
