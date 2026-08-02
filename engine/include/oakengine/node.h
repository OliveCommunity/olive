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
#include "videoparams.h"

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
	OAK_NODE_VALUE_STRING, /**< k_file; string APIs only, never in the POD */
	OAK_NODE_VALUE_TEXT, /**< k_text; string APIs only */
	OAK_NODE_VALUE_FONT, /**< k_font; string APIs only */
	OAK_NODE_VALUE_STR_COMBO, /**< k_str_combo; string APIs only */
	OAK_NODE_VALUE_BINARY, /**< k_binary; binary data, no POD representation */
	OAK_NODE_VALUE_BEZIER, /**< k_bezier; bezier control point */
	OAK_NODE_VALUE_TEXTURE, /**< k_texture; texture */
	OAK_NODE_VALUE_SAMPLES, /**< k_samples; audio samples */
	OAK_NODE_VALUE_VIDEO_PARAMS, /**< k_video_params; video parameters */
	OAK_NODE_VALUE_AUDIO_PARAMS /**< k_audio_params; audio parameters */
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
 * @brief Opaque keyframe handle (borrowed from the input's track list).
 */
typedef struct OakEngineKeyframe OakEngineKeyframe;

/**
 * @brief Opaque input-dragger handle (created by oakengine_dragger_create()).
 */
typedef struct OakEngineNodeDragger OakEngineNodeDragger;

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

/* ---- Node factory --------------------------------------------------------- */

/**
 * @brief Number of registered node types (wraps NodeFactory::get_library().size()).
 */
OAKENGINE_API int oakengine_node_factory_id_count(void);

/**
 * @brief Create a node of `type_id` WITHOUT adding it to any project.
 * The caller owns the returned handle and must add it to a project (e.g.
 * via oakengine_project_add_node or a custom undo command) before the
 * engine can manage its lifecycle.
 *
 * Returns NULL when `type_id` is unknown.
 */
OAKENGINE_API OakEngineNode *
oakengine_node_factory_create_from_id(const char *type_id);

/**
 * @brief The display name (translated) of the node type identified by
 * `type_id`, or an empty string when the id is unknown (buf/size
 * convention).
 */
OAKENGINE_API int oakengine_node_factory_name_from_id(const char *type_id,
													  char *buf,
													  int buf_size);

/**
 * @brief Borrowed pointer to the prototype node at `index` in the
 * registered library, or NULL when out of range.
 *
 * The returned handle is a prototype instance owned by the engine's
 * NodeFactory; do not delete it or add it to a project. Use it only
 * for read-only metadata queries (name, category, flags, etc.).
 */
OAKENGINE_API OakEngineNode *
oakengine_node_factory_node_at(int index);

/**
 * @brief Number of category IDs assigned to this node
 * (Node::category().size()). 0 for NULL.
 */
OAKENGINE_API int oakengine_node_category_count(const OakEngineNode *self);

/**
 * @brief The category ID (Node::CategoryID ordinal) at `index` in the
 * node's category list (Node::category()). Returns -1 for NULL or an
 * out-of-range index.
 */
OAKENGINE_API int oakengine_node_category_at(const OakEngineNode *self,
											 int index);

/**
 * @brief The node's flags (Node::get_flags()), an OR-combination of
 * Node::Flag values. 0 for NULL.
 */
OAKENGINE_API uint64_t oakengine_node_get_flags(const OakEngineNode *self);

/**
 * @brief The value of the Node::k_dont_show_in_create_menu flag.
 */
OAKENGINE_API uint64_t oakengine_node_flag_dont_show_in_create_menu(void);

/**
 * @brief The value of the Node::k_dont_show_in_param_view flag.
 */
OAKENGINE_API uint64_t oakengine_node_flag_dont_show_in_param_view(void);

/**
 * @brief The value of the Node::k_video_effect flag.
 */
OAKENGINE_API uint64_t oakengine_node_flag_video_effect(void);

/**
 * @brief The value of the Node::k_audio_effect flag.
 */
OAKENGINE_API uint64_t oakengine_node_flag_audio_effect(void);

/**
 * @brief Refresh the node's translated strings (Node::retranslate()).
 * No-op for NULL.
 */
OAKENGINE_API void oakengine_node_retranslate(OakEngineNode *self);

/**
 * @brief The node's sub-category for secondary grouping
 * (Node::sub_category()). buf/size convention.
 */
OAKENGINE_API int oakengine_node_get_sub_category(const OakEngineNode *self,
												  char *buf, int buf_size);

/**
 * @brief The node's description (Node::description()). buf/size
 * convention.
 */
OAKENGINE_API int oakengine_node_get_description(const OakEngineNode *self,
												 char *buf, int buf_size);

/**
 * @brief Create a copy of the node (Node::copy()). Unlike
 * oakengine_node_copy_in_graph(), the copy is standalone: the caller
 * owns it and it is NOT added to any project or undo command.
 * Returns NULL for NULL.
 */
OAKENGINE_API OakEngineNode *
oakengine_node_create_copy(const OakEngineNode *self);

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
 * @brief The node's short display name (Node::short_name(), the virtual
 * used by the node graph item). buf/size convention.
 */
