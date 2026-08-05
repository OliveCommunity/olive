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

#include "node/block.h"
#include "node/error.h"
#include "node/track.h"

namespace
{

OakNodeBlock *make_gap(int length_num, int length_den)
{
	OakNodeBlock *gap = oaknode_block_gap_create();
	EXPECT_NE(gap, nullptr);
	EXPECT_EQ(oaknode_block_set_length_and_media_out(gap, length_num,
													 length_den),
			  OAKNODE_OK);
	return gap;
}

void expect_rational(int num, int den, int expected_num, int expected_den)
{
	EXPECT_EQ(num, expected_num);
	EXPECT_EQ(den, expected_den);
}

} // namespace

TEST(TrackTest, CreateFree)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);
	int type = -1;
	ASSERT_EQ(oaknode_track_get_type(t, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_VIDEO);
	oaknode_track_free(t);

	EXPECT_EQ(oaknode_track_create(OAKNODE_TRACK_TYPE_NONE), nullptr);
	EXPECT_EQ(oaknode_track_create(OAKNODE_TRACK_TYPE_COUNT), nullptr);
	oaknode_track_free(nullptr);
}

TEST(TrackTest, NullHandleReturnsInvalid)
{
	int v;
	double d;
	EXPECT_EQ(oaknode_track_get_type(nullptr, &v), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_set_type(nullptr, OAKNODE_TRACK_TYPE_VIDEO),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_get_height(nullptr, &d), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_set_muted(nullptr, 1), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_append_block(nullptr, nullptr), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_tracklist_get_track_count(nullptr, &v),
			  OAKNODE_E_INVALID);
}

TEST(TrackTest, TypeHeightIndexFlags)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_AUDIO);
	ASSERT_NE(t, nullptr);

	ASSERT_EQ(oaknode_track_set_type(t, OAKNODE_TRACK_TYPE_SUBTITLE),
			  OAKNODE_OK);
	int type = -1;
	ASSERT_EQ(oaknode_track_get_type(t, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_SUBTITLE);
	EXPECT_EQ(oaknode_track_set_type(t, OAKNODE_TRACK_TYPE_NONE),
			  OAKNODE_E_INVALID);

	double h = 0.0;
	ASSERT_EQ(oaknode_track_get_height(t, &h), OAKNODE_OK);
	EXPECT_GT(h, 0.0);
	ASSERT_EQ(oaknode_track_set_height(t, 4.5), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_height(t, &h), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(h, 4.5);

	int px = 0;
	ASSERT_EQ(oaknode_track_set_height_in_pixels(t, 52), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_height_in_pixels(t, &px), OAKNODE_OK);
	EXPECT_EQ(px, 52);
	EXPECT_GT(oaknode_track_get_default_height_in_pixels(), 0);
	EXPECT_GT(oaknode_track_get_minimum_height_in_pixels(), 0);

	int index = 99;
	ASSERT_EQ(oaknode_track_get_index(t, &index), OAKNODE_OK);
	EXPECT_EQ(index, -1);
	ASSERT_EQ(oaknode_track_set_index(t, 3), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_index(t, &index), OAKNODE_OK);
	EXPECT_EQ(index, 3);

	int flag = -1;
	ASSERT_EQ(oaknode_track_get_muted(t, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 0);
	ASSERT_EQ(oaknode_track_set_muted(t, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_muted(t, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 1);

	flag = -1;
	ASSERT_EQ(oaknode_track_get_locked(t, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 0);
	ASSERT_EQ(oaknode_track_set_locked(t, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_locked(t, &flag), OAKNODE_OK);
	EXPECT_EQ(flag, 1);

	// Fresh track has no sequence
	OakNodeSequence *seq = reinterpret_cast<OakNodeSequence *>(0x1);
	ASSERT_EQ(oaknode_track_get_sequence(t, &seq), OAKNODE_OK);
	EXPECT_EQ(seq, nullptr);

	oaknode_track_free(t);
}

TEST(TrackTest, AppendAndQueryBlocks)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);

	int count = -1;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);

	OakNodeBlock *a = make_gap(1, 1);
	OakNodeBlock *b = make_gap(2, 1);
	ASSERT_EQ(oaknode_track_append_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_append_block(t, b), OAKNODE_OK);

	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 2);

	// In/out points are recomputed from the chain
	int num, den;
	ASSERT_EQ(oaknode_block_get_in(a, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 0, 1);
	ASSERT_EQ(oaknode_block_get_in(b, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 1);
	ASSERT_EQ(oaknode_block_get_out(b, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 3, 1);

	// Adjacency and back-pointer
	OakNodeBlock *nb = nullptr;
	ASSERT_EQ(oaknode_block_get_next(a, &nb), OAKNODE_OK);
	EXPECT_EQ(nb, b);
	ASSERT_EQ(oaknode_block_get_previous(b, &nb), OAKNODE_OK);
	EXPECT_EQ(nb, a);
	OakNodeTrack *owner = nullptr;
	ASSERT_EQ(oaknode_block_get_track(a, &owner), OAKNODE_OK);
	EXPECT_EQ(owner, t);

	// Length of the whole track
	ASSERT_EQ(oaknode_track_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 3, 1);

	// Index / lookup
	int index = -1;
	ASSERT_EQ(oaknode_track_get_block_index(t, b, &index), OAKNODE_OK);
	EXPECT_EQ(index, 1);

	OakNodeBlock *at = nullptr;
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	EXPECT_EQ(at, a);
	EXPECT_EQ(oaknode_track_get_block_at(t, 2, &at), OAKNODE_E_NOT_FOUND);

	ASSERT_EQ(oaknode_track_get_block_containing_time(t, 1, 2, &at),
			  OAKNODE_OK);
	EXPECT_EQ(at, a);
	ASSERT_EQ(oaknode_track_get_block_containing_time(t, 3, 2, &at),
			  OAKNODE_OK);
	EXPECT_EQ(at, b);
	// Boundary time is not "contained"
	EXPECT_EQ(oaknode_track_get_block_containing_time(t, 1, 1, &at),
			  OAKNODE_E_NOT_FOUND);
	// Past the end
	EXPECT_EQ(oaknode_track_get_visible_block_at_time(t, 10, 1, &at),
			  OAKNODE_E_NOT_FOUND);

	int free_range = -1;
	ASSERT_EQ(oaknode_track_is_range_free(t, 3, 1, 5, 1, &free_range),
			  OAKNODE_OK);
	EXPECT_EQ(free_range, 1);
	// A range holding only GapBlocks still counts as free
	ASSERT_EQ(oaknode_track_is_range_free(t, 0, 1, 2, 1, &free_range),
			  OAKNODE_OK);
	EXPECT_EQ(free_range, 1);

	// Tear down manually (no project graph owns these)
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, b), OAKNODE_OK);
	oaknode_block_free(a);
	oaknode_block_free(b);
	oaknode_track_free(t);
}

TEST(TrackTest, PrependInsertAndRippleRemove)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);

	OakNodeBlock *a = make_gap(1, 1);
	OakNodeBlock *b = make_gap(1, 1);
	OakNodeBlock *c = make_gap(1, 1);
	OakNodeBlock *d = make_gap(1, 1);

	ASSERT_EQ(oaknode_track_append_block(t, c), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_prepend_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_at_index(t, b, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_after(t, d, c), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 4);

	OakNodeBlock *at = nullptr;
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	EXPECT_EQ(at, a);
	ASSERT_EQ(oaknode_track_get_block_at(t, 1, &at), OAKNODE_OK);
	EXPECT_EQ(at, b);
	ASSERT_EQ(oaknode_track_get_block_at(t, 3, &at), OAKNODE_OK);
	EXPECT_EQ(at, d);

	// Ripple-removing b shifts c back to t=1
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, b), OAKNODE_OK);
	int num, den;
	ASSERT_EQ(oaknode_block_get_in(c, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 1, 1);
	ASSERT_EQ(oaknode_track_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 3, 1);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, c), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, d), OAKNODE_OK);
	oaknode_block_free(a);
	oaknode_block_free(b);
	oaknode_block_free(c);
	oaknode_block_free(d);
	oaknode_track_free(t);
}

