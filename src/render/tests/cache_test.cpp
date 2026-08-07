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

#include "render/cache.h"

#include <gtest/gtest.h>

#include <vector>

#include "node/node.h"
#include "node/project.h"
#include "node/sequence.h"

TEST(OakRenderCacheTest, CreateFree)
{
	const int alive_before = oakrender_debug_alive_count();

	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);

	oakrender_cache_free(&cache);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);

	// NULL / already-cleared is a no-op
	oakrender_cache_free(nullptr);
	oakrender_cache_free(&cache);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

TEST(OakRenderCacheTest, InvalidateValidateStateMachine)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);
	ASSERT_EQ(oakrender_cache_set_timebase(cache, 1001, 30000), OAKRENDER_OK);

	// Fresh cache: nothing validated
	EXPECT_EQ(oakrender_cache_has_validated_ranges(cache), 0);

	// Validate [0, 30): caller-triggered, then state is read back (no
	// events cross the ABI, M7 §2.2)
	oakrender_cache_validate(cache, 0, 30);
	EXPECT_EQ(oakrender_cache_has_validated_ranges(cache), 1);

	// Invalidate a subrange: some validated range remains
	oakrender_cache_invalidate(cache, 10, 20);
	EXPECT_EQ(oakrender_cache_has_validated_ranges(cache), 1);

	// Invalidate everything: back to the empty state
	oakrender_cache_invalidate(cache, 0, 30);
	EXPECT_EQ(oakrender_cache_has_validated_ranges(cache), 0);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, InvalidateValidateNullCacheIsNoOp)
{
	// Must not crash
	oakrender_cache_invalidate(OakRenderCache{}, 0, 10);
	oakrender_cache_validate(OakRenderCache{}, 0, 10);
	EXPECT_EQ(oakrender_cache_has_validated_ranges(OakRenderCache{}), 0);
}

TEST(OakRenderCacheTest, SetTimebaseValidation)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_timebase(cache, 1, 25), OAKRENDER_OK);
	EXPECT_EQ(oakrender_cache_set_timebase(OakRenderCache{}, 1, 25),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_set_timebase(cache, 0, 25), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_set_timebase(cache, 1, 0), OAKRENDER_E_INVALID);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, SetUuid)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_uuid(cache, "{01234567-89ab-cdef-0123-456789abcdef}"),
			  OAKRENDER_OK);
	EXPECT_EQ(oakrender_cache_set_uuid(cache, nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_set_uuid(OakRenderCache{}, "x"),
			  OAKRENDER_E_INVALID);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, WrapBorrowedNullIsEmpty)
{
	// A borrowed wrapper around a NULL native cache is an empty handle
	EXPECT_EQ(oakrender_cache_wrap_borrowed(nullptr).ctx, nullptr);
}

TEST(OakRenderCacheTest, IndicatorHeightIsConstant)
{
	EXPECT_EQ(oakrender_cache_indicator_height(), 4);
}

TEST(OakRenderCacheTest, FrameCacheLoadNotFound)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);
	ASSERT_EQ(oakrender_cache_set_timebase(cache, 1, 25), OAKRENDER_OK);

	// Nothing cached under this uuid: the load must fail cleanly
	// (E_NOT_FOUND, or E_FAILED when the decoder layer rejects the read).
	OakCodecFrame frame = {};
	const int r = oakrender_frame_cache_load(cache, "/tmp", "{no-such-uuid}",
											 0, &frame);
	EXPECT_TRUE(r == OAKRENDER_E_NOT_FOUND || r == OAKRENDER_E_FAILED);
	EXPECT_EQ(frame.ctx, nullptr);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, FrameCacheLoadInvalidArgs)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);
	OakCodecFrame frame = {};

	EXPECT_EQ(oakrender_frame_cache_load(OakRenderCache{}, "/tmp", "u", 0,
										 &frame),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_frame_cache_load(cache, nullptr, "u", 0, &frame),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_frame_cache_load(cache, "/tmp", nullptr, 0, &frame),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_frame_cache_load(cache, "/tmp", "u", 0, nullptr),
			  OAKRENDER_E_INVALID);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, FrameCacheSaveUnallocatedFrameIsSafe)
{
	OakRenderCache cache = oakrender_cache_create();
	ASSERT_NE(cache.ctx, nullptr);
	ASSERT_EQ(oakrender_cache_set_timebase(cache, 1, 25), OAKRENDER_OK);

	// The transitional codec Frame cannot allocate, so the save fails
	// internally; the ABI contract is that it never crashes and empty/NULL
	// arguments are no-ops. A real save/load round-trip needs the oakcodec
	// wave (M5) and is covered there.
	OakCodecFrame frame = oakrender_codec_frame_create();
	ASSERT_NE(frame.ctx, nullptr);
	oakrender_frame_cache_save(cache, "/tmp", "{save-test}", frame);
	oakrender_frame_cache_save(OakRenderCache{}, "/tmp", "{save-test}", frame);
	oakrender_frame_cache_save(cache, nullptr, "{save-test}", frame);
	oakrender_frame_cache_save(cache, "/tmp", nullptr, frame);
	oakrender_frame_cache_save(cache, "/tmp", "{save-test}", OakCodecFrame{});
	oakrender_codec_frame_free(&frame);

	oakrender_cache_free(&cache);
}

