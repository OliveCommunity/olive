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
#include "node/node.h"
#include "node/error.h"
#include "node/sequence.h"
#include "node/track.h"

namespace
{

constexpr uint64_t k_stereo_layout = 0x3; // AV_CH_LAYOUT_STEREO
constexpr int k_sample_format_f32_packed = 10; // SampleFormat::f32

void expect_rational(int num, int den, int expected_num, int expected_den)
{
	EXPECT_EQ(num, expected_num);
	EXPECT_EQ(den, expected_den);
}

} // namespace

TEST(SequenceTest, CreateFree)
{
	int base = oaknode_debug_alive_count();
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), base + 1);
	oaknode_sequence_free(seq);
	EXPECT_EQ(oaknode_debug_alive_count(), base);
	oaknode_sequence_free(nullptr);
}

TEST(SequenceTest, NullHandleReturnsInvalid)
{
	int v;
	OakNodeTrackList *list;
	EXPECT_EQ(oaknode_sequence_get_track_list(nullptr, OAKNODE_TRACK_TYPE_VIDEO,
											  &list),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_sequence_get_track_count(nullptr, 0, &v),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_sequence_get_playhead(nullptr, &v, &v), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_sequence_get_video_params(nullptr, 0, nullptr),
			  OAKNODE_E_INVALID);
}

TEST(SequenceTest, TrackLists)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	for (int type = OAKNODE_TRACK_TYPE_VIDEO; type < OAKNODE_TRACK_TYPE_COUNT;
		 type++) {
		OakNodeTrackList *list = nullptr;
		ASSERT_EQ(oaknode_sequence_get_track_list(seq, type, &list),
				  OAKNODE_OK);
		ASSERT_NE(list, nullptr);

		int list_type = -1;
		ASSERT_EQ(oaknode_tracklist_get_type(list, &list_type), OAKNODE_OK);
		EXPECT_EQ(list_type, type);

		int count = -1;
		ASSERT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
		EXPECT_EQ(count, 0);
	}

	OakNodeTrackList *list;
	EXPECT_EQ(oaknode_sequence_get_track_list(seq, OAKNODE_TRACK_TYPE_NONE,
											  &list),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_sequence_get_track_count(seq, OAKNODE_TRACK_TYPE_COUNT,
											   nullptr),
			  OAKNODE_E_INVALID);

	oaknode_sequence_free(seq);
}

