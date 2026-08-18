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

#ifndef OAK_EDITOR_NODE_KEYFRAME_H
#define OAK_EDITOR_NODE_KEYFRAME_H

#include <stdint.h>

#include "node/error.h"
#include "node/node.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file keyframe.h
 * @brief C ABI for olive::NodeKeyframe (src/node/src/keyframe.h).
 *
 * An OakNodeKeyframe wraps an olive::NodeKeyframe. Handles created by
 * oaknode_keyframe_create() are owned and must be released with
 * oaknode_keyframe_free(); keyframes attached to a node input's track
 * are owned by the node.
 *
 * Every setter comes in a live variant and an undoable variant (suffix
 * _undoable) returning an owned, un-executed OakUndoCommand.
 */

/**
 * @brief Interpolation type of a keyframe (olive::NodeKeyframe::Type).
 */
typedef enum oaknode_keyframe_type {
	OAKNODE_KEYFRAME_INVALID = -1,
	OAKNODE_KEYFRAME_LINEAR = 0,
	OAKNODE_KEYFRAME_HOLD = 1,
	OAKNODE_KEYFRAME_BEZIER = 2
} oaknode_keyframe_type;

/**
 * @brief Bezier handle selector (olive::NodeKeyframe::BezierType).
 */
typedef enum oaknode_keyframe_bezier {
	OAKNODE_KEYFRAME_IN_HANDLE = 0,
	OAKNODE_KEYFRAME_OUT_HANDLE = 1
} oaknode_keyframe_bezier;

/**
 * @brief Reference-counted handle to a keyframe (olive::NodeKeyframe).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * oaknode_keyframe_create() returns a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero.
 */
