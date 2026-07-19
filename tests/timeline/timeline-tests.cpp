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

#include "core.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/math/math/math.h"
#include "node/math/merge/merge.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "testutil.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "undo/undocommand.h"

namespace olive
{

#define TIMELINE_TEST_START             \
	ColorManager::set_up_default_config(); \
	Project project;                    \
	Sequence sequence;                  \
	sequence.setParent(&project)

OAK_ADD_TEST(add_track)
{
	TIMELINE_TEST_START;

	Track *first_video_track, *first_audio_track;

	{
		// Test creating initial video track
		first_video_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_video));

		OAK_ASSERT(sequence.get_connected_output(Sequence::k_texture_input) ==
					 first_video_track);
		OAK_ASSERT(sequence.track_list(Track::k_video)->get_track_count() == 1);
		OAK_ASSERT(sequence.track_list(Track::k_video)->get_track_at(0) ==
					 first_video_track);
	}

	{
		// Test creating initial audio track
		first_audio_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_audio));

		OAK_ASSERT(sequence.get_connected_output(Sequence::k_samples_input) ==
					 first_audio_track);
		OAK_ASSERT(sequence.track_list(Track::k_audio)->get_track_count() == 1);
		OAK_ASSERT(sequence.track_list(Track::k_audio)->get_track_at(0) ==
					 first_audio_track);
	}

	{
		// Test creating second video track with merge
		Track *second_video_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_video), true);
		OAK_ASSERT(sequence.get_connected_output(Sequence::k_texture_input) !=
					 first_video_track);
		OAK_ASSERT(sequence.get_connected_output(Sequence::k_texture_input) !=
					 second_video_track);
		OAK_ASSERT(sequence.track_list(Track::k_video)->get_track_count() == 2);
		OAK_ASSERT(sequence.track_list(Track::k_video)->get_track_at(1) ==
					 second_video_track);

		MergeNode *merge = dynamic_cast<MergeNode *>(
			sequence.get_connected_output(Sequence::k_texture_input));
		OAK_ASSERT(merge);
		OAK_ASSERT(merge->get_connected_output(MergeNode::k_base_in) ==
					 first_video_track);
		OAK_ASSERT(merge->get_connected_output(MergeNode::k_blend_in) ==
					 second_video_track);
	}

	{
		// Test creating second audio track with merge
		Track *second_audio_track = TimelineAddTrackCommand::run_immediately(
			sequence.track_list(Track::k_audio), true);
		OAK_ASSERT(sequence.get_connected_output(Sequence::k_samples_input) !=
					 first_audio_track);
		OAK_ASSERT(sequence.get_connected_output(Sequence::k_samples_input) !=
					 second_audio_track);
		OAK_ASSERT(sequence.track_list(Track::k_audio)->get_track_count() == 2);
		OAK_ASSERT(sequence.track_list(Track::k_audio)->get_track_at(1) ==
					 second_audio_track);

		MathNode *merge = dynamic_cast<MathNode *>(
			sequence.get_connected_output(Sequence::k_samples_input));
		OAK_ASSERT(merge);
		OAK_ASSERT(merge->get_connected_output(MathNode::k_param_a_in) ==
					 first_audio_track);
		OAK_ASSERT(merge->get_connected_output(MathNode::k_param_b_in) ==
					 second_audio_track);
	}

	OAK_TEST_END;
}

OAK_ADD_TEST(SequenceDefaults)
{
	TIMELINE_TEST_START;

	sequence.add_default_nodes();

	OAK_ASSERT(sequence.get_tracks().size() == 2);

	Track *tex_connect =
		dynamic_cast<Track *>(sequence.get_connected_texture_output());
	OAK_ASSERT(tex_connect);
	Track *smp_connect =
		dynamic_cast<Track *>(sequence.get_connected_sample_output());
	OAK_ASSERT(smp_connect);
	OAK_ASSERT(tex_connect != smp_connect);
	OAK_ASSERT(sequence.get_tracks().contains(tex_connect));
	OAK_ASSERT(sequence.get_tracks().contains(smp_connect));

	OAK_TEST_END;
}

