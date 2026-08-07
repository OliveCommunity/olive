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

#include "node/keyframe.h"

#include <gtest/gtest.h>

#include "undo/undocommand.h"

#include "testnode.h"

namespace
{

using oaknode_test::TestNode;
using oaknode_test::as_handle;

oaknode_value make_float(double f)
{
	oaknode_value v = oaknode_value();
	v.type = OAKNODE_VALUE_FLOAT;
	v.f[0] = f;
	return v;
}

TEST(NodeKeyframeTest, EnumOrdinalsArePinned)
{
	EXPECT_EQ(OAKNODE_KEYFRAME_INVALID, -1);
	EXPECT_EQ(OAKNODE_KEYFRAME_LINEAR, 0);
	EXPECT_EQ(OAKNODE_KEYFRAME_HOLD, 1);
	EXPECT_EQ(OAKNODE_KEYFRAME_BEZIER, 2);
	EXPECT_EQ(OAKNODE_KEYFRAME_IN_HANDLE, 0);
	EXPECT_EQ(OAKNODE_KEYFRAME_OUT_HANDLE, 1);
}

TEST(NodeKeyframeTest, CreateAccessorsFree)
{
	TestNode node;
	OakNodeNode *node_handle = as_handle(&node);

	int alive_before = oaknode_debug_alive_count();

	oaknode_value value = make_float(1.5);
	OakNodeKeyframe *key = oaknode_keyframe_create(2, 1, &value,
												   OAKNODE_KEYFRAME_LINEAR, 0,
												   -1, "float_in",
												   node_handle);
	ASSERT_NE(key, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	int64_t num = 0, den = 0;
	EXPECT_EQ(oaknode_keyframe_get_time(key, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 2);
	EXPECT_EQ(den, 1);

	// The parent node's declared type pins the value mapping.
	oaknode_value out;
	EXPECT_EQ(oaknode_keyframe_get_value(key, &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 1.5);

	int scalar = -1;
	EXPECT_EQ(oaknode_keyframe_get_type(key, &scalar), OAKNODE_OK);
	EXPECT_EQ(scalar, OAKNODE_KEYFRAME_LINEAR);
	EXPECT_EQ(oaknode_keyframe_get_track(key, &scalar), OAKNODE_OK);
	EXPECT_EQ(scalar, 0);
	EXPECT_EQ(oaknode_keyframe_get_element(key, &scalar), OAKNODE_OK);
	EXPECT_EQ(scalar, -1);

	char buf[32];
	EXPECT_EQ(oaknode_keyframe_get_input(key, buf, sizeof(buf)), 9);
	EXPECT_STREQ(buf, "float_in");

	OakNodeNode *parent = nullptr;
	EXPECT_EQ(oaknode_keyframe_get_parent(key, &parent), OAKNODE_OK);
	EXPECT_EQ(parent, node_handle);

	oaknode_keyframe_free(key);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	oaknode_keyframe_free(nullptr); // no crash
}

TEST(NodeKeyframeTest, CreateErrorPaths)
{
	oaknode_value value = make_float(1.0);
	// Invalid interpolation type.
	EXPECT_EQ(oaknode_keyframe_create(0, 1, &value, 99, 0, -1, "x", nullptr),
			  nullptr);
	// STRING does not fit the POD.
	value.type = OAKNODE_VALUE_STRING;
	EXPECT_EQ(oaknode_keyframe_create(0, 1, &value, OAKNODE_KEYFRAME_LINEAR, 0,
									  -1, "x", nullptr),
			  nullptr);
}

TEST(NodeKeyframeTest, TimeLiveAndUndoable)
{
	OakNodeKeyframe *key = oaknode_keyframe_create(0, 1, nullptr,
												   OAKNODE_KEYFRAME_LINEAR, 0,
												   -1, "float_in", nullptr);
	ASSERT_NE(key, nullptr);

	EXPECT_EQ(oaknode_keyframe_set_time(key, 1, 2), OAKNODE_OK);
	int64_t num = 0, den = 0;
	EXPECT_EQ(oaknode_keyframe_get_time(key, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 1);
	EXPECT_EQ(den, 2);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_keyframe_set_time_undoable(key, 5, 1, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oaknode_keyframe_get_time(key, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 1); // not yet executed

	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_time(key, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 5);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_time(key, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 1);

	oakundo_command_free(&command);
	oaknode_keyframe_free(key);

	EXPECT_EQ(oaknode_keyframe_set_time(nullptr, 0, 1), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_get_time(nullptr, &num, &den),
			  OAKNODE_E_INVALID);
}

TEST(NodeKeyframeTest, ValueLiveAndUndoable)
{
	OakNodeKeyframe *key = oaknode_keyframe_create(0, 1, nullptr,
												   OAKNODE_KEYFRAME_LINEAR, 0,
												   -1, "float_in", nullptr);
	ASSERT_NE(key, nullptr);

	oaknode_value in = make_float(3.25);
	EXPECT_EQ(oaknode_keyframe_set_value(key, &in), OAKNODE_OK);

	// Orphan fallback: numeric values report as FLOAT.
	oaknode_value out;
	EXPECT_EQ(oaknode_keyframe_get_value(key, &out), OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 3.25);

	in = make_float(7.0);
	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_keyframe_set_value_undoable(key, &in, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_value(key, &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 7.0);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_value(key, &out), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(out.f[0], 3.25);
	oakundo_command_free(&command);

	// STRING rejected by the POD setter.
	in.type = OAKNODE_VALUE_STRING;
	EXPECT_EQ(oaknode_keyframe_set_value(key, &in), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_set_value(key, nullptr), OAKNODE_E_INVALID);

	oaknode_keyframe_free(key);
}

TEST(NodeKeyframeTest, StringValueLiveAndUndoable)
{
	OakNodeKeyframe *key = oaknode_keyframe_create(0, 1, nullptr,
												   OAKNODE_KEYFRAME_HOLD, 0, -1,
												   "text_in", nullptr);
	ASSERT_NE(key, nullptr);

	EXPECT_EQ(oaknode_keyframe_set_value_string(key, "hello"), OAKNODE_OK);
	char buf[16];
	EXPECT_EQ(oaknode_keyframe_get_value_string(key, buf, sizeof(buf)), 6);
	EXPECT_STREQ(buf, "hello");

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_keyframe_set_value_string_undoable(key, "world",
														 &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_value_string(key, buf, sizeof(buf)), 6);
	EXPECT_STREQ(buf, "world");
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_value_string(key, buf, sizeof(buf)), 6);
	EXPECT_STREQ(buf, "hello");
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_keyframe_set_value_string(key, nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_get_value_string(nullptr, buf, sizeof(buf)),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(key);
}

TEST(NodeKeyframeTest, TypeLiveAndUndoable)
{
	OakNodeKeyframe *key = oaknode_keyframe_create(0, 1, nullptr,
												   OAKNODE_KEYFRAME_LINEAR, 0,
												   -1, "float_in", nullptr);
	ASSERT_NE(key, nullptr);

	EXPECT_EQ(oaknode_keyframe_set_type(key, OAKNODE_KEYFRAME_HOLD),
			  OAKNODE_OK);
	int type = -1;
	EXPECT_EQ(oaknode_keyframe_get_type(key, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_KEYFRAME_HOLD);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_keyframe_set_type_undoable(key, OAKNODE_KEYFRAME_BEZIER,
												 &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_type(key, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_KEYFRAME_BEZIER);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_type(key, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_KEYFRAME_HOLD);
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_keyframe_set_type(key, 99), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_set_type(nullptr, OAKNODE_KEYFRAME_HOLD),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(key);
}

TEST(NodeKeyframeTest, BezierControlLiveAndUndoable)
{
	OakNodeKeyframe *key = oaknode_keyframe_create(0, 1, nullptr,
												   OAKNODE_KEYFRAME_BEZIER, 0,
												   -1, "float_in", nullptr);
	ASSERT_NE(key, nullptr);

	EXPECT_EQ(oaknode_keyframe_set_bezier_control(
				  key, OAKNODE_KEYFRAME_IN_HANDLE, -1.0, 0.5),
			  OAKNODE_OK);
	double x = 0.0, y = 0.0;
	EXPECT_EQ(oaknode_keyframe_get_bezier_control(
				  key, OAKNODE_KEYFRAME_IN_HANDLE, &x, &y),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, -1.0);
	EXPECT_DOUBLE_EQ(y, 0.5);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_keyframe_set_bezier_control_undoable(
				  key, OAKNODE_KEYFRAME_OUT_HANDLE, 2.0, -0.5, &command),
			  OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_bezier_control(
				  key, OAKNODE_KEYFRAME_OUT_HANDLE, &x, &y),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 2.0);
	EXPECT_DOUBLE_EQ(y, -0.5);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oaknode_keyframe_get_bezier_control(
				  key, OAKNODE_KEYFRAME_OUT_HANDLE, &x, &y),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 0.0);
	EXPECT_DOUBLE_EQ(y, 0.0);
	oakundo_command_free(&command);

	EXPECT_EQ(oaknode_keyframe_set_bezier_control(key, 99, 0.0, 0.0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_get_bezier_control(nullptr,
												  OAKNODE_KEYFRAME_IN_HANDLE,
												  &x, &y),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(key);
}

}
