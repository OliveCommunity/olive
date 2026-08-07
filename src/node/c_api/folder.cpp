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

#include "node/folder.h"

#include "../../undo/c_api/commandhandle.h"

#include <new>

#include "node/node.h"

#include "../src/project.h"
#include "../src/project/folder/folder.h"

#include "nodehandle.h"

using oaknode_c_api::delete_as;
using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

OakNodeFolder oaknode_folder_create(OakNodeProject project)
{
	if (!project.ctx) {
		return OakNodeFolder{};
	}

	try {
		auto *folder = new (std::nothrow) olive::Folder();
		if (!folder) {
			return OakNodeFolder{};
		}
		to_native<olive::Project>(project)->add_node(folder);
		// Borrowed handle: the project graph owns the folder.
		return make_handle<OakNodeFolder>(folder, false,
										  &delete_as<olive::Folder>);
	} catch (...) {
		return OakNodeFolder{};
	}
}

int oaknode_folder_child_count(OakNodeFolder folder)
{
	if (!folder.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_native<olive::Folder>(folder)->item_child_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode oaknode_folder_child_at(OakNodeFolder folder, int index)
{
	if (!folder.ctx || index < 0 ||
		index >= to_native<olive::Folder>(folder)->item_child_count()) {
		return OakNodeNode{};
	}

	try {
		return make_handle<OakNodeNode>(
			to_native<olive::Folder>(folder)->item_child(index), false,
			&delete_as<olive::Node>);
	} catch (...) {
		return OakNodeNode{};
	}
}

int oaknode_folder_add_child(OakNodeFolder folder, OakNodeNode child)
{
	if (!folder.ctx || !child.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *f = to_native<olive::Folder>(folder);
		olive::Node *c = to_native<olive::Node>(child);

		if (c->folder()) {
			return OAKNODE_E_STATE;
		}

		olive::FolderAddChild cmd(f, c);
		cmd.redo_now();
		// The graph now owns the child; releasing `child` must not
		// delete it.
		oaknode_c_api::mark_container_owned(child);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_remove_child(OakNodeFolder folder, OakNodeNode child)
{
	if (!folder.ctx || !child.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *f = to_native<olive::Folder>(folder);
		olive::Node *c = to_native<olive::Node>(child);

		if (f->index_of_child(c) == -1) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Folder::RemoveElementCommand cmd(f, c);
		cmd.redo_now();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_move_children(const OakNodeNode *nodes, int count,
								 OakNodeFolder dest_folder)
{
	if (!nodes || count < 0 || !dest_folder.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *dest = to_native<olive::Folder>(dest_folder);

		for (int i = 0; i < count; i++) {
			if (!nodes[i].ctx) {
				return OAKNODE_E_INVALID;
			}

			olive::Node *node = to_native<olive::Node>(nodes[i]);
			olive::Folder *old_folder = node->folder();

			if (old_folder == dest) {
				continue;
			}

			if (old_folder) {
				olive::Folder::RemoveElementCommand remove_cmd(old_folder, node);
				remove_cmd.redo_now();
			}

			olive::FolderAddChild add_cmd(dest, node);
			add_cmd.redo_now();
			// The graph owns the moved node; releasing the caller's
			// handle must not delete it.
			oaknode_c_api::mark_container_owned(nodes[i]);
		}

		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_has_child_recursive(OakNodeFolder folder,
									   OakNodeNode child)
{
	if (!folder.ctx || !child.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_native<olive::Folder>(folder)->has_child_recursive(
				   to_native<olive::Node>(child))
				   ? 1
				   : 0;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_index_of_child(OakNodeFolder folder,
								  OakNodeNode child)
{
	if (!folder.ctx || !child.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		int index = to_native<olive::Folder>(folder)->index_of_child(
			to_native<olive::Node>(child));
		return index == -1 ? OAKNODE_E_NOT_FOUND : index;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeFolder oaknode_folder_parent_of(OakNodeNode node)
{
	if (!node.ctx) {
		return OakNodeFolder{};
	}

	try {
		return make_handle<OakNodeFolder>(
			to_native<olive::Node>(node)->folder(), false,
			&delete_as<olive::Folder>);
	} catch (...) {
		return OakNodeFolder{};
	}
}

OakNodeNode oaknode_folder_as_node(OakNodeFolder folder)
{
	// Borrowed cast: same object, the handle only releases itself.
	return make_handle<OakNodeNode>(to_native<olive::Folder>(folder), false,
									&delete_as<olive::Node>);
}

OakUndoCommand oaknode_command_create_folder_add_child(
	OakNodeFolder folder, OakNodeNode child)
{
	if (!folder.ctx || !child.ctx) {
		return OakUndoCommand{};
	}

	try {
		return oakundo_capi::make_command_handle(
			new olive::FolderAddChild(to_native<olive::Folder>(folder),
									  to_native<olive::Node>(child)),
			true);
	} catch (...) {
		return OakUndoCommand{};
	}
}