OAK_ADD_TEST(Trim)
{
	TIMELINE_TEST_START;

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
	OAK_ASSERT(track->blocks().size() == 2);

	{
		// Trim out point of second block
		BlockTrimCommand command(track, block2, 1, Timeline::k_trim_out);
		command.redo_now();

		// No block should have been added
		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(block2->length() == 1);
		OAK_ASSERT(block1->length() == 2);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->length() == 2);
	}

	{
		// Trim in point of second block
		BlockTrimCommand command(track, block2, 1, Timeline::k_trim_in);
		command.redo_now();

		// Gap should be inserted in between
		OAK_ASSERT(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(1));
		OAK_ASSERT(gap);
		OAK_ASSERT(gap->length() == 1);
		OAK_ASSERT(block2->length() == 1);
		OAK_ASSERT(block1->length() == 2);
		OAK_ASSERT(block1->next() == gap);
		OAK_ASSERT(block2->previous() == gap);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->length() == 2);
	}

	{
		// Trim out point of first block
		BlockTrimCommand command(track, block1, 1, Timeline::k_trim_out);
		command.redo_now();

		// Gap should be inserted in between
		OAK_ASSERT(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(1));
		OAK_ASSERT(gap);
		OAK_ASSERT(gap->length() == 1);
		OAK_ASSERT(block1->length() == 1);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->next() == gap);
		OAK_ASSERT(block2->previous() == gap);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->length() == 2);
	}

	{
		// Trim in point of first block
		BlockTrimCommand command(track, block1, 1, Timeline::k_trim_in);
		command.redo_now();

		// Gap should be prepended to the start
		OAK_ASSERT(track->blocks().size() == 3);
		GapBlock *gap = dynamic_cast<GapBlock *>(track->blocks().at(0));
		OAK_ASSERT(gap);
		OAK_ASSERT(gap->length() == 1);
		OAK_ASSERT(block1->length() == 1);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->next() == block2);
		OAK_ASSERT(block1->previous() == gap);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(block2->length() == 2);
		OAK_ASSERT(block1->length() == 2);
	}

	OAK_TEST_END;
}

OAK_ADD_TEST(ReplaceBlockWithGap_ClipsOnly)
{
	TIMELINE_TEST_START;

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
		OAK_ASSERT(track->blocks().size() == 2);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
	}

	{
		// Replace clip B with a gap
		TrackReplaceBlockWithGapCommand command(track, b);
		command.redo_now();

		// B should be replaced with a gap
		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) != b);
		OAK_ASSERT(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		OAK_ASSERT(track->blocks().at(1)->length() == b->length());
		OAK_ASSERT(track->blocks().at(2) == c);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
	}

	OAK_TEST_END;
}

OAK_ADD_TEST(ReplaceBlockWithGap_ClipsAndGaps)
{
	TIMELINE_TEST_START;

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
		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);

		// Test undo
		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
		OAK_ASSERT(track->blocks().at(3) == d);
		OAK_ASSERT(track->blocks().at(4) == e);
	}

	{
		// Replace clip A with a gap
		Rational original_length_of_a = a->length();
		Rational original_length_of_b = b->length();

		TrackReplaceBlockWithGapCommand command(track, a);
		command.redo_now();

		// A should be removed and B should take its place
		OAK_ASSERT(track->blocks().size() == 4);

		OAK_ASSERT(track->blocks().at(0) == b);
		OAK_ASSERT(track->blocks().at(1) == c);
		OAK_ASSERT(track->blocks().at(2) == d);
		OAK_ASSERT(track->blocks().at(3) == e);
		OAK_ASSERT(b->length() ==
					 original_length_of_a + original_length_of_b);

		// Test undo
		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
		OAK_ASSERT(track->blocks().at(3) == d);
		OAK_ASSERT(track->blocks().at(4) == e);
		OAK_ASSERT(a->length() == original_length_of_a);
		OAK_ASSERT(b->length() == original_length_of_b);
	}

	{
		// Replace clip c with a gap
		Rational original_length_of_b = b->length();
		Rational original_length_of_c = c->length();
		Rational original_length_of_d = d->length();

		TrackReplaceBlockWithGapCommand command(track, c);
		command.redo_now();

		// c and D should be removed, and B should take both of their places
		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == e);
		OAK_ASSERT(b->length() == original_length_of_b +
										original_length_of_c +
										original_length_of_d);

		// Test undo
		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
		OAK_ASSERT(track->blocks().at(3) == d);
		OAK_ASSERT(track->blocks().at(4) == e);
		OAK_ASSERT(b->length() == original_length_of_b);
		OAK_ASSERT(c->length() == original_length_of_c);
		OAK_ASSERT(d->length() == original_length_of_d);
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
		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
		OAK_ASSERT(track->blocks().at(3) == d);
		OAK_ASSERT(track->blocks().at(4) == f);
		OAK_ASSERT(d->length() ==
					 original_length_of_d + original_length_of_e);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 6);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
		OAK_ASSERT(track->blocks().at(3) == d);
		OAK_ASSERT(track->blocks().at(4) == e);
		OAK_ASSERT(track->blocks().at(5) == f);
		OAK_ASSERT(d->length() == original_length_of_d);
		OAK_ASSERT(e->length() == original_length_of_e);
	}

	OAK_TEST_END;
}