typedef struct OakNodeKeyframe {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeKeyframe;

/**
 * @brief Create a standalone keyframe (owned; release with
 * oaknode_keyframe_free()).
 *
 * `value` may be NULL (null variant); OAKNODE_VALUE_STRING is rejected
 * (use oaknode_keyframe_set_value_string() after creation). `type` is an
 * oaknode_keyframe_type. `parent_or_null` may be an empty handle.
 *
 * @return Keyframe handle with count 1; ctx is NULL on invalid argument
 *         or allocation failure.
 */
OakNodeKeyframe oaknode_keyframe_create(int64_t time_num, int64_t time_den,
										const oaknode_value *value, int type,
										int track, int element,
										const char *input_id,
										OakNodeNode parent_or_null);

/**
 * @brief Release one reference to a keyframe handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * keyframe when the count reaches zero and the handle owns it. NULL
 * handle or NULL ctx is a no-op; clears `keyframe->ctx` after releasing.
 * Never free a keyframe that is attached to a node's track.
 */
void oaknode_keyframe_free(OakNodeKeyframe *keyframe);

/**
 * @brief The keyframe's time as a rational (numerator/denominator).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_keyframe_get_time(OakNodeKeyframe keyframe,
							  int64_t *out_num, int64_t *out_den);

/**
 * @brief Set the keyframe's time directly (live).
 */
int oaknode_keyframe_set_time(OakNodeKeyframe keyframe, int64_t time_num,
							  int64_t time_den);

/**
 * @brief Create a set-time command (olive::NodeParamSetKeyframeTimeCommand).
 */
int oaknode_keyframe_set_time_undoable(OakNodeKeyframe keyframe,
									   int64_t time_num, int64_t time_den,
									   OakUndoCommand *out_command);

/**
 * @brief Read the keyframe's value mapped into `out`. Values without a
 * POD representation fail with OAKNODE_E_FAILED.
 */
int oaknode_keyframe_get_value(OakNodeKeyframe keyframe,
							   oaknode_value *out);

/**
 * @brief Set the keyframe's value directly (live).
 * OAKNODE_VALUE_STRING is rejected (use
 * oaknode_keyframe_set_value_string()).
 */
int oaknode_keyframe_set_value(OakNodeKeyframe keyframe,
							   const oaknode_value *v);

/**
 * @brief Create a set-value command
 * (olive::NodeParamSetKeyframeValueCommand).
 */
int oaknode_keyframe_set_value_undoable(OakNodeKeyframe keyframe,
										const oaknode_value *v,
										OakUndoCommand *out_command);

/**
 * @brief Read a string value. Two-stage getter.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKNODE_E_* error code.
 */
int oaknode_keyframe_get_value_string(OakNodeKeyframe keyframe,
									  char *buf, int buf_size);

/**
 * @brief Set a string value directly (live).
 */
int oaknode_keyframe_set_value_string(OakNodeKeyframe keyframe,
									  const char *value);

/**
 * @brief Create a set-string-value command.
 */
int oaknode_keyframe_set_value_string_undoable(OakNodeKeyframe keyframe,
											   const char *value,
											   OakUndoCommand *out_command);

/**
 * @brief The keyframe's interpolation type (oaknode_keyframe_type).
 */
int oaknode_keyframe_get_type(OakNodeKeyframe keyframe, int *out_type);

/**
 * @brief Set the interpolation type directly (live,
 * NodeKeyframe::set_type(), which adjusts neighbouring bezier handles).
 */
int oaknode_keyframe_set_type(OakNodeKeyframe keyframe, int type);

/**
 * @brief Create a set-type command (same semantics as the live variant).
 */
int oaknode_keyframe_set_type_undoable(OakNodeKeyframe keyframe, int type,
									   OakUndoCommand *out_command);

/**
 * @brief A bezier control point (`handle` is an
 * oaknode_keyframe_bezier).
 */
int oaknode_keyframe_get_bezier_control(OakNodeKeyframe keyframe,
										int handle, double *out_x,
										double *out_y);

/**
 * @brief Set a bezier control point directly (live).
 */
int oaknode_keyframe_set_bezier_control(OakNodeKeyframe keyframe, int handle,
										double x, double y);

/**
 * @brief Create a set-bezier-control command.
 */
int oaknode_keyframe_set_bezier_control_undoable(OakNodeKeyframe keyframe,
												 int handle, double x, double y,
												 OakUndoCommand *out_command);

/**
 * @brief The keyframe's track index.
 */
int oaknode_keyframe_get_track(OakNodeKeyframe keyframe,
							   int *out_track);

/**
 * @brief The keyframe's element index.
 */
int oaknode_keyframe_get_element(OakNodeKeyframe keyframe,
								 int *out_element);

/**
 * @brief The id of the input this keyframe belongs to. Two-stage getter.
 */
int oaknode_keyframe_get_input(OakNodeKeyframe keyframe, char *buf,
							   int buf_size);

/**
 * @brief The node this keyframe belongs to (borrowed handle written to
 * `out_node`; release it with oaknode_node_free()), an empty handle when
 * orphaned. OAKNODE_OK either way.
 */
int oaknode_keyframe_get_parent(OakNodeKeyframe keyframe,
								OakNodeNode *out_node);

/**
 * @brief A bezier control point guaranteed valid for animation
 * (NodeKeyframe::valid_bezier_control_in()/out()).
 *
 * Unlike oaknode_keyframe_get_bezier_control(), the returned point is
 * clamped so the curve never overlaps: the in-handle's x cannot pass the
 * previous keyframe's time and the out-handle's x cannot pass the next
 * keyframe's time. `handle` is an oaknode_keyframe_bezier.
 */
int oaknode_keyframe_get_valid_bezier_control(OakNodeKeyframe keyframe,
											   int handle, double *out_x,
											   double *out_y);

/**
 * @brief The opposing bezier handle type
 * (NodeKeyframe::get_opposing_bezier_type): OAKNODE_KEYFRAME_IN_HANDLE
 * (0) <-> OAKNODE_KEYFRAME_OUT_HANDLE (1).
 *
 * @return The opposing handle type, or OAKNODE_E_INVALID for a type
 *         outside the two handle values.
 */
int oaknode_keyframe_opposing_bezier_type(int type);

/**
 * @brief Compute the combined node value to use when inserting
 * `keyframe` onto `target_node` (the keyframe paste path).
 *
 * Takes the target node's split value at the keyframe's time, replaces
 * the keyframe's own track with the keyframe's value, and combines the
 * per-track components into a single normal value (mirrors the facade's
 * oakengine_keyframe_compute_paste_value). OAKNODE_E_NOT_FOUND when the
 * keyframe's input id does not exist on `target_node`; OAKNODE_E_FAILED
 * for input types without a POD representation.
 */
int oaknode_keyframe_compute_paste_value(OakNodeNode target_node,
										 OakNodeKeyframe keyframe,
										 oaknode_value *out);

/**
 * @brief 1 if a sibling keyframe exists at the given rational time on
 * this keyframe's own track (NodeKeyframe::has_sibling_at_time(): the
 * track's key at `time` that is not this keyframe — the move-collision
 * check). Unlike the facade, the time is an exact rational rather than a
 * whole-second frame timestamp, and no track argument is needed (the
 * lookup is relative to this keyframe's track).
 *
 * An orphaned keyframe (no parent node) has no siblings: `*out_value`
 * is set to 0 and OAKNODE_OK is returned.
 */
int oaknode_keyframe_has_sibling_at_time(OakNodeKeyframe keyframe,
										 int64_t time_num, int64_t time_den,
										 int *out_value);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_KEYFRAME_H