TEST(OakRenderCacheTest, AliveCountReturnsToBaseline)
{
	const int alive_before = oakrender_debug_alive_count();

	OakRenderCache a = oakrender_cache_create();
	OakRenderCache b = oakrender_cache_create();
	OakCodecFrame f = oakrender_codec_frame_create();
	ASSERT_NE(a.ctx, nullptr);
	ASSERT_NE(b.ctx, nullptr);
	ASSERT_NE(f.ctx, nullptr);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 3);

	oakrender_codec_frame_free(&f);
	oakrender_cache_free(&a);
	oakrender_cache_free(&b);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

namespace
{

/**
 * @brief Minimal project+sequence fixture for node-owned caches.
 */
class NodeCacheFixture : public ::testing::Test {
protected:
	void SetUp() override
	{
		project_ = oaknode_project_init();
		ASSERT_NE(project_.ctx, nullptr);
		sequence_ = oaknode_sequence_create();
		ASSERT_NE(sequence_.ctx, nullptr);
		node_ = oaknode_sequence_as_node(sequence_);
		OakNodeProject owner = {};
		if (oaknode_node_get_project(node_, &owner) != OAKNODE_OK ||
			!owner.ctx) {
			ASSERT_EQ(oaknode_project_add_node(project_, node_),
					  OAKNODE_OK);
		}
	}

	void TearDown() override
	{
		oaknode_project_free(&project_);
	}

	OakNodeProject project_ = {};
	OakNodeSequence sequence_ = {};
	OakNodeNode node_ = {};
};

} // namespace

TEST_F(NodeCacheFixture, CreateForNodeAllKinds)
{
	const int alive_before = oakrender_debug_alive_count();
	const int kinds[] = { OAKRENDER_CACHE_VIDEO_FRAME,
						  OAKRENDER_CACHE_THUMBNAIL,
						  OAKRENDER_CACHE_AUDIO_PLAYBACK,
						  OAKRENDER_CACHE_AUDIO_WAVEFORM };
	for (int kind : kinds) {
		OakRenderCache c = oakrender_cache_create_for_node(node_, kind);
		ASSERT_NE(c.ctx, nullptr) << "kind " << kind;
		oakrender_cache_free(&c);
	}
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);

	EXPECT_EQ(oakrender_cache_create_for_node(OakNodeNode{},
											  OAKRENDER_CACHE_VIDEO_FRAME)
				  .ctx,
			  nullptr);
	EXPECT_EQ(oakrender_cache_create_for_node(node_, -1).ctx, nullptr);
}