OAKENGINE_API int oakengine_node_get_short_name(const OakEngineNode *self,
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

/**
 * @brief Set one label on several nodes at once (undoable, ONE command;
 * olive::NodeRenameCommand with all of them, like the application's
 * Core::label_nodes() after its rename dialog).
 *
 * `nodes` holds borrowed handles (the handle is the engine node pointer
 * in this family, so the application can pass its own nodes directly).
 * Every entry must be non-NULL. `label` may be NULL for an empty label.
 */
OAKENGINE_API int oakengine_node_set_label_many(OakEngineNode **nodes,
												int count,
												const char *label);

/**
 * @brief Set one label on several nodes at once, with optional parent
 * MultiUndoCommand for composition (like Core::label_nodes() with a
 * non-NULL parent).
 *
 * When `parent_multi_or_NULL` is non-NULL, the new NodeRenameCommand is
 * added as a child of that MultiUndoCommand and is NOT pushed onto the
 * global undo stack.  The caller is responsible for pushing the parent.
 * When `parent_multi_or_NULL` is NULL, behavior matches
 * oakengine_node_set_label_many().
 */
OAKENGINE_API int oakengine_node_rename_many(OakEngineNode **nodes,
											int count,
											const char *label,
											void *parent_multi_or_NULL);

/**
 * @brief Create a NodeRenameCommand as an opaque command pointer for a single
 * node. Returns NULL on invalid arguments.
 */
OAKENGINE_API void *oakengine_node_rename_command(OakEngineNode *node,
											  const char *label);

/**
 * @brief Set the color-label index of several nodes at once (undoable,
 * ONE command; olive::NodeOverrideColorCommand per node, like the
 * timeline panel's color-label menu). `nodes` holds borrowed handles.
 */
OAKENGINE_API int oakengine_node_set_color_label(OakEngineNode **nodes,
												 int count, int color_index);

/**
 * @brief Create a NodeOverrideColorCommand as an opaque command pointer
 * without executing or pushing it.
 */
OAKENGINE_API void *oakengine_node_set_color_label_command(
	OakEngineNode *node, int color_index);

/**
 * @brief The node's color-label index (Node::get_override_color(); -1 =
 * none). -1 on a NULL handle.
 */
OAKENGINE_API int oakengine_node_get_color_label(const OakEngineNode *self);

/**
 * @brief The node's effective color-label index (Node::color()'s index:
 * the override color when set, otherwise the category-based "CatColor<N>"
 * config value). Feed into the app's ColorCoding::get_color().
 */
OAKENGINE_API int oakengine_node_get_effective_color_label(
	const OakEngineNode *self);

/**
 * @brief The node's title-bar brush (Node::brush()), written into a
 * caller-provided QBrush.
 *
 * QBrush is a Qt value type and crosses the ABI as an opaque pointer
 * (same precedent as QPainter* in oakengine_playback_cache_draw()):
 * `out_qbrush` must point to a live, constructed QBrush which receives
 * the result via copy assignment. No-op for NULL arguments.
 */
OAKENGINE_API void oakengine_node_get_brush(const OakEngineNode *self,
											double top, double bottom,
											void *out_qbrush);

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

/**
 * @brief Create a NodeParamSetStandardValueCommand as an opaque command pointer.
 * Sets the standard value of `input_id` on `track` (track -1 writes the whole
 * single-track value). Returns NULL on invalid arguments or type mismatch.
 */
OAKENGINE_API void *oakengine_node_set_standard_value_command(
	OakEngineNode *self, const char *input_id, int element, int track,
	const oak_node_value *v);

/**
 * @brief Create a command that sets an input's value at a rational time
 * (olive::Node::set_value_at_time) as an opaque command pointer. `time_num`
 * / `time_den` are rational seconds. The returned command is a
 * MultiUndoCommand; add it to a parent or push it with oakengine_undo_push().
 */
OAKENGINE_API void *oakengine_node_set_value_at_time_command(
	void *node, const char *input, int element, int64_t time_num,
	int64_t time_den, const oak_node_value *value, int track,
	int insert_on_all_tracks_if_no_key);

/**
 * @brief Create a NodeParamSetStandardValueCommand for a k_video_params input
 * as an opaque command pointer. `params` must describe a valid VideoParams.
 */
OAKENGINE_API void *oakengine_node_set_input_video_params_command(
	OakEngineNode *self, const char *input_id, const oak_video_params *params);

/**
 * @brief The frame timebase used for keyframe/parameter frame timestamps
 * (seconds per frame: the frame rate of the project's first sequence
 * flipped, or the engine default 1001/30000). Any pointer may be NULL.
 * Use it to convert rational seconds to the timestamps this family
 * takes, exactly like the facade does internally.
 */
OAKENGINE_API int oakengine_node_frame_time_base(
	const OakEngineNode *self, int *num, int *den);

/**
 * @brief Write an input's value at a time (undoable, ONE command;
 * olive::Node::set_value_at_time() -- the application's parameter
 * panel commit path).
 *
 * When the input is keyframed this inserts or updates the keyframe at
 * `time_ts` (the engine's set_value_at_time semantics); otherwise it
 * sets the standard value on `track`. `element` is the array element
 * (-1 for non-array inputs). `track` is the keyframe track/component;
 * pass -1 to write ALL components of a split-track type (COLOR/VEC2/3/4)
 * from `v->f[]` in the same command. `v->type` must match the input's
 * declared type; a k_bezier input takes an OAK_NODE_VALUE_FLOAT
 * component per track. String-family inputs (k_file/k_text/k_font/
 * k_str_combo) are rejected -- use oakengine_node_set_input_string_at_
 * time(). `insert_on_all_tracks` mirrors set_value_at_time's
 * insert_on_all_tracks_if_no_key (ignored for `track` -1).
 */
OAKENGINE_API int oakengine_node_set_input_at_time(
	OakEngineNode *self, const char *input_id, int element, int64_t time_ts,
	int track, const oak_node_value *v, int insert_on_all_tracks);

/**
 * @brief Write a string-family input's value at a time (undoable, ONE
 * command; k_file/k_text/k_font/k_str_combo on track 0, with
 * insert_on_all_tracks_if_no_key semantics like the panel).
 */
OAKENGINE_API int oakengine_node_set_input_string_at_time(
	OakEngineNode *self, const char *input_id, int element, int64_t time_ts,
	const char *value);

/* ---- Array inputs --------------------------------------------------------- */

/**
 * @brief Insert an element into an array input at `index` (undoable,
 * olive::NodeArrayInsertCommand; the panel's array append/insert
 * buttons). `index` must be >= 0.
 */
OAKENGINE_API int oakengine_node_array_insert_at(OakEngineNode *self,
												 const char *input_id,
												 int index);

/**
 * @brief Remove the array element at `index` (undoable,
 * olive::NodeArrayRemoveCommand). OAKENGINE_E_NOT_FOUND for an
 * out-of-range index.
 */
OAKENGINE_API int oakengine_node_array_remove_at(OakEngineNode *self,
												 const char *input_id,
												 int index);

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
 * @brief Schedule a node for deferred deletion (QObject::deleteLater).
 *
 * Use this to dispose of orphaned nodes that were created but never added
 * to a project (e.g. a sequence whose creation dialog was cancelled).
 * The handle becomes invalid after the next event-loop iteration.
 */
OAKENGINE_API void oakengine_node_delete_later(OakEngineNode *node);

/**
 * @brief Destroy an OWNED node immediately (C++ `delete`). NULL-safe
 * no-op.
 *
 * ONLY valid for owned handles that were never added to a project and
 * never referenced by an undo command -- i.e. the products of
 * oakengine_node_factory_create_from_id(), oakengine_node_create_copy()
 * and oakengine_clip_create_empty() while they are still orphaned. Once a
 * node lives in a project graph its lifetime belongs to the project (and
 * to any undo command referencing it); freeing such a node, or freeing
 * the same owned handle twice, is a use-after-free. Unlike
 * oakengine_node_delete_later() the destruction is synchronous and does
 * not need an event loop.
 */
OAKENGINE_API void oakengine_node_free(OakEngineNode *node);

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

/**
 * @brief Remove the edge feeding `input_node`'s `input_id` at `element`
 * (undoable). Same as oakengine_node_disconnect() (which passes element
 * -1) but addresses an array element, like the panel's connected-label
 * disconnect for array inputs.
 */
OAKENGINE_API int oakengine_node_disconnect_ex(OakEngineNode *input_node,
											   const char *input_id,
											   int element);

/**
 * @brief Create a NodeEdgeAddCommand as an opaque command pointer without
 * executing or pushing it. Ownership passes to the caller; add it to a
 * MultiUndoCommand with oakengine_undo_command_multi_add_child() or push
 * it with oakengine_undo_push(). `element` is -1 for non-array inputs.
 */
OAKENGINE_API void *oakengine_node_connect_command(OakEngineNode *output_node,
											   OakEngineNode *input_node,
											   const char *input_id,
											   int element);

/**
 * @brief Create a NodeEdgeRemoveCommand as an opaque command pointer without
 * executing or pushing it.
 */
OAKENGINE_API void *oakengine_node_disconnect_command(
	OakEngineNode *input_node, const char *input_id, int element);

/**
 * @brief Link or unlink two blocks/nodes directly (olive::Node::link/unlink).
 * Returns 1 on success, 0 on failure, OAKENGINE_E_INVALID if either pointer
 * is NULL.
 */
OAKENGINE_API int oakengine_block_link(void *a, void *b, int linked);

/**
 * @brief Create a NodeAddCommand as an opaque command pointer without executing
 * or pushing it. Adds an existing `node` to `project` on redo.
 */
OAKENGINE_API void *oakengine_node_add_to_project_command(
	OakEngineProject *project, OakEngineNode *node);

/**
 * @brief Set a traverse value hint on an input (Node::set_value_hint_for_input()).
 *
 * `type` is an oak_node_value_type (0-19), or -1 to match the input's declared
 * type. `index` is the traverse table row index (-1 for auto-detect). `tag` is
 * an optional string hint (may be NULL). Returns OAKENGINE_OK on success,
 * OAKENGINE_E_INVALID for NULL/type-999-style args, OAKENGINE_E_NOT_FOUND for
 * an unknown input id.
 */
OAKENGINE_API int oakengine_node_set_value_hint(OakEngineNode *self,
												const char *input_id,
												int element, int type,
												int index, const char *tag);

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
 * @brief Create a NodeParamInsertKeyframeCommand as an opaque command pointer.
 */
OAKENGINE_API void *oakengine_node_insert_keyframe_command(
	OakEngineNode *self, const char *input_id, int element, int track,
	int64_t time_ts, const oak_node_value *value, int type, float x1, float y1,
	float x2, float y2);

/**
 * @brief Create a NodeParamRemoveKeyframeCommand as an opaque command pointer
 * from a borrowed keyframe handle.
 */
OAKENGINE_API void *oakengine_node_remove_keyframe_command(
	OakEngineKeyframe *keyframe);

/**
 * @brief Create a NodeParamSetKeyframeTimeCommand as an opaque command pointer
 * from a borrowed keyframe handle. The previous time is captured at apply
 * time; `new_time_ts` is in the project's frame timestamp timebase.
 */
OAKENGINE_API void *oakengine_keyframe_set_time_command(
	OakEngineKeyframe *keyframe, int64_t new_time_ts);

/**
 * @brief Create a NodeParamSetKeyframeValueCommand as an opaque command pointer
 * from a borrowed keyframe handle. The previous value is captured at apply
 * time. `value->type` must match the keyframe's declared input type.
 */
OAKENGINE_API void *oakengine_keyframe_set_value_command(
	OakEngineKeyframe *keyframe, const oak_node_value *value);

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
 * @brief Change only the easing TYPE of several keyframes of one input
 * (undoable, ONE command; the application's keyframe view
 * KeyframeSetTypeCommand, batched like its context-menu action).
 *
 * Unlike the rest of this family (track 0 only), keyframes are addressed
 * individually by (`times_ts`[i], `tracks`[i]) because the view's
 * selection may span tracks; `element` addresses the input's array
 * element (-1 for non-array). Bezier control points are left untouched.
 * Every address must name an existing keyframe or the whole call fails
 * with OAKENGINE_E_NOT_FOUND and nothing is pushed. Returns the number
 * of affected keyframes (>= 0) or a negative code.
 */
OAKENGINE_API int oakengine_node_keyframes_set_type_many(
	OakEngineNode *self, const char *input_id, int element,
	const int64_t *times_ts, const int *tracks, int count, int type);

/**
 * @brief Move several keyframes of one input to `new_time_ts` (undoable,
 * ONE command; olive::NodeParamSetKeyframeTimeCommand per key, like the
 * application's keyframe properties dialog).
 *
 * Keyframes are addressed individually by (`old_times_ts`[i],
 * `tracks`[i]); `element` addresses the input's array element (-1 for
 * non-array). Every old address must name an existing keyframe
 * (OAKENGINE_E_NOT_FOUND otherwise), and no other keyframe may already
 * sit at `new_time_ts` on a target track (OAKENGINE_E_STATE; moving to
 * the key's own current time is allowed). On any failure nothing is
 * pushed. Returns the number of moved keyframes (>= 0) or a negative
 * code.
 */
OAKENGINE_API int oakengine_node_keyframes_set_time_many(
	OakEngineNode *self, const char *input_id, int element,
	const int64_t *old_times_ts, const int *tracks, int count,
	int64_t new_time_ts);

/**
 * @brief Change the value of several keyframes of one input (undoable,
 * ONE command; olive::NodeParamSetKeyframeValueCommand per key).
 *
 * `values`[i] is the new per-track component (mapped like
 * oakengine_node_set_input_at_time(); the type must match the input's
 * declared type). When `old_values` is not NULL, `old_values`[i] is
 * recorded as the undo value -- for callers that already live-set the
 * new values (the curve view's drag release); when NULL, each key's
 * current value is captured at apply time. Every address must name an
 * existing keyframe (OAKENGINE_E_NOT_FOUND; nothing pushed on failure).
 * Returns the number of changed keyframes (>= 0) or a negative code.
 */
OAKENGINE_API int oakengine_node_keyframes_set_value_many(
	OakEngineNode *self, const char *input_id, int element,
	const int64_t *times_ts, const int *tracks, int count,
	const oak_node_value *values, const oak_node_value *old_values);

/**
 * @brief Set both bezier control points of several keyframes of one
 * input (undoable, ONE command; two point commands per key, like the
 * keyframe properties dialog). The previous points are captured per
 * key. Every address must name an existing keyframe
 * (OAKENGINE_E_NOT_FOUND; nothing pushed on failure). Returns the
 * number of affected keyframes (>= 0) or a negative code.
 */
OAKENGINE_API int oakengine_node_keyframes_set_bezier_many(
	OakEngineNode *self, const char *input_id, int element,
	const int64_t *times_ts, const int *tracks, int count, double in_x,
	double in_y, double out_x, double out_y);

/**
 * @brief Set one bezier control point of one keyframe (undoable;
 * the curve view's bezier-handle drag release).
 *
 * `point_index` is 0 for the in-handle and 1 for the out-handle. The
 * new point is (x, y); (`old_x`, `old_y`) is the undo point recorded
 * for callers that already live-set the new point during the drag --
 * pass NaN for either old component to capture the key's current point
 * at apply time instead. OAKENGINE_E_NOT_FOUND when no keyframe exists
 * at that time/track.
 */
OAKENGINE_API int oakengine_node_keyframe_set_bezier_point(
	OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int point_index, double x, double y,
	double old_x, double old_y);

/**
 * @brief Remove all keyframes from the input (undoable,
 * olive::NodeImmediateRemoveAllKeyframesCommand). A no-op (OAKENGINE_OK)
 * when the input has no keyframes.
 */
OAKENGINE_API int oakengine_node_keyframes_clear(OakEngineNode *self,
												 const char *input_id);

/* ---- Extended input introspection ----------------------------------------- */

/**
 * @brief 1 if the input is an array-type input.
 */
OAKENGINE_API int oakengine_node_input_is_array(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief Number of elements in the array input (0 for non-array inputs).
 */
OAKENGINE_API int oakengine_node_input_array_size(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief The input's flags bitmask (Node::get_input_flags(); 0 on NULL).
 */
OAKENGINE_API int oakengine_node_input_get_flags(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief The input's data type (NodeValue::Type enum ordinal; -1 on NULL
 * or unknown input).
 */
OAKENGINE_API int oakengine_node_input_get_data_type(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief 1 if the input can accept a connection (connectable).
 */
OAKENGINE_API int oakengine_node_input_is_connectable(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief 1 if the input supports keyframing (keyframable).
 */
OAKENGINE_API int oakengine_node_input_is_keyframable(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief 1 if the input is hidden (k_input_flag_hidden).
 */
OAKENGINE_API int oakengine_node_input_is_hidden(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief 1 if keyframing is enabled for this input
 * (Node::is_input_keyframing()). `element` addresses the input's array
 * element (-1 for non-array inputs).
 */
OAKENGINE_API int oakengine_node_input_is_keyframed_ex(
	const OakEngineNode *self, const char *input_id, int element);

/**
 * @brief The node's label and name combined (buf/size).
 */
OAKENGINE_API int oakengine_node_get_label_and_name(
	const OakEngineNode *self, char *buf, int buf_size);

/**
 * @brief The human-readable name of the input (buf/size).
 */
OAKENGINE_API int oakengine_node_get_input_name(
	const OakEngineNode *self, const char *input_id, char *buf,
	int buf_size);

/**
 * @brief The default value of the input at a track index.
 */
OAKENGINE_API int oakengine_node_input_get_default_value(
	const OakEngineNode *self, const char *input_id, int track,
	oak_node_value *out);

/**
 * @brief The project that owns this node (NULL on NULL input).
 */
OAKENGINE_API OakEngineProject *oakengine_node_get_project(
	const OakEngineNode *self);

/**
 * @brief The node's parent project (Node::parent(); NULL on NULL input).
 * Same value as oakengine_node_get_project(); provided for graph-parent
 * semantics parity with the engine API.
 */
OAKENGINE_API OakEngineProject *oakengine_node_parent(
	const OakEngineNode *self);

/**
 * @brief 1 if the node is an "item" (Node::is_item(), i.e. appears in
 * the project tree / footage management).
 */
OAKENGINE_API int oakengine_node_is_item(const OakEngineNode *self);

/**
 * @brief The folder this item node belongs to (Node::folder(); NULL if
 * the node is not an item or has no folder).
 */
OAKENGINE_API OakEngineNode *oakengine_node_folder(const OakEngineNode *self);

/**
 * @brief The node connected to the input, or NULL (element -1 for
 * non-array inputs).
 */
OAKENGINE_API OakEngineNode *oakengine_node_input_get_connected_node(
	const OakEngineNode *self, const char *input_id, int element);

/**
 * @brief Copy the values (not connections) from `src` to `dest`
 * (undoable, ONE command).
 */
OAKENGINE_API int oakengine_node_copy_inputs(
	OakEngineNode *dest, const OakEngineNode *src);

/**
 * @brief Get the value of an input at a specific time (frame timestamp
 * timebase). String inputs fail with OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_node_get_input_at_time(
	const OakEngineNode *self, const char *input_id, int element, int track,
	int64_t time_ts, int track_for_time, oak_node_value *out);

/**
 * @brief Get a string input's value at a specific time (buf/size).
 */
OAKENGINE_API int oakengine_node_get_input_string_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, char *buf, int buf_size);

/**
 * @brief Get the bezier value of an input at a specific time (fails with
 * E_INVALID for non-bezier inputs).
 */
OAKENGINE_API int oakengine_node_get_input_bezier_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, double *out_6);

/**
 * @brief Get the binary value of an input at a specific time (fails with
 * E_INVALID for non-binary inputs).
 */
OAKENGINE_API int oakengine_node_get_input_binary_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, char *buf, int buf_size);

/* ---- Input properties ----------------------------------------------------- */

/**
 * @brief 1 if the input has a property with the given key.
 */
OAKENGINE_API int oakengine_node_input_has_property(
	const OakEngineNode *self, const char *input_id, const char *key);

/**
 * @brief Set a string property on an input (undoable; notify != 0 sends
 * change notification).
 */
OAKENGINE_API int oakengine_node_set_input_property_string(
	OakEngineNode *self, const char *input_id, const char *key,
	const char *value, int notify);

/**
 * @brief Read a string property (buf/size).
 */
OAKENGINE_API int oakengine_node_input_get_property_string(
	const OakEngineNode *self, const char *input_id, const char *key,
	char *buf, int buf_size);

/**
 * @brief Read a numeric property as a double (-1 track = whole value).
 */
OAKENGINE_API int oakengine_node_input_get_property_number(
	const OakEngineNode *self, const char *input_id, const char *key,
	int track, double *out);

/**
 * @brief Read an integer property.
 */
OAKENGINE_API int oakengine_node_input_get_property_int(
	const OakEngineNode *self, const char *input_id, const char *key,
	int64_t *out);

/**
 * @brief Read a rational property (numerator/denominator; any may be NULL).
 */
OAKENGINE_API int oakengine_node_input_get_property_rational(
	const OakEngineNode *self, const char *input_id, const char *key,
	int *num, int *den);

/**
 * @brief Read a numeric input property as the per-track component for
 * `track` (the curve view's "offset" path).
 *
 * The property value (the input's declared type, e.g. a QVector2D for a
 * vec2 input) is split into its keyframe tracks and the `track`-th
 * component is returned in `out`. For single-track types `track` must be
 * 0. Returns OAKENGINE_OK, OAKENGINE_E_NOT_FOUND when the property is
 * missing, or OAKENGINE_E_INVALID for a non-numeric property / bad track.
 */
OAKENGINE_API int oakengine_node_input_get_property_track_number(
	const OakEngineNode *self, const char *input_id, const char *key,
	int track, double *out);

/**
 * @brief The number of properties on the input.
 */
OAKENGINE_API int oakengine_node_input_get_property_count(
	const OakEngineNode *self, const char *input_id);

/**
 * @brief Enumerate the property key at `index` (buf/size; 0-based index
 * into the property map). Returns the length on success, negative on error.
 */
OAKENGINE_API int oakengine_node_input_get_property_key(
	const OakEngineNode *self, const char *input_id, int index,
	char *buf, int buf_size);

/**
 * @brief The number of elements in a string-list property.
 */
OAKENGINE_API int oakengine_node_input_get_property_string_list_count(
	const OakEngineNode *self, const char *input_id, const char *key);

/**
 * @brief Read one element of a string-list property (buf/size).
 */
OAKENGINE_API int oakengine_node_input_get_property_string_list(
	const OakEngineNode *self, const char *input_id, const char *key,
	int index, char *buf, int buf_size);

/* ---- Node type queries ---------------------------------------------------- */

/**
 * @brief 1 if the node is a group node.
 */
OAKENGINE_API int oakengine_node_is_group(const OakEngineNode *self);

/**
 * @brief 1 if the node is a multi-camera node.
 */
OAKENGINE_API int oakengine_node_is_multicam(const OakEngineNode *self);

/* ---- Context positions ---------------------------------------------------- */

/**
 * @brief The number of nodes visible in the given context
 * (Node::context_count() for the underlying context; -1 on NULL context).
 */
OAKENGINE_API int oakengine_node_context_node_count(
	const OakEngineNode *context);

/**
 * @brief 1 if the context contains the node.
 */
OAKENGINE_API int oakengine_node_context_contains_node(
	const OakEngineNode *context, const OakEngineNode *node);

/**
 * @brief The node at an index in the context (NULL when out of range;
 * returns x/y/expanded pointers if non-NULL).
 */
OAKENGINE_API OakEngineNode *oakengine_node_context_node_at(
	OakEngineNode *context, int index, double *x, double *y,
	int *expanded);

/**
 * @brief Set the context position of a node (undoable).
 */
OAKENGINE_API int oakengine_node_set_context_position(
	OakEngineNode *context, OakEngineNode *node, double x, double y);

/**
 * @brief Get the context position of a node.
 */
OAKENGINE_API int oakengine_node_get_context_position(
	const OakEngineNode *context, const OakEngineNode *node,
	double *x, double *y, int *expanded);

/**
 * @brief Set the expanded flag of a node in a context (undoable).
 */
OAKENGINE_API int oakengine_node_set_context_expanded(
	OakEngineNode *context, OakEngineNode *node, int expanded);

/* ---- Effect input --------------------------------------------------------- */

/**
 * @brief Get the node's effect input id and element (typically the texture
 * input for generators/filters). OAKENGINE_E_NOT_FOUND when none.
 */
OAKENGINE_API int oakengine_node_get_effect_input(
	const OakEngineNode *self, char *input_id, int input_id_size,
	int *element);

/* ---- Group passthrough ---------------------------------------------------- */

/**
 * @brief Create a detached group node (equivalent to new NodeGroup()).
 * The caller owns the returned handle and must add it to a project before
 * the engine manages its lifecycle.
 */
OAKENGINE_API OakEngineNode *oakengine_node_group_create(void);

/**
 * @brief Walk one group-passthrough level: if `*inout_node` is a group and
 * its input `*inout_input`/`*inout_element` is a passthrough, replace them
 * with the inner node/input/element and return 1. Returns 0 when the node
 * is not a group or the input is not a passthrough (inouts unchanged).
 */
OAKENGINE_API int oakengine_node_group_get_inner(
	OakEngineNode **inout_node, char *inout_input, int inout_input_size,
	int *inout_element);

/**
 * @brief The number of input passthroughs on the group (OAKENGINE_E_INVALID
 * when the node is not a group).
 */
OAKENGINE_API int oakengine_group_input_passthrough_count(
	const OakEngineNode *self);

/**
 * @brief Add an input passthrough to the group (direct, no undo).
 */
OAKENGINE_API int oakengine_group_add_input_passthrough(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element,
	const char *preferred_id, char *out_id, int out_id_size);

/**
 * @brief Read the i-th input passthrough of the group.
 */
OAKENGINE_API int oakengine_group_input_passthrough_at(
	const OakEngineNode *self, int index, char *id, int id_size,
	OakEngineNode **node, char *input_id, int input_id_size,
	int *element);

/**
 * @brief Look up a passthrough id by (node, input, element).
 */
OAKENGINE_API int oakengine_group_get_id_of_passthrough(
	const OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element, char *id, int id_size);

/**
 * @brief Look up (node, input, element) by passthrough id.
 */
OAKENGINE_API int oakengine_group_get_passthrough_from_id(
	const OakEngineNode *self, const char *id, OakEngineNode **out_node,
	char *out_input, int out_input_size, int *out_element);

/**
 * @brief Get the output passthrough node (or NULL).
 */
OAKENGINE_API OakEngineNode *oakengine_group_get_output_passthrough(
	const OakEngineNode *self);

/**
 * @brief Set the output passthrough node (direct, no undo).
 */
OAKENGINE_API int oakengine_group_set_output_passthrough(
	OakEngineNode *self, OakEngineNode *inner_node);

/**
 * @brief Resolve a passthrough id to its real node and input (handles
 * nested groups).
 */
OAKENGINE_API int oakengine_group_resolve_input(
	const OakEngineNode *self, const char *id, int element,
	OakEngineNode **out_node, char *out_input, int out_input_size,
	int *out_element);

/**
 * @brief Remove an input passthrough (direct, no undo).
 */
OAKENGINE_API int oakengine_group_remove_input_passthrough(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element);

/**
 * @brief Create a NodeGroupAddInputPassthrough command as an opaque command
 * pointer without executing or pushing it.
 */
OAKENGINE_API void *oakengine_group_add_input_passthrough_command(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element,
	const char *preferred_id);

/**
 * @brief Create a NodeGroupSetOutputPassthrough command as an opaque command
 * pointer without executing or pushing it.
 */
OAKENGINE_API void *oakengine_group_set_output_passthrough_command(
	OakEngineNode *self, OakEngineNode *inner_node);

/**
 * @brief Add an input passthrough (undoable; ONE undoable command).
 */
OAKENGINE_API int oakengine_group_add_input_passthrough_undoable(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element,
	const char *preferred_id);

/**
 * @brief Set the output passthrough node (undoable; ONE undoable command).
 */
OAKENGINE_API int oakengine_group_set_output_passthrough_undoable(
	OakEngineNode *self, OakEngineNode *inner_node);

/* ---- Multi-camera --------------------------------------------------------- */

/**
 * @brief The input id string for the current camera.
 */
OAKENGINE_API const char *oakengine_multicam_input_current(void);

/**
 * @brief The input id string for the sources array.
 */
OAKENGINE_API const char *oakengine_multicam_input_sources(void);

/**
 * @brief The input id string for the sequence.
 */
OAKENGINE_API const char *oakengine_multicam_input_sequence(void);

/**
 * @brief The input id string for the sequence type.
 */
OAKENGINE_API const char *oakengine_multicam_input_sequence_type(void);

/**
 * @brief Number of connected source cameras (OAKENGINE_E_INVALID when
 * the node is not a multicam).
 */
OAKENGINE_API int oakengine_multicam_get_source_count(
	const OakEngineNode *self);

/**
 * @brief Compute the grid (rows, cols) for the given number of sources.
 */
OAKENGINE_API int oakengine_multicam_get_rows_and_columns(
	int source_count, int *rows, int *cols);

/**
 * @brief Convert a flat index to (row, col) in the grid.
 */
OAKENGINE_API int oakengine_multicam_index_to_row_cols(
	int index, int rows, int cols, int *out_row, int *out_col);

/**
 * @brief Convert (row, col) to a flat index.
 */
OAKENGINE_API int oakengine_multicam_rows_cols_to_index(
	int row, int col, int rows, int cols);

/**
 * @brief Current source index of a multicam node.
 * Returns the index or OAKENGINE_E_INVALID when `node` is not a multicam.
 */
OAKENGINE_API int oakengine_multicam_get_current_source(
	const OakEngineNode *node);

/* ---- Shape node ----------------------------------------------------------- */

/**
 * @brief Set a shape node's rectangle (undoable). `x`/`y`/`w`/`h` are in
 * pixels; `video_params` is an oak_video_params POD describing the target
 * resolution. Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_shape_set_rect_undoable(
	OakEngineNode *node, double x, double y, double w, double h,
	const oak_video_params *video_params, void *command);

/* ---- Subtitle block ------------------------------------------------------- */

/** @brief The input id string for the subtitle text input. */
OAKENGINE_API const char *oakengine_subtitle_text_input_id(void);

/**
 * @brief Get the subtitle block's text (buf/size convention).
 * Returns the would-be length (excluding NUL) or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_subtitle_get_text(const OakEngineNode *node,
											  char *buf, int buf_size);

/**
 * @brief Set the subtitle block's text (non-undoable).
 * Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_subtitle_set_text(OakEngineNode *node,
											  const char *text);

/* ---- Bulk graph deletion -------------------------------------------------- */

/**
 * @brief Delete several nodes and their edges in one undoable command.
 *
 * `node_count` may be 0 (pass `nodes`/`contexts` as NULL) to delete only
 * edges; the call is invalid only when both counts are 0.
 */
OAKENGINE_API int oakengine_nodes_delete_many(
	OakEngineNode *const *nodes, OakEngineNode *const *contexts,
	int node_count, OakEngineNode *const *edge_outputs,
	OakEngineNode *const *edge_input_nodes,
	const char *const *edge_input_ids,
	const int *edge_input_elements, int edge_count);

/**
 * @brief oakengine_nodes_delete_many() plus edges to (re)connect AFTER the
 * deletion, still inside the same single undoable command.
 *
 * The reconnect edges are applied after the nodes are gone, so they may
 * target inputs that were occupied by the deleted nodes (effect-bypass
 * rewiring in the parameter editor). Redo order: delete, then reconnect;
 * undo order is the reverse.
 */
OAKENGINE_API int oakengine_nodes_delete_many_ex(
	OakEngineNode *const *nodes, OakEngineNode *const *contexts,
	int node_count, OakEngineNode *const *edge_outputs,
	OakEngineNode *const *edge_input_nodes,
	const char *const *edge_input_ids,
	const int *edge_input_elements, int edge_count,
	OakEngineNode *const *reconnect_outputs,
	OakEngineNode *const *reconnect_input_nodes,
	const char *const *reconnect_input_ids,
	const int *reconnect_input_elements, int reconnect_count);

/* ---- Keyframe best type at time ------------------------------------------- */

/**
 * @brief The best easing type for a keyframe at the given time (used by
 * the panel to determine the default type when adding keys).
 */
OAKENGINE_API int oakengine_node_keyframe_best_type_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int default_type);

/* ---- Handle-based keyframe API -------------------------------------------- */

/**
 * @brief Number of keyframe tracks on the input (-1 for all).
 */
OAKENGINE_API int oakengine_node_keyframe_track_count(
	const OakEngineNode *self, const char *input_id, int element);

/**
 * @brief Number of keyframes on a specific track.
 */
OAKENGINE_API int oakengine_node_keyframe_count_on_track(
	const OakEngineNode *self, const char *input_id, int element,
	int track);

/**
 * @brief Toggle keyframing on/off at a time (add/remove one key).
 */
OAKENGINE_API int oakengine_node_keyframes_toggle_at_time(
	OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int on, const char *undo_name);

/**
 * @brief 1 if a keyframe exists at the given time on the given track.
 */
OAKENGINE_API int oakengine_node_has_keyframe_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track);

/**
 * @brief The earliest keyframe time on the input. Returns 1 if found,
 * 0 if no keyframes (and the output rational is set).
 */
OAKENGINE_API int oakengine_node_keyframe_earliest_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t *num, int64_t *den);

/**
 * @brief The latest keyframe time on the input. Returns 1 if found,
 * 0 if no keyframes.
 */
OAKENGINE_API int oakengine_node_keyframe_latest_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t *num, int64_t *den);

