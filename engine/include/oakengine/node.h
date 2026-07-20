/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKENGINE_NODE_H
#define OAKENGINE_NODE_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file node.h
 * @brief C ABI for the node graph: enumeration, metadata, input
 * introspection, parameter access and edge editing
 *
 * An OakEngineNode wraps the engine's olive::Node (engine/node/node.h).
 * Handles are borrowed views of nodes owned by their project (QObject
 * parent chain); they become invalid when the project is freed or the node
 * is removed (e.g. by undoing oakengine_project_add_node()).
 *
 * Parameter values cross the boundary as the small POD oak_node_value;
 * which of its fields are meaningful depends on the oak_node_value_type
 * (see the enum). String-typed inputs (NodeValue::k_file) do not fit the
 * POD and use the dedicated oakengine_node_get/set_input_string() pair
 * (buf/size convention).
 *
 * Every mutating call is undoable through the global undo stack, like the
 * timeline editing primitives (direct non-undoable application when the
 * engine is not initialized). Errors follow the family model: negative
 * OAKENGINE_E_* codes, NULL handles as no-ops, and a thread-local
 * human-readable reason via oakengine_node_last_error().
 */

/**
 * @brief Value type of an oak_node_value / a node input.
 *
 * Mirrors the data-carrying subset of olive::NodeValue::Type
 * (engine/node/value.h): combo maps to k_combo, STRING maps to k_file
 * (string-typed inputs handled by the dedicated string functions). Input
 * types without a POD representation (texture, samples, params, bezier,
 * binary, ...) report as OAK_NODE_VALUE_NONE.
 */
typedef enum oak_node_value_type {
	OAK_NODE_VALUE_NONE = 0,
	OAK_NODE_VALUE_INT, /**< num (olive k_int) */
	OAK_NODE_VALUE_FLOAT, /**< f[0] (olive k_float) */
	OAK_NODE_VALUE_BOOL, /**< num 0/1 (olive k_boolean) */
	OAK_NODE_VALUE_RATIONAL, /**< num/den (olive k_rational) */
	OAK_NODE_VALUE_COLOR, /**< f[0..3] = r,g,b,a (olive k_color) */
	OAK_NODE_VALUE_VEC2, /**< f[0..1] (olive k_vec2) */
	OAK_NODE_VALUE_VEC3, /**< f[0..2] (olive k_vec3) */
	OAK_NODE_VALUE_VEC4, /**< f[0..3] (olive k_vec4) */
	OAK_NODE_VALUE_COMBO, /**< num = selected index (olive k_combo) */
	OAK_NODE_VALUE_STRING /**< k_file; string APIs only, never in the POD */
} oak_node_value_type;

/**
 * @brief POD parameter value. Only the fields documented for the value's
 * `type` are meaningful.
 */
typedef struct oak_node_value {
	int type; /**< oak_node_value_type. */
	int64_t num; /**< INT/COMBO value, BOOL 0/1, RATIONAL numerator. */
	int64_t den; /**< RATIONAL denominator. */
	double f[4]; /**< FLOAT f[0]; VEC2/3/4 f[0..n-1]; COLOR r,g,b,a. */
} oak_node_value;

/**
 * @brief Opaque node handle (borrowed from the owning project).
 */
typedef struct OakEngineNode OakEngineNode;

/**
 * @brief Human-readable reason for the last failed node call on this
 * thread (buf/size convention).
 */
OAKENGINE_API int oakengine_node_last_error(char *buf, int buf_size);

/* ---- Enumeration --------------------------------------------------------- */

/**
 * @brief Number of nodes in the project's graph (Project::nodes()).
 */
OAKENGINE_API int oakengine_project_node_count(const OakEngineProject *self);

/**
 * @brief Borrowed handle of the node at `index`, or NULL when out of range.
 */
OAKENGINE_API OakEngineNode *
oakengine_project_node_at(const OakEngineProject *self, int index);

/* ---- Metadata -------------------------------------------------------------- */

/**
 * @brief The node's type id (Node::id(), e.g.
 * "org.olivevideoeditor.Olive.solidgenerator"). buf/size convention.
 */
OAKENGINE_API int oakengine_node_get_type_id(const OakEngineNode *self,
											 char *buf, int buf_size);

/**
 * @brief The node's display name (Node::name(), translated).
 * buf/size convention.
 */
OAKENGINE_API int oakengine_node_get_name(const OakEngineNode *self,
										  char *buf, int buf_size);

/**
 * @brief The node's user label (Node::get_label()). buf/size convention.
 */
