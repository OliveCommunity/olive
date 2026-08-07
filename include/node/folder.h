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

#ifndef OAK_EDITOR_NODE_FOLDER_H
#define OAK_EDITOR_NODE_FOLDER_H

#include <stdint.h>

#include "node/error.h"
#include "undo/undocommand.h"
#include "node/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file folder.h
 * @brief C ABI for olive::Folder (oaknode)
 *
 * A folder is a project node that organizes item children (footage,
 * sequences, subfolders). Folder handles are borrowed from the owning
 * project; they become invalid when the project is freed or cleared.
 *
 * Child add/remove/move operations execute the underlying undo commands
 * live (redo_now); wiring them onto an undo stack is the oakundo /
 * facade layer's job, not this layer's.
 */

/**
 * @brief Reference-counted handle to a folder node (olive::Folder).
 *
 * Semantics are shared_ptr-like (see OakNodeProject): addref(ctx) takes a
 * reference, release(ctx) drops one. Folder handles handed out by this API
 * are borrowed views into the owning project's graph: releasing them only
 * releases the handle itself, never the folder.
 */
typedef struct OakNodeFolder {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeFolder;

/**
 * @brief Create a folder node owned by `project`.
 *
 * The folder is added to the project's graph (Project::add_node()) but is
 * NOT attached under any parent folder; use oaknode_folder_add_child() to
 * place it. The returned handle is borrowed: the project owns the folder,
 * so releasing the handle only releases the handle itself.
 *
 * @return Folder handle; ctx is NULL on failure.
 */
OakNodeFolder oaknode_folder_create(OakNodeProject project);

/**
 * @brief Number of direct item children (Folder::item_child_count()).
 * Negative OAKNODE_E_* code on an empty handle.
 */
int oaknode_folder_child_count(OakNodeFolder folder);

/**
 * @brief Borrowed node handle of the item child at `index`
 * (Folder::item_child()).
 *
 * The returned handle only releases the handle itself. Empty handle
 * (ctx == NULL) when out of range.
 */
OakNodeNode oaknode_folder_child_at(OakNodeFolder folder, int index);

/**
 * @brief Add `child` as a direct item child of `folder` (live, non-undoable;
 * executes FolderAddChild::redo()).
 *
 * After a successful call the graph owns `child`: releasing the child
 * handle only releases the handle itself.
 *
 * @return OAKNODE_OK, OAKNODE_E_STATE if `child` already belongs to a
 *         folder, or another negative OAKNODE_E_* error code.
 */
int oaknode_folder_add_child(OakNodeFolder folder, OakNodeNode child);

/**
 * @brief Borrowed cast from a folder handle to its node handle.
 *
 * The returned handle only releases the handle itself. Empty handle for an
 * empty handle.
 */
OakNodeNode oaknode_folder_as_node(OakNodeFolder folder);

/**
 * @brief Create an undoable FolderAddChild command.
 *
 * @return Command handle with reference count 1 (release with
 *         oakundo_command_free()); ctx is NULL on failure.
 */
OakUndoCommand oaknode_command_create_folder_add_child(
	OakNodeFolder folder, OakNodeNode child);

/**
 * @brief Remove `child` from `folder` without deleting it (live,
 * non-undoable; executes Folder::RemoveElementCommand::redo()).
 *
 * @return OAKNODE_OK, OAKNODE_E_NOT_FOUND if `child` is not a direct child,
 *         or another negative OAKNODE_E_* error code.
 */
int oaknode_folder_remove_child(OakNodeFolder folder, OakNodeNode child);

/**
 * @brief Move several nodes into `dest_folder` (live, non-undoable).
 *
 * Each node is removed from its current folder (if any) and appended to
 * `dest_folder`; the graph assumes the lifetime of every moved node. Nodes
 * already directly inside `dest_folder` are skipped.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_folder_move_children(const OakNodeNode *nodes, int count,
								 OakNodeFolder dest_folder);

/**
 * @brief 1 if `folder` recursively contains `child`, 0 otherwise
 * (Folder::has_child_recursive()). Negative OAKNODE_E_* code on empty
 * handles.
 */
int oaknode_folder_has_child_recursive(OakNodeFolder folder,
									   OakNodeNode child);

/**
 * @brief Index of `child` in `folder`'s direct children
 * (Folder::index_of_child()).
 *
 * @return The index, OAKNODE_E_NOT_FOUND if not a direct child, or
 *         OAKNODE_E_INVALID on empty handles.
 */
int oaknode_folder_index_of_child(OakNodeFolder folder,
								  OakNodeNode child);

/**
 * @brief Borrowed handle of the folder a node currently belongs to
 * (Node::folder()).
 *
 * The returned handle only releases the handle itself. Empty handle
 * (ctx == NULL) if the node is not in any folder.
 */
OakNodeFolder oaknode_folder_parent_of(OakNodeNode node);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_FOLDER_H
