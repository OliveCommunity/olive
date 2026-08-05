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

#include "common/loopmode.h"
#include "node/block.h"
#include "node/node.h"
#include "node/error.h"
#include "node/track.h"

namespace
{

void expect_rational(int num, int den, int expected_num, int expected_den)
{
	EXPECT_EQ(num, expected_num);
	EXPECT_EQ(den, expected_den);
}

} // namespace

TEST(BlockTest, CreateFreeClip)
{
	int base = oaknode_debug_alive_count();
	OakNodeBlock *clip = oaknode_block_clip_create();
	ASSERT_NE(clip, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), base + 1);
	oaknode_block_free(clip);
	EXPECT_EQ(oaknode_debug_alive_count(), base);
}

TEST(BlockTest, CreateFreeGap)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);
	oaknode_block_free(gap);
}

TEST(BlockTest, CreateTransitions)
{
	OakNodeBlock *cd =
		oaknode_block_transition_create(OAKNODE_TRANSITION_CROSS_DISSOLVE);
	EXPECT_NE(cd, nullptr);
	OakNodeBlock *dc =
		oaknode_block_transition_create(OAKNODE_TRANSITION_DIP_TO_COLOR);
	EXPECT_NE(dc, nullptr);
	EXPECT_EQ(oaknode_block_transition_create(99), nullptr);
	EXPECT_EQ(oaknode_block_transition_create(-1), nullptr);
	oaknode_block_free(cd);
	oaknode_block_free(dc);
}

TEST(BlockTest, FreeNullIsNoOp)
{
	oaknode_block_free(nullptr);
}

TEST(BlockTest, NullHandleReturnsInvalid)
{
	int num, den;
	EXPECT_EQ(oaknode_block_get_in(nullptr, &num, &den), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_block_set_in(nullptr, 0, 1), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_block_get_length(nullptr, &num, &den), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_block_get_enabled(nullptr, &num), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_clip_get_speed(nullptr, nullptr), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_transition_is_dual(nullptr, &num), OAKNODE_E_INVALID);

	OakNodeBlock *clip = oaknode_block_clip_create();
	ASSERT_NE(clip, nullptr);
	EXPECT_EQ(oaknode_block_get_in(clip, nullptr, &den), OAKNODE_E_INVALID);
	oaknode_block_free(clip);
}

