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

#include <gtest/gtest.h>

#include "node/folder.h"
#include "node/node.h"
#include "node/project.h"

#include "../c_api/nodehandle.h"

namespace
{

OakNodeNode as_node(OakNodeFolder folder)
{
	return oaknode_folder_as_node(folder);
}

/**
 * @brief Identity compare for value handles (each handle has its own
 * control block, so compare the wrapped objects).
 */
template <typename H1, typename H2>
bool same_object(H1 a, H2 b)
{
	return oaknode_c_api::to_native<void>(a) == oaknode_c_api::to_native<void>(b);
}

/**
 * @brief Detach all folder edges before project teardown.
 *
 * WORKAROUND: Project::clear() calls set_parent(nullptr) on each node
 * BEFORE deleting it, so Node::~Node() -> disconnect_all() trips the
 * same-graph assertion in Node::disconnect_edge() for any remaining
 * folder edges (upstream bug, reported to the parent agent). Removing
 * the edges first keeps teardown clean.
 */
void detach_all(OakNodeFolder root, OakNodeFolder a, OakNodeFolder b)
{
	if (!root.ctx) {
		return;
	}
	if (a.ctx && oaknode_folder_index_of_child(root, as_node(a)) >= 0) {
		oaknode_folder_remove_child(root, as_node(a));
	}
	if (b.ctx && oaknode_folder_index_of_child(root, as_node(b)) >= 0) {
		oaknode_folder_remove_child(root, as_node(b));
	}
	if (a.ctx && b.ctx &&
		oaknode_folder_index_of_child(a, as_node(b)) >= 0) {
		oaknode_folder_remove_child(a, as_node(b));
	}
}

} // namespace

TEST(NodeFolder, CreateAndHierarchy)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	OakNodeFolder root = oaknode_project_root(project);
	ASSERT_NE(root.ctx, nullptr);

	// Creating a folder requires a project
	EXPECT_EQ(oaknode_folder_create(OakNodeProject{}).ctx, nullptr);

	OakNodeFolder a = oaknode_folder_create(project);
	OakNodeFolder b = oaknode_folder_create(project);
	ASSERT_NE(a.ctx, nullptr);
	ASSERT_NE(b.ctx, nullptr);

	EXPECT_EQ(oaknode_folder_child_count(root), 0);
	EXPECT_EQ(oaknode_folder_child_count(a), 0);

	// add_child builds the hierarchy
	EXPECT_EQ(oaknode_folder_add_child(root, as_node(a)), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_child_count(root), 1);
	EXPECT_TRUE(same_object(oaknode_folder_child_at(root, 0), as_node(a)));
	EXPECT_EQ(oaknode_folder_index_of_child(root, as_node(a)), 0);
	EXPECT_TRUE(same_object(oaknode_folder_parent_of(as_node(a)), root));

	EXPECT_EQ(oaknode_folder_add_child(a, as_node(b)), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_has_child_recursive(root, as_node(b)), 1);
	EXPECT_EQ(oaknode_folder_has_child_recursive(a, as_node(b)), 1);
	EXPECT_EQ(oaknode_folder_has_child_recursive(b, as_node(a)), 0);

	// A node can only be in one folder at a time
	EXPECT_EQ(oaknode_folder_add_child(root, as_node(b)), OAKNODE_E_STATE);

	// Out-of-range child access yields an empty handle
	EXPECT_EQ(oaknode_folder_child_at(root, -1).ctx, nullptr);
	EXPECT_EQ(oaknode_folder_child_at(root, 1).ctx, nullptr);

	// index_of_child on a non-child is E_NOT_FOUND
	EXPECT_EQ(oaknode_folder_index_of_child(root, as_node(b)),
			  OAKNODE_E_NOT_FOUND);

	detach_all(root, a, b);
	oaknode_project_free(&project);
}