/**
 * @brief The closest keyframe time before the given time.
 * Returns 1 if found, 0 if none.
 */
OAKENGINE_API int oakengine_node_keyframe_closest_time_before(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int64_t *num, int64_t *den);

/**
 * @brief The closest keyframe time after the given time.
 * Returns 1 if found, 0 if none.
 */
OAKENGINE_API int oakengine_node_keyframe_closest_time_after(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int64_t *num, int64_t *den);

/**
 * @brief Borrowed handle of the keyframe at the given on-track index,
 * or NULL.
 */
OAKENGINE_API OakEngineKeyframe *oakengine_node_keyframe_handle_on_track(
	const OakEngineNode *self, const char *input_id, int element,
	int track, int index);

/**
 * @brief Borrowed handle of the keyframe at the given rational time on a
 * track, or NULL (Node::get_keyframe_at_time_on_track()).
 */
OAKENGINE_API OakEngineKeyframe *oakengine_node_keyframe_handle_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int track, int64_t time_num, int64_t time_den);

/**
 * @brief Fill an array with the keyframe handles at a given rational time
 * across all tracks of the input (Node::get_keyframes_at_time()). Returns
 * the number filled.
 */
OAKENGINE_API int oakengine_node_keyframes_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_num, int64_t time_den,
	OakEngineKeyframe **out_handles, int max_handles);

