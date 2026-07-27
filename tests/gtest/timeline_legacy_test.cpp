/***

  Olive - Non-Linear video Editor
  Copyright (c) 2022 Olive Team
  Modifications Copyright (c) 2025 mikesolar

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

// Migrated to Google Test from the legacy OAK_ADD_TEST macro framework
// (tests/timeline/timeline-tests.cpp). See docs/zh/plans/gtest-migration-guide.md.

#include <gtest/gtest.h>

#include "node/block/clip/clip.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/math/merge/merge.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "undo/undocommand.h"

using namespace olive;

namespace
{

// Mirrors the legacy TIMELINE_TEST_START prologue: default color config,
// a project and a sequence parented to it.
class TimelineLegacyTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		sequence.setParent(&project);
	}

	Project project;
	Sequence sequence;
};

#define UsingTransition CrossDissolveTransition

} // namespace

TEST_F(TimelineLegacyTest, add_track)
{
	Track *first_video_track, *first_audio_track;

	{
		// Test creating initial video track
		first_video_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_video));

		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_texture_input) ==
					first_video_track);
		ASSERT_TRUE(sequence.track_list(Track::k_video)->get_track_count() == 1);
		ASSERT_TRUE(sequence.track_list(Track::k_video)->get_track_at(0) ==
					first_video_track);
	}

	{
		// Test creating initial audio track
		first_audio_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_audio));

		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_samples_input) ==
					first_audio_track);
		ASSERT_TRUE(sequence.track_list(Track::k_audio)->get_track_count() == 1);
		ASSERT_TRUE(sequence.track_list(Track::k_audio)->get_track_at(0) ==
					first_audio_track);
	}

	{
		// Test creating second video track with merge
		Track *second_video_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_video), true);
		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_texture_input) !=
					first_video_track);
		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_texture_input) !=
					second_video_track);
		ASSERT_TRUE(sequence.track_list(Track::k_video)->get_track_count() == 2);
		ASSERT_TRUE(sequence.track_list(Track::k_video)->get_track_at(1) ==
					second_video_track);

		MergeNode *merge = dynamic_cast<MergeNode *>(
			sequence.get_connected_output(Sequence::k_texture_input));
		ASSERT_TRUE(merge);
		ASSERT_TRUE(merge->get_connected_output(MergeNode::k_base_in) ==
					first_video_track);
		ASSERT_TRUE(merge->get_connected_output(MergeNode::k_blend_in) ==
					second_video_track);
	}

	{
		// Test creating second audio track with merge
		Track *second_audio_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_audio), true);
		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_samples_input) !=
					first_audio_track);
		ASSERT_TRUE(sequence.get_connected_output(Sequence::k_samples_input) !=
					second_audio_track);
		ASSERT_TRUE(sequence.track_list(Track::k_audio)->get_track_count() == 2);
		ASSERT_TRUE(sequence.track_list(Track::k_audio)->get_track_at(1) ==
					second_audio_track);

		MathNode *merge = dynamic_cast<MathNode *>(
			sequence.get_connected_output(Sequence::k_samples_input));
		ASSERT_TRUE(merge);
		ASSERT_TRUE(merge->get_connected_output(MathNode::k_param_a_in) ==
					first_audio_track);
		ASSERT_TRUE(merge->get_connected_output(MathNode::k_param_b_in) ==
					second_audio_track);
	}
}

TEST_F(TimelineLegacyTest, SequenceDefaults)
{
	sequence.add_default_nodes();

	ASSERT_TRUE(sequence.get_tracks().size() == 2);

	Track *tex_connect =
		dynamic_cast<Track *>(sequence.get_connected_texture_output());
	ASSERT_TRUE(tex_connect);
	Track *smp_connect =
		dynamic_cast<Track *>(sequence.get_connected_sample_output());
	ASSERT_TRUE(smp_connect);
	ASSERT_TRUE(tex_connect != smp_connect);
	ASSERT_TRUE(sequence.get_tracks().contains(tex_connect));
	ASSERT_TRUE(sequence.get_tracks().contains(smp_connect));
}

TEST_F(TimelineLegacyTest, Trim)
{
	sequence.add_default_nodes();

	Track *track = sequence.get_tracks().first();

	ClipBlock *block1 = new ClipBlock();
	block1->set_length_and_media_out(2);
	block1->setParent(&project);
	track->append_block(block1);

	ClipBlock *block2 = new ClipBlock();
	block2->set_length_and_media_out(2);
	block2->setParent(&project);
	track->append_block(block2);

	// There should be two blocks right now
	ASSERT_TRUE(track->blocks().size() == 2);

	{
		// Trim out point of second block
		BlockTrimCommand command(track, block2, 1, Timeline::k_trim_out);
		command.redo_now();

		// No block should have been added
		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(block2->length() == 1);
		ASSERT_TRUE(block1->length() == 2);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->length() == 2);
	}

	{
		// Trim in point of second block
		BlockTrimCommand command(track, block2, 1, Timeline::k_trim_in);
		command.redo_now();

		// Gap should be inserted in between
		ASSERT_TRUE(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(1));
		ASSERT_TRUE(gap);
		ASSERT_TRUE(gap->length() == 1);
		ASSERT_TRUE(block2->length() == 1);
		ASSERT_TRUE(block1->length() == 2);
		ASSERT_TRUE(block1->next() == gap);
		ASSERT_TRUE(block2->previous() == gap);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->length() == 2);
	}

	{
		// Trim out point of first block
		BlockTrimCommand command(track, block1, 1, Timeline::k_trim_out);
		command.redo_now();

		// Gap should be inserted in between
		ASSERT_TRUE(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(1));
		ASSERT_TRUE(gap);
		ASSERT_TRUE(gap->length() == 1);
		ASSERT_TRUE(block1->length() == 1);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->next() == gap);
		ASSERT_TRUE(block2->previous() == gap);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->length() == 2);
	}

	{
		// Trim in point of first block
		BlockTrimCommand command(track, block1, 1, Timeline::k_trim_in);
		command.redo_now();

		// Gap should be prepended to the start
		ASSERT_TRUE(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(0));
		ASSERT_TRUE(gap);
		ASSERT_TRUE(gap->length() == 1);
		ASSERT_TRUE(block1->length() == 1);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->next() == block2);
		ASSERT_TRUE(block1->previous() == gap);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(block2->length() == 2);
		ASSERT_TRUE(block1->length() == 2);
	}
}

TEST_F(TimelineLegacyTest, ReplaceBlockWithGap_ClipsOnly)
{
	// create a track that goes clip -> clip -> clip
	sequence.add_default_nodes();
	Track *track = sequence.track_list(Track::k_video)->get_tracks().first();

	ClipBlock *a = new ClipBlock();
	a->setParent(&project);
	track->append_block(a);

	ClipBlock *b = new ClipBlock();
	b->setParent(&project);
	track->append_block(b);

	ClipBlock *c = new ClipBlock();
	c->setParent(&project);
	track->append_block(c);

	{
		// Replace clip c with a gap
		TrackReplaceBlockWithGapCommand command(track, c);
		command.redo_now();

		// Clip should be removed without any gap actually taking its place, since the clip is at the
		// end of the track
		ASSERT_TRUE(track->blocks().size() == 2);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
	}

	{
		// Replace clip B with a gap
		TrackReplaceBlockWithGapCommand command(track, b);
		command.redo_now();

		// B should be replaced with a gap
		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) != b);
		ASSERT_TRUE(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		ASSERT_TRUE(track->blocks().at(1)->length() == b->length());
		ASSERT_TRUE(track->blocks().at(2) == c);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
	}
}

TEST_F(TimelineLegacyTest, ReplaceBlockWithGap_ClipsAndGaps)
{
	// create a track that goes clip -> gap -> clip -> clip -> gap -> clip
	sequence.add_default_nodes();
	Track *track = sequence.track_list(Track::k_video)->get_tracks().first();

	ClipBlock *a = new ClipBlock();
	a->setParent(&project);
	track->append_block(a);

	GapBlock *b = new GapBlock();
	b->setParent(&project);
	track->append_block(b);

	ClipBlock *c = new ClipBlock();
	c->setParent(&project);
	track->append_block(c);

	GapBlock *d = new GapBlock();
	d->setParent(&project);
	track->append_block(d);

	ClipBlock *e = new ClipBlock();
	e->setParent(&project);
	track->append_block(e);

	{
		// Replace clip E with a gap
		TrackReplaceBlockWithGapCommand command(track, e);
		command.redo_now();

		// Both clips D and E should be removed because this command should remove any trailing gaps
		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);

		// Test undo
		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
		ASSERT_TRUE(track->blocks().at(3) == d);
		ASSERT_TRUE(track->blocks().at(4) == e);
	}

	{
		// Replace clip A with a gap
		Rational original_length_of_a = a->length();
		Rational original_length_of_b = b->length();

		TrackReplaceBlockWithGapCommand command(track, a);
		command.redo_now();

		// A should be removed and B should take its place
		ASSERT_TRUE(track->blocks().size() == 4);

		ASSERT_TRUE(track->blocks().at(0) == b);
		ASSERT_TRUE(track->blocks().at(1) == c);
		ASSERT_TRUE(track->blocks().at(2) == d);
		ASSERT_TRUE(track->blocks().at(3) == e);
		ASSERT_TRUE(b->length() ==
					original_length_of_a + original_length_of_b);

		// Test undo
		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
		ASSERT_TRUE(track->blocks().at(3) == d);
		ASSERT_TRUE(track->blocks().at(4) == e);
		ASSERT_TRUE(a->length() == original_length_of_a);
		ASSERT_TRUE(b->length() == original_length_of_b);
	}

	{
		// Replace clip c with a gap
		Rational original_length_of_b = b->length();
		Rational original_length_of_c = c->length();
		Rational original_length_of_d = d->length();

		TrackReplaceBlockWithGapCommand command(track, c);
		command.redo_now();

		// c and D should be removed, and B should take both of their places
		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == e);
		ASSERT_TRUE(b->length() == original_length_of_b +
									   original_length_of_c +
									   original_length_of_d);

		// Test undo
		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
		ASSERT_TRUE(track->blocks().at(3) == d);
		ASSERT_TRUE(track->blocks().at(4) == e);
		ASSERT_TRUE(b->length() == original_length_of_b);
		ASSERT_TRUE(c->length() == original_length_of_c);
		ASSERT_TRUE(d->length() == original_length_of_d);
	}

	{
		// add a fourth clip at the end of the track
		ClipBlock *f = new ClipBlock();
		f->setParent(&project);
		track->append_block(f);

		// Try replacing E with a block again
		TrackReplaceBlockWithGapCommand command(track, e);
		Rational original_length_of_d = d->length();
		Rational original_length_of_e = e->length();
		command.redo_now();

		// E should be removed and D should have taken its place
		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
		ASSERT_TRUE(track->blocks().at(3) == d);
		ASSERT_TRUE(track->blocks().at(4) == f);
		ASSERT_TRUE(d->length() ==
					original_length_of_d + original_length_of_e);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 6);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
		ASSERT_TRUE(track->blocks().at(3) == d);
		ASSERT_TRUE(track->blocks().at(4) == e);
		ASSERT_TRUE(track->blocks().at(5) == f);
		ASSERT_TRUE(d->length() == original_length_of_d);
		ASSERT_TRUE(e->length() == original_length_of_e);
	}
}

TEST_F(TimelineLegacyTest, ReplaceBlockWithGap_ClipsAndTransitions)
{
	// create a track that goes clip -> gap -> clip -> clip -> gap -> clip
	sequence.add_default_nodes();
	Track *track = sequence.track_list(Track::k_video)->get_tracks().first();

	UsingTransition *a_in = new UsingTransition();
	a_in->setParent(&project);
	track->append_block(a_in);

	ClipBlock *a = new ClipBlock();
	a->setParent(&project);
	track->append_block(a);

	UsingTransition *a_to_b = new UsingTransition();
	a_to_b->setParent(&project);
	track->append_block(a_to_b);

	ClipBlock *b = new ClipBlock();
	b->setParent(&project);
	track->append_block(b);

	UsingTransition *b_out = new UsingTransition();
	b_out->setParent(&project);
	track->append_block(b_out);

	Node::connect_edge(a, NodeInput(a_in, UsingTransition::k_in_block_input));
	Node::connect_edge(a, NodeInput(a_to_b, UsingTransition::k_out_block_input));
	Node::connect_edge(b, NodeInput(a_to_b, UsingTransition::k_in_block_input));
	Node::connect_edge(b, NodeInput(b_out, UsingTransition::k_out_block_input));

	{
		// Replace A with gap
		TrackReplaceBlockWithGapCommand command(track, a);
		command.redo_now();

		// A should be replaced with a gap and so should A_IN since A was the only clip connected to it.
		// Also A_TO_B should only be connected to B now
		ASSERT_TRUE(track->blocks().size() == 4);
		ASSERT_TRUE(dynamic_cast<GapBlock *>(track->blocks().at(0)));
		ASSERT_TRUE(track->blocks().at(1) == a_to_b);
		ASSERT_TRUE(track->blocks().at(2) == b);
		ASSERT_TRUE(track->blocks().at(3) == b_out);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a_in);
		ASSERT_TRUE(track->blocks().at(1) == a);
		ASSERT_TRUE(track->blocks().at(2) == a_to_b);
		ASSERT_TRUE(track->blocks().at(3) == b);
		ASSERT_TRUE(track->blocks().at(4) == b_out);
	}
}

TEST_F(TimelineLegacyTest, InsertGaps_SingleTrack)
{
	sequence.add_default_nodes();

	TrackList *list = sequence.track_list(Track::k_video);
	Track *track = list->get_tracks().first();

	ClipBlock *a = new ClipBlock();
	a->set_length_and_media_out(1);
	a->setParent(&project);
	track->append_block(a);

	ClipBlock *b = new ClipBlock();
	b->set_length_and_media_out(1);
	b->setParent(&project);
	track->append_block(b);

	ClipBlock *c = new ClipBlock();
	c->set_length_and_media_out(1);
	c->setParent(&project);
	track->append_block(c);

	ASSERT_TRUE(track->blocks().size() == 3);
	ASSERT_TRUE(track->blocks().at(0) == a);
	ASSERT_TRUE(track->blocks().at(1) == b);
	ASSERT_TRUE(track->blocks().at(2) == c);

	{
		// insert gap at the start of the track, all blocks should be unsplit and shifted to the right
		TrackListInsertGaps command(list, 0, 2);
		command.redo_now();

		ASSERT_TRUE(track->blocks().size() == 4);
		ASSERT_TRUE(dynamic_cast<GapBlock *>(track->blocks().at(0)));
		ASSERT_TRUE(track->blocks().at(0)->length() == 2);
		ASSERT_TRUE(track->blocks().at(1) == a);
		ASSERT_TRUE(track->blocks().at(2) == b);
		ASSERT_TRUE(track->blocks().at(3) == c);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
	}

	{
		// insert gap in the middle of block A, block A should be halved with a copy at 2 and the gap at 1
		TrackListInsertGaps command(list, Rational(1, 2), 2);
		command.redo_now();

		ASSERT_TRUE(track->blocks().size() == 5);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(0)->length() == Rational(1, 2));
		ASSERT_TRUE(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		ASSERT_TRUE(dynamic_cast<ClipBlock *>(track->blocks().at(2)));
		ASSERT_TRUE(track->blocks().at(3) == b);
		ASSERT_TRUE(track->blocks().at(4) == c);

		command.undo_now();

		ASSERT_EQ(track->blocks().size(), 3);
		ASSERT_EQ(track->blocks().at(0), a);
		ASSERT_EQ(track->blocks().at(0)->length(), 1);
		ASSERT_EQ(track->blocks().at(1), b);
		ASSERT_EQ(track->blocks().at(2), c);
	}

	{
		// insert gap between block A and B, blocks should be unsplit with a gap at 1
		TrackListInsertGaps command(list, 1, 2);
		command.redo_now();

		ASSERT_TRUE(track->blocks().size() == 4);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		ASSERT_TRUE(track->blocks().at(2) == b);
		ASSERT_TRUE(track->blocks().at(3) == c);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
	}

	{
		// insert gap at end, nothing should be added
		TrackListInsertGaps command(list, 3, 2);
		command.redo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);

		command.undo_now();

		ASSERT_TRUE(track->blocks().size() == 3);
		ASSERT_TRUE(track->blocks().at(0) == a);
		ASSERT_TRUE(track->blocks().at(1) == b);
		ASSERT_TRUE(track->blocks().at(2) == c);
	}
}
