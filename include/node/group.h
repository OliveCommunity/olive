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

#ifndef OAK_EDITOR_NODE_GROUP_H
#define OAK_EDITOR_NODE_GROUP_H

#include "node/error.h"
#include "node/node.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file group.h
 * @brief C ABI for olive::NodeGroup (src/node/src/group/group.h):
 * input passthrough management and input resolution.
 *
 * An OakNodeGroup is a reinterpreted olive::NodeGroup (a Node subclass);
 * group handles borrow the same lifetime rules as OakNodeNode.
 */

/**
 * @brief Opaque group handle (olive::NodeGroup).
 */
typedef struct OakNodeGroup OakNodeGroup;

/**
 * @brief Create a standalone NodeGroup (owned; release with
 * oaknode_node_free() on the OakNodeNode view or oaknode_group_free()
 * while still orphaned).
 *
 * @return Group handle, or NULL on allocation failure.
 */
OakNodeGroup *oaknode_group_create(void);

/**
 * @brief Borrow a group view of a node, or NULL when the node is not a
 * NodeGroup (dynamic_cast).
 */
OakNodeGroup *oaknode_group_cast(OakNodeNode *node);

/**
 * @brief Destroy an OWNED group (same rules as oaknode_node_free()).
 * NULL is a no-op.
 */
void oaknode_group_free(OakNodeGroup *group);

/**
 * @brief Add an input passthrough for (`node`, `input_id`, `element`)
 * (live, NodeGroup::add_input_passthrough()). The generated passthrough
 * id is returned through the two-stage string convention.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKNODE_E_* error code.
 */
int oaknode_group_add_input_passthrough(OakNodeGroup *group,
										OakNodeNode *node,
										const char *input_id, int element,
										char *buf, int buf_size);

/**
 * @brief Create an add-passthrough command
 * (olive::NodeGroupAddInputPassthrough). The generated id is NOT
 * retrievable through this call (the command computes it on redo).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_group_add_input_passthrough_undoable(OakNodeGroup *group,
												 OakNodeNode *node,
												 const char *input_id,
												 int element,
												 OakUndoCommand *out_command);

/**
 * @brief Remove the passthrough for (`node`, `input_id`, `element`)
 * (live). OAKNODE_E_NOT_FOUND when no such passthrough exists.
 */
int oaknode_group_remove_input_passthrough(OakNodeGroup *group,
										   OakNodeNode *node,
										   const char *input_id, int element);

/**
 * @brief Number of registered input passthroughs.
 */
int oaknode_group_passthrough_count(const OakNodeGroup *group, int *out_count);

/**
 * @brief The passthrough id at `index`. Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_group_passthrough_id_at(const OakNodeGroup *group, int index,
									char *buf, int buf_size);

/**
 * @brief The inner input behind passthrough `index`: node (borrowed
 * handle), input id (two-stage string) and element.
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_group_passthrough_input_at(const OakNodeGroup *group, int index,
									   OakNodeNode **out_node, char *buf,
									   int buf_size, int *out_element);

/**
 * @brief The output passthrough node (borrowed handle), or NULL when
 * unset. OAKNODE_OK is returned either way.
 */
int oaknode_group_get_output_passthrough(const OakNodeGroup *group,
										 OakNodeNode **out_node);

/**
 * @brief Set the output passthrough node directly (live).
 */
int oaknode_group_set_output_passthrough(OakNodeGroup *group,
										 OakNodeNode *node);

/**
 * @brief Create a set-output-passthrough command
 * (olive::NodeGroupSetOutputPassthrough).
 */
int oaknode_group_set_output_passthrough_undoable(
	OakNodeGroup *group, OakNodeNode *node, OakUndoCommand *out_command);

/**
 * @brief Resolve an input through group passthroughs
 * (NodeGroup::resolve_input()): follows a group's passthrough id to the
 * inner node input. Non-group inputs resolve to themselves.
 *
 * `out_node` (may be NULL) receives a borrowed handle; the resolved input
 * id uses the two-stage string convention; `out_element` (may be NULL)
 * receives the element. OAKNODE_E_NOT_FOUND when the input does not
 * resolve to a valid target.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKNODE_E_* error code.
 */
int oaknode_group_resolve_input(OakNodeNode *node, const char *input_id,
								int element, OakNodeNode **out_node,
								char *buf, int buf_size, int *out_element);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_GROUP_H
