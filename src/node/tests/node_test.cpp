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

#include "node/node.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "undo/undocommand.h"

#include "../src/value.h"
#include "testnode.h"

namespace
{

using oaknode_test::TestNode;
using oaknode_test::as_handle;

std::string get_string(int (*fn)(const OakNodeNode *, char *, int),
					   const OakNodeNode *node)
{
	int required = fn(node, nullptr, 0);
	EXPECT_GT(required, 0);
	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(fn(node, buf.data(), required), required);
	return std::string(buf.data());
}

oaknode_value make_float(double f)
{
	oaknode_value v = oaknode_value();
	v.type = OAKNODE_VALUE_FLOAT;
	v.f[0] = f;
	return v;
}

TEST(NodeValueMappingTest, OakEnumOrdinalsArePinned)
{
	EXPECT_EQ(OAKNODE_VALUE_NONE, 0);
	EXPECT_EQ(OAKNODE_VALUE_INT, 1);
	EXPECT_EQ(OAKNODE_VALUE_FLOAT, 2);
	EXPECT_EQ(OAKNODE_VALUE_BOOL, 3);
	EXPECT_EQ(OAKNODE_VALUE_RATIONAL, 4);
	EXPECT_EQ(OAKNODE_VALUE_COLOR, 5);
	EXPECT_EQ(OAKNODE_VALUE_VEC2, 6);
	EXPECT_EQ(OAKNODE_VALUE_VEC3, 7);
	EXPECT_EQ(OAKNODE_VALUE_VEC4, 8);
	EXPECT_EQ(OAKNODE_VALUE_COMBO, 9);
	EXPECT_EQ(OAKNODE_VALUE_STRING, 10);
}

TEST(NodeValueMappingTest, OliveToOakMappingIsPinned)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	struct {
		const char *input;
		int expected;
	} cases[] = {
		{ "float_in", OAKNODE_VALUE_FLOAT }, { "int_in", OAKNODE_VALUE_INT },
		{ "text_in", OAKNODE_VALUE_STRING }, { "color_in", OAKNODE_VALUE_COLOR },
		{ "vec2_in", OAKNODE_VALUE_VEC2 },	 { "rational_in", OAKNODE_VALUE_RATIONAL },
		{ "enabled_in", OAKNODE_VALUE_BOOL },
	};

	for (const auto &c : cases) {
		int type = -1;
		EXPECT_EQ(oaknode_node_input_get_type(handle, c.input, &type),
				  OAKNODE_OK);
		EXPECT_EQ(type, c.expected) << c.input;
	}
}

TEST(NodeMetadataTest, IdNameLabelRoundtrip)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	EXPECT_EQ(get_string(oaknode_node_get_id, handle), TestNode::k_id);
	EXPECT_EQ(get_string(oaknode_node_get_name, handle), "Test Node");
	EXPECT_EQ(get_string(oaknode_node_get_label, handle), "");

	EXPECT_EQ(oaknode_node_set_label(handle, "hero"), OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_node_get_label, handle), "hero");

	// Truncation still reports the full required size.
	char small[3];
	EXPECT_EQ(oaknode_node_get_label(handle, small, sizeof(small)), 5);
	EXPECT_STREQ(small, "he");
}

TEST(NodeMetadataTest, LabelUndoableSymmetry)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	EXPECT_EQ(oaknode_node_set_label(handle, "before"), OAKNODE_OK);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_label_undoable(handle, "after", &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(get_string(oaknode_node_get_label, handle), "before");

	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(get_string(oaknode_node_get_label, handle), "after");
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(get_string(oaknode_node_get_label, handle), "before");

	oakundo_command_free(&command);
}

TEST(NodeMetadataTest, OverrideColorLiveAndUndoable)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	int color = -2;
	EXPECT_EQ(oaknode_node_get_override_color(handle, &color), OAKNODE_OK);
	EXPECT_EQ(color, -1);

	EXPECT_EQ(oaknode_node_set_override_color(handle, 3), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_override_color(handle, &color), OAKNODE_OK);
	EXPECT_EQ(color, 3);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_override_color_undoable(handle, 7, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_override_color(handle, &color), OAKNODE_OK);
	EXPECT_EQ(color, 7);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_override_color(handle, &color), OAKNODE_OK);
	EXPECT_EQ(color, 3);
	oakundo_command_free(&command);
}

