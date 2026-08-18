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

#ifndef OAK_EDITOR_CURRENT_H
#define OAK_EDITOR_CURRENT_H

#include "common/error.h"
#include "common/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to the process-wide Current singleton.
 *
 * Uses the standard handle layout (see common/handle.h) but with
 * singleton semantics: ctx points to a statically allocated object that
 * lives until process exit, so addref() and release() are intentionally
 * no-ops and never destroy anything. abi_version is always
 * OAKCOMMON_ABI_VERSION.
 */
typedef struct OakCurrent {
	void *ctx; /**< Opaque pointer to the singleton object. */
	void (*addref)(void *ctx); /**< No-op (singleton). */
	void (*release)(void *ctx); /**< No-op (singleton). */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakCurrent;

/**
 * @brief Destructor callback for objects handed to Current slots.
 *
 * Called when the slot is overwritten or cleared. May be NULL if the
 * caller keeps ownership of the object.
 */
typedef void (*OakDestroyFn)(void *obj);

/**
 * @brief Return a handle to the process-wide Current singleton.
 *
 * The returned handle is borrowed: its ctx is valid for the lifetime of
 * the process. addref/release on it are no-ops; calling
 * oakcommon_current_free() is allowed for symmetry and does nothing.
 */
OakCurrent oakcommon_current_instance(void);

/**
 * @brief Release a Current handle.
 *
 * No-op: the underlying object is a singleton whose release() never
 * destroys anything. Safe to call with NULL or a ctx == NULL handle.
 */
void oakcommon_current_free(OakCurrent *self);

/**
 * @brief Store a pointer in a Current slot, taking over destruction.
 *
 * Passing NULL for obj clears the slot (destroy is ignored). If a
 * previous object with a destroy callback was stored, it is destroyed.
 *
 * @param self Handle from oakcommon_current_instance().
 * @param obj Opaque pointer to the external object (e.g. a
 * VideoParams), or NULL to clear.
 * @param destroy Optional destructor invoked when the slot is replaced.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if self.ctx is NULL.
 */
int oakcommon_current_set_video_params(OakCurrent self, void *obj,
				       OakDestroyFn destroy);
int oakcommon_current_set_audio_params(OakCurrent self, void *obj,
				       OakDestroyFn destroy);
int oakcommon_current_set_plugin_host(OakCurrent self, void *obj,
				      OakDestroyFn destroy);
int oakcommon_current_set_plugin_cache(OakCurrent self, void *obj,
				       OakDestroyFn destroy);

/**
 * @brief Fetch the raw pointer currently stored in a slot.
 *
 * The returned pointer is borrowed and remains valid until the slot is
 * overwritten or cleared. *out is set to NULL when the slot is empty.
 *
 * @param self Handle from oakcommon_current_instance().
 * @param out Receives the stored pointer.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if self.ctx or
 * out is NULL.
 */
int oakcommon_current_get_video_params(OakCurrent self, void **out);
int oakcommon_current_get_audio_params(OakCurrent self, void **out);
int oakcommon_current_get_plugin_host(OakCurrent self, void **out);
int oakcommon_current_get_plugin_cache(OakCurrent self, void **out);

/**
 * @brief Query whether the session is interactive.
 *
 * @param self Handle from oakcommon_current_instance().
 * @param out Receives 1 for interactive, 0 otherwise.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if self.ctx or
 * out is NULL.
 */
int oakcommon_current_is_interactive(OakCurrent self, int *out);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_CURRENT_H
