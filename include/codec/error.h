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

#ifndef OAK_EDITOR_CODEC_ERROR_H
#define OAK_EDITOR_CODEC_ERROR_H

/**
 * @brief Status and error codes shared by all oakcodec C API families.
 *
 * Return-code convention (mirrors the other split modules):
 * 0 (OAKCODEC_OK) on success, a negative OAKCODEC_E_* error code on
 * failure. String getters return the required buffer size in bytes
 * (including the terminating NUL) as a non-negative value instead.
 */
#define OAKCODEC_OK 0 /**< Success. */
#define OAKCODEC_E_INVALID (-1) /**< NULL handle or invalid argument. */
#define OAKCODEC_E_STATE (-2) /**< Call not valid in the current state. */
#define OAKCODEC_E_FAILED (-3) /**< The underlying operation failed. */
#define OAKCODEC_E_NOT_FOUND (-4) /**< Index out of range / entry not found. */
#define OAKCODEC_E_NOMEM (-5) /**< Allocation failed. */
#define OAKCODEC_E_CANCELLED (-6) /**< The operation was cancelled. */

/**
 * @brief Current ABI version stamped into every oakcodec handle.
 *
 * Bump whenever the handle layout or the semantics of any exported
 * function change incompatibly. Consumers should compare a handle's
 * abi_version field against the value they were compiled with before
 * dereferencing ctx.
 */
#define OAKCODEC_ABI_VERSION 1

/**
 * @brief Export macro for the oakcodec C ABI.
 *
 * oakcodec is built with -fvisibility=hidden (01 §1 rule 5): only the
 * oakcodec_* functions marked with this macro leave the shared library.
 * This also keeps codec-internal C++ classes (whose olive::* names may
 * collide with transition stubs inside other modules) from participating
 * in cross-library weak-symbol coalescing.
 */
#define OAKCODEC_API __attribute__((visibility("default")))

#endif //OAK_EDITOR_CODEC_ERROR_H
