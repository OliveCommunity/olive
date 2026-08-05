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

#include "node/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file node.h
 * @brief C ABI for olive::Node (src/node/src/node.h).
 *
 * The handle IS the C++ object pointer (M3 handle convention 3): an
 * OakNodeNode is a reinterpreted olive::Node, no wrapper allocation.
 * Handles borrowed from a graph become invalid when the owning project or
 * node is destroyed. Owned handles (from oaknode_factory_create_from_id()
 * or oaknode_node_create_copy()) must be released with
 * oaknode_node_free() while still orphaned; once a node lives in a project
 * graph its lifetime belongs to the graph.
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
 * @brief Opaque node handle (olive::Node).
 */
typedef struct OakNodeNode OakNodeNode;

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
int oaknode_node_get_id(const OakNodeNode *node, char *buf, int buf_size);

/**
 * @brief The node's display name (Node::name()). Two-stage getter,
 * same return convention as oaknode_node_get_id().
 */
int oaknode_node_get_name(const OakNodeNode *node, char *buf, int buf_size);

/**
 * @brief The node's user label (Node::get_label()). Two-stage getter,
 * same return convention as oaknode_node_get_id().
 */
int oaknode_node_get_label(const OakNodeNode *node, char *buf, int buf_size);

/**
 * @brief Set the node's user label directly (Node::set_label(), live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_label(OakNodeNode *node, const char *label);

/**
 * @brief Create a label-change command (olive::NodeRenameCommand).
 *
 * The command is NOT executed; `out_command` receives an owned command
 * handle.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_label_undoable(OakNodeNode *node, const char *label,
									OakUndoCommand **out_command);

/**
 * @brief The node's override color index (Node::get_override_color();
 * -1 = none).
 *
 * @param out_value Receives the result. Must not be NULL.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_get_override_color(const OakNodeNode *node, int *out_value);

/**
 * @brief Set the override color index directly (-1 = none; live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_override_color(OakNodeNode *node, int index);

/**
 * @brief Create an override-color command (olive::NodeOverrideColorCommand).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_override_color_undoable(OakNodeNode *node, int index,
											 OakUndoCommand **out_command);

/**
 * @brief 1 if the node is enabled (the boolean "enabled_in" input's
 * standard value).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_is_enabled(const OakNodeNode *node, int *out_value);

/**
 * @brief Set the node's enabled state directly (live).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_enabled(OakNodeNode *node, int enabled);

/**
 * @brief Create an enabled-state command
 * (olive::NodeParamSetStandardValueCommand on "enabled_in").
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_set_enabled_undoable(OakNodeNode *node, int enabled,
									  OakUndoCommand **out_command);

/* ---- Input introspection ------------------------------------------------ */

/**
 * @brief Number of declared inputs (Node::inputs(); array elements are not
 * counted separately).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_node_input_count(const OakNodeNode *node, int *out_count);

/**
 * @brief The input id at `index` (Node::inputs()). Two-stage getter;
 * returns OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_input_id(const OakNodeNode *node, int index, char *buf,
						  int buf_size);

/**
 * @brief The input's value type mapped to oaknode_value_type (see the
 * pinned mapping on oaknode_value_type). OAKNODE_E_NOT_FOUND for an
 * unknown input id.
 */
int oaknode_node_input_get_type(const OakNodeNode *node, const char *input_id,
								int *out_type);

/**
 * @brief 1 if the input currently has a connected edge
 * (Node::is_input_connected()). OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_input_is_connected(const OakNodeNode *node,
									const char *input_id, int *out_value);

/**
 * @brief 1 if the input accepts connections (Node::is_input_connectable()).
 * OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_input_is_connectable(const OakNodeNode *node,
									  const char *input_id, int *out_value);

/**
 * @brief The human-readable name of the input (Node::get_input_name()).
 * Two-stage getter; OAKNODE_E_NOT_FOUND for an unknown id.
 */
int oaknode_node_get_input_name(const OakNodeNode *node, const char *input_id,
								char *buf, int buf_size);

/**
 * @brief The node feeding this input, or NULL when not connected
 * (Node::get_connected_output(), element -1). `out_node` receives a
 * borrowed handle. OAKNODE_E_NOT_FOUND for an unknown input id.
 */
int oaknode_node_input_get_connected_node(const OakNodeNode *node,
										  const char *input_id,
										  OakNodeNode **out_node);

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
int oaknode_node_get_input(const OakNodeNode *node, const char *input_id,
						   oaknode_value *out);

/**
 * @brief Write an input's standard value directly (live,
 * Node::set_standard_value()).
 *
 * `v->type` must match the input's declared type; OAKNODE_VALUE_STRING is
 * rejected (use oaknode_node_set_input_string()).
 */
int oaknode_node_set_input(OakNodeNode *node, const char *input_id,
						   const oaknode_value *v);

/**
 * @brief Create a set-standard-value command
 * (olive::NodeParamSetStandardValueCommand, track -1 semantics via the
 * whole-value reference on track 0).
 *
 * Same type rules as oaknode_node_set_input().
 */
int oaknode_node_set_input_undoable(OakNodeNode *node, const char *input_id,
									const oaknode_value *v,
									OakUndoCommand **out_command);

/**
 * @brief Read a string-family input's standard value. Two-stage getter.
 */
int oaknode_node_get_input_string(const OakNodeNode *node,
								  const char *input_id, char *buf,
								  int buf_size);

/**
 * @brief Write a string-family input's standard value directly (live).
 */
int oaknode_node_set_input_string(OakNodeNode *node, const char *input_id,
								  const char *value);

/**
 * @brief Create a set-standard-value command for a string-family input.
 */
