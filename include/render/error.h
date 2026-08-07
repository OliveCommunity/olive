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

#ifndef OAK_EDITOR_RENDER_ERROR_H
#define OAK_EDITOR_RENDER_ERROR_H

/**
 * @brief Status and error codes shared by all oakrender C API families.
 *
 * Return-code convention (mirrors include/node/error.h):
 * 0 (OAKRENDER_OK) on success, a negative OAKRENDER_E_* error code on
 * failure. String getters return the required buffer size in bytes
 * (including the terminating NUL) as a non-negative value instead.
 */
/**
 * @brief Current ABI version stamped into every oakrender handle.
 *
 * Bump whenever a handle layout or the semantics of any exported function
 * change incompatibly. Consumers should compare a handle's abi_version
 * field against the value they were compiled with before dereferencing
 * ctx.
 */
#define OAKRENDER_ABI_VERSION 1

#define OAKRENDER_OK 0 /**< Success. */
#define OAKRENDER_E_INVALID (-1) /**< NULL handle or invalid argument. */
#define OAKRENDER_E_STATE (-2) /**< Call not valid in the current state. */
#define OAKRENDER_E_FAILED (-3) /**< The underlying operation failed. */
#define OAKRENDER_E_NOT_FOUND (-4) /**< Index out of range / entry not found. */
#define OAKRENDER_E_NOMEM (-5) /**< Allocation failed. */

#endif //OAK_EDITOR_RENDER_ERROR_H
