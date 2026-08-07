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

#include "node/group.h"

#include <gtest/gtest.h>

#include <vector>

#include "undo/undocommand.h"

#include "../c_api/nodehandle.h"
#include "../src/group/group.h"
#include "testnode.h"

namespace
{

using oaknode_test::TestNode;
using oaknode_test::as_handle;

/**
 * @brief Borrowed OakNodeNode view of a group handle (the group IS a
 * node). Release with oaknode_node_free().
 */
OakNodeNode group_as_node(OakNodeGroup group)
{
	return oaknode_c_api::make_handle<OakNodeNode>(
		oaknode_c_api::to_native<olive::NodeGroup>(group), false, nullptr);
}

/**
 * @brief Create a group with `inner` registered in its context
 * (NodeGroup::add_input_passthrough() asserts context membership).
 */
OakNodeGroup make_group_with_inner(OakNodeNode inner)
{
	OakNodeGroup group = oaknode_group_create();
	EXPECT_NE(group.ctx, nullptr);
	OakNodeNode node_view = group_as_node(group);
	EXPECT_EQ(oaknode_node_set_context_position(node_view, inner, 0.0, 0.0, 0),
			  OAKNODE_OK);
	oaknode_node_free(&node_view);
	return group;
}

TEST(NodeGroupTest, CreateCastFree)
{
	int alive_before = oaknode_debug_alive_count();

	OakNodeGroup group = oaknode_group_create();
	ASSERT_NE(group.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	OakNodeNode as_node = group_as_node(group);
	OakNodeGroup casted = oaknode_group_cast(as_node);
	ASSERT_NE(casted.ctx, nullptr);
	EXPECT_EQ(oaknode_c_api::to_native<olive::NodeGroup>(casted),
			  oaknode_c_api::to_native<olive::NodeGroup>(group));
	oaknode_group_free(&casted);
	oaknode_node_free(&as_node);

	TestNode plain;
	EXPECT_EQ(oaknode_group_cast(as_handle(&plain)).ctx, nullptr);
	EXPECT_EQ(oaknode_group_cast(OakNodeNode{}).ctx, nullptr);

	oaknode_group_free(&group);
	EXPECT_EQ(group.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	oaknode_group_free(nullptr); // no crash
}

TEST(NodeGroupTest, PassthroughAddEnumerateRemove)
{
	TestNode inner;
	OakNodeNode inner_handle = as_handle(&inner);
	OakNodeGroup group = make_group_with_inner(inner_handle);
	ASSERT_NE(group.ctx, nullptr);
	OakNodeNode group_node = group_as_node(group);

	// Two-stage: query the generated id size first.
	int float_required = oaknode_group_add_input_passthrough(
		group, inner_handle, "float_in", -1, nullptr, 0);
	EXPECT_EQ(float_required, 9); // "float_in"
	char int_id[16];
	int int_required = oaknode_group_add_input_passthrough(
		group, inner_handle, "int_in", -1, int_id, sizeof(int_id));
	EXPECT_EQ(int_required, 7); // "int_in"
	EXPECT_STREQ(int_id, "int_in");

	int count = 0;
	EXPECT_EQ(oaknode_group_passthrough_count(group, &count), OAKNODE_OK);
	EXPECT_EQ(count, 2);

	char buf[64];
	EXPECT_EQ(oaknode_group_passthrough_id_at(group, 0, buf, sizeof(buf)), 9);
	EXPECT_STREQ(buf, "float_in");
	EXPECT_EQ(oaknode_group_passthrough_id_at(group, 2, buf, sizeof(buf)),
			  OAKNODE_E_NOT_FOUND);

	// The passthrough becomes an input of the group.
	int type = -1;
	EXPECT_EQ(oaknode_node_input_get_type(group_node, buf, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_VALUE_FLOAT);

	OakNodeNode pt_node = {};
	int pt_element = -2;
	char pt_id[64];
	EXPECT_EQ(oaknode_group_passthrough_input_at(group, 0, &pt_node, pt_id,
												 sizeof(pt_id), &pt_element),
			  9);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(pt_node),
			  oaknode_c_api::to_native<olive::Node>(inner_handle));
	EXPECT_STREQ(pt_id, "float_in");
	EXPECT_EQ(pt_element, -1);
	oaknode_node_free(&pt_node);
	EXPECT_EQ(oaknode_group_passthrough_input_at(group, 5, &pt_node, pt_id,
												 sizeof(pt_id), &pt_element),
			  OAKNODE_E_NOT_FOUND);

	EXPECT_EQ(oaknode_group_remove_input_passthrough(group, inner_handle,
													 "float_in", -1),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_group_passthrough_count(group, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	EXPECT_EQ(oaknode_group_remove_input_passthrough(group, inner_handle,
													 "float_in", -1),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_group_remove_input_passthrough(OakNodeGroup{},
													 inner_handle,
													 "float_in", -1),
			  OAKNODE_E_INVALID);

	oaknode_node_free(&group_node);
	oaknode_group_free(&group);
}

TEST(NodeGroupTest, PassthroughAddUndoable)
{
	TestNode inner;
	OakNodeGroup group = make_group_with_inner(as_handle(&inner));
	ASSERT_NE(group.ctx, nullptr);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_group_add_input_passthrough_undoable(
				  group, as_handle(&inner), "float_in", -1, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);

	int count = 0;
	EXPECT_EQ(oaknode_group_passthrough_count(group, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);

	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_group_passthrough_count(group, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);

	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_group_passthrough_count(group, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);

	oakundo_command_free(&command);
	oaknode_group_free(&group);
}

TEST(NodeGroupTest, OutputPassthroughLiveAndUndoable)
{
	TestNode inner;
	OakNodeNode inner_handle = as_handle(&inner);
	OakNodeGroup group = make_group_with_inner(inner_handle);
	ASSERT_NE(group.ctx, nullptr);

	OakNodeNode out = {};
	out.ctx = reinterpret_cast<void *>(uintptr_t(1)); // sentinel: must be overwritten
	EXPECT_EQ(oaknode_group_get_output_passthrough(group, &out), OAKNODE_OK);
	EXPECT_EQ(out.ctx, nullptr);

	EXPECT_EQ(oaknode_group_set_output_passthrough(group, inner_handle),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_group_get_output_passthrough(group, &out), OAKNODE_OK);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(out),
			  oaknode_c_api::to_native<olive::Node>(inner_handle));
	oaknode_node_free(&out);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_group_set_output_passthrough_undoable(
				  group, OakNodeNode{}, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_group_get_output_passthrough(group, &out), OAKNODE_OK);
	EXPECT_EQ(out.ctx, nullptr);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_group_get_output_passthrough(group, &out), OAKNODE_OK);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(out),
			  oaknode_c_api::to_native<olive::Node>(inner_handle));
	oaknode_node_free(&out);
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_group_set_output_passthrough(OakNodeGroup{}, inner_handle),
			  OAKNODE_E_INVALID);

	oaknode_group_free(&group);
}

TEST(NodeGroupTest, ResolveInput)
{
	TestNode inner;
	OakNodeNode inner_handle = as_handle(&inner);
	OakNodeGroup group = make_group_with_inner(inner_handle);
	ASSERT_NE(group.ctx, nullptr);
	OakNodeNode group_node = group_as_node(group);

	char id_buf[64];
	int required = oaknode_group_add_input_passthrough(group, inner_handle,
													   "float_in", -1, id_buf,
													   sizeof(id_buf));
	ASSERT_GT(required, 1);

	// Resolving the group's passthrough id yields the inner input.
	OakNodeNode resolved_node = {};
	int resolved_element = -2;
	char resolved_id[64];
	EXPECT_EQ(oaknode_group_resolve_input(group_node, id_buf, -1,
										  &resolved_node, resolved_id,
										  sizeof(resolved_id),
										  &resolved_element),
			  9);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(resolved_node),
			  oaknode_c_api::to_native<olive::Node>(inner_handle));
	EXPECT_STREQ(resolved_id, "float_in");
	EXPECT_EQ(resolved_element, -1);
	oaknode_node_free(&resolved_node);

	// A non-group input resolves to itself.
	EXPECT_EQ(oaknode_group_resolve_input(inner_handle, "float_in", -1,
										  &resolved_node, resolved_id,
										  sizeof(resolved_id),
										  &resolved_element),
			  9);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(resolved_node),
			  oaknode_c_api::to_native<olive::Node>(inner_handle));
	EXPECT_STREQ(resolved_id, "float_in");
	oaknode_node_free(&resolved_node);

	// Error paths.
	EXPECT_EQ(oaknode_group_resolve_input(OakNodeNode{}, id_buf, -1,
										  &resolved_node, resolved_id,
										  sizeof(resolved_id),
										  &resolved_element),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_group_resolve_input(group_node, nullptr, -1,
										  &resolved_node, resolved_id,
										  sizeof(resolved_id),
										  &resolved_element),
			  OAKNODE_E_INVALID);

	oaknode_node_free(&group_node);
	oaknode_group_free(&group);
}

}