/**
 * @brief Enable or disable keyframing on an input for a given element
 * (undoable). If enabling, one default-type key per track is added.
 */
OAKENGINE_API int oakengine_node_set_input_keyframing(
	OakEngineNode *self, const char *input_id, int element,
	int keyframing, int track, int enable_all_tracks,
	const char *undo_name);

/**
 * @brief Create a NodeParamSetKeyframingCommand as an opaque command pointer.
 */
OAKENGINE_API void *oakengine_node_set_input_keyframing_command(
	OakEngineNode *self, const char *input_id, int element, int keyframing);

/**
 * @brief Paste detached keyframes onto the input's track (undoable,
 * ONE command).
 */
OAKENGINE_API int oakengine_node_keyframes_paste(
	OakEngineNode *self, OakEngineKeyframe *const *keyframes,
	int count, const char *undo_name);

/* ---- OakEngineKeyframe accessors ------------------------------------------ */

/**
 * @brief The keyframe's time as a rational.
 */
OAKENGINE_API int oakengine_keyframe_get_time(
	const OakEngineKeyframe *self, int64_t *num, int64_t *den);

/**
 * @brief The input id that owns this keyframe (buf/size).
 */
OAKENGINE_API int oakengine_keyframe_get_input_id(
	const OakEngineKeyframe *self, char *buf, int buf_size);