TEST(NodeFolder, RemoveChild)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	OakNodeFolder root = oaknode_project_root(project);
	OakNodeFolder a = oaknode_folder_create(project);
	ASSERT_NE(root.ctx, nullptr);
	ASSERT_NE(a.ctx, nullptr);

	ASSERT_EQ(oaknode_folder_add_child(root, as_node(a)), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_remove_child(root, as_node(a)), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_child_count(root), 0);
	EXPECT_EQ(oaknode_folder_parent_of(as_node(a)).ctx, nullptr);

	// Removing a non-child is E_NOT_FOUND
	EXPECT_EQ(oaknode_folder_remove_child(root, as_node(a)),
			  OAKNODE_E_NOT_FOUND);

	// The folder survives removal and can be re-added
	EXPECT_EQ(oaknode_folder_add_child(root, as_node(a)), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_child_count(root), 1);

	detach_all(root, a, OakNodeFolder{});
	oaknode_project_free(&project);
}

TEST(NodeFolder, MoveChildren)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	OakNodeFolder root = oaknode_project_root(project);
	OakNodeFolder a = oaknode_folder_create(project);
	OakNodeFolder b = oaknode_folder_create(project);
	OakNodeFolder c = oaknode_folder_create(project);
	ASSERT_NE(root.ctx, nullptr);
	ASSERT_NE(a.ctx, nullptr);
	ASSERT_NE(b.ctx, nullptr);
	ASSERT_NE(c.ctx, nullptr);

	ASSERT_EQ(oaknode_folder_add_child(root, as_node(a)), OAKNODE_OK);
	ASSERT_EQ(oaknode_folder_add_child(root, as_node(b)), OAKNODE_OK);
	ASSERT_EQ(oaknode_folder_add_child(a, as_node(c)), OAKNODE_OK);

	// Move b and c into a in one call
	OakNodeNode to_move[] = { as_node(b), as_node(c) };
	EXPECT_EQ(oaknode_folder_move_children(to_move, 2, a), OAKNODE_OK);

	EXPECT_EQ(oaknode_folder_child_count(root), 1);
	// c was already in a and is skipped; a ends up with {c, b}
	EXPECT_EQ(oaknode_folder_child_count(a), 2);
	EXPECT_TRUE(same_object(oaknode_folder_parent_of(as_node(b)), a));
	EXPECT_TRUE(same_object(oaknode_folder_parent_of(as_node(c)), a));
	EXPECT_EQ(oaknode_folder_index_of_child(a, as_node(c)), 0);
	EXPECT_EQ(oaknode_folder_index_of_child(a, as_node(b)), 1);

	// Moving a node already in the destination is a no-op
	EXPECT_EQ(oaknode_folder_move_children(to_move, 2, a), OAKNODE_OK);
	EXPECT_EQ(oaknode_folder_child_count(a), 2);

	// A node with no folder is simply appended
	EXPECT_EQ(oaknode_folder_remove_child(root, as_node(a)), OAKNODE_OK);
	OakNodeNode one[] = { as_node(a) };
	EXPECT_EQ(oaknode_folder_move_children(one, 1, root), OAKNODE_OK);
	EXPECT_TRUE(same_object(oaknode_folder_parent_of(as_node(a)), root));

	// Detach every remaining edge before teardown (see detach_all).
	detach_all(root, a, b);
	detach_all(a, b, c);
	detach_all(b, a, c);
	oaknode_project_free(&project);
}

TEST(NodeFolder, NullHandleErrors)
{
	EXPECT_EQ(oaknode_folder_child_count(OakNodeFolder{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_child_at(OakNodeFolder{}, 0).ctx, nullptr);
	EXPECT_EQ(oaknode_folder_add_child(OakNodeFolder{}, OakNodeNode{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_remove_child(OakNodeFolder{}, OakNodeNode{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_move_children(nullptr, 0, OakNodeFolder{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_has_child_recursive(OakNodeFolder{},
												 OakNodeNode{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_index_of_child(OakNodeFolder{}, OakNodeNode{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_folder_parent_of(OakNodeNode{}).ctx, nullptr);
}
