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

#ifndef OAK_EDITOR_NODE_NODE_H
#define OAK_EDITOR_NODE_NODE_H

#include <stdint.h>

#include "common/videoparams.h"
#include "node/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file node.h
 * @brief C ABI for olive::Node (src/node/src/node.h).
 *
 * Handles are by-value reference-counted structs (see
 * include/common/handle.h): every OakNodeNode carries ctx/addref/release/
 * abi_version and behaves like a shared_ptr at the ABI level. Factory
 * functions return a handle with reference count 1; release it with
 * oaknode_node_free(). Handles borrowed from a graph only release the
 * handle itself when freed; once a node lives in a project graph its
 * lifetime belongs to the graph (the implementation flips ownership
 * internally), and borrowed handles become invalid when the owning project
 * or node is destroyed.
 *
 * Parameter values cross the boundary as the POD oaknode_value; the
 * meaningful fields depend on its type (oaknode_value_type). String-typed
 * inputs (NodeValue::k_file/k_text/k_font/k_str_combo) do not fit the POD
 * and use the dedicated *_input_string() pair (two-stage buf/size getters
 * return the required size including the terminating NUL).
 *
 * Every mutating function comes in a live variant (applies immediately)
 * and an undoable variant (suffix _undoable) that creates an
 * olive::UndoCommand without executing it and returns it as an owned
 * OakUndoCommand handle. Execute it with oakundo_command_redo_now(),
 * push it onto an OakUndoStack, or release it with
 * oakundo_command_free().
 */

/**
 * @brief Value type of an oaknode_value / a node input.
 *
 * Pinned mapping to olive::NodeValue::Type (src/node/src/value.h):
 * NONE -> k_none, INT -> k_int, FLOAT -> k_float, BOOL -> k_boolean,
 * RATIONAL -> k_rational, COLOR -> k_color, VEC2 -> k_vec2,
 * VEC3 -> k_vec3, VEC4 -> k_vec4, COMBO -> k_combo,
 * STRING -> k_file (string-family inputs: k_file/k_text/k_font/
 * k_str_combo, handled by the dedicated string functions). Types without
 * a POD representation (texture, samples, matrix, params, bezier, binary,
 * ...) report as OAKNODE_VALUE_NONE.
 */
typedef enum oaknode_value_type {
	OAKNODE_VALUE_NONE = 0,
	OAKNODE_VALUE_INT, /**< num (olive k_int, int64_t) */
	OAKNODE_VALUE_FLOAT, /**< f[0] (olive k_float, double) */
	OAKNODE_VALUE_BOOL, /**< num 0/1 (olive k_boolean) */
	OAKNODE_VALUE_RATIONAL, /**< num/den (olive k_rational) */
	OAKNODE_VALUE_COLOR, /**< f[0..3] = r,g,b,a (olive k_color) */
	OAKNODE_VALUE_VEC2, /**< f[0..1] (olive k_vec2) */
	OAKNODE_VALUE_VEC3, /**< f[0..2] (olive k_vec3) */
	OAKNODE_VALUE_VEC4, /**< f[0..3] (olive k_vec4) */
	OAKNODE_VALUE_COMBO, /**< num = selected index (olive k_combo) */
	OAKNODE_VALUE_STRING, /**< k_file string family; string APIs only */
	OAKNODE_VALUE_COUNT
} oaknode_value_type;

/**
 * @brief POD parameter value. Only the fields documented for the value's
 * `type` are meaningful.
 */
typedef struct oaknode_value {
	int type; /**< oaknode_value_type. */
	int64_t num; /**< INT/COMBO value, BOOL 0/1, RATIONAL numerator. */
	int64_t den; /**< RATIONAL denominator. */
	double f[4]; /**< FLOAT f[0]; VEC2/3/4 f[0..n-1]; COLOR r,g,b,a. */
} oaknode_value;