/**
 * @brief The track this keyframe belongs to.
 */
OAKENGINE_API int oakengine_keyframe_get_track(
	const OakEngineKeyframe *self);

/**
 * @brief The element this keyframe belongs to.
 */
OAKENGINE_API int oakengine_keyframe_get_element(
	const OakEngineKeyframe *self);

/**
 * @brief The node that owns this keyframe.
 */
OAKENGINE_API OakEngineNode *oakengine_keyframe_get_node(
	const OakEngineKeyframe *self);

/**
 * @brief The easing type of the keyframe (0=linear, 1=bezier, 2=hold;
 * -1 on NULL).
 */
OAKENGINE_API int oakengine_keyframe_get_type(
	const OakEngineKeyframe *self);

/**
 * @brief The default easing type for a new keyframe.
 */
OAKENGINE_API int oakengine_keyframe_default_type(void);

/**
 * @brief The opposing bezier handle type (0=k_in_handle ⇄ 1=k_out_handle).
 */
OAKENGINE_API int oakengine_keyframe_opposing_bezier_type(int type);

/**
 * @brief The value of the keyframe on its track.
 */
OAKENGINE_API int oakengine_keyframe_get_value(
	const OakEngineKeyframe *self, oak_node_value *out);

/**
 * @brief Compute the combined node value to use when inserting
 * `keyframe` onto `target_node` (the keyframe paste path).
 *
 * Takes the target node's split value at the keyframe's time, replaces
 * the keyframe's own track with the keyframe's value, and combines the
 * per-track components into a single normal value (mirrors the old
 * app-side KeyframeToOakNodeValue helper). Returns OAKENGINE_OK on
 * success.
 */