OAKENGINE_API int oakengine_node_get_label(const OakEngineNode *self,
										   char *buf, int buf_size);

/**
 * @brief Set the node's user label (undoable, olive::NodeRenameCommand).
 */
OAKENGINE_API int oakengine_node_set_label(OakEngineNode *self,
										   const char *label);

/**
 * @brief Set the node's user label with an explicit undoable flag.
 *
 * `undoable` != 0 behaves like oakengine_node_set_label() (one undoable
 * NodeRenameCommand on the global undo stack, degrading to a direct
 * rename when the engine is not initialized); 0 renames directly with no
 * undo entry.
 */
OAKENGINE_API int oakengine_node_set_label_ex(OakEngineNode *self,
											  const char *label, int undoable);

/* ---- Input introspection ---------------------------------------------------- */

/**
 * @brief Number of declared inputs (Node::inputs(); array elements are not
 * counted separately).
 */
OAKENGINE_API int oakengine_node_input_count(const OakEngineNode *self);

/**
 * @brief The input id at `index` (Node::inputs()). buf/size convention;
 * returns OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_node_input_id(const OakEngineNode *self,
										  int index, char *buf, int buf_size);

/**
 * @brief The input's value type as oak_node_value_type.
 *
 * Unknown ids and inputs whose NodeValue::Type has no POD representation
 * (texture, samples, params, ...) report OAK_NODE_VALUE_NONE; k_file
 * reports OAK_NODE_VALUE_STRING.
 */
OAKENGINE_API int oakengine_node_input_get_type(const OakEngineNode *self,
												const char *input_id);

/**
 * @brief 1 if the input currently has a connected edge
 * (Node::is_input_connected()).
 */
OAKENGINE_API int oakengine_node_input_is_connected(
	const OakEngineNode *self, const char *input_id);

/* ---- Parameter access -------------------------------------------------------- */

/**
 * @brief Read an input's standard value (Node::get_standard_value())
 * mapped into `out`.
 *
 * String (k_file) inputs fail with OAKENGINE_E_INVALID -- use
 * oakengine_node_get_input_string(). Types without a POD representation
 * fail with OAKENGINE_E_NOT_FOUND; a missing input id fails with
 * OAKENGINE_E_NOT_FOUND as well.
 */
OAKENGINE_API int oakengine_node_get_input(const OakEngineNode *self,
										   const char *input_id,
										   oak_node_value *out);

/**
 * @brief Write an input's standard value (undoable,
 * olive::NodeParamSetStandardValueCommand).
 *
 * `v->type` must equal the input's declared type (STRING is rejected --
 * use oakengine_node_set_input_string()); a type mismatch or an unknown
 * input id returns OAKENGINE_E_INVALID / OAKENGINE_E_NOT_FOUND.
 */
OAKENGINE_API int oakengine_node_set_input(OakEngineNode *self,
										   const char *input_id,
										   const oak_node_value *v);

/**
 * @brief Read a string-typed (k_file) input's value (buf/size convention).
 */
OAKENGINE_API int oakengine_node_get_input_string(const OakEngineNode *self,
												  const char *input_id,
												  char *buf, int buf_size);

/**
 * @brief Write a string-typed (k_file) input's value (undoable).
 */
OAKENGINE_API int oakengine_node_set_input_string(OakEngineNode *self,
												  const char *input_id,
												  const char *s);

/* ---- Graph editing ------------------------------------------------------------- */

/**
 * @brief Create a node of `type_id` in the project (undoable:
 * NodeFactory::create_from_id() + olive::NodeAddCommand).
 *
 * Returns the borrowed node handle, or NULL when `type_id` is not a
 * registered node id (see oakengine_node_last_error()).
 */
OAKENGINE_API OakEngineNode *
oakengine_project_add_node(OakEngineProject *project, const char *type_id);

/**
 * @brief Remove a node from the project, disconnecting its edges
 * (undoable, olive::NodeRemoveAndDisconnectCommand).
 */
OAKENGINE_API int oakengine_project_remove_node(OakEngineProject *project,
												OakEngineNode *node);

/**
 * @brief Connect `output_node`'s output into `input_node`'s `input_id`
 * (undoable, olive::NodeEdgeAddCommand).
 *
 * Fails with OAKENGINE_E_INVALID when the input is not connectable or the
 * id is unknown, and with OAKENGINE_E_STATE when the input is already
 * connected (disconnect first).
 */
OAKENGINE_API int oakengine_node_connect(OakEngineNode *output_node,
										 OakEngineNode *input_node,
										 const char *input_id);