int oaknode_node_set_input_string_undoable(OakNodeNode *node,
										   const char *input_id,
										   const char *value,
										   OakUndoCommand **out_command);

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
int oaknode_node_connect(OakNodeNode *output_node, OakNodeNode *input_node,
						 const char *input_id);

/**
 * @brief Create an edge-add command (olive::NodeEdgeAddCommand,
 * element -1). Same validation as oaknode_node_connect() except the
 * different-graph check (the command may legitimately be redone after
 * graph changes).
 */
int oaknode_node_connect_undoable(OakNodeNode *output_node,
								  OakNodeNode *input_node,
								  const char *input_id,
								  OakUndoCommand **out_command);

/**
 * @brief Remove the edge feeding `input_node`'s `input_id` directly
 * (live, Node::disconnect_edge(), element -1). OAKNODE_E_NOT_FOUND when
 * the input is unknown or not connected.
 */
int oaknode_node_disconnect(OakNodeNode *input_node, const char *input_id);

/**
 * @brief Create an edge-remove command (olive::NodeEdgeRemoveCommand,
 * element -1). OAKNODE_E_NOT_FOUND when not connected.
 */
int oaknode_node_disconnect_undoable(OakNodeNode *input_node,
									 const char *input_id,
									 OakUndoCommand **out_command);

/**
 * @brief Number of outgoing edges (Node::output_connections()).
 */
int oaknode_node_output_connection_count(const OakNodeNode *node,
										 int *out_count);

/**
 * @brief The node at the input end of outgoing edge `index`
 * (borrowed handle). OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_output_connection_node_at(const OakNodeNode *node, int index,
										   OakNodeNode **out_node);

/**
 * @brief The input id at the input end of outgoing edge `index`.
 * Two-stage getter; OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_output_connection_input_id_at(const OakNodeNode *node,
											   int index, char *buf,
											   int buf_size);

/**
 * @brief The input element at the input end of outgoing edge `index`
 * (-1 for non-array inputs). OAKNODE_E_NOT_FOUND for an out-of-range
 * index.
 */
int oaknode_node_output_connection_element_at(const OakNodeNode *node,
											  int index, int *out_element);

/* ---- Links --------------------------------------------------------------- */

/**
 * @brief Link two nodes directly (live, Node::link()). `out_linked`
 * receives 1 on success, 0 when the link was rejected (e.g. either node
 * rejects links). `out_linked` may be NULL.
 */
int oaknode_node_link(OakNodeNode *a, OakNodeNode *b, int *out_linked);

/**
 * @brief Unlink two nodes directly (live, Node::unlink()).
 * `out_unlinked` receives 1 on success, 0 otherwise; may be NULL.
 */
int oaknode_node_unlink(OakNodeNode *a, OakNodeNode *b, int *out_unlinked);

/**
 * @brief Create a link/unlink command (olive::NodeLinkCommand;
 * `link` != 0 links, 0 unlinks).
 */
int oaknode_node_link_undoable(OakNodeNode *a, OakNodeNode *b, int link,
							   OakUndoCommand **out_command);

/**
 * @brief 1 if the two nodes are linked (Node::are_linked()).
 */
int oaknode_node_are_linked(const OakNodeNode *a, const OakNodeNode *b,
							int *out_value);

/**
 * @brief Number of linked nodes (Node::links()).
 */
int oaknode_node_link_count(const OakNodeNode *node, int *out_count);

/**
 * @brief The linked node at `index` (borrowed handle).
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_link_at(const OakNodeNode *node, int index,
						 OakNodeNode **out_node);

/* ---- Context positions ---------------------------------------------------- */

/**
 * @brief Number of context entries (Node::get_context_positions()).
 */
int oaknode_node_context_count(const OakNodeNode *node, int *out_count);

/**
 * @brief The context node at `index` (borrowed handle).
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_node_context_node_at(const OakNodeNode *node, int index,
								 OakNodeNode **out_node);

/**
 * @brief The node's position in `context` (any out pointer may be NULL).
 * OAKNODE_E_NOT_FOUND when the context does not contain this node.
 */
int oaknode_node_get_context_position(const OakNodeNode *node,
									  OakNodeNode *context, double *out_x,
									  double *out_y, int *out_expanded);

/**
 * @brief Set the node's position in `context` directly (live,
 * Node::set_node_position_in_context() + set_node_expanded_in_context()).
 */
int oaknode_node_set_context_position(OakNodeNode *node, OakNodeNode *context,
									  double x, double y, int expanded);

/**
 * @brief Create a set-position command (olive::NodeSetPositionCommand).
 */
int oaknode_node_set_context_position_undoable(OakNodeNode *node,
											   OakNodeNode *context, double x,
											   double y, int expanded,
											   OakUndoCommand **out_command);

/**
 * @brief Remove the node from `context` directly (live).
 * OAKNODE_E_NOT_FOUND when not contained.
 */
int oaknode_node_remove_from_context(OakNodeNode *node, OakNodeNode *context);

/* ---- Lifetime --------------------------------------------------------------- */

/**
 * @brief Create a standalone copy of the node (Node::copy()). The copy is
 * NOT added to any graph; the caller owns it and must release it with
 * oaknode_node_free() while it is still orphaned. Returns NULL for NULL.
 */
OakNodeNode *oaknode_node_create_copy(const OakNodeNode *node);

/**
 * @brief Destroy an OWNED node immediately (C++ delete). NULL is a no-op.
 *
 * ONLY valid for owned handles that were never added to a graph: the
 * products of oaknode_factory_create_from_id(),
 * oaknode_node_create_copy() and oaknode_group_create() while still
 * orphaned. Freeing a graph-owned node, or freeing twice, is a
 * use-after-free.
 */
void oaknode_node_free(OakNodeNode *node);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_NODE_H