/**
 * @brief Reference-counted handle to a node (olive::Node).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * init/factory functions return a handle with reference count 1,
 * addref(ctx) takes another reference, release(ctx) drops one; release a
 * handle with oaknode_node_free(). Borrowed handles into graph-owned
 * objects only release the handle itself.
 */
typedef struct OakNodeNode {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeNode;

/* Re-declared here so node.h is self-contained; see node/project.h. */
typedef struct OakNodeProject OakNodeProject;

/* Re-declared here so node.h is self-contained; see node/footage.h. */
typedef struct OakNodeFootage OakNodeFootage;

/**
 * @brief Timeline data owned by viewer nodes (TimelineMarkerList /
 * TimelineWorkArea in oaktimeline) cross the boundary as oaktimeline
 * value handles. Forward-declared here so node.h stays self-contained;
 * include timeline/marker.h / timeline/workarea.h for the definitions.
 */
struct OakTimelineMarkerList;
struct OakTimelineWorkArea;

/**
 * @brief Opaque borrowed handle to a node's video frame cache
 * (olive::FrameHashCache in oakrender). oakrender reinterprets this into
 * its own handle types.
 */
typedef struct OakNodeFrameCache OakNodeFrameCache;

/* oakcore handles used by the viewer setters. */
typedef struct OakAudioParams OakAudioParams;

/**
 * @brief Number of live owned objects created through this API
 * (nodes from oaknode_factory_create_from_id()/oaknode_node_create_copy(),
 * keyframes, groups, traversers, traverser databases). Debug aid for
 * leak checking; thread-unsafe, test/diagnostic use only.
 */
int oaknode_debug_alive_count(void);

/* ---- Metadata --------------------------------------------------------- */

/**
 * @brief The node's unique type id (Node::id(), e.g.
 * "org.olivevideoeditor.Olive.solidgenerator"). Two-stage getter.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKNODE_E_* error code.
 */
int oaknode_node_get_id(OakNodeNode node, char *buf, int buf_size);

/**
 * @brief The node's display name (Node::name()). Two-stage getter,
 * same return convention as oaknode_node_get_id().
 */
int oaknode_node_get_name(OakNodeNode node, char *buf, int buf_size);

/**
 * @brief The node's user label (Node::get_label()). Two-stage getter,
 * same return convention as oaknode_node_get_id().
 */
int oaknode_node_get_label(OakNodeNode node, char *buf, int buf_size);

/**
 * @brief Set the node's user label directly (Node::set_label(), live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_label(OakNodeNode node, const char *label);

/**
 * @brief Create a label-change command (olive::NodeRenameCommand).
 *
 * The command is NOT executed; `out_command` receives an owned command
 * handle.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_label_undoable(OakNodeNode node, const char *label,
									OakUndoCommand *out_command);

/**
 * @brief The node's override color index (Node::get_override_color();
 * -1 = none).
 *
 * @param out_value Receives the result. Must not be NULL.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_get_override_color(OakNodeNode node, int *out_value);

/**
 * @brief Set the override color index directly (-1 = none; live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_override_color(OakNodeNode node, int index);

/**
 * @brief Create an override-color command (olive::NodeOverrideColorCommand).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_override_color_undoable(OakNodeNode node, int index,
											 OakUndoCommand *out_command);

/**
 * @brief 1 if the node is enabled (the boolean "enabled_in" input's
 * standard value).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_is_enabled(OakNodeNode node, int *out_value);

/**
 * @brief Set the node's enabled state directly (live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_enabled(OakNodeNode node, int enabled);

/**
 * @brief Create an enabled-state command
 * (olive::NodeParamSetStandardValueCommand on "enabled_in").
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_enabled_undoable(OakNodeNode node, int enabled,
									  OakUndoCommand *out_command);

/* ---- Input introspection ------------------------------------------------ */

/**
 * @brief Number of declared inputs (Node::inputs(); array elements are not
 * counted separately).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_input_count(OakNodeNode node, int *out_count);

/**
 * @brief The input id at `index` (Node::inputs()). Two-stage getter;
 * returns OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_input_id(OakNodeNode node, int index, char *buf,
						  int buf_size);

/**
 * @brief The input's value type mapped to oaknode_value_type (see the
 * pinned mapping on oaknode_value_type). OAKNODE_E_NOT_FOUND for an
 * unknown input id.
 */
int oaknode_node_input_get_type(OakNodeNode node, const char *input_id,
								int *out_type);

/**
 * @brief 1 if the input currently has a connected edge
 * (Node::is_input_connected()). OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_input_is_connected(OakNodeNode node, const char *input_id,
									int *out_value);

/**
 * @brief 1 if the input accepts connections (Node::is_input_connectable()).
 * OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_input_is_connectable(OakNodeNode node, const char *input_id,
									  int *out_value);

/**
 * @brief The human-readable name of the input (Node::get_input_name()).
 * Two-stage getter; OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_get_input_name(OakNodeNode node, const char *input_id,
								char *buf, int buf_size);

/**
 * @brief The node feeding this input (Node::get_connected_output(),
 * element -1). `out_node` receives a borrowed handle (empty, ctx == NULL,
 * when not connected; releasing it only releases the handle).
 * OAKNODE_E_NOT_FOUND for an unknown input id.
 */
int oaknode_node_input_get_connected_node(OakNodeNode node,
										  const char *input_id,
										  OakNodeNode *out_node);

/* ---- Parameter access ----------------------------------------------------- */

/**
 * @brief Read an input's standard value (Node::get_standard_value())
 * mapped into `out`.
 *
 * String-family inputs fail with OAKNODE_E_INVALID (use
 * oaknode_node_get_input_string()); types without a POD representation
 * fail with OAKNODE_E_FAILED; an unknown input id fails with
 * OAKNODE_E_NOT_FOUND.
 */
int oaknode_node_get_input(OakNodeNode node, const char *input_id,
						   oaknode_value *out);

/**
 * @brief Write an input's standard value directly (live,
 * Node::set_standard_value()).
 *
 * `v->type` must match the input's declared type; OAKNODE_VALUE_STRING is
 * rejected (use oaknode_node_set_input_string()).
 */
int oaknode_node_set_input(OakNodeNode node, const char *input_id,
						   const oaknode_value *v);

/**
 * @brief Create a set-standard-value command
 * (olive::NodeParamSetStandardValueCommand, track -1 semantics via the
 * whole-value reference on track 0).
 *
 * Same type rules as oaknode_node_set_input().
 */
int oaknode_node_set_input_undoable(OakNodeNode node, const char *input_id,
									const oaknode_value *v,
									OakUndoCommand *out_command);

/**
 * @brief Read a string-family input's standard value. Two-stage getter.
 */
int oaknode_node_get_input_string(OakNodeNode node, const char *input_id,
								  char *buf, int buf_size);

/**
 * @brief Write a string-family input's standard value directly (live).
 */
int oaknode_node_set_input_string(OakNodeNode node, const char *input_id,
								  const char *value);

/**
 * @brief Create a set-standard-value command for a string-family input.
 */
int oaknode_node_set_input_string_undoable(OakNodeNode node,
										   const char *input_id,
										   const char *value,
										   OakUndoCommand *out_command);

/* ---- Graph editing -------------------------------------------------------- */

/**
 * @brief Connect `output_node`'s output into `input_node`'s `input_id`
 * directly (live, Node::connect_edge(), element -1).
 *
 * Fails with OAKNODE_E_NOT_FOUND for an unknown input id,
 * OAKNODE_E_INVALID when the input is not connectable, and
 * OAKNODE_E_STATE when the input is already connected or the nodes belong
 * to different graphs.
 */
int oaknode_node_connect(OakNodeNode output_node, OakNodeNode input_node,
						 const char *input_id);

/**
 * @brief Create an edge-add command (olive::NodeEdgeAddCommand,
 * element -1). Same validation as oaknode_node_connect() except the
 * different-graph check (the command may legitimately be redone after
 * graph changes).
 */
int oaknode_node_connect_undoable(OakNodeNode output_node,
								  OakNodeNode input_node,
								  const char *input_id,
								  OakUndoCommand *out_command);

/**
 * @brief Remove the edge feeding `input_node`'s `input_id` directly
 * (live, Node::disconnect_edge(), element -1). OAKNODE_E_NOT_FOUND when
 * the input is unknown or not connected.
 */
int oaknode_node_disconnect(OakNodeNode input_node, const char *input_id);

/**
 * @brief Create an edge-remove command (olive::NodeEdgeRemoveCommand,
 * element -1). OAKNODE_E_NOT_FOUND when not connected.
 */
int oaknode_node_disconnect_undoable(OakNodeNode input_node,
									 const char *input_id,
									 OakUndoCommand *out_command);

/**
 * @brief Number of outgoing edges (Node::output_connections()).
 */
int oaknode_node_output_connection_count(OakNodeNode node, int *out_count);

/**
 * @brief The node at the input end of outgoing edge `index` (borrowed
 * handle; releasing it only releases the handle). OAKNODE_E_NOT_FOUND for
 * an out-of-range index.
 */
int oaknode_node_output_connection_node_at(OakNodeNode node, int index,
										   OakNodeNode *out_node);

/**
 * @brief The input id at the input end of outgoing edge `index`.
 * Two-stage getter; OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_output_connection_input_id_at(OakNodeNode node, int index,
											   char *buf, int buf_size);

/**
 * @brief The input element at the input end of outgoing edge `index`
 * (-1 for non-array inputs). OAKNODE_E_NOT_FOUND for an out-of-range
 * index.
 */
int oaknode_node_output_connection_element_at(OakNodeNode node, int index,
											  int *out_element);

/* ---- Links --------------------------------------------------------------- */

/**
 * @brief Link two nodes directly (live, Node::link()). `out_linked`
 * receives 1 on success, 0 when the link was rejected (e.g. either node
 * rejects links). `out_linked` may be NULL.
 */
int oaknode_node_link(OakNodeNode a, OakNodeNode b, int *out_linked);

/**
 * @brief Unlink two nodes directly (live, Node::unlink()).
 * `out_unlinked` receives 1 on success, 0 otherwise; may be NULL.
 */
int oaknode_node_unlink(OakNodeNode a, OakNodeNode b, int *out_unlinked);

/**
 * @brief Create a link/unlink command (olive::NodeLinkCommand;
 * `link` != 0 links, 0 unlinks).
 */
int oaknode_node_link_undoable(OakNodeNode a, OakNodeNode b, int link,
							   OakUndoCommand *out_command);

/**
 * @brief 1 if the two nodes are linked (Node::are_linked()).
 */
int oaknode_node_are_linked(OakNodeNode a, OakNodeNode b, int *out_value);

/**
 * @brief Number of linked nodes (Node::links()).
 */
int oaknode_node_link_count(OakNodeNode node, int *out_count);

/**
 * @brief The linked node at `index` (borrowed handle; releasing it only
 * releases the handle). OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_link_at(OakNodeNode node, int index,
						 OakNodeNode *out_node);

/* ---- Context positions ---------------------------------------------------- */

/**
 * @brief Number of context entries (Node::get_context_positions()).
 */
int oaknode_node_context_count(OakNodeNode node, int *out_count);

/**
 * @brief The context node at `index` (borrowed handle; releasing it only
 * releases the handle). OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_context_node_at(OakNodeNode node, int index,
								 OakNodeNode *out_node);

/**
 * @brief The node's position in `context` (any out pointer may be NULL).
 * OAKNODE_E_NOT_FOUND when the context does not contain this node.
 */
int oaknode_node_get_context_position(OakNodeNode node, OakNodeNode context,
									  double *out_x, double *out_y,
									  int *out_expanded);

/**
 * @brief Set the node's position in `context` directly (live,
 * Node::set_node_position_in_context() + set_node_expanded_in_context()).
 */
int oaknode_node_set_context_position(OakNodeNode node, OakNodeNode context,
									  double x, double y, int expanded);

/**
 * @brief Create a set-position command (olive::NodeSetPositionCommand).
 */
int oaknode_node_set_context_position_undoable(OakNodeNode node,
											   OakNodeNode context, double x,
											   double y, int expanded,
											   OakUndoCommand *out_command);

/**
 * @brief Remove the node from `context` directly (live).
 * OAKNODE_E_NOT_FOUND when not contained.
 */
int oaknode_node_remove_from_context(OakNodeNode node, OakNodeNode context);

/* ---- Lifetime --------------------------------------------------------------- */

/**
 * @brief Create a standalone copy of the node (Node::copy()). The copy is
 * NOT added to any graph; the returned handle has reference count 1 and
 * must be released with oaknode_node_free() while it is still orphaned.
 * Returns an empty handle (ctx == NULL) for an empty handle or on failure.
 */
OakNodeNode oaknode_node_create_copy(OakNodeNode node);

/**
 * @brief Copy a node inside its graph (Node::copy_node_in_graph()),
 * recording the reconnect operations in a new MultiUndoCommand.
 *
 * `*out_command` receives an owned undo command handle (free with
 * oakundo_command_free()). The copy is inserted into the graph only when
 * the returned command is redone; treat it as owned (oaknode_node_free())
 * until then. Returns an empty handle (ctx == NULL) on failure.
 */
OakNodeNode oaknode_node_copy_in_graph(OakNodeNode node,
									   OakUndoCommand *out_command);

/**
 * @brief Get the project this node belongs to. `out` receives a borrowed
 * handle (empty, ctx == NULL, if the node is orphaned; releasing it only
 * releases the handle).
 */
int oaknode_node_get_project(OakNodeNode node, OakNodeProject *out);

/**
 * @brief Insert/remove an element in an input array (live,
 * Node::input_array_insert/remove()). OAKNODE_E_NOT_FOUND for an
 * unknown input id.
 */
int oaknode_node_input_array_insert(OakNodeNode node, const char *input_id,
									int index);
int oaknode_node_input_array_remove(OakNodeNode node, const char *input_id,
									int index);

/**
 * @brief Element-aware variants of oaknode_node_connect()/disconnect()
 * (NodeInput element != -1, e.g. Sequence's track_in_N array inputs).
 */
int oaknode_node_connect_element(OakNodeNode output_node,
								 OakNodeNode input_node,
								 const char *input_id, int element);
int oaknode_node_disconnect_element(OakNodeNode input_node,
									const char *input_id, int element);

/**
 * @brief Create a command that adds a node to a project's graph
 * (olive::NodeAddCommand). Owned; free with oakundo_command_free().
 */
OakUndoCommand oaknode_command_create_add_node(OakNodeProject graph,
											   OakNodeNode node);

/**
 * @brief Create a command that sets a node's position in a context and
 * repositions its dependencies recursively
 * (olive::NodeSetPositionAndDependenciesRecursivelyCommand). Owned.
 */
OakUndoCommand oaknode_command_create_set_position_recursive(
	OakNodeNode node, OakNodeNode context, double x, double y);

/**
 * @brief Marker list / work area of a viewer node, as addref'd
 * oaktimeline value handles (release with
 * oaktimeline_marker_list_free()/oaktimeline_workarea_free()). *out is
 * an empty handle (ctx == NULL) when the node is not a viewer or for
 * an empty node handle.
 */
int oaknode_node_get_markers(OakNodeNode node,
							 struct OakTimelineMarkerList *out);
int oaknode_node_get_work_area(OakNodeNode node,
							   struct OakTimelineWorkArea *out);

/**
 * @brief Borrowed video frame cache of a node (NULL when the node has
 *        none or for an empty handle).
 */
int oaknode_node_get_video_frame_cache(OakNodeNode node,
									   OakNodeFrameCache **out);

/**
 * @brief Copy input values/connections from one node to another
 *        (Node::copy_inputs()). include_connections != 0 also copies
 *        input connections.
 */
int oaknode_node_copy_inputs(OakNodeNode dst, OakNodeNode src,
							 int include_connections);

/**
 * @brief Set a track-routing value hint on an input
 *        (Node::set_value_hint_for_input() with a single texture type
 *        and a Track::Reference string).
 */
int oaknode_node_set_value_hint_track(OakNodeNode node, const char *input_id,
									  int track_type, int track_index);

/**
 * @brief Set a viewer node's video/audio params (ViewerOutput::
 * set_video_params/set_audio_params, stream index 0). `params` is an
 * oakcommon handle (video) or borrowed oakcore handle (audio).
 */
int oaknode_viewer_set_video_params(OakNodeNode viewer,
									const OakVideoParams *params);
int oaknode_viewer_set_audio_params(OakNodeNode viewer,
									const OakAudioParams *params);

/**
 * @brief Find a footage node upstream of this node's inputs
 *        (Node::find_input_nodes<Footage>(), first match). `out` receives
 *        a borrowed handle (empty, ctx == NULL, when none; releasing it
 *        only releases the handle).
 */
int oaknode_node_find_input_footage(OakNodeNode node, OakNodeFootage *out);

/**
 * @brief Value of an input at a specific time (Node::get_value_at_time(),
 * element -1). Same POD rules as oaknode_node_get_input().
 */
int oaknode_node_get_input_at_time(OakNodeNode node,
								   const char *input_id, int64_t time_num,
								   int64_t time_den, oaknode_value *out);

/**
 * @brief Set an input's value at a specific time with keyframe logic
 *        (Node::set_value_at_time(), element -1, track 0,
 *        insert_on_all_tracks_if_no_key = true). `*out_command` receives
 *        an owned undo command handle.
 */
int oaknode_node_set_input_at_time_undoable(OakNodeNode node,
		const char *input_id, int64_t time_num, int64_t time_den,
		const oaknode_value *v, int track, OakUndoCommand *out_command);

/**
 * @brief Identity of the underlying node object as an opaque integer
 *        (address-cast; for registry keys only, never dereference).
 */
uintptr_t oaknode_node_identity(OakNodeNode node);

/**
 * @brief Append a value-at-time set into an existing multi command
 *        (same semantics as oaknode_node_set_input_at_time_undoable but
 *        batches into `multi_command` from oakundo_command_init_multi()).
 */
int oaknode_node_set_input_at_time_into(OakNodeNode node,
		const char *input_id, int64_t time_num, int64_t time_den,
		const oaknode_value *v, int track, OakUndoCommand multi_command);

/**
 * @brief Create a command that removes a node from its graph together
 * with its exclusive dependencies and disconnects its edges
 * (NodeRemoveWithExclusiveDependenciesAndDisconnect).
 *
 * Owned command handle; free with oakundo_command_free(). Returns an
 * empty handle (ctx == NULL) on failure.
 */
OakUndoCommand oaknode_command_create_remove_node(OakNodeNode node);

/**
 * @brief Release one reference to a node handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): the underlying
 * node is destroyed only when the last reference of an OWNED handle is
 * released; releasing a borrowed handle into a graph-owned object only
 * destroys the handle itself. NULL handle or NULL ctx is a no-op; clears
 * `node->ctx` after releasing.
 */
void oaknode_node_free(OakNodeNode *node);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_NODE_H