OAKENGINE_API int oakengine_keyframe_compute_paste_value(
	OakEngineNode *target_node, OakEngineKeyframe *keyframe,
	oak_node_value *out);

/**
 * @brief 1 if there is a sibling keyframe at the given time on a different
 * track of the same input.
 */
OAKENGINE_API int oakengine_keyframe_has_sibling_at_time(
	const OakEngineKeyframe *self, int64_t time_ts, int track);

/**
 * @brief Live-set a bezier control point (no undo).
 */
OAKENGINE_API int oakengine_keyframe_set_bezier_point_live(
	OakEngineKeyframe *self, int point_index, double x, double y);

/**
 * @brief Read a bezier control point (0 = in-handle, 1 = out-handle).
 */
OAKENGINE_API int oakengine_keyframe_get_bezier_point(
	const OakEngineKeyframe *self, int point_index, double *x,
	double *y);

/**
 * @brief Read a bezier control point that is valid (returns the
 * point or the identity point for non-bezier keyframes).
 */
OAKENGINE_API int oakengine_keyframe_get_valid_bezier_point(
	const OakEngineKeyframe *self, int point_index, double *x,
	double *y);

/**
 * @brief Live-set the value of a keyframe (no undo).
 */
OAKENGINE_API int oakengine_keyframe_set_value_live(
	OakEngineKeyframe *self, const oak_node_value *value);

/**
 * @brief Live-set the time of a keyframe (no undo).
 */
OAKENGINE_API int oakengine_keyframe_set_time_live(
	OakEngineKeyframe *self, int64_t num, int64_t den);

/**
 * @brief Remove several keyframes in one undoable command.
 */
OAKENGINE_API int oakengine_keyframes_remove_many(
	OakEngineKeyframe *const *keyframes, int count,
	const char *undo_name);

/**
 * @brief Create a detached keyframe (not yet on any track).
 */
OAKENGINE_API OakEngineKeyframe *oakengine_keyframe_create(
	OakEngineNode *node, const char *input_id, int element,
	int track, int64_t time_ts, int type,
	const oak_node_value *value, int64_t duration_ts);

/**
 * @brief Dispose a detached keyframe (no-op on NULL).
 */
OAKENGINE_API void oakengine_keyframe_dispose(
	OakEngineKeyframe *keyframe);

/* ---- Input dragger -------------------------------------------------------- */

/**
 * @brief Create an input dragger for live-drag of a keyframe value.
 */
OAKENGINE_API OakEngineNodeDragger *oakengine_dragger_create(
	OakEngineNode *node, const char *input_id, int element,
	int track);

/**
 * @brief Start the drag at the given frame timestamp (creates a keyframe).
 */
OAKENGINE_API int oakengine_dragger_start(
	OakEngineNodeDragger *self, int64_t time_ts, int track,
	int insert_on_all_tracks);

/**
 * @brief Drag to a new value (live; no undo).
 */
OAKENGINE_API int oakengine_dragger_drag(
	OakEngineNodeDragger *self, const oak_node_value *value);

/**
 * @brief End the drag, pushing ONE undoable command.
 */
OAKENGINE_API int oakengine_dragger_end(
	OakEngineNodeDragger *self, const char *undo_name);

/**
 * @brief 1 if the dragger has been started.
 */
OAKENGINE_API int oakengine_dragger_is_started(
	const OakEngineNodeDragger *self);

/**
 * @brief Free the dragger (no-op on NULL).
 */
OAKENGINE_API void oakengine_dragger_free(
	OakEngineNodeDragger *self);

/* ---- Node static data and helpers ----------------------------------------- */

/**
 * @brief Node::k_enabled_input. Static string, never freed.
 */
OAKENGINE_API const char *oakengine_node_enabled_input_id(void);

