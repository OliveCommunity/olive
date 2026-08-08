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

#include "../c_api/nodehandle.h"
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
	OakNodeNode node_handle = as_handle(&node);

	int alive_before = oaknode_debug_alive_count();

	oaknode_value value = make_float(1.5);
	OakNodeKeyframe key = oaknode_keyframe_create(2, 1, &value,
												  OAKNODE_KEYFRAME_LINEAR, 0,
												  -1, "float_in",
												  node_handle);
	ASSERT_NE(key.ctx, nullptr);
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

	OakNodeNode parent = {};
	EXPECT_EQ(oaknode_keyframe_get_parent(key, &parent), OAKNODE_OK);
	EXPECT_EQ(oaknode_c_api::to_native<olive::Node>(parent),
			  oaknode_c_api::to_native<olive::Node>(node_handle));
	oaknode_node_free(&parent);

	oaknode_keyframe_free(&key);
	EXPECT_EQ(key.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	oaknode_keyframe_free(nullptr); // no crash
}

TEST(NodeKeyframeTest, CreateErrorPaths)
{
	oaknode_value value = make_float(1.0);
	// Invalid interpolation type.
	EXPECT_EQ(oaknode_keyframe_create(0, 1, &value, 99, 0, -1, "x",
									  OakNodeNode{})
				  .ctx,
			  nullptr);
	// STRING does not fit the POD.
	value.type = OAKNODE_VALUE_STRING;
	EXPECT_EQ(oaknode_keyframe_create(0, 1, &value, OAKNODE_KEYFRAME_LINEAR, 0,
									  -1, "x", OakNodeNode{})
				  .ctx,
			  nullptr);
}