TEST(NodeMetadataTest, EnabledLiveAndUndoable)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	int enabled = 0;
	EXPECT_EQ(oaknode_node_is_enabled(handle, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 1);

	EXPECT_EQ(oaknode_node_set_enabled(handle, 0), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_is_enabled(handle, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 0);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_enabled_undoable(handle, 1, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_is_enabled(handle, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 1);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_is_enabled(handle, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 0);
	oakundo_command_free(&command);
}

TEST(NodeInputTest, EnumerateInputs)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	int count = 0;
	EXPECT_EQ(oaknode_node_input_count(handle, &count), OAKNODE_OK);
	EXPECT_EQ(count, 7); // 6 TestNode inputs + enabled_in

	char buf[64];
	EXPECT_EQ(oaknode_node_input_id(handle, 0, buf, sizeof(buf)), 11);
	EXPECT_STREQ(buf, "enabled_in");
	EXPECT_EQ(oaknode_node_input_id(handle, count, buf, sizeof(buf)),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_node_input_id(handle, -1, buf, sizeof(buf)),
			  OAKNODE_E_NOT_FOUND);

	int flags = 0;
	EXPECT_EQ(oaknode_node_input_is_connectable(handle, "float_in", &flags),
			  OAKNODE_OK);
	EXPECT_EQ(flags, 1);
	EXPECT_EQ(oaknode_node_input_is_connectable(handle, "nope", &flags),
			  OAKNODE_E_NOT_FOUND);

	node.retranslate();
	EXPECT_EQ(oaknode_node_get_input_name(handle, "enabled_in", buf,
										  sizeof(buf)),
			  8);
	EXPECT_STREQ(buf, "Enabled");
}

TEST(NodeInputTest, ValueRoundtrip)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	oaknode_value out;
	EXPECT_EQ(oaknode_node_get_input(handle, "float_in", &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 0.0);

	oaknode_value in = make_float(2.5);
	EXPECT_EQ(oaknode_node_set_input(handle, "float_in", &in), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "float_in", &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 2.5);

	in = oaknode_value();
	in.type = OAKNODE_VALUE_INT;
	in.num = 42;
	EXPECT_EQ(oaknode_node_set_input(handle, "int_in", &in), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "int_in", &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_INT);
	EXPECT_EQ(out.num, 42);

	in = oaknode_value();
	in.type = OAKNODE_VALUE_RATIONAL;
	in.num = 30000;
	in.den = 1001;
	EXPECT_EQ(oaknode_node_set_input(handle, "rational_in", &in), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "rational_in", &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_RATIONAL);
	EXPECT_EQ(out.num, 30000);
	EXPECT_EQ(out.den, 1001);

	in = oaknode_value();
	in.type = OAKNODE_VALUE_VEC2;
	in.f[0] = 1.0;
	in.f[1] = -2.0;
	EXPECT_EQ(oaknode_node_set_input(handle, "vec2_in", &in), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "vec2_in", &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_VEC2);
	EXPECT_DOUBLE_EQ(out.f[0], 1.0);
	EXPECT_DOUBLE_EQ(out.f[1], -2.0);

	in = oaknode_value();
	in.type = OAKNODE_VALUE_COLOR;
	in.f[0] = 0.1;
	in.f[1] = 0.2;
	in.f[2] = 0.3;
	in.f[3] = 0.4;
	EXPECT_EQ(oaknode_node_set_input(handle, "color_in", &in), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "color_in", &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_COLOR);
	EXPECT_FLOAT_EQ(float(out.f[0]), 0.1f);
	EXPECT_FLOAT_EQ(float(out.f[3]), 0.4f);
}

TEST(NodeInputTest, ValueErrorPaths)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	oaknode_value out;
	EXPECT_EQ(oaknode_node_get_input(nullptr, "float_in", &out),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_get_input(handle, "unknown_in", &out),
			  OAKNODE_E_NOT_FOUND);
	// String-family input via the POD getter.
	EXPECT_EQ(oaknode_node_get_input(handle, "text_in", &out),
			  OAKNODE_E_INVALID);

	// Type mismatch on set.
	oaknode_value in = make_float(1.0);
	EXPECT_EQ(oaknode_node_set_input(handle, "int_in", &in), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_set_input(handle, "unknown_in", &in),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_node_set_input(nullptr, "int_in", &in),
			  OAKNODE_E_INVALID);
	in.type = OAKNODE_VALUE_STRING;
	EXPECT_EQ(oaknode_node_set_input(handle, "text_in", &in),
			  OAKNODE_E_INVALID);
}

TEST(NodeInputTest, ValueUndoableSymmetry)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	oaknode_value in = make_float(9.0);
	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_input_undoable(handle, "float_in", &in, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);

	oaknode_value out;
	EXPECT_EQ(oaknode_node_get_input(handle, "float_in", &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 0.0); // not yet executed

	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "float_in", &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 9.0);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_input(handle, "float_in", &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 0.0);
	oakundo_command_free(&command);
}