TEST(TrackTest, InsertBefore)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);

	OakNodeBlock *a = make_gap(1, 1);
	OakNodeBlock *b = make_gap(1, 1);
	ASSERT_EQ(oaknode_track_append_block(t, b), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_before(t, a, b), OAKNODE_OK);

	OakNodeBlock *at = nullptr;
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	EXPECT_EQ(at, a);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, b), OAKNODE_OK);
	oaknode_block_free(a);
	oaknode_block_free(b);
	oaknode_track_free(t);
}

TEST(TrackTest, ReplaceBlock)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);

	OakNodeBlock *old_gap = make_gap(2, 1);
	OakNodeBlock *new_gap = make_gap(2, 1);
	ASSERT_EQ(oaknode_track_append_block(t, old_gap), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_replace_block(t, old_gap, new_gap), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	OakNodeBlock *at = nullptr;
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	EXPECT_EQ(at, new_gap);
	int num, den;
	ASSERT_EQ(oaknode_track_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 2, 1);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, new_gap), OAKNODE_OK);
	oaknode_block_free(old_gap);
	oaknode_block_free(new_gap);
	oaknode_track_free(t);
}

TEST(TrackTest, RangeOccupiedByClipIsNotFree)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);

	OakNodeBlock *clip = oaknode_block_clip_create();
	ASSERT_NE(clip, nullptr);
	ASSERT_EQ(oaknode_block_set_length_and_media_out(clip, 2, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_append_block(t, clip), OAKNODE_OK);

	int free_range = -1;
	ASSERT_EQ(oaknode_track_is_range_free(t, 0, 1, 2, 1, &free_range),
			  OAKNODE_OK);
	EXPECT_EQ(free_range, 0);
	ASSERT_EQ(oaknode_track_is_range_free(t, 2, 1, 4, 1, &free_range),
			  OAKNODE_OK);
	EXPECT_EQ(free_range, 1);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, clip), OAKNODE_OK);
	oaknode_block_free(clip);
	oaknode_track_free(t);
}

TEST(TrackTest, Reference)
{
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_AUDIO);
	ASSERT_NE(t, nullptr);
	ASSERT_EQ(oaknode_track_set_index(t, 2), OAKNODE_OK);

	int type = -1, index = -1;
	ASSERT_EQ(oaknode_track_get_reference(t, &type, &index), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_AUDIO);
	EXPECT_EQ(index, 2);

	oaknode_track_free(t);
}