TEST(NodeKeyframeTest, TimeLiveAndUndoable)
{
	OakNodeKeyframe key = oaknode_keyframe_create(0, 1, nullptr,
												  OAKNODE_KEYFRAME_LINEAR, 0,
												  -1, "float_in",
												  OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);

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
	oaknode_keyframe_free(&key);

	EXPECT_EQ(oaknode_keyframe_set_time(OakNodeKeyframe{}, 0, 1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_get_time(OakNodeKeyframe{}, &num, &den),
			  OAKNODE_E_INVALID);
}

TEST(NodeKeyframeTest, ValueLiveAndUndoable)
{
	OakNodeKeyframe key = oaknode_keyframe_create(0, 1, nullptr,
												  OAKNODE_KEYFRAME_LINEAR, 0,
												  -1, "float_in",
												  OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);

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

	oaknode_keyframe_free(&key);
}

TEST(NodeKeyframeTest, StringValueLiveAndUndoable)
{
	OakNodeKeyframe key = oaknode_keyframe_create(0, 1, nullptr,
												  OAKNODE_KEYFRAME_HOLD, 0, -1,
												  "text_in", OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);

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
	EXPECT_EQ(oaknode_keyframe_get_value_string(OakNodeKeyframe{}, buf,
												sizeof(buf)),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(&key);
}

TEST(NodeKeyframeTest, TypeLiveAndUndoable)
{
	OakNodeKeyframe key = oaknode_keyframe_create(0, 1, nullptr,
												  OAKNODE_KEYFRAME_LINEAR, 0,
												  -1, "float_in",
												  OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);

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
	EXPECT_EQ(oaknode_keyframe_set_type(OakNodeKeyframe{},
										OAKNODE_KEYFRAME_HOLD),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(&key);
}

TEST(NodeKeyframeTest, BezierControlLiveAndUndoable)
{
	OakNodeKeyframe key = oaknode_keyframe_create(0, 1, nullptr,
												  OAKNODE_KEYFRAME_BEZIER, 0,
												  -1, "float_in",
												  OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);

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
	EXPECT_EQ(oaknode_keyframe_get_bezier_control(OakNodeKeyframe{},
												  OAKNODE_KEYFRAME_IN_HANDLE,
												  &x, &y),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(&key);
}

TEST(NodeKeyframeTest, OpposingBezierType)
{
	EXPECT_EQ(oaknode_keyframe_opposing_bezier_type(
				  OAKNODE_KEYFRAME_IN_HANDLE),
			  OAKNODE_KEYFRAME_OUT_HANDLE);
	EXPECT_EQ(oaknode_keyframe_opposing_bezier_type(
				  OAKNODE_KEYFRAME_OUT_HANDLE),
			  OAKNODE_KEYFRAME_IN_HANDLE);
	// Only the two handle values are valid.
	EXPECT_EQ(oaknode_keyframe_opposing_bezier_type(99),
			  OAKNODE_E_INVALID);
}

TEST(NodeKeyframeTest, ValidBezierControlClampsToNeighbours)
{
	// Orphan keyframe (no previous/next): the valid point equals the
	// stored control point.
	OakNodeKeyframe key = oaknode_keyframe_create(10, 1, nullptr,
												  OAKNODE_KEYFRAME_BEZIER, 0,
												  -1, "float_in",
												  OakNodeNode{});
	ASSERT_NE(key.ctx, nullptr);
	EXPECT_EQ(oaknode_keyframe_set_bezier_control(
				  key, OAKNODE_KEYFRAME_IN_HANDLE, -5.0, 2.0),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_keyframe_set_bezier_control(
				  key, OAKNODE_KEYFRAME_OUT_HANDLE, 3.0, -1.0),
			  OAKNODE_OK);
	double x = 0.0, y = 0.0;
	EXPECT_EQ(oaknode_keyframe_get_valid_bezier_control(
				  key, OAKNODE_KEYFRAME_IN_HANDLE, &x, &y),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, -5.0);
	EXPECT_DOUBLE_EQ(y, 2.0);
	EXPECT_EQ(oaknode_keyframe_get_valid_bezier_control(
				  key, OAKNODE_KEYFRAME_OUT_HANDLE, &x, &y),
			  OAKNODE_OK);
	EXPECT_DOUBLE_EQ(x, 3.0);
	EXPECT_DOUBLE_EQ(y, -1.0);

	EXPECT_EQ(oaknode_keyframe_get_valid_bezier_control(key, 99, &x, &y),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_get_valid_bezier_control(
				  OakNodeKeyframe{}, OAKNODE_KEYFRAME_IN_HANDLE, &x, &y),
			  OAKNODE_E_INVALID);
	oaknode_keyframe_free(&key);

	// Attached keyframe: the out-handle cannot pass the next keyframe's
	// time. Keys at t=10 and t=20 on the same track.
	TestNode node;
	OakNodeNode node_handle = as_handle(&node);
	node.set_input_is_keyframing("float_in", true);
	olive::MultiUndoCommand setup;
	olive::Node::set_value_at_time(olive::NodeInput(&node, "float_in"),
								   olive::core::Rational(10, 1),
								   olive::Variant(1.0), 0, &setup, true);
	olive::Node::set_value_at_time(olive::NodeInput(&node, "float_in"),
								   olive::core::Rational(20, 1),
								   olive::Variant(2.0), 0, &setup, true);
	setup.redo_now();

	olive::NodeKeyframe *first = node.get_keyframe_at_time_on_track(
		"float_in", olive::core::Rational(10, 1), 0);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(first->next(), nullptr);
	first->set_bezier_control_out(olive::PointF(50.0, 0.0));

	OakNodeKeyframe first_handle = oaknode_c_api::make_handle<OakNodeKeyframe>(
		first, false, nullptr);
	EXPECT_EQ(oaknode_keyframe_get_valid_bezier_control(
				  first_handle, OAKNODE_KEYFRAME_OUT_HANDLE, &x, &y),
			  OAKNODE_OK);
	// 20 (next key's time) - 10 (this key's time) = 10.
	EXPECT_DOUBLE_EQ(x, 10.0);
	EXPECT_DOUBLE_EQ(y, 0.0);
	oaknode_keyframe_free(&first_handle);
}

TEST(NodeKeyframeTest, ComputePasteValue)
{
	TestNode node;
	OakNodeNode node_handle = as_handle(&node);

	// A detached track-1 key for "vec2_in" at t=10.
	oaknode_value comp = make_float(7.0);
	OakNodeKeyframe key = oaknode_keyframe_create(10, 1, &comp,
												  OAKNODE_KEYFRAME_LINEAR, 1,
												  -1, "vec2_in",
												  node_handle);
	ASSERT_NE(key.ctx, nullptr);

	// The target's split value at t=10 is (0,0); replacing track 1 with
	// the key's component combines to (0, 7).
	oaknode_value out;
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(node_handle, key, &out),
			  OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_VEC2);
	EXPECT_DOUBLE_EQ(out.f[0], 0.0);
	EXPECT_DOUBLE_EQ(out.f[1], 7.0);

	// A scalar key pastes as the whole value.
	oaknode_value f = make_float(2.5);
	OakNodeKeyframe fkey = oaknode_keyframe_create(3, 1, &f,
												   OAKNODE_KEYFRAME_LINEAR, 0,
												   -1, "float_in",
												   node_handle);
	ASSERT_NE(fkey.ctx, nullptr);
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(node_handle, fkey, &out),
			  OAKNODE_OK);
	EXPECT_EQ(out.type, OAKNODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 2.5);

	// Failure paths.
	OakNodeKeyframe foreign = oaknode_keyframe_create(
		0, 1, &f, OAKNODE_KEYFRAME_LINEAR, 0, -1, "not_on_target",
		OakNodeNode{});
	ASSERT_NE(foreign.ctx, nullptr);
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(node_handle, foreign,
												   &out),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(OakNodeNode{}, fkey,
												   &out),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(node_handle,
												   OakNodeKeyframe{}, &out),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_compute_paste_value(node_handle, fkey,
												   nullptr),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(&foreign);
	oaknode_keyframe_free(&fkey);
	oaknode_keyframe_free(&key);
}

TEST(NodeKeyframeTest, HasSiblingAtTime)
{
	TestNode node;
	OakNodeNode node_handle = as_handle(&node);
	node.set_input_is_keyframing("vec2_in", true);

	// A sibling is another key on THIS keyframe's own track at the given
	// time (the move-collision check). Keys at t=10 and t=20 on track 0
	// (each set_value_at_time call also seeds track 1 via
	// insert_on_all_tracks; the times differ so no same-time collision).
	olive::MultiUndoCommand setup;
	olive::Node::set_value_at_time(olive::NodeInput(&node, "vec2_in"),
								   olive::core::Rational(10, 1),
								   olive::Variant(1.0), 0, &setup, true);
	olive::Node::set_value_at_time(olive::NodeInput(&node, "vec2_in"),
								   olive::core::Rational(20, 1),
								   olive::Variant(2.0), 0, &setup, true);
	setup.redo_now();

	olive::NodeKeyframe *key10 = node.get_keyframe_at_time_on_track(
		"vec2_in", olive::core::Rational(10, 1), 0);
	olive::NodeKeyframe *key20 = node.get_keyframe_at_time_on_track(
		"vec2_in", olive::core::Rational(20, 1), 0);
	ASSERT_NE(key10, nullptr);
	ASSERT_NE(key20, nullptr);

	OakNodeKeyframe handle10 = oaknode_c_api::make_handle<OakNodeKeyframe>(
		key10, false, nullptr);
	OakNodeKeyframe handle20 = oaknode_c_api::make_handle<OakNodeKeyframe>(
		key20, false, nullptr);

	// Each key sees the other as a sibling.
	int has = -1;
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(handle10, 20, 1, &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 1);
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(handle20, 10, 1, &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 1);

	// A key is not its own sibling, and a time with no key has none.
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(handle10, 10, 1, &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 0);
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(handle10, 5, 1, &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 0);

	// A key alone on its track (track 0 only at t=30) has no sibling.
	olive::MultiUndoCommand setup2;
	olive::Node::set_value_at_time(olive::NodeInput(&node, "vec2_in"),
								   olive::core::Rational(30, 1),
								   olive::Variant(3.0), 0, &setup2, false);
	setup2.redo_now();
	olive::NodeKeyframe *alone = node.get_keyframe_at_time_on_track(
		"vec2_in", olive::core::Rational(30, 1), 0);
	ASSERT_NE(alone, nullptr);
	OakNodeKeyframe alone_handle = oaknode_c_api::make_handle<OakNodeKeyframe>(
		alone, false, nullptr);
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(alone_handle, 30, 1,
												   &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 0);

	// An orphaned keyframe has no siblings.
	OakNodeKeyframe orphan = oaknode_keyframe_create(0, 1, nullptr,
													 OAKNODE_KEYFRAME_LINEAR,
													 0, -1, "vec2_in",
													 OakNodeNode{});
	ASSERT_NE(orphan.ctx, nullptr);
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(orphan, 0, 1, &has),
			  OAKNODE_OK);
	EXPECT_EQ(has, 0);

	// Invalid arguments.
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(
				  OakNodeKeyframe{}, 10, 1, &has),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_keyframe_has_sibling_at_time(handle10, 10, 1, nullptr),
			  OAKNODE_E_INVALID);

	oaknode_keyframe_free(&orphan);
	oaknode_keyframe_free(&alone_handle);
	oaknode_keyframe_free(&handle20);
	oaknode_keyframe_free(&handle10);
}

}
