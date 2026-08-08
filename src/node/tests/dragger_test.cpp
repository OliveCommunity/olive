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

#include "node/dragger.h"

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

TEST(NodeDraggerTest, CreateInvalidArguments)
{
	// Empty node handle / NULL input id / unknown input id.
	EXPECT_EQ(oaknode_dragger_create(OakNodeNode{}, "float_in", -1, 0).ctx,
			  nullptr);

	TestNode node;
	OakNodeNode handle = as_handle(&node);
	EXPECT_EQ(oaknode_dragger_create(handle, nullptr, -1, 0).ctx, nullptr);
	EXPECT_EQ(
		oaknode_dragger_create(handle, "does_not_exist", -1, 0).ctx, nullptr);
}

TEST(NodeDraggerTest, StateMachineTransitions)
{
	TestNode node;
	OakNodeNode handle = as_handle(&node);
	node.set_input_is_keyframing("float_in", true);

	OakNodeDragger dragger = oaknode_dragger_create(handle, "float_in", -1, 0);
	ASSERT_NE(dragger.ctx, nullptr);

	// Freshly created: not started.
	int started = -1;
	EXPECT_EQ(oaknode_dragger_is_started(dragger, &started), OAKNODE_OK);
	EXPECT_EQ(started, 0);

	// drag/end before start are state errors.
	oaknode_value v = make_float(1.0);
	EXPECT_EQ(oaknode_dragger_drag(dragger, &v), OAKNODE_E_STATE);
	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_dragger_end(dragger, &command), OAKNODE_E_STATE);

	// A negative track is invalid (NodeKeyframeTrackReference::is_valid).
	EXPECT_EQ(oaknode_dragger_start(dragger, 0, 1, -1, 1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_start(dragger, 0, 1, 0, 1), OAKNODE_OK);
	EXPECT_EQ(oaknode_dragger_is_started(dragger, &started), OAKNODE_OK);
	EXPECT_EQ(started, 1);

	// Double start is a state error.
	EXPECT_EQ(oaknode_dragger_start(dragger, 1, 1, 0, 1), OAKNODE_E_STATE);

	// A value whose POD type mismatches the input's declared type is
	// invalid and leaves the drag intact.
	oaknode_value bad = oaknode_value();
	bad.type = OAKNODE_VALUE_INT;
	bad.num = 7;
	EXPECT_EQ(oaknode_dragger_drag(dragger, &bad), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_drag(dragger, nullptr), OAKNODE_E_INVALID);

	// Drag a matching value.
	EXPECT_EQ(oaknode_dragger_drag(dragger, &v), OAKNODE_OK);

	// End hands back one owned command and resets the state machine.
	EXPECT_EQ(oaknode_dragger_end(dragger, &command), OAKNODE_OK);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oaknode_dragger_is_started(dragger, &started), OAKNODE_OK);
	EXPECT_EQ(started, 0);

	// Invalid-argument checks on every family entry.
	EXPECT_EQ(oaknode_dragger_end(dragger, nullptr), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_is_started(dragger, nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_is_started(OakNodeDragger{}, &started),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_start(OakNodeDragger{}, 0, 1, 0, 1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_dragger_drag(OakNodeDragger{}, &v),
			  OAKNODE_E_INVALID);

	oakundo_command_free(&command);
	oaknode_dragger_free(&dragger);
	EXPECT_EQ(dragger.ctx, nullptr);
	oaknode_dragger_free(nullptr); // no crash
}

TEST(NodeDraggerTest, CommittedDragCreatesKeyframe)
{
	TestNode node;
	OakNodeNode handle = as_handle(&node);
	node.set_input_is_keyframing("float_in", true);

	OakNodeDragger dragger = oaknode_dragger_create(handle, "float_in", -1, 0);
	ASSERT_NE(dragger.ctx, nullptr);

	EXPECT_EQ(oaknode_dragger_start(dragger, 5, 1, 0, 1), OAKNODE_OK);
	oaknode_value v = make_float(3.5);
	EXPECT_EQ(oaknode_dragger_drag(dragger, &v), OAKNODE_OK);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_dragger_end(dragger, &command), OAKNODE_OK);
	oaknode_dragger_free(&dragger);
	ASSERT_NE(command.ctx, nullptr);

	// Nothing is on the node until the returned command is executed.
	EXPECT_EQ(node.get_keyframe_at_time_on_track(
				  "float_in", olive::core::Rational(5, 1), 0),
			  nullptr);

	// Redo inserts the key with the final (dragged) value...
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	olive::NodeKeyframe *key = node.get_keyframe_at_time_on_track(
		"float_in", olive::core::Rational(5, 1), 0);
	ASSERT_NE(key, nullptr);
	EXPECT_DOUBLE_EQ(key->value().to_double(), 3.5);

	// ...and undo removes it entirely (the whole drag is ONE entry).
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(node.get_keyframe_at_time_on_track(
				  "float_in", olive::core::Rational(5, 1), 0),
			  nullptr);

	oakundo_command_free(&command);
}