TEST(NodeInputTest, StringValueLiveAndUndoable)
{
	TestNode node;
	OakNodeNode *handle = as_handle(&node);

	char buf[64];
	EXPECT_EQ(oaknode_node_get_input_string(handle, "text_in", buf,
											sizeof(buf)),
			  1);
	EXPECT_EQ(oaknode_node_set_input_string(handle, "text_in", "hello"),
			  OAKNODE_OK);
	int required = oaknode_node_get_input_string(handle, "text_in", buf,
												 sizeof(buf));
	EXPECT_EQ(required, 6);
	EXPECT_STREQ(buf, "hello");

	// Wrong family / unknown input.
	EXPECT_EQ(oaknode_node_get_input_string(handle, "float_in", buf,
											sizeof(buf)),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_set_input_string(handle, "unknown_in", "x"),
			  OAKNODE_E_NOT_FOUND);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_input_string_undoable(handle, "text_in",
													 "world", &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_input_string(handle, "text_in", buf,
											sizeof(buf)),
			  6);
	EXPECT_STREQ(buf, "world");
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_input_string(handle, "text_in", buf,
											sizeof(buf)),
			  6);
	EXPECT_STREQ(buf, "hello");
	oakundo_command_free(&command);
}

TEST(NodeGraphTest, ConnectDisconnectLive)
{
	TestNode source;
	TestNode dest;
	OakNodeNode *src = as_handle(&source);
	OakNodeNode *dst = as_handle(&dest);

	int connected = -1;
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 0);

	EXPECT_EQ(oaknode_node_connect(src, dst, "float_in"), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 1);

	OakNodeNode *upstream = nullptr;
	EXPECT_EQ(oaknode_node_input_get_connected_node(dst, "float_in", &upstream),
			  OAKNODE_OK);
	EXPECT_EQ(upstream, src);

	// Output side enumeration.
	int count = 0;
	EXPECT_EQ(oaknode_node_output_connection_count(src, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	OakNodeNode *downstream = nullptr;
	EXPECT_EQ(oaknode_node_output_connection_node_at(src, 0, &downstream),
			  OAKNODE_OK);
	EXPECT_EQ(downstream, dst);
	char buf[64];
	EXPECT_EQ(oaknode_node_output_connection_input_id_at(src, 0, buf,
														 sizeof(buf)),
			  9);
	EXPECT_STREQ(buf, "float_in");
	int element = -2;
	EXPECT_EQ(oaknode_node_output_connection_element_at(src, 0, &element),
			  OAKNODE_OK);
	EXPECT_EQ(element, -1);
	EXPECT_EQ(oaknode_node_output_connection_node_at(src, 1, &downstream),
			  OAKNODE_E_NOT_FOUND);

	// Double connect is a state error.
	EXPECT_EQ(oaknode_node_connect(src, dst, "float_in"), OAKNODE_E_STATE);
	// Unknown input id.
	EXPECT_EQ(oaknode_node_connect(src, dst, "unknown_in"),
			  OAKNODE_E_NOT_FOUND);

	EXPECT_EQ(oaknode_node_disconnect(dst, "float_in"), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 0);
	EXPECT_EQ(oaknode_node_disconnect(dst, "float_in"), OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_node_disconnect(nullptr, "float_in"), OAKNODE_E_INVALID);
}

TEST(NodeGraphTest, ConnectDisconnectUndoable)
{
	TestNode source;
	TestNode dest;
	OakNodeNode *src = as_handle(&source);
	OakNodeNode *dst = as_handle(&dest);

	OakUndoCommand add = {};
	EXPECT_EQ(oaknode_node_connect_undoable(src, dst, "float_in", &add),
			  OAKNODE_OK);
	ASSERT_NE(add.ctx, nullptr);

	int connected = 0;
	EXPECT_EQ(oakundo_command_redo_now(add), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 1);
	EXPECT_EQ(oakundo_command_undo_now(add), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 0);

	// Redo again so the remove command has an edge to work on.
	EXPECT_EQ(oakundo_command_redo_now(add), OAKUNDO_OK);

	OakUndoCommand remove = {};
	EXPECT_EQ(oaknode_node_disconnect_undoable(dst, "float_in", &remove),
			  OAKNODE_OK);
	ASSERT_NE(remove.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(remove), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 0);
	EXPECT_EQ(oakundo_command_undo_now(remove), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_input_is_connected(dst, "float_in", &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, 1);

	EXPECT_EQ(oaknode_node_disconnect_undoable(dst, "int_in", &remove),
			  OAKNODE_E_NOT_FOUND);

	oakundo_command_free(&remove);
	oakundo_command_free(&add);
}

TEST(NodeLinkTest, LinkUnlinkLiveAndUndoable)
{
	TestNode a_node;
	TestNode b_node;
	OakNodeNode *a = as_handle(&a_node);
	OakNodeNode *b = as_handle(&b_node);

	int linked = -1;
	EXPECT_EQ(oaknode_node_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 0);

	int done = 0;
	EXPECT_EQ(oaknode_node_link(a, b, &done), OAKNODE_OK);
	EXPECT_EQ(done, 1);
	EXPECT_EQ(oaknode_node_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 1);

	int count = 0;
	EXPECT_EQ(oaknode_node_link_count(a, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	OakNodeNode *other = nullptr;
	EXPECT_EQ(oaknode_node_link_at(a, 0, &other), OAKNODE_OK);
	EXPECT_EQ(other, b);
	EXPECT_EQ(oaknode_node_link_at(a, 1, &other), OAKNODE_E_NOT_FOUND);

	EXPECT_EQ(oaknode_node_unlink(a, b, &done), OAKNODE_OK);
	EXPECT_EQ(done, 1);
	EXPECT_EQ(oaknode_node_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 0);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_link_undoable(a, b, 1, &command), OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 1);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 0);
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_node_link(nullptr, b, &done), OAKNODE_E_INVALID);
}

TEST(NodeContextTest, PositionsLiveAndUndoable)
{
	TestNode node;
	TestNode context;
	OakNodeNode *n = as_handle(&node);
	OakNodeNode *ctx = as_handle(&context);

	double x = 0.0, y = 0.0;
	int expanded = -1;
	EXPECT_EQ(oaknode_node_get_context_position(n, ctx, &x, &y, &expanded),
			  OAKNODE_E_NOT_FOUND);

	EXPECT_EQ(oaknode_node_set_context_position(n, ctx, 10.5, -4.0, 1),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_node_get_context_position(n, ctx, &x, &y, &expanded),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 10.5);
	EXPECT_DOUBLE_EQ(y, -4.0);
	EXPECT_EQ(expanded, 1);

	int count = 0;
	EXPECT_EQ(oaknode_node_context_count(n, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	OakNodeNode *entry = nullptr;
	EXPECT_EQ(oaknode_node_context_node_at(n, 0, &entry), OAKNODE_OK);
	EXPECT_EQ(entry, ctx);
	EXPECT_EQ(oaknode_node_context_node_at(n, 1, &entry), OAKNODE_E_NOT_FOUND);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_node_set_context_position_undoable(n, ctx, 1.0, 2.0, 0,
														 &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_context_position(n, ctx, &x, &y, &expanded),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 1.0);
	EXPECT_DOUBLE_EQ(y, 2.0);
	EXPECT_EQ(expanded, 0);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_node_get_context_position(n, ctx, &x, &y, &expanded),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 10.5);
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_node_remove_from_context(n, ctx), OAKNODE_OK);
	EXPECT_EQ(oaknode_node_remove_from_context(n, ctx), OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_node_context_count(n, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);
}

TEST(NodeLifetimeTest, CopyAndFree)
{
	TestNode node;
	oaknode_value in = make_float(5.0);
	EXPECT_EQ(oaknode_node_set_input(as_handle(&node), "float_in", &in),
			  OAKNODE_OK);

	int alive_before = oaknode_debug_alive_count();

	OakNodeNode *copy = oaknode_node_create_copy(as_handle(&node));
	ASSERT_NE(copy, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	// copy() clones the type, not the values.
	EXPECT_EQ(get_string(oaknode_node_get_id, copy), TestNode::k_id);

	oaknode_node_free(copy);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	oaknode_node_free(nullptr); // no crash
	EXPECT_EQ(oaknode_node_create_copy(nullptr), nullptr);
}

TEST(NodeHandleTest, NullHandleErrors)
{
	char buf[16];
	int value = 0;
	EXPECT_EQ(oaknode_node_get_id(nullptr, buf, sizeof(buf)),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_set_label(nullptr, "x"), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_get_override_color(nullptr, &value),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_is_enabled(nullptr, &value), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_input_count(nullptr, &value), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_are_linked(nullptr, nullptr, &value),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_context_count(nullptr, &value), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_node_output_connection_count(nullptr, &value),
			  OAKNODE_E_INVALID);
}

}
