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

#include <stdint.h>

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
 * An OakNodeGroup wraps an olive::NodeGroup (a Node subclass); group
 * handles share the reference-counted lifetime rules of OakNodeNode.
 */

/**
 * @brief Reference-counted handle to a node group (olive::NodeGroup).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * oaknode_group_create() returns a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero. Handles returned by
 * oaknode_group_cast() are borrowed views of a node: releasing them
 * never destroys the underlying group.
 */
typedef struct OakNodeGroup {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeGroup;

/**
 * @brief Create a standalone NodeGroup (owned; release with
 * oaknode_group_free() while still orphaned).
 *
 * @return Group handle with count 1; ctx is NULL on allocation failure.
 */
OakNodeGroup oaknode_group_create(void);

/**
 * @brief Borrow a group view of a node (dynamic_cast). The returned
 * handle is non-owning; release it with oaknode_group_free().
 *
 * @return Borrowed group handle; ctx is NULL when the node is not a
 *         NodeGroup.
 */
OakNodeGroup oaknode_group_cast(OakNodeNode node);

/**
 * @brief Release one reference to a group handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * group when the count reaches zero and the handle owns it. NULL handle
 * or NULL ctx is a no-op; clears `group->ctx` after releasing.
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
int oaknode_group_add_input_passthrough(OakNodeGroup group,
										OakNodeNode node,
										const char *input_id, int element,
										char *buf, int buf_size);

/**
 * @brief Create an add-passthrough command
 * (olive::NodeGroupAddInputPassthrough). The generated id is NOT
 * retrievable through this call (the command computes it on redo).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_group_add_input_passthrough_undoable(OakNodeGroup group,
												 OakNodeNode node,
												 const char *input_id,
												 int element,
												 OakUndoCommand *out_command);

/**
 * @brief Remove the passthrough for (`node`, `input_id`, `element`)
 * (live). OAKNODE_E_NOT_FOUND when no such passthrough exists.
 */
int oaknode_group_remove_input_passthrough(OakNodeGroup group,
										   OakNodeNode node,
										   const char *input_id, int element);

/**
 * @brief Number of registered input passthroughs.
 */
int oaknode_group_passthrough_count(OakNodeGroup group, int *out_count);

/**
 * @brief The passthrough id at `index`. Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_group_passthrough_id_at(OakNodeGroup group, int index,
									char *buf, int buf_size);

/**
 * @brief The inner input behind passthrough `index`: node (borrowed
 * handle written to `out_node` when non-NULL; release it with
 * oaknode_node_free()), input id (two-stage string) and element.
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_group_passthrough_input_at(OakNodeGroup group, int index,
									   OakNodeNode *out_node, char *buf,
									   int buf_size, int *out_element);

/**
 * @brief The output passthrough node (borrowed handle written to
 * `out_node`; release it with oaknode_node_free()), an empty handle when
 * unset. OAKNODE_OK is returned either way.
 */
int oaknode_group_get_output_passthrough(OakNodeGroup group,
										 OakNodeNode *out_node);

/**
 * @brief Set the output passthrough node directly (live). `node` may be
 * an empty handle to clear the passthrough.
 */
int oaknode_group_set_output_passthrough(OakNodeGroup group,
										 OakNodeNode node);

/**
 * @brief Create a set-output-passthrough command
 * (olive::NodeGroupSetOutputPassthrough).
 */
int oaknode_group_set_output_passthrough_undoable(
	OakNodeGroup group, OakNodeNode node, OakUndoCommand *out_command);

/**
 * @brief Resolve an input through group passthroughs
 * (NodeGroup::resolve_input()): follows a group's passthrough id to the
 * inner node input. Non-group inputs resolve to themselves.
 *
 * `out_node` (may be NULL) receives a borrowed handle (release it with
 * oaknode_node_free()); the resolved input id uses the two-stage string
 * convention; `out_element` (may be NULL) receives the element.
 * OAKNODE_E_NOT_FOUND when the input does not resolve to a valid target.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKNODE_E_* error code.
 */
int oaknode_group_resolve_input(OakNodeNode node, const char *input_id,
								int element, OakNodeNode *out_node,
								char *buf, int buf_size, int *out_element);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_GROUP_H