/** @brief VolumeNode::k_samples_input. Static string, never freed. */
OAKENGINE_API const char *oakengine_volume_samples_input_id(void);

/** @brief TransformDistortNode::k_texture_input. Static string. */
OAKENGINE_API const char *oakengine_transform_texture_input_id(void);

/** @brief TransitionBlock::k_in_block_input. Static string. */
OAKENGINE_API const char *oakengine_transition_in_block_input_id(void);

/** @brief TransitionBlock::k_out_block_input. Static string. */
OAKENGINE_API const char *oakengine_transition_out_block_input_id(void);

/** @brief AudioVisualWaveform::k_maximum_sample_rate as a double. */
OAKENGINE_API double oakengine_audio_waveform_max_sample_rate(void);

/**
 * @brief Node::get_category_name() (buf/size convention).
 * `category_id` is a Node::CategoryID value.
 */
OAKENGINE_API int oakengine_node_category_name(int category_id,
											  char *buf, int buf_size);

/**
 * @brief Create a NodeLinkCommand as an opaque command pointer.
 * `link` != 0 links the two nodes, 0 unlinks them.
 */
OAKENGINE_API void *oakengine_node_link_command(OakEngineNode *a,
											   OakEngineNode *b, int link);

/**
 * @brief Node::copy_node_in_graph(). Returns the copy as a borrowed
 * OakEngineNode*, or NULL on failure. The copy is added to `command`
 * (a MultiUndoCommand*) when non-NULL; when NULL a standalone command
 * is pushed.
 */
OAKENGINE_API OakEngineNode *oakengine_node_copy_in_graph(
	OakEngineNode *node, void *command);

/**
 * @brief Node::copy_dependency_graph(). `nodes` and `copies` are
 * parallel arrays of the same length; the function connects the copies
 * the same way the originals are connected. `command` is a
 * MultiUndoCommand* (may be NULL for direct application).
 */
OAKENGINE_API int oakengine_node_copy_dependency_graph(
	OakEngineNode *const *nodes, OakEngineNode *const *copies, int count,
	void *command);

/**
 * @brief Node::get_connect_command_string() (buf/size convention).
 * Returns a human-readable description of connecting `output` to the
 * input `input_id`/`element` of `input_node`.
 */
OAKENGINE_API int oakengine_node_connect_command_string(
	OakEngineNode *output, OakEngineNode *input_node,
	const char *input_id, int element, char *buf, int buf_size);

/**
 * @brief Node::transform_time_to(). Transforms a time range through the
 * node graph from `from` to `to`. Returns the transformed range as
 * rational seconds (in_num/in_den, out_num/out_den).
 */
OAKENGINE_API int oakengine_node_transform_time_to(
	OakEngineNode *from, OakEngineNode *to, int direction,
	int path_index, int64_t in_num, int64_t in_den,
	int64_t out_num, int64_t out_den,
	int64_t *result_in_num, int64_t *result_in_den,
	int64_t *result_out_num, int64_t *result_out_den);

/* ---- NodeValue static methods (F class: 4 symbols) ----------------------- */

/**
 * @brief NodeValue::get_number_of_keyframe_tracks(type) using C enum.
 *
 * `c_type` is an oak_node_value_type value (NOT olive::NodeValue::Type
 * enum ordinal). Returns the number of keyframe tracks for the type:
 * 1 for scalar types, 2/3/4/6 for VEC2/VEC3/VEC4/COLOR/BEZIER.
 */
OAKENGINE_API int oakengine_node_value_keyframe_track_count(int c_type);

/**
 * @brief NodeValue::get_pretty_data_type_name(type) into buf (buf/size).
 *
 * `c_type` is an oak_node_value_type value. Returns the would-be string
 * length (excluding NUL), or -1 for unknown type.
 */
OAKENGINE_API int oakengine_node_value_pretty_type_name(int c_type,
	char *buf, int buf_size);

/**
 * @brief NodeValue::split_normal_value_into_track_values() into a
 * pre-allocated array.
 *
 * `c_type` is an oak_node_value_type. `normal` is the input value.
 * `tracks_out` must hold at least `track_count` oak_node_value entries
 * (caller allocates; get track_count first via
 * oakengine_node_value_keyframe_track_count()). Returns OAKENGINE_OK
 * or OAKENGINE_E_INVALID.
 *
 * For non-split types (VEC2/3/4/COLOR/BEZIER), the value is split into
 * per-component tracks. For scalar types, tracks_out[0] gets the value.
 */
OAKENGINE_API int oakengine_node_value_split_to_tracks(int c_type,
	const oak_node_value *normal, oak_node_value *tracks_out, int track_count);

/**
 * @brief NodeValue::combine_track_values_into_normal_value() — split
 * reverse.
 *
 * `c_type` is an oak_node_value_type. `tracks` must have at least
 * `track_count` entries (from oakengine_node_value_keyframe_track_count).
 * Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_node_value_combine_tracks(int c_type,
	const oak_node_value *tracks, int track_count, oak_node_value *normal_out);

/* ---- Node type queries (dynamic_cast replacements) ------------------------- */

/**
 * @brief 1 if the node is a ClipBlock (or subclass thereof).
 */
OAKENGINE_API int oakengine_node_is_clip(const OakEngineNode *self);

/**
 * @brief 1 if the node is a Track.
 */
OAKENGINE_API int oakengine_node_is_track(const OakEngineNode *self);

/**
 * @brief 1 if the node is a ViewerOutput (or subclass: Sequence, Footage).
 */
OAKENGINE_API int oakengine_node_is_viewer_output(const OakEngineNode *self);

/**
 * @brief 1 if the node is a Footage.
 */
OAKENGINE_API int oakengine_node_is_footage(const OakEngineNode *self);

/**
 * @brief 1 if the node is a Sequence.
 */
OAKENGINE_API int oakengine_node_is_sequence(const OakEngineNode *self);

/**
 * @brief 1 if the node is a Folder.
 */
OAKENGINE_API int oakengine_node_is_folder(const OakEngineNode *self);

/* ---- Clip / Track specific ------------------------------------------------- */

/**
 * @brief The track that owns this clip block (ClipBlock::track()).
 * Returns NULL when the node is not a clip or has no parent track.
 */
OAKENGINE_API OakEngineNode *oakengine_clip_get_track(
	const OakEngineNode *clip);

/**
 * @brief The track type (Track::Type enum: 0=video, 1=audio, 2=subtitle;
 * -1 for NULL or non-track node).
 */
OAKENGINE_API int oakengine_track_get_type(const OakEngineNode *track);

/**
 * @brief The track index within its sequence (-1 for NULL or non-track).
 */
OAKENGINE_API int oakengine_track_get_index(const OakEngineNode *track);

/**
 * @brief The sequence that owns this track (Track::sequence()). Returns
 * NULL when the node is not a track or the track has no parent sequence.
 */
OAKENGINE_API OakEngineNode *oakengine_track_get_sequence(
	const OakEngineNode *track);

/**
 * @brief The block's length as rational seconds (out - in).
 * Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_block_get_length_rational(
	const OakEngineNode *block, int *num, int *den);

/**
 * @brief The block's in-point as rational seconds.
 */
OAKENGINE_API int oakengine_block_get_in_rational(
	const OakEngineNode *block, int *num, int *den);

/**
 * @brief The block's out-point as rational seconds.
 */
OAKENGINE_API int oakengine_block_get_out_rational(
	const OakEngineNode *block, int *num, int *den);

/* ---- ViewerOutput specific ------------------------------------------------- */

/**
 * @brief The node connected to the viewer's texture input
 * (ViewerOutput::get_connected_texture_output()). NULL when none.
 */
OAKENGINE_API OakEngineNode *oakengine_viewer_output_get_connected_texture(
	const OakEngineNode *self);

/* ---- Gizmo access ---------------------------------------------------------- */

/**
 * @brief 1 if the node has any gizmos (Node::has_gizmos()).
 */
OAKENGINE_API int oakengine_node_has_gizmos(const OakEngineNode *self);