TEST(BlockTest, InOutLength)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);

	int num, den;
	ASSERT_EQ(oaknode_block_set_length_and_media_out(gap, 2, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_block_get_length(gap, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 2, 1);

	// In/out are timeline positions: they only move once the block sits on
	// a track (Track::update_in_out_from), so a trackless block stays 0
	ASSERT_EQ(oaknode_block_get_out(gap, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 0, 1);

	ASSERT_EQ(oaknode_block_set_in(gap, 1, 2), OAKNODE_OK);
	ASSERT_EQ(oaknode_block_get_in(gap, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 2);

	ASSERT_EQ(oaknode_block_set_out(gap, 5, 2), OAKNODE_OK);
	ASSERT_EQ(oaknode_block_get_out(gap, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 5, 2);

	ASSERT_EQ(oaknode_block_set_length_and_media_in(gap, 1, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_block_get_length(gap, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 1);

	oaknode_block_free(gap);
}

TEST(BlockTest, EnabledFlag)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);

	int enabled = 0;
	ASSERT_EQ(oaknode_block_get_enabled(gap, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 1);

	ASSERT_EQ(oaknode_block_set_enabled(gap, 0), OAKNODE_OK);
	ASSERT_EQ(oaknode_block_get_enabled(gap, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 0);

	oaknode_block_free(gap);
}

TEST(BlockTest, TracklessBlockHasNoNeighbours)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);

	OakNodeBlock *neighbour = reinterpret_cast<OakNodeBlock *>(0x1);
	OakNodeTrack *track = reinterpret_cast<OakNodeTrack *>(0x1);
	ASSERT_EQ(oaknode_block_get_previous(gap, &neighbour), OAKNODE_OK);
	EXPECT_EQ(neighbour, nullptr);
	ASSERT_EQ(oaknode_block_get_next(gap, &neighbour), OAKNODE_OK);
	EXPECT_EQ(neighbour, nullptr);
	ASSERT_EQ(oaknode_block_get_track(gap, &track), OAKNODE_OK);
	EXPECT_EQ(track, nullptr);

	oaknode_block_free(gap);
}

TEST(BlockTest, LinkUnlink)
{
	OakNodeBlock *a = oaknode_block_clip_create();
	OakNodeBlock *b = oaknode_block_clip_create();
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);

	int linked = -1;
	ASSERT_EQ(oaknode_block_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 0);

	ASSERT_EQ(oaknode_block_link(a, b), OAKNODE_OK);
	// Double link fails
	EXPECT_EQ(oaknode_block_link(a, b), OAKNODE_E_FAILED);

	ASSERT_EQ(oaknode_block_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 1);

	int count = 0;
	ASSERT_EQ(oaknode_block_get_link_count(a, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);

	OakNodeBlock *other = nullptr;
	ASSERT_EQ(oaknode_block_get_link_at(a, 0, &other), OAKNODE_OK);
	EXPECT_EQ(other, b);
	EXPECT_EQ(oaknode_block_get_link_at(a, 1, &other), OAKNODE_E_NOT_FOUND);

	ASSERT_EQ(oaknode_block_unlink(a, b), OAKNODE_OK);
	EXPECT_EQ(oaknode_block_unlink(a, b), OAKNODE_E_FAILED);
	ASSERT_EQ(oaknode_block_are_linked(a, b, &linked), OAKNODE_OK);
	EXPECT_EQ(linked, 0);

	EXPECT_EQ(oaknode_block_link(a, nullptr), OAKNODE_E_INVALID);

	oaknode_block_free(a);
	oaknode_block_free(b);
}

TEST(BlockTest, ClipProperties)
{
	OakNodeBlock *clip = oaknode_block_clip_create();
	ASSERT_NE(clip, nullptr);

	int num, den;
	ASSERT_EQ(oaknode_clip_set_media_in(clip, 3, 2), OAKNODE_OK);
	ASSERT_EQ(oaknode_clip_get_media_in(clip, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 3, 2);

	double speed = 0.0;
	ASSERT_EQ(oaknode_clip_get_speed(clip, &speed), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(speed, 1.0);
	ASSERT_EQ(oaknode_clip_set_speed(clip, 2.5), OAKNODE_OK);
	ASSERT_EQ(oaknode_clip_get_speed(clip, &speed), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(speed, 2.5);

	int flag = -1;
	ASSERT_EQ(oaknode_clip_get_reverse(clip, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 0);
	ASSERT_EQ(oaknode_clip_set_reverse(clip, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_clip_get_reverse(clip, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 1);

	flag = -1;
	ASSERT_EQ(oaknode_clip_get_maintain_audio_pitch(clip, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 0);
	ASSERT_EQ(oaknode_clip_set_maintain_audio_pitch(clip, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_clip_get_maintain_audio_pitch(clip, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 1);

	int loop = -1;
	ASSERT_EQ(oaknode_clip_get_loop_mode(clip, &loop), OAKNODE_OK);
	EXPECT_EQ(loop, OAKCOMMON_LOOP_MODE_OFF);
	ASSERT_EQ(oaknode_clip_set_loop_mode(clip, OAKCOMMON_LOOP_MODE_CLAMP),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_clip_get_loop_mode(clip, &loop), OAKNODE_OK);
	EXPECT_EQ(loop, OAKCOMMON_LOOP_MODE_CLAMP);

	// Trackless clip reports no track type
	int type = 99;
	ASSERT_EQ(oaknode_clip_get_track_type(clip, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_NONE);

	oaknode_block_free(clip);
}

TEST(BlockTest, ClipApiRejectsNonClip)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);

	double speed;
	int num, den;
	EXPECT_EQ(oaknode_clip_get_speed(gap, &speed), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_clip_set_media_in(gap, 1, 1), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_clip_get_media_in(gap, &num, &den), OAKNODE_E_INVALID);

	oaknode_block_free(gap);
}

TEST(BlockTest, TransitionOffsets)
{
	OakNodeBlock *t =
		oaknode_block_transition_create(OAKNODE_TRANSITION_CROSS_DISSOLVE);
	ASSERT_NE(t, nullptr);

	int num, den;
	ASSERT_EQ(oaknode_transition_set_offsets_and_length(t, 1, 2, 1, 2),
			  OAKNODE_OK);
	// Length is the sum of both offsets
	ASSERT_EQ(oaknode_block_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 1);

	// Unconnected transitions report zero offsets; the center is
	// len/2 - in_offset = 0 here
	ASSERT_EQ(oaknode_transition_get_in_offset(t, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 0);
	ASSERT_EQ(oaknode_transition_get_out_offset(t, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 0);
	ASSERT_EQ(oaknode_transition_get_offset_center(t, &num, &den), OAKNODE_OK);
	EXPECT_EQ(num, 0);

	ASSERT_EQ(oaknode_transition_set_offset_center(t, 1, 4), OAKNODE_OK);
	ASSERT_EQ(oaknode_transition_get_offset_center(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 4);

	int dual = -1;
	ASSERT_EQ(oaknode_transition_is_dual(t, &dual), OAKNODE_OK);
	EXPECT_EQ(dual, 0);

	OakNodeBlock *connected = reinterpret_cast<OakNodeBlock *>(0x1);
	ASSERT_EQ(oaknode_transition_get_connected_out_block(t, &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, nullptr);
	ASSERT_EQ(oaknode_transition_get_connected_in_block(t, &connected),
			  OAKNODE_OK);
	EXPECT_EQ(connected, nullptr);

	oaknode_block_free(t);
}

TEST(BlockTest, TransitionApiRejectsNonTransition)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);

	int num, den;
	EXPECT_EQ(oaknode_transition_get_in_offset(gap, &num, &den),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_transition_set_offset_center(gap, 1, 2),
			  OAKNODE_E_INVALID);

	oaknode_block_free(gap);
}