TEST(SequenceTest, AddAndRemoveTracks)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	OakNodeTrackList *video = nullptr;
	ASSERT_EQ(oaknode_sequence_get_track_list(seq, OAKNODE_TRACK_TYPE_VIDEO,
											  &video),
			  OAKNODE_OK);

	OakNodeTrack *t1 = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	OakNodeTrack *t2 = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t1, nullptr);
	ASSERT_NE(t2, nullptr);

	ASSERT_EQ(oaknode_tracklist_add_track(video, t1), OAKNODE_OK);
	ASSERT_EQ(oaknode_tracklist_add_track(video, t2), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_sequence_get_track_count(seq, OAKNODE_TRACK_TYPE_VIDEO,
											   &count),
			  OAKNODE_OK);
	EXPECT_EQ(count, 2);

	// Indexes were assigned in list order
	int index = -1;
	ASSERT_EQ(oaknode_track_get_index(t1, &index), OAKNODE_OK);
	EXPECT_EQ(index, 0);
	ASSERT_EQ(oaknode_track_get_index(t2, &index), OAKNODE_OK);
	EXPECT_EQ(index, 1);

	// Second track inherits the first track's height
	double h1 = 0.0, h2 = 0.0;
	ASSERT_EQ(oaknode_track_get_height(t1, &h1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_get_height(t2, &h2), OAKNODE_OK);
	EXPECT_DOUBLE_EQ(h1, h2);

	// Array size tracks the number of connected tracks
	int array_size = 0;
	ASSERT_EQ(oaknode_tracklist_get_array_size(video, &array_size), OAKNODE_OK);
	EXPECT_EQ(array_size, 2);

	// Sequence back-pointer and reference
	OakNodeSequence *owner = nullptr;
	ASSERT_EQ(oaknode_track_get_sequence(t1, &owner), OAKNODE_OK);
	EXPECT_EQ(owner, seq);

	// Flat cache spans all types
	int all = 0;
	ASSERT_EQ(oaknode_sequence_get_all_track_count(seq, &all), OAKNODE_OK);
	EXPECT_EQ(all, 2);
	OakNodeTrack *at = nullptr;
	ASSERT_EQ(oaknode_sequence_get_all_track_at(seq, 1, &at), OAKNODE_OK);
	EXPECT_EQ(at, t2);
	EXPECT_EQ(oaknode_sequence_get_all_track_at(seq, 2, &at),
			  OAKNODE_E_NOT_FOUND);

	// Borrowed lookup by type/index
	ASSERT_EQ(oaknode_sequence_get_track_at(seq, OAKNODE_TRACK_TYPE_VIDEO, 0,
											&at),
			  OAKNODE_OK);
	EXPECT_EQ(at, t1);
	EXPECT_EQ(oaknode_sequence_get_track_at(seq, OAKNODE_TRACK_TYPE_AUDIO, 0,
											&at),
			  OAKNODE_E_NOT_FOUND);

	// Remove the first track; the second shifts down
	ASSERT_EQ(oaknode_tracklist_remove_track(video, t1), OAKNODE_OK);
	ASSERT_EQ(oaknode_sequence_get_track_count(seq, OAKNODE_TRACK_TYPE_VIDEO,
											   &count),
			  OAKNODE_OK);
	EXPECT_EQ(count, 1);
	ASSERT_EQ(oaknode_sequence_get_track_at(seq, OAKNODE_TRACK_TYPE_VIDEO, 0,
											&at),
			  OAKNODE_OK);
	EXPECT_EQ(at, t2);
	ASSERT_EQ(oaknode_track_get_index(t2, &index), OAKNODE_OK);
	EXPECT_EQ(index, 0);

	// Removing a foreign track reports NOT_FOUND
	EXPECT_EQ(oaknode_tracklist_remove_track(video, t1), OAKNODE_E_NOT_FOUND);

	ASSERT_EQ(oaknode_tracklist_remove_track(video, t2), OAKNODE_OK);
	oaknode_track_free(t1);
	oaknode_track_free(t2);
	oaknode_sequence_free(seq);
}

TEST(SequenceTest, TrackLengthFlowsIntoSequence)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	OakNodeTrackList *video = nullptr;
	ASSERT_EQ(oaknode_sequence_get_track_list(seq, OAKNODE_TRACK_TYPE_VIDEO,
											  &video),
			  OAKNODE_OK);
	OakNodeTrack *t = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(t, nullptr);
	ASSERT_EQ(oaknode_tracklist_add_track(video, t), OAKNODE_OK);

	OakNodeBlock *gap = oaknode_block_gap_create();
	ASSERT_NE(gap, nullptr);
	ASSERT_EQ(oaknode_block_set_length_and_media_out(gap, 5, 1), OAKNODE_OK);
	ASSERT_EQ(oaknode_track_append_block(t, gap), OAKNODE_OK);

	int num, den;
	ASSERT_EQ(oaknode_tracklist_get_total_length(video, &num, &den),
			  OAKNODE_OK);
	expect_rational(num, den, 5, 1);

	ASSERT_EQ(oaknode_sequence_verify_length(seq), OAKNODE_OK);
	ASSERT_EQ(oaknode_sequence_get_video_length(seq, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 5, 1);
	ASSERT_EQ(oaknode_sequence_get_length(seq, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 5, 1);

	ASSERT_EQ(oaknode_track_ripple_remove_block(t, gap), OAKNODE_OK);
	oaknode_block_free(gap);
	ASSERT_EQ(oaknode_tracklist_remove_track(video, t), OAKNODE_OK);
	oaknode_track_free(t);
	oaknode_sequence_free(seq);
}

TEST(SequenceTest, Playhead)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	int num = -1, den = -1;
	ASSERT_EQ(oaknode_sequence_get_playhead(seq, &num, &den), OAKNODE_OK);

	ASSERT_EQ(oaknode_sequence_set_playhead(seq, 7, 2), OAKNODE_OK);
	ASSERT_EQ(oaknode_sequence_get_playhead(seq, &num, &den), OAKNODE_OK);
	expect_rational(num, den, 7, 2);

	oaknode_sequence_free(seq);
}

