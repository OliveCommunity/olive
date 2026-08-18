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

#ifndef OAK_EDITOR_HANDLE_H
#define OAK_EDITOR_HANDLE_H

#include <stdint.h>

/**
 * @brief Current ABI version stamped into every oakcommon handle.
 *
 * Bump whenever the handle layout or the semantics of any exported
 * function change incompatibly. Consumers should compare a handle's
 * abi_version field against the value they were compiled with before
 * dereferencing ctx.
 */
#define OAKCOMMON_ABI_VERSION 1

/**
 * @brief Neutral handle convention shared by all oakcommon wrappers.
 *
 * Every wrapper type is a by-value struct with the same four fields:
 *
 *     typedef struct OakXxx {
 *         void *ctx;                       // opaque, points to the impl
 *         void (*addref)(void *ctx);       // atomic +1, owner-DLL code
 *         void (*release)(void *ctx);      // atomic -1, destroys at 0
 *         uint32_t abi_version;            // OAKCOMMON_ABI_VERSION
 *     } OakXxx;
 *
 * Rules:
 * - oakcommon_<name>_init*() returns a handle whose underlying object
 *   has reference count 1.
 * - Copying the struct copies the pointer, not the count: call
 *   handle.addref(handle.ctx) for every additional long-lived copy and
 *   handle.release(handle.ctx) (or the oakcommon_<name>_free()
 *   convenience wrapper) when done with each copy.
 * - release() decrements the atomic count and destroys the underlying
 *   object when it reaches zero; the destructor runs in the DLL that
 *   created the object, so cross-DLL handing is safe.
 * - The struct itself carries no ownership: it is never heap-allocated
 *   by the API, so it needs no destruction of its own.
 * - Functions that only read a handle take it BY VALUE (OakXxx self);
 *   an empty handle (ctx == NULL) is reported as OAKCOMMON_E_INVALID.
 *   oakcommon_<name>_free() deliberately stays a pointer API
 *   (OakXxx *h, like av_frame_unref()/av_buffer_unref()) so it can
 *   null out the caller's ctx after the final release; NULL and
 *   ctx == NULL are no-ops. Out parameters that produce a handle
 *   (e.g. option/positional-argument registration) also stay pointers.
 */

#endif //OAK_EDITOR_HANDLE_H