TEST(NodeDraggerTest, NonKeyframingDragSetsStandardValue)
{
	TestNode node;
	OakNodeNode handle = as_handle(&node);

	OakNodeDragger dragger = oaknode_dragger_create(handle, "float_in", -1, 0);
	ASSERT_NE(dragger.ctx, nullptr);

	EXPECT_EQ(oaknode_dragger_start(dragger, 0, 1, 0, 0), OAKNODE_OK);
	oaknode_value v = make_float(2.25);
	EXPECT_EQ(oaknode_dragger_drag(dragger, &v), OAKNODE_OK);

	// Without keyframing the drag live-sets the standard value on the
	// track.
	EXPECT_DOUBLE_EQ(node.get_standard_value("float_in").to_double(), 2.25);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_dragger_end(dragger, &command), OAKNODE_OK);
	oaknode_dragger_free(&dragger);
	ASSERT_NE(command.ctx, nullptr);

	// The returned command captures start (0.0) -> end (2.25).
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_DOUBLE_EQ(node.get_standard_value("float_in").to_double(), 2.25);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_DOUBLE_EQ(node.get_standard_value("float_in").to_double(), 0.0);

	oakundo_command_free(&command);
}

TEST(NodeDraggerTest, InsertOnAllTracksCreatesSiblingKeys)
{
	TestNode node;
	OakNodeNode handle = as_handle(&node);
	node.set_input_is_keyframing("vec2_in", true);

	OakNodeDragger dragger = oaknode_dragger_create(handle, "vec2_in", -1, 1);
	ASSERT_NE(dragger.ctx, nullptr);

	// Track 1 with insert_on_all_tracks: keys on BOTH tracks.
	EXPECT_EQ(oaknode_dragger_start(dragger, 10, 1, 1, 1), OAKNODE_OK);
	// For a split-track (vec2) input the POD must carry the declared
	// type; the dragged component sits in f[0].
	oaknode_value v = oaknode_value();
	v.type = OAKNODE_VALUE_VEC2;
	v.f[0] = 4.0;
	EXPECT_EQ(oaknode_dragger_drag(dragger, &v), OAKNODE_OK);

	OakUndoCommand command = {};
	EXPECT_EQ(oaknode_dragger_end(dragger, &command), OAKNODE_OK);
	oaknode_dragger_free(&dragger);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);

	olive::NodeKeyframe *key0 = node.get_keyframe_at_time_on_track(
		"vec2_in", olive::core::Rational(10, 1), 0);
	olive::NodeKeyframe *key1 = node.get_keyframe_at_time_on_track(
		"vec2_in", olive::core::Rational(10, 1), 1);
	ASSERT_NE(key0, nullptr);
	ASSERT_NE(key1, nullptr);
	EXPECT_DOUBLE_EQ(key1->value().to_double(), 4.0);

	oakundo_command_free(&command);
}

} // namespace