/**
 * @brief Remove the edge feeding `input_node`'s `input_id` (undoable,
 * olive::NodeEdgeRemoveCommand). OAKENGINE_E_NOT_FOUND when not connected.
 */
OAKENGINE_API int oakengine_node_disconnect(OakEngineNode *input_node,
											const char *input_id);

/* ---- Parameter animation (keyframes) --------------------------------------
 *
 * Keyframes live on an input's keyframe tracks (olive::NodeKeyframe). All
 * functions of this family operate on TRACK 0 only: for single-track types
 * (FLOAT, INT, BOOL, RATIONAL, COMBO, STRING) that is the whole value; for
 * split-track types (COLOR, VEC2/3/4) it is the FIRST component only (e.g.
 * red / x) -- multi-component keyframe editing is a later milestone.
 *
 * Keyframe times cross the boundary as frame timestamps (int64) in the
 * timebase of the project's first sequence's frame rate; projects without a
 * sequence fall back to the engine's default frame rate (1001/30000 s per
 * frame). The facade easing type is 0 = linear, 1 = bezier, 2 = hold (the
 * engine's NodeKeyframe::Type in a different order). Bezier control points
 * are the in-handle (x1, y1) and out-handle (x2, y2) in curve space; they
 * are only meaningful for bezier keyframes.
 *
 * All mutating calls are undoable like the other editing primitives.
 * Errors follow the family model (oakengine_node_last_error()).
 */

/**
 * @brief 1 if the input is keyframing-enabled (Node::is_input_keyframing()).
 */
OAKENGINE_API int oakengine_node_input_is_keyframed(const OakEngineNode *self,
													const char *input_id);

/**
 * @brief Number of keyframes on track 0 of the input.
 */
OAKENGINE_API int oakengine_node_keyframe_count(const OakEngineNode *self,
												const char *input_id);

/**
 * @brief Read the keyframe at `index` (time-ordered): its time as a frame
 * timestamp (`time_ts`, may be NULL) and its value mapped like
 * oakengine_node_get_input() (`value`, may be NULL; split-track types get
 * their first component in f[0]/num).
 */
OAKENGINE_API int oakengine_node_keyframe_at(const OakEngineNode *self,
											 const char *input_id, int index,
											 int64_t *time_ts,
											 oak_node_value *value);

/**
 * @brief Read the easing of the keyframe at `index`: facade type into
 * `type` (0 = linear, 1 = bezier, 2 = hold; may be NULL) and the bezier
 * in/out control points into (x1, y1, x2, y2) -- zeroed for non-bezier
 * keyframes. Any pointer may be NULL.
 */
OAKENGINE_API int oakengine_node_keyframe_get_easing(
	const OakEngineNode *self, const char *input_id, int index, float *x1,
	float *y1, float *x2, float *y2, int *type);

/**
 * @brief Add a keyframe at `time_ts` (undoable; enables keyframing on the
 * input first when needed).
 *
 * `value` maps like oakengine_node_set_input() and must match the input's
 * declared type (for split-track types, the first component is used).
 * `type` is the facade easing type; the four control floats only apply to
 * bezier (type 1). Fails with OAKENGINE_E_STATE when a keyframe already
 * exists at that exact time.
 */
OAKENGINE_API int oakengine_node_keyframe_add(OakEngineNode *self,
											  const char *input_id,
											  int64_t time_ts,
											  const oak_node_value *value,
											  int type, float x1, float y1,
											  float x2, float y2);

/**
 * @brief Remove the keyframe at `time_ts` (undoable).
 * OAKENGINE_E_NOT_FOUND when no keyframe exists at that exact time.
 */
OAKENGINE_API int oakengine_node_keyframe_remove(OakEngineNode *self,
												 const char *input_id,
												 int64_t time_ts);

/**
 * @brief Change the easing of the keyframe at `time_ts` (undoable; set type
 * plus bezier control points, mirroring the application's keyframe view
 * commands). OAKENGINE_E_NOT_FOUND when no keyframe exists at that time;
 * OAKENGINE_E_INVALID for an unknown easing type.
 */
OAKENGINE_API int oakengine_node_keyframe_set_easing(
	OakEngineNode *self, const char *input_id, int64_t time_ts, int type,
	float x1, float y1, float x2, float y2);

/**
 * @brief Remove all keyframes from the input (undoable,
 * olive::NodeImmediateRemoveAllKeyframesCommand). A no-op (OAKENGINE_OK)
 * when the input has no keyframes.
 */
OAKENGINE_API int oakengine_node_keyframes_clear(OakEngineNode *self,
												 const char *input_id);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_NODE_H */
