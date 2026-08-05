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

#include <new>

#include "../src/project.h"
#include "../src/project/folder/folder.h"

namespace
{

olive::Folder *to_cpp(OakNodeFolder *folder)
{
	return reinterpret_cast<olive::Folder *>(folder);
}

const olive::Folder *to_cpp(const OakNodeFolder *folder)
{
	return reinterpret_cast<const olive::Folder *>(folder);
}

olive::Node *to_cpp(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

const olive::Node *to_cpp(const OakNodeNode *node)
{
	return reinterpret_cast<const olive::Node *>(node);
}

OakNodeNode *to_c(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

olive::Project *to_cpp(OakNodeProject *project)
{
	return reinterpret_cast<olive::Project *>(project);
}

} // namespace

OakNodeFolder *oaknode_folder_create(OakNodeProject *project)
{
	if (!project) {
		return NULL;
	}

	try {
		auto *folder = new (std::nothrow) olive::Folder();
		if (!folder) {
			return NULL;
		}
		to_cpp(project)->add_node(folder);
		return reinterpret_cast<OakNodeFolder *>(folder);
	} catch (...) {
		return NULL;
	}
}

int oaknode_folder_child_count(const OakNodeFolder *folder)
{
	if (!folder) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(folder)->item_child_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode *oaknode_folder_child_at(const OakNodeFolder *folder, int index)
{
	if (!folder || index < 0 || index >= to_cpp(folder)->item_child_count()) {
		return NULL;
	}

	try {
		return to_c(to_cpp(folder)->item_child(index));
	} catch (...) {
		return NULL;
	}
}

int oaknode_folder_add_child(OakNodeFolder *folder, OakNodeNode *child)
{
	if (!folder || !child) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *f = to_cpp(folder);
		olive::Node *c = to_cpp(child);

		if (c->folder()) {
			return OAKNODE_E_STATE;
		}

		olive::FolderAddChild cmd(f, c);
		cmd.redo_now();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_remove_child(OakNodeFolder *folder, OakNodeNode *child)
{
	if (!folder || !child) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *f = to_cpp(folder);
		olive::Node *c = to_cpp(child);

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

int oaknode_folder_move_children(OakNodeNode *const *nodes, int count,
								 OakNodeFolder *dest_folder)
{
	if (!nodes || count < 0 || !dest_folder) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Folder *dest = to_cpp(dest_folder);

		for (int i = 0; i < count; i++) {
			if (!nodes[i]) {
				return OAKNODE_E_INVALID;
			}

			olive::Node *node = to_cpp(nodes[i]);
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
		}

		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_has_child_recursive(const OakNodeFolder *folder,
									   const OakNodeNode *child)
{
	if (!folder || !child) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(folder)->has_child_recursive(
				   const_cast<olive::Node *>(to_cpp(child)))
				   ? 1
				   : 0;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_folder_index_of_child(const OakNodeFolder *folder,
								  const OakNodeNode *child)
{
	if (!folder || !child) {
		return OAKNODE_E_INVALID;
	}

	try {
		int index = to_cpp(folder)->index_of_child(
			const_cast<olive::Node *>(to_cpp(child)));
		return index == -1 ? OAKNODE_E_NOT_FOUND : index;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeFolder *oaknode_folder_parent_of(const OakNodeNode *node)
{
	if (!node) {
		return NULL;
	}

	try {
		return reinterpret_cast<OakNodeFolder *>(to_cpp(node)->folder());
	} catch (...) {
		return NULL;
	}
}