TEST(SequenceTest, VideoParamsRoundTrip)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	int count = 0;
	ASSERT_EQ(oaknode_sequence_get_video_stream_count(seq, &count),
			  OAKNODE_OK);
	ASSERT_GE(count, 1);

	// Default slot is readable
	OakCommonVideoParams *params = nullptr;
	ASSERT_EQ(oaknode_sequence_get_video_params(seq, 0, &params), OAKNODE_OK);
	ASSERT_NE(params, nullptr);
	oakcommon_videoparams_free(params);

	// Out of range
	EXPECT_EQ(oaknode_sequence_get_video_params(seq, count, &params),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_sequence_set_video_params(seq, count, nullptr),
			  OAKNODE_E_INVALID);

	// Replace with explicit 1920x1080 @ 25fps params
	OakCommonVideoParams *replacement = oakcommon_videoparams_init_with_time_base(
		1920, 1080, 1, 25, 0 /*pixel_format*/, 4 /*nb_channels*/, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	ASSERT_NE(replacement, nullptr);
	ASSERT_EQ(oaknode_sequence_set_video_params(seq, 0, replacement),
			  OAKNODE_OK);
	oakcommon_videoparams_free(replacement);

	OakCommonVideoParams *readback = nullptr;
	ASSERT_EQ(oaknode_sequence_get_video_params(seq, 0, &readback), OAKNODE_OK);
	ASSERT_NE(readback, nullptr);
	int width = 0, height = 0, tb_num = 0, tb_den = 0;
	ASSERT_EQ(oakcommon_videoparams_get_width(readback, &width), OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_videoparams_get_height(readback, &height),
			  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_videoparams_get_time_base(readback, &tb_num, &tb_den),
			  OAKCOMMON_OK);
	EXPECT_EQ(width, 1920);
	EXPECT_EQ(height, 1080);
	expect_rational(tb_num, tb_den, 1, 25);
	oakcommon_videoparams_free(readback);

	oaknode_sequence_free(seq);
}

TEST(SequenceTest, AudioParamsRoundTrip)
{
	OakNodeSequence *seq = oaknode_sequence_create();
	ASSERT_NE(seq, nullptr);

	int count = 0;
	ASSERT_EQ(oaknode_sequence_get_audio_stream_count(seq, &count),
			  OAKNODE_OK);
	ASSERT_GE(count, 1);

	OakAudioParams *params = oakcore_audioparams_create(
		48000, k_stereo_layout, k_sample_format_f32_packed);
	ASSERT_NE(params, nullptr);
	ASSERT_EQ(oaknode_sequence_set_audio_params(seq, 0, params), OAKNODE_OK);
	oakcore_audioparams_free(params);

	OakAudioParams *readback = nullptr;
	ASSERT_EQ(oaknode_sequence_get_audio_params(seq, 0, &readback), OAKNODE_OK);
	ASSERT_NE(readback, nullptr);
	EXPECT_EQ(oakcore_audioparams_sample_rate(readback), 48000);
	EXPECT_EQ(oakcore_audioparams_channel_layout(readback), k_stereo_layout);
	EXPECT_EQ(oakcore_audioparams_channel_count(readback), 2);
	oakcore_audioparams_free(readback);

	EXPECT_EQ(oaknode_sequence_get_audio_params(seq, count, &readback),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_sequence_set_audio_params(seq, 0, nullptr),
			  OAKNODE_E_INVALID);

	oaknode_sequence_free(seq);
}