#define UsingTransition CrossDissolveTransition

OAK_ADD_TEST(ReplaceBlockWithGap_ClipsAndTransitions)
{
	TIMELINE_TEST_START;

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
		OAK_ASSERT(track->blocks().size() == 4);
		OAK_ASSERT(dynamic_cast<GapBlock *>(track->blocks().at(0)));
		OAK_ASSERT(track->blocks().at(1) == a_to_b);
		OAK_ASSERT(track->blocks().at(2) == b);
		OAK_ASSERT(track->blocks().at(3) == b_out);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a_in);
		OAK_ASSERT(track->blocks().at(1) == a);
		OAK_ASSERT(track->blocks().at(2) == a_to_b);
		OAK_ASSERT(track->blocks().at(3) == b);
		OAK_ASSERT(track->blocks().at(4) == b_out);
	}

	OAK_TEST_END;
}

OAK_ADD_TEST(InsertGaps_SingleTrack)
{
	TIMELINE_TEST_START;

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

	OAK_ASSERT(track->blocks().size() == 3);
	OAK_ASSERT(track->blocks().at(0) == a);
	OAK_ASSERT(track->blocks().at(1) == b);
	OAK_ASSERT(track->blocks().at(2) == c);

	{
		// insert gap at the start of the track, all blocks should be unsplit and shifted to the right
		TrackListInsertGaps command(list, 0, 2);
		command.redo_now();

		OAK_ASSERT(track->blocks().size() == 4);
		OAK_ASSERT(dynamic_cast<GapBlock *>(track->blocks().at(0)));
		OAK_ASSERT(track->blocks().at(0)->length() == 2);
		OAK_ASSERT(track->blocks().at(1) == a);
		OAK_ASSERT(track->blocks().at(2) == b);
		OAK_ASSERT(track->blocks().at(3) == c);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
	}

	{
		// insert gap in the middle of block A, block A should be halved with a copy at 2 and the gap at 1
		TrackListInsertGaps command(list, Rational(1, 2), 2);
		command.redo_now();

		OAK_ASSERT(track->blocks().size() == 5);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(0)->length() == Rational(1, 2));
		OAK_ASSERT(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		OAK_ASSERT(dynamic_cast<ClipBlock *>(track->blocks().at(2)));
		OAK_ASSERT(track->blocks().at(3) == b);
		OAK_ASSERT(track->blocks().at(4) == c);

		command.undo_now();

		OAK_ASSERT_EQUAL(track->blocks().size(), 3);
		OAK_ASSERT_EQUAL(track->blocks().at(0), a);
		OAK_ASSERT_EQUAL(track->blocks().at(0)->length(), 1);
		OAK_ASSERT_EQUAL(track->blocks().at(1), b);
		OAK_ASSERT_EQUAL(track->blocks().at(2), c);
	}

	{
		// insert gap between block A and B, blocks should be unsplit with a gap at 1
		TrackListInsertGaps command(list, 1, 2);
		command.redo_now();

		OAK_ASSERT(track->blocks().size() == 4);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(dynamic_cast<GapBlock *>(track->blocks().at(1)));
		OAK_ASSERT(track->blocks().at(2) == b);
		OAK_ASSERT(track->blocks().at(3) == c);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
	}

	{
		// insert gap at end, nothing should be added
		TrackListInsertGaps command(list, 3, 2);
		command.redo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);

		command.undo_now();

		OAK_ASSERT(track->blocks().size() == 3);
		OAK_ASSERT(track->blocks().at(0) == a);
		OAK_ASSERT(track->blocks().at(1) == b);
		OAK_ASSERT(track->blocks().at(2) == c);
	}

	OAK_TEST_END;
}

}