TEST_F(NodeCacheFixture, UuidRoundTripTwoStage)
{
	OakRenderCache c =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	ASSERT_NE(c.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_uuid(c, "{test-uuid-1}"), OAKRENDER_OK);
	int needed = oakrender_cache_get_uuid(c, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(static_cast<size_t>(needed));
	EXPECT_EQ(oakrender_cache_get_uuid(c, buf.data(), needed), needed);
	EXPECT_STREQ(buf.data(), "{test-uuid-1}");

	EXPECT_EQ(oakrender_cache_set_uuid(OakRenderCache{}, "x"),
			  OAKRENDER_E_INVALID);
	EXPECT_LT(oakrender_cache_get_uuid(OakRenderCache{}, nullptr, 0), 0);

	oakrender_cache_free(&c);
}

TEST_F(NodeCacheFixture, InvalidateRangeAndQuery)
{
	OakRenderCache c =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	ASSERT_NE(c.ctx, nullptr);

	// A fresh cache has nothing validated: the whole query range is
	// reported invalidated regardless of prior invalidate() calls.
	oakrender_cache_invalidate_range(c, 1, 2, 3, 2);
	int n = oakrender_cache_get_invalidated_ranges(c, 0, 1, 2, 1, nullptr,
												   0);
	ASSERT_EQ(n, 1);

	std::vector<int64_t> ranges(4);
	EXPECT_EQ(oakrender_cache_get_invalidated_ranges(c, 0, 1, 2, 1,
													 ranges.data(), 1),
			  1);
	EXPECT_EQ(ranges[0], 0);
	EXPECT_EQ(ranges[1], 1);
	EXPECT_EQ(ranges[2], 2);
	EXPECT_EQ(ranges[3], 1);

	oakrender_cache_invalidate_range(OakRenderCache{}, 0, 1, 1, 1); // no-op
	oakrender_cache_free(&c);
}

TEST_F(NodeCacheFixture, PassthroughRoundTrip)
{
	OakRenderCache a =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	OakRenderCache b =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	ASSERT_NE(a.ctx, nullptr);
	ASSERT_NE(b.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_passthrough(a, b), OAKRENDER_OK);
	// The passthrough list mirrors the other cache's validated ranges;
	// with none, the list stays empty but the link is established.
	EXPECT_GE(oakrender_cache_get_passthroughs(a, nullptr, 0), 0);

	EXPECT_EQ(oakrender_cache_set_passthrough(a, OakRenderCache{}),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_set_passthrough(OakRenderCache{}, b),
			  OAKRENDER_E_INVALID);

	oakrender_cache_free(&a);
	oakrender_cache_free(&b);
}

TEST_F(NodeCacheFixture, TimebaseAndFilename)
{
	OakRenderCache c =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	ASSERT_NE(c.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_timebase(c, 1, 25), OAKRENDER_OK);
	int num = 0, den = 0;
	EXPECT_EQ(oakrender_cache_get_timebase(c, &num, &den), OAKRENDER_OK);
	EXPECT_EQ(num, 1);
	EXPECT_EQ(den, 25);

	// A frame-hash cache reports a filename for a time
	int needed = oakrender_cache_get_valid_cache_filename(c, 1, 25,
														  nullptr, 0);
	EXPECT_GT(needed, 0);

	// ... but an audio cache is not a frame-hash cache
	OakRenderCache audio = oakrender_cache_create_for_node(
		node_, OAKRENDER_CACHE_AUDIO_PLAYBACK);
	ASSERT_NE(audio.ctx, nullptr);
	EXPECT_EQ(oakrender_cache_get_timebase(audio, &num, &den),
			  OAKRENDER_E_INVALID);
	EXPECT_LT(oakrender_cache_get_valid_cache_filename(audio, 0, 1,
													   nullptr, 0),
			  0);

	oakrender_cache_free(&audio);
	oakrender_cache_free(&c);
}

TEST_F(NodeCacheFixture, SavingEnabledLockUnlockStateErrors)
{
	OakRenderCache c = oakrender_cache_create_for_node(
		node_, OAKRENDER_CACHE_AUDIO_WAVEFORM);
	ASSERT_NE(c.ctx, nullptr);

	EXPECT_EQ(oakrender_cache_set_saving_enabled(c, 0), OAKRENDER_OK);
	EXPECT_EQ(oakrender_cache_set_saving_enabled(OakRenderCache{}, 1),
			  OAKRENDER_E_INVALID);

	// Lock/unlock pair (no-op on empty handle)
	oakrender_cache_lock(c);
	oakrender_cache_unlock(c);
	oakrender_cache_lock(OakRenderCache{});
	oakrender_cache_unlock(OakRenderCache{});

	EXPECT_EQ(oakrender_cache_load_state(OakRenderCache{}),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_save_state(OakRenderCache{}),
			  OAKRENDER_E_INVALID);

	oakrender_cache_free(&c);
}

TEST_F(NodeCacheFixture, RequestValidation)
{
	OakRenderCache c =
		oakrender_cache_create_for_node(node_, OAKRENDER_CACHE_VIDEO_FRAME);
	ASSERT_NE(c.ctx, nullptr);

	// The sequence node is a viewer: valid request
	EXPECT_EQ(oakrender_cache_request(c, node_, 0, 1, 1, 1), OAKRENDER_OK);

	// Empty cache / empty context are rejected
	EXPECT_EQ(oakrender_cache_request(OakRenderCache{}, node_, 0, 1, 1, 1),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cache_request(c, OakNodeNode{}, 0, 1, 1, 1),
			  OAKRENDER_E_INVALID);

	oakrender_cache_free(&c);
}
