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

#ifndef OAK_EDITOR_ERROR_H
#define OAK_EDITOR_ERROR_H

/**
 * @brief Status and error codes shared by all oakcommon C API families.
 *
 * Return-code convention (mirrors engine/include/oakengine/init.h):
 * 0 (OAKCOMMON_OK) on success, a negative OAKCOMMON_E_* error code on
 * failure. String getters return the required buffer size in bytes
 * (including the terminating NUL) as a non-negative value instead.
 *
 * Project-wide error code scheme (-MMCCCC, 2026-08):
 * every module's error codes are negative integers of the form
 * -(MM * 10000 + CCCC), where MM is the module number from the registry
 * below and CCCC is a module-local code. The first module-local codes
 * are reserved and identical across modules: 0001 INVALID, 0002 STATE,
 * 0003 FAILED, 0004 NOT_FOUND, 0005 NOMEM, 0006 CANCELLED.
 *
 * An error code crossing a module boundary is passed through
 * UNTRANSLATED — the numeric module prefix preserves provenance
 * (e.g. -30004 is oaknode's NOT_FOUND no matter which module reports it
 * to the caller).
 *
 * Module number registry (only ever appended to; numbers are frozen):
 */
#define OAK_ERROR_MODULE_COMMON 1   /**< oakcommon */
#define OAK_ERROR_MODULE_UNDO 2     /**< oakundo */
#define OAK_ERROR_MODULE_NODE 3     /**< oaknode */
#define OAK_ERROR_MODULE_TIMELINE 4 /**< oaktimeline */
#define OAK_ERROR_MODULE_CODEC 5    /**< oakcodec */
#define OAK_ERROR_MODULE_AUDIO 6    /**< oakaudio */
#define OAK_ERROR_MODULE_RENDER 7   /**< oakrender */
#define OAK_ERROR_MODULE_TASK 8     /**< oaktask */
#define OAK_ERROR_MODULE_PLUGIN 9   /**< oakplugin */
#define OAK_ERROR_MODULE_STORAGE 10 /**< oakstorage (reserved) */

#define OAKCOMMON_OK 0 /**< Success. */
#define OAKCOMMON_E_INVALID (-10001) /**< Empty handle (ctx == NULL) or invalid argument. */
#define OAKCOMMON_E_STATE (-10002) /**< Call not valid in the current state. */
#define OAKCOMMON_E_FAILED (-10003) /**< The underlying operation failed. */
#define OAKCOMMON_E_NOT_FOUND (-10004) /**< Index out of range / entry not found. */
#define OAKCOMMON_E_NOMEM (-10005) /**< Allocation failed. */

#define SUCCESS OAKCOMMON_OK /**< @deprecated Use OAKCOMMON_OK. */

#endif //OAK_EDITOR_ERROR_H
