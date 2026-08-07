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
#include "node/sequence.h"
#include "node/track.h"

namespace
{

OakNodeBlock make_gap(int length_num, int length_den)
{
	OakNodeBlock gap = oaknode_block_gap_create();
	EXPECT_NE(gap.ctx, nullptr);
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

/**
 * @brief Identity probe: block handles are distinct boxes per accessor
 * call, so "is the same object" is checked through per-object state
 * (length / in point / enabled) instead of pointer equality.
 */
void expect_block_length(OakNodeBlock block, int expected_num,
						 int expected_den)
{
	int num = -1, den = -1;
	ASSERT_EQ(oaknode_block_get_length(block, &num, &den), OAKNODE_OK);
	expect_rational(num, den, expected_num, expected_den);
}

void expect_block_in(OakNodeBlock block, int expected_num, int expected_den)
{
	int num = -1, den = -1;
	ASSERT_EQ(oaknode_block_get_in(block, &num, &den), OAKNODE_OK);
	expect_rational(num, den, expected_num, expected_den);
}

} // namespace

TEST(TrackTest, CreateFree)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);
	int type = -1;
	ASSERT_EQ(oaknode_track_get_type(t, &type), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_VIDEO);
	oaknode_track_free(&t);
	EXPECT_EQ(t.ctx, nullptr);

	EXPECT_EQ(oaknode_track_create(OAKNODE_TRACK_TYPE_NONE).ctx, nullptr);
	EXPECT_EQ(oaknode_track_create(OAKNODE_TRACK_TYPE_COUNT).ctx, nullptr);
	oaknode_track_free(nullptr);
}

TEST(TrackTest, NullHandleReturnsInvalid)
{
	int v;
	double d;
	OakNodeTrack empty_track = {};
	OakNodeBlock empty_block = {};
	OakNodeTrackList empty_list = {};
	EXPECT_EQ(oaknode_track_get_type(empty_track, &v), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_set_type(empty_track, OAKNODE_TRACK_TYPE_VIDEO),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_get_height(empty_track, &d), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_set_muted(empty_track, 1), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_track_append_block(empty_track, empty_block),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_tracklist_get_track_count(empty_list, &v),
			  OAKNODE_E_INVALID);
}

TEST(TrackTest, TypeHeightIndexFlags)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_AUDIO);
	ASSERT_NE(t.ctx, nullptr);

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
	OakNodeSequence seq = {};
	seq.ctx = reinterpret_cast<void *>(0x1);
	ASSERT_EQ(oaknode_track_get_sequence(t, &seq), OAKNODE_OK);
	EXPECT_EQ(seq.ctx, nullptr);

	oaknode_track_free(&t);
}

TEST(TrackTest, AppendAndQueryBlocks)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);

	int count = -1;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);

	OakNodeBlock a = make_gap(1, 1);
	OakNodeBlock b = make_gap(2, 1);
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

	// Adjacency (identified by their distinct lengths: a = 1, b = 2)
	OakNodeBlock nb = {};
	ASSERT_EQ(oaknode_block_get_next(a, &nb), OAKNODE_OK);
	expect_block_length(nb, 2, 1); // nb is b
	ASSERT_EQ(oaknode_block_get_previous(b, &nb), OAKNODE_OK);
	expect_block_length(nb, 1, 1); // nb is a

	// Back-pointer: mutating through the borrowed track handle must be
	// visible through the owning track handle (same object)
	OakNodeTrack owner = {};
	ASSERT_EQ(oaknode_block_get_track(a, &owner), OAKNODE_OK);
	ASSERT_NE(owner.ctx, nullptr);
	ASSERT_EQ(oaknode_track_set_muted(owner, 1), OAKNODE_OK);
	int muted = -1;
	ASSERT_EQ(oaknode_track_get_muted(t, &muted), OAKNODE_OK);
	EXPECT_EQ(muted, 1);

	// Length of the whole track
	ASSERT_EQ(oaknode_track_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 3, 1);

	// Index / lookup
	int index = -1;
	ASSERT_EQ(oaknode_track_get_block_index(t, b, &index), OAKNODE_OK);
	EXPECT_EQ(index, 1);

	OakNodeBlock at = {};
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	expect_block_length(at, 1, 1); // at is a
	EXPECT_EQ(oaknode_track_get_block_at(t, 2, &at), OAKNODE_E_NOT_FOUND);

	ASSERT_EQ(oaknode_track_get_block_containing_time(t, 1, 2, &at),
			  OAKNODE_OK);
	expect_block_length(at, 1, 1); // at is a
	ASSERT_EQ(oaknode_track_get_block_containing_time(t, 3, 2, &at),
			  OAKNODE_OK);
	expect_block_length(at, 2, 1); // at is b
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
	oaknode_block_free(&a);
	oaknode_block_free(&b);
	oaknode_track_free(&t);
}

