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

#ifndef OAK_EDITOR_RENDER_CANCELATOM_H
#define OAK_EDITOR_RENDER_CANCELATOM_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Current ABI version stamped into every oakrender handle.
 *
 * Bump whenever a handle layout or the semantics of any exported function
 * change incompatibly. Consumers should compare a handle's abi_version
 * field against the value they were compiled with before dereferencing
 * ctx.
 */
#define OAKRENDER_ABI_VERSION 1

/**
 * @file cancelatom.h
 * @brief C ABI for the oakrender cancellation primitive
 *        (olive::CancelAtom), a thread-safe cancel flag shared between a
 *        render/encode caller and its worker.
 *
 * OakCancelAtom follows the neutral by-value handle convention (see
 * oakcommon's common/handle.h): oakrender_cancelatom_init() returns a
 * handle whose underlying object has reference count 1, the addref and
 * release function pointers adjust that count atomically (release
 * destroys the object at zero), and abi_version is always
 * OAKRENDER_ABI_VERSION. Copying the struct copies the pointer, not the
 * count: call addref for every additional long-lived copy and release (or
 * oakrender_cancelatom_free()) when done with each copy. Functions that
 * only use a handle take it BY VALUE; an empty handle (ctx == NULL) is
 * reported as OAKRENDER_E_INVALID.
 */
typedef struct OakCancelAtom {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakCancelAtom;

/**
 * @brief Create a cancellation atom in the not-cancelled state.
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakCancelAtom oakrender_cancelatom_init(void);

/**
 * @brief Release one reference to a cancellation atom.
 *
 * Convenience wrapper around atom->release(atom->ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero,
 * then nulls atom->ctx. No-op when atom is NULL or atom->ctx is NULL.
 */
void oakrender_cancelatom_free(OakCancelAtom *atom);

/**
 * @brief Set the cancel flag (CancelAtom::cancel()). Thread-safe.
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for an empty handle.
 */
int oakrender_cancelatom_cancel(OakCancelAtom atom);

/**
 * @brief Read the cancel flag (CancelAtom::is_cancelled()).
 *
 * Reading a set flag also records that a consumer heard the
 * cancellation; see oakrender_cancelatom_heard_cancel().
 *
 * @param cancelled Receives 1 when cancelled, 0 otherwise.
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for an empty handle or a
 *         NULL out parameter.
 */
int oakrender_cancelatom_is_cancelled(OakCancelAtom atom, int *cancelled);

/**
 * @brief Whether any consumer has observed the cancel flag through
 *        oakrender_cancelatom_is_cancelled() (CancelAtom::heard_cancel()).
 *
 * @param heard Receives 1 when the cancellation was heard, 0 otherwise.
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for an empty handle or a
 *         NULL out parameter.
 */
int oakrender_cancelatom_heard_cancel(OakCancelAtom atom, int *heard);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_CANCELATOM_H
