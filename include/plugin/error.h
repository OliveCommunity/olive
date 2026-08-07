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

#ifndef OAK_EDITOR_PLUGIN_ERROR_H
#define OAK_EDITOR_PLUGIN_ERROR_H

#include <stdint.h>

/**
 * @brief Status and error codes shared by all oakplugin C API families.
 */
#define OAKPLUGIN_OK 0 /**< Success. */
#define OAKPLUGIN_E_INVALID (-1) /**< NULL handle or invalid argument. */
#define OAKPLUGIN_E_STATE (-2) /**< Call not valid in the current state. */
#define OAKPLUGIN_E_FAILED (-3) /**< The underlying operation failed. */
#define OAKPLUGIN_E_NOT_FOUND (-4) /**< Entry not found. */
#define OAKPLUGIN_E_NOMEM (-5) /**< Allocation failed. */
#define OAKPLUGIN_E_CANCELLED (-6) /**< The operation was cancelled. */

/** @brief ABI version stamped into every oakplugin handle. */
#define OAKPLUGIN_ABI_VERSION 1

#endif //OAK_EDITOR_PLUGIN_ERROR_H