/**
 * @brief Number of gizmos on the node (Node::get_gizmos().size()).
 */
OAKENGINE_API int oakengine_node_gizmo_count(const OakEngineNode *self);

/**
 * @brief Borrowed opaque gizmo handle at `index`, or NULL when out of
 * range. The handle is a NodeGizmo* internally; use gizmo.h APIs.
 */
OAKENGINE_API void *oakengine_node_gizmo_at(const OakEngineNode *self,
											 int index);

/**
 * @brief Recalculate gizmo positions for the given time
 * (Node::update_gizmo_positions()). `node_value_row` is an opaque
 * pointer to the engine's NodeValueRow (the traverse result); pass NULL
 * for an empty row. `time_num`/`time_den` are rational seconds.
 * `video_width`/`video_height` describe the resolution context.
 * No-op for NULL or nodes without gizmos.
 */
OAKENGINE_API int oakengine_node_update_gizmo_positions(
	OakEngineNode *self, void *node_value_row,
	int video_width, int video_height,
	int64_t time_num, int64_t time_den);

/* ---- Graph topology -------------------------------------------------------- */

/**
 * @brief 1 if this node directly (or recursively when `recursive` != 0)
 * receives input from `other` (Node::inputs_from()).
 */
OAKENGINE_API int oakengine_node_inputs_from(const OakEngineNode *self,
											  const OakEngineNode *other,
											  int recursive);

/**
 * @brief Number of output connections from this node
 * (Node::output_connections().size()).
 */
OAKENGINE_API int oakengine_node_output_connection_count(
	const OakEngineNode *self);

/**
 * @brief Read the output connection at `index`: the receiving node into
 * `*input_node`, the input id into `input_id_buf` (buf/size), and the
 * array element into `*element`. Returns OAKENGINE_OK or
 * OAKENGINE_E_NOT_FOUND for out-of-range.
 */
OAKENGINE_API int oakengine_node_output_connection_at(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element);

/**
 * @brief Like oakengine_node_output_connection_at(), additionally reporting
 * whether the receiving input is hidden (`*hidden` = 1 when
 * NodeInput::is_hidden()). `hidden` may be NULL.
 */
OAKENGINE_API int oakengine_node_output_connection_at_ex(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element, int *hidden);

/**
 * @brief Total number of input connections on this node
 * (Node::input_connections().size()), i.e. a flat enumeration over all
 * connected inputs regardless of input id/element.
 */
OAKENGINE_API int oakengine_node_input_connection_count_all(
	const OakEngineNode *self);

/**
 * @brief Read the input connection at flat `index` (Node::input_connections()
 * iteration order): the connected input lives on `*input_node` with id
 * `input_id_buf` (buf/size) and `*element`; `*source_node` receives the
 * output node feeding it; `*hidden` reports NodeInput::is_hidden() (may be
 * NULL). Returns OAKENGINE_OK or OAKENGINE_E_NOT_FOUND for out-of-range.
 */
OAKENGINE_API int oakengine_node_input_connection_at_all(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element,
	OakEngineNode **source_node, int *hidden);

/**
 * @brief Number of connections feeding a specific input
 * (Node::input_connections() filtered by input_id/element).
 */
OAKENGINE_API int oakengine_node_input_connection_count(
	const OakEngineNode *self, const char *input_id, int element);

/**
 * @brief The output node feeding `input_id`/`element` at connection
 * `index`. Returns NULL when out of range or not connected.
 */
OAKENGINE_API OakEngineNode *oakengine_node_input_connection_at(
	const OakEngineNode *self, const char *input_id, int element,
	int index);

/* ---- Node data (project tree columns) ------------------------------------- */

/**
 * @brief Read a node's display data (Node::data(DataType)) as a POD.
 *
 * `role` selects the data kind: 0=icon, 1=duration, 2=created_time,
 * 3=modified_time, 4=frequency_rate, 5=tooltip. On return `*out_type`
 * describes the variant: 0=invalid (no data), 1=string (written to
 * `out_str` using buf/size), 2=int64 (written to `*out_int`). Any of the
 * out pointers may be NULL. Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_node_get_data(const OakEngineNode *self, int role,
										  int *out_type, int64_t *out_int,
										  char *out_str, int out_str_size);

/**
 * @brief Number of exclusive dependencies (Node::get_exclusive_dependencies()
 * size): nodes that should be removed together with this node.
 */
OAKENGINE_API int oakengine_node_get_exclusive_dependency_count(
	const OakEngineNode *self);

/**
 * @brief Borrowed handle of the exclusive dependency at `index`, or NULL
 * when out of range.
 */
OAKENGINE_API OakEngineNode *oakengine_node_get_exclusive_dependency_at(
	const OakEngineNode *self, int index);

/* ---- Plugin messages ------------------------------------------------------- */

/**
 * @brief 1 if the node has an OFX plugin instance attached
 * (Node::getPluginInstance() != nullptr), 0 otherwise.
 */
OAKENGINE_API int oakengine_node_has_plugin(const OakEngineNode *self);

/**
 * @brief Number of persistent messages on the node's plugin instance
 * (0 when the node has no plugin or no messages).
 */
OAKENGINE_API int oakengine_node_plugin_message_count(
	const OakEngineNode *self);

/**
 * @brief Read the plugin message at `index`: type into `*type`
 * (0=error, 1=warning, 2=message) and text into `msg_buf` (buf/size).
 * Returns OAKENGINE_OK or OAKENGINE_E_NOT_FOUND.
 */
OAKENGINE_API int oakengine_node_plugin_message_at(
	const OakEngineNode *self, int index, int *type, char *msg_buf,
	int msg_buf_size);

/**
 * @brief Clear all persistent messages on the node's plugin instance.
 */
OAKENGINE_API int oakengine_node_plugin_clear_messages(
	OakEngineNode *self);

/* ---- Node cache objects -----------------------------------------------------
 *
 * Borrowed handles of the caches every node owns (engine/node/node.h:
 * Node::thumbnail_cache() / waveform_cache() / video_frame_cache()). The
 * handle types are defined in oakengine/viewer.h (forward-declared here
 * like project.h does for OakEnginePlaybackCache); the application only
 * passes them on to the cache accessor families there. NULL on a NULL
 * handle. Handles become invalid with their owning node.
 */

typedef struct OakEngineFrameCache OakEngineFrameCache;
typedef struct OakEngineThumbnailCache OakEngineThumbnailCache;
typedef struct OakEngineWaveformCache OakEngineWaveformCache;

/**
 * @brief The node's thumbnail cache (Node::thumbnail_cache(); an
 * olive::ThumbnailCache, a FrameHashCache subclass).
 */
OAKENGINE_API OakEngineThumbnailCache *
oakengine_node_get_thumbnail_cache(const OakEngineNode *self);

/**
 * @brief The node's audio waveform cache (Node::waveform_cache(); an
 * olive::AudioWaveformCache), for the oakengine_waveform_cache_* family.
 */
OAKENGINE_API OakEngineWaveformCache *
oakengine_node_get_waveform_cache(const OakEngineNode *self);

/**
 * @brief The node's video frame cache (Node::video_frame_cache(); an
 * olive::FrameHashCache).
 */
OAKENGINE_API OakEngineFrameCache *
oakengine_node_get_video_frame_cache(const OakEngineNode *self);

#ifdef __cplusplus
}
#endif

/* Qt meta-type support: these opaque C handles are used as signal/slot
 * parameters across the C ABI boundary. Declaring them as opaque pointers
 * lets QMetaType store them (queued connections, QSignalSpy, QVariant). */
#ifdef __cplusplus
#include <QtCore/qmetatype.h>
Q_DECLARE_OPAQUE_POINTER(OakEngineNode *)
Q_DECLARE_OPAQUE_POINTER(OakEngineKeyframe *)
Q_DECLARE_OPAQUE_POINTER(OakEngineNodeDragger *)
#endif

#endif /* OAKENGINE_NODE_H */