TEST(TrackTest, PrependInsertAndRippleRemove)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);

	OakNodeBlock a = make_gap(1, 1);
	OakNodeBlock b = make_gap(1, 1);
	OakNodeBlock c = make_gap(1, 1);
	OakNodeBlock d = make_gap(1, 1);

	ASSERT_EQ(oaknode_track_append_block(t, c), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_prepend_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_at_index(t, b, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_after(t, d, c), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 4);

	// All gaps have equal lengths; identity is checked through the
	// per-object in point (a at 0, b at 1, d at 3)
	OakNodeBlock at = {};
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	expect_block_in(at, 0, 1); // at is a
	ASSERT_EQ(oaknode_track_get_block_at(t, 1, &at), OAKNODE_OK);
	expect_block_in(at, 1, 1); // at is b
	ASSERT_EQ(oaknode_track_get_block_at(t, 3, &at), OAKNODE_OK);
	expect_block_in(at, 3, 1); // at is d

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
	oaknode_block_free(&a);
	oaknode_block_free(&b);
	oaknode_block_free(&c);
	oaknode_block_free(&d);
	oaknode_track_free(&t);
}

TEST(TrackTest, InsertBefore)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);

	OakNodeBlock a = make_gap(1, 1);
	OakNodeBlock b = make_gap(1, 1);
	ASSERT_EQ(oaknode_track_append_block(t, b), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_insert_block_before(t, a, b), OAKNODE_OK);

	OakNodeBlock at = {};
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	expect_block_in(at, 0, 1); // at is a

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, a), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_ripple_remove_block(t, b), OAKNODE_OK);
	oaknode_block_free(&a);
	oaknode_block_free(&b);
	oaknode_track_free(&t);
}

TEST(TrackTest, ReplaceBlock)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);

	OakNodeBlock old_gap = make_gap(2, 1);
	OakNodeBlock new_gap = make_gap(2, 1);
	// Marker distinguishing new_gap from the equal-length old_gap
	ASSERT_EQ(oaknode_block_set_enabled(new_gap, 0), OAKNODE_OK);

	ASSERT_EQ(oaknode_track_append_block(t, old_gap), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_replace_block(t, old_gap, new_gap), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_track_get_block_count(t, &count), OAKNODE_OK);
	EXPECT_EQ(count, 1);
	OakNodeBlock at = {};
	ASSERT_EQ(oaknode_track_get_block_at(t, 0, &at), OAKNODE_OK);
	int enabled = -1;
	ASSERT_EQ(oaknode_block_get_enabled(at, &enabled), OAKNODE_OK);
	EXPECT_EQ(enabled, 0); // at is new_gap
	int num, den;
	ASSERT_EQ(oaknode_track_get_length(t, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 2, 1);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, new_gap), OAKNODE_OK);
	oaknode_block_free(&old_gap);
	oaknode_block_free(&new_gap);
	oaknode_track_free(&t);
}

TEST(TrackTest, RangeOccupiedByClipIsNotFree)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t.ctx, nullptr);

	OakNodeBlock clip = oaknode_block_clip_create();
	ASSERT_NE(clip.ctx, nullptr);
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
	oaknode_block_free(&clip);
	oaknode_track_free(&t);
}

TEST(TrackTest, Reference)
{
	OakNodeTrack t = oaknode_track_create(OAKNODE_TRACK_TYPE_AUDIO);
	ASSERT_NE(t.ctx, nullptr);
	ASSERT_EQ(oaknode_track_set_index(t, 2), OAKNODE_OK);

	int type = -1, index = -1;
	ASSERT_EQ(oaknode_track_get_reference(t, &type, &index), OAKNODE_OK);
	EXPECT_EQ(type, OAKNODE_TRACK_TYPE_AUDIO);
	EXPECT_EQ(index, 2);

	oaknode_track_free(&t);
}
