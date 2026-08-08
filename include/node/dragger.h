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

#ifndef OAK_EDITOR_NODE_DRAGGER_H
#define OAK_EDITOR_NODE_DRAGGER_H

#include <stdint.h>

#include "node/error.h"
#include "node/node.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dragger.h
 * @brief C ABI for olive::NodeInputDragger (src/node/src/inputdragger.h):
 * live drag of an input's value with a single commit command.
 *
 * A dragger wraps the engine's NodeInputDragger state machine
 * (start -> drag* -> end). start() records the drag anchor and, when the
 * input is keyframing, creates one keyframe at the drag time (on every
 * track when requested); drag() live-sets the dragged component (clamped
 * by the input's min/max properties when present); end() returns ONE
 * undoable command that commits the whole drag -- undo removes the
 * created keyframe(s) (restoring the pre-drag keyframe count), redo
 * re-creates them with the final value.
 *
 * A dragger must be ended before it is freed; freeing a started dragger
 * leaks the created keyframe(s) (the same ownership rule as the C++
 * class).
 */

/**
 * @brief Reference-counted handle to an input dragger
 * (olive::NodeInputDragger).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * oaknode_dragger_create() returns a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero.
 */
typedef struct OakNodeDragger {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeDragger;

/**
 * @brief Create an input dragger for live-drag of an input's value.
 *
 * `input_id` must name an existing input of `node`; `element` addresses
 * an array input's element (-1 for non-array inputs). `track` is the
 * create-time default; the track passed to oaknode_dragger_start()
 * establishes the actual drag track.
 *
 * @return Dragger handle with count 1; ctx is NULL on invalid arguments
 *         or allocation failure.
 */
OakNodeDragger oaknode_dragger_create(OakNodeNode node, const char *input_id,
									  int element, int track);

/**
 * @brief Start the drag at the given rational time (creates a keyframe
 * when the input is keyframing).
 *
 * `insert_on_all_tracks` != 0 also creates sibling keyframes on every
 * other track of the input. OAKNODE_E_STATE when the dragger was already
 * started.
 */
int oaknode_dragger_start(OakNodeDragger dragger, int64_t time_num,
						  int64_t time_den, int track,
						  int insert_on_all_tracks);

/**
 * @brief Drag to a new per-track component value (live; no undo).
 *
 * `value` carries the dragged component of the input's declared type:
 * scalar types in f[0]/num; for split-track types (VEC2/3/4/COLOR) the
 * POD type must match the input's declared type and the dragged
 * component sits in f[0] (the facade's dragger convention). The value is
 * clamped to the input's min/max properties when present.
 * OAKNODE_E_STATE when the dragger was not started.
 */
int oaknode_dragger_drag(OakNodeDragger dragger, const oaknode_value *value);

/**
 * @brief End the drag, returning ONE undoable command for the whole drag.
 *
 * `*out_command` receives an owned command handle (execute it with
 * oakundo_command_redo_now(), push it onto an OakUndoStack, or release
 * it with oakundo_command_free()). OAKNODE_E_STATE when the dragger was
 * not started.
 */
int oaknode_dragger_end(OakNodeDragger dragger, OakUndoCommand *out_command);

/**
 * @brief 1 if the dragger has been started and not yet ended.
 */
int oaknode_dragger_is_started(OakNodeDragger dragger, int *out_started);

/**
 * @brief Release one reference to a dragger handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * dragger when the count reaches zero. NULL handle or NULL ctx is a
 * no-op; clears `dragger->ctx` after releasing. The dragger must have
 * been ended (see the file comment).
 */
void oaknode_dragger_free(OakNodeDragger *dragger);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_DRAGGER_H
