#include <gtest/gtest.h>

#include <memory>

#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/math/math/math.h"
#include "node/math/merge/merge.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundogeneral.h"

namespace
{

olive::Sequence *create_sequence(olive::Project *project)
{
	auto *sequence = new olive::Sequence();
	sequence->setParent(project);
	return sequence;
}

olive::Track *create_track(olive::Project *project)
{
	auto *track = new olive::Track();
	track->setParent(project);
	return track;
}

olive::ClipBlock *create_clip(olive::Project *project,
							 const olive::core::Rational &length)
{
	auto *clip = new olive::ClipBlock();
	clip->setParent(project);
	clip->set_length_and_media_out(length);
	return clip;
}

olive::GapBlock *create_gap(olive::Project *project,
						   const olive::core::Rational &length)
{
	auto *gap = new olive::GapBlock();
	gap->setParent(project);
	gap->set_length_and_media_out(length);
	return gap;
}

olive::CrossDissolveTransition *create_transition(olive::Project *project,
												 const olive::core::Rational &length)
{
	auto *transition = new olive::CrossDissolveTransition();
	transition->setParent(project);
	transition->set_length_and_media_out(length);
	return transition;
}

void append_track_to_list(olive::TrackList *list, olive::Track *track)
{
	list->array_append();
	olive::Node::connect_edge(track,
							 list->track_input(list->array_size() - 1));
}

} // namespace

class TimelineUndoGeneralTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	std::unique_ptr<olive::Project> project_;
};

TEST_F(TimelineUndoGeneralTest, BlockResizeCommandChangesLength)
{
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(4));

	olive::BlockResizeCommand cmd(clip, olive::core::Rational(2));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(2));
	// Resizing from the out point leaves the media in point alone
	EXPECT_EQ(clip->media_in(), olive::core::Rational(0));

	cmd.undo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(4));

	// Resizing to zero length is allowed on a detached block
	olive::BlockResizeCommand to_zero(clip, olive::core::Rational(0));
	to_zero.redo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(0));
	to_zero.undo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(4));
}

TEST_F(TimelineUndoGeneralTest, BlockResizeWithMediaInCommandShiftsMediaIn)
{
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(4));
	ASSERT_EQ(clip->media_in(), olive::core::Rational(0));

	olive::BlockResizeWithMediaInCommand cmd(clip, olive::core::Rational(2));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(2));
	// Resizing from the in point pushes the media in point forward
	EXPECT_EQ(clip->media_in(), olive::core::Rational(2));

	cmd.undo_now();
	EXPECT_EQ(clip->length(), olive::core::Rational(4));
	EXPECT_EQ(clip->media_in(), olive::core::Rational(0));
}

TEST_F(TimelineUndoGeneralTest, BlockSetMediaInCommandSetsAndRestores)
{
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(4));
	ASSERT_EQ(clip->media_in(), olive::core::Rational(0));

	olive::BlockSetMediaInCommand cmd(clip, olive::core::Rational(3));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(clip->media_in(), olive::core::Rational(3));

	cmd.undo_now();
	EXPECT_EQ(clip->media_in(), olive::core::Rational(0));
}

TEST_F(TimelineUndoGeneralTest, BlockEnableDisableCommandToggles)
{
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(4));
	ASSERT_TRUE(clip->is_enabled());

	olive::BlockEnableDisableCommand cmd(clip, false);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_FALSE(clip->is_enabled());

	cmd.undo_now();
	EXPECT_TRUE(clip->is_enabled());
}

TEST_F(TimelineUndoGeneralTest, AddTrackCommandConnectsDirectly)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::TimelineAddTrackCommand cmd(list, false);
	olive::Track *track = cmd.track();
	ASSERT_NE(track, nullptr);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->get_track_at(0), track);
	EXPECT_EQ(track->sequence(), sequence);
	EXPECT_EQ(track->type(), olive::Track::k_video);
	EXPECT_EQ(track->index(), 0);
	EXPECT_EQ(track->project(), project_.get());
	// The first track connects straight to the sequence's texture input
	EXPECT_TRUE(sequence->is_input_connected(olive::ViewerOutput::k_texture_input));
	EXPECT_EQ(sequence->get_connected_texture_output(), track);

	cmd.undo_now();
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(list->array_size(), 0);
	EXPECT_EQ(track->sequence(), nullptr);
	EXPECT_EQ(track->project(), nullptr);
	EXPECT_FALSE(sequence->is_input_connected(olive::ViewerOutput::k_texture_input));
}

TEST_F(TimelineUndoGeneralTest, AddTrackCommandInsertsVideoMergeNode)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	// The first track takes the direct connection
	olive::TimelineAddTrackCommand first(list, false);
	first.redo_now();
	olive::Track *track1 = first.track();
	ASSERT_EQ(sequence->get_connected_texture_output(), track1);

	// Adding another video track with automerge inserts a merge node
	olive::TimelineAddTrackCommand second(list, true);
	olive::Track *track2 = second.track();

	second.redo_now();
	EXPECT_EQ(list->get_track_count(), 2);

	olive::Node *merge = sequence->get_connected_texture_output();
	ASSERT_NE(merge, nullptr);
	EXPECT_NE(merge, track1);
	EXPECT_NE(merge, track2);
	EXPECT_EQ(merge->project(), project_.get());
	EXPECT_EQ(olive::NodeInput(merge, olive::MergeNode::k_base_in)
				  .get_connected_output(),
			  track1);
	EXPECT_EQ(olive::NodeInput(merge, olive::MergeNode::k_blend_in)
				  .get_connected_output(),
			  track2);

	second.undo_now();
	// The direct connection from the first track is restored
	EXPECT_EQ(sequence->get_connected_texture_output(), track1);
	EXPECT_EQ(merge->project(), nullptr);
	EXPECT_EQ(track2->project(), nullptr);
	EXPECT_EQ(list->get_track_count(), 1);
}

TEST_F(TimelineUndoGeneralTest, AddTrackCommandMergesAudioWithMathNode)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_audio);

	olive::TimelineAddTrackCommand first(list, false);
	first.redo_now();
	olive::Track *track1 = first.track();
	ASSERT_EQ(sequence->get_connected_sample_output(), track1);

	olive::TimelineAddTrackCommand second(list, true);
	olive::Track *track2 = second.track();

	second.redo_now();
	EXPECT_EQ(list->get_track_count(), 2);

	// Audio tracks are summed with a math (add) node
	olive::Node *math = sequence->get_connected_sample_output();
	ASSERT_NE(math, nullptr);
	EXPECT_NE(math, track1);
	EXPECT_NE(math, track2);
	EXPECT_EQ(math->id(), QStringLiteral("org.olivevideoeditor.Olive.math"));
	EXPECT_EQ(olive::NodeInput(math, olive::MathNode::k_param_a_in)
				  .get_connected_output(),
			  track1);
	EXPECT_EQ(olive::NodeInput(math, olive::MathNode::k_param_b_in)
				  .get_connected_output(),
			  track2);

	second.undo_now();
	EXPECT_EQ(sequence->get_connected_sample_output(), track1);
	EXPECT_EQ(math->project(), nullptr);
	EXPECT_EQ(list->get_track_count(), 1);
}

TEST_F(TimelineUndoGeneralTest, RemoveTrackCommandRemovesAndRestores)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(clip);
	append_track_to_list(list, track);
	ASSERT_EQ(track->index(), 0);

	olive::TimelineRemoveTrackCommand cmd(track);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(track->project(), nullptr);
	EXPECT_EQ(track->sequence(), nullptr);
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(list->array_size(), 0);
	// The clip was an exclusive dependency of the track and left the graph too
	EXPECT_EQ(clip->project(), nullptr);

	cmd.undo_now();
	EXPECT_EQ(track->project(), project_.get());
	EXPECT_EQ(clip->project(), project_.get());
	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->array_size(), 1);
	EXPECT_EQ(list->get_track_at(0), track);
	EXPECT_EQ(track->index(), 0);
	EXPECT_EQ(track->sequence(), sequence);
}

TEST_F(TimelineUndoGeneralTest, TransitionRemoveCommandRestoresClipLengths)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(1));
	olive::CrossDissolveTransition *transition =
		create_transition(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	b->set_media_in(olive::core::Rational(1));
	track->append_block(a);
	track->append_block(transition);
	track->append_block(b);
	olive::Node::connect_edge(
		a, olive::NodeInput(transition,
							olive::TransitionBlock::k_out_block_input));
	olive::Node::connect_edge(
		b, olive::NodeInput(transition, olive::TransitionBlock::k_in_block_input));
	// Layout: a [0,1], transition [1,3], b [3,6] with media_in 1
	ASSERT_EQ(transition->connected_out_block(), a);
	ASSERT_EQ(transition->connected_in_block(), b);
	ASSERT_TRUE(transition->is_dual_transition());

	olive::TransitionRemoveCommand cmd(transition, true);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	// Both clips reclaim the half of the transition that overlapped them
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
	EXPECT_EQ(transition->track(), nullptr);
	EXPECT_EQ(transition->project(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), transition);
	EXPECT_EQ(transition->project(), project_.get());
	EXPECT_EQ(transition->in(), olive::core::Rational(1));
	EXPECT_EQ(transition->out(), olive::core::Rational(3));
	EXPECT_EQ(transition->connected_out_block(), a);
	EXPECT_EQ(transition->connected_in_block(), b);
	EXPECT_EQ(a->length(), olive::core::Rational(1));
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->media_in(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapCreatesGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	// Layout: a [0,2], b [2,5], c [5,6]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	olive::Block *gap = track->blocks().at(1);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->in(), olive::core::Rational(2));
	EXPECT_EQ(gap->out(), olive::core::Rational(5));
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapExtendsPreviousGap)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(g);
	track->append_block(b);
	track->append_block(c);
	// Layout: gap [0,2], b [2,5], c [5,6]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), g);
	// The preceding gap grows to absorb the removed block's time
	EXPECT_EQ(g->length(), olive::core::Rational(5));
	EXPECT_EQ(g->in(), olive::core::Rational(0));
	EXPECT_EQ(g->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(c->out(), olive::core::Rational(6));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(g->length(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapExtendsNextGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(b);
	track->append_block(g);
	// Layout: a [0,2], b [2,4], gap [4,6]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), g);
	// The following gap grows backwards to absorb the removed block's time
	EXPECT_EQ(g->length(), olive::core::Rational(4));
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(6));
	EXPECT_EQ(a->out(), olive::core::Rational(2));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(4));
	EXPECT_EQ(g->length(), olive::core::Rational(2));
	EXPECT_EQ(g->in(), olive::core::Rational(4));
	EXPECT_EQ(g->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapMergesSurroundingGaps)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g1 = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g2 = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(g1);
	track->append_block(b);
	track->append_block(g2);
	track->append_block(c);
	// Layout: gap [0,2], b [2,4], gap [4,6], c [6,8]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), g1);
	// Both surrounding gaps merge into one covering the removed block
	EXPECT_EQ(g1->length(), olive::core::Rational(6));
	EXPECT_EQ(g1->in(), olive::core::Rational(0));
	EXPECT_EQ(g1->out(), olive::core::Rational(6));
	EXPECT_EQ(c->in(), olive::core::Rational(6));
	EXPECT_EQ(c->out(), olive::core::Rational(8));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 4);
	EXPECT_EQ(track->blocks().at(0), g1);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(track->blocks().at(2), g2);
	EXPECT_EQ(track->blocks().at(3), c);
	EXPECT_EQ(g1->length(), olive::core::Rational(2));
	EXPECT_EQ(g2->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(4));
	EXPECT_EQ(g2->in(), olive::core::Rational(4));
	EXPECT_EQ(g2->out(), olive::core::Rational(6));
	EXPECT_EQ(c->in(), olive::core::Rational(6));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapRemovesLastBlock)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);

	cmd.redo_now();
	// The last block needs no gap, it is simply removed
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoGeneralTest, ReplaceBlockWithGapRemovesPrecedingGapAtEnd)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(g);
	track->append_block(b);
	// Layout: gap [0,2], b [2,5]

	olive::TrackReplaceBlockWithGapCommand cmd(track, b);

	cmd.redo_now();
	// Removing the last block also removes the now-pointless gap before it
	EXPECT_TRUE(track->blocks().isEmpty());
	EXPECT_EQ(track->track_length(), olive::core::Rational(0));
	EXPECT_EQ(g->track(), nullptr);
	EXPECT_EQ(b->track(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), g);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(g->in(), olive::core::Rational(0));
	EXPECT_EQ(g->out(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoGeneralTest, InsertGapsSplitsClipAndInsertsGap)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(a);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,2], b [2,6]

	olive::TrackListInsertGaps cmd(list, olive::core::Rational(3),
								   olive::core::Rational(2));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	// b is split at the insert point and a gap goes between the halves
	ASSERT_EQ(track->blocks().size(), 4);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	olive::Block *gap = track->blocks().at(2);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->in(), olive::core::Rational(3));
	EXPECT_EQ(gap->out(), olive::core::Rational(5));
	olive::Block *second_half = track->blocks().at(3);
	EXPECT_EQ(second_half->in(), olive::core::Rational(5));
	EXPECT_EQ(second_half->out(), olive::core::Rational(8));
	EXPECT_EQ(track->track_length(), olive::core::Rational(8));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));
}

TEST_F(TimelineUndoGeneralTest, InsertGapsExtendsExistingGap)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(4));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(g);
	track->append_block(c);
	append_track_to_list(list, track);
	// Layout: gap [0,4], c [4,6]

	olive::TrackListInsertGaps cmd(list, olive::core::Rational(3),
								   olive::core::Rational(2));

	cmd.redo_now();
	// A gap already at the insert point simply grows
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(g->length(), olive::core::Rational(6));
	EXPECT_EQ(g->out(), olive::core::Rational(6));
	EXPECT_EQ(c->in(), olive::core::Rational(6));
	EXPECT_EQ(c->out(), olive::core::Rational(8));

	cmd.undo_now();
	EXPECT_EQ(g->length(), olive::core::Rational(4));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(c->in(), olive::core::Rational(4));
	EXPECT_EQ(c->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoGeneralTest, InsertGapsSkipsLockedTracks)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *t1 = create_track(project_.get());
	t1->append_block(create_clip(project_.get(), olive::core::Rational(4)));
	append_track_to_list(list, t1);

	olive::Track *t2 = create_track(project_.get());
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(4));
	t2->append_block(c);
	append_track_to_list(list, t2);
	t2->set_locked(true);
	// Layout: t1 [0,4] / t2 [0,4]

	olive::TrackListInsertGaps cmd(list, olive::core::Rational(2),
								   olive::core::Rational(2));

	cmd.redo_now();
	EXPECT_EQ(t1->track_length(), olive::core::Rational(6));
	// The locked track is untouched
	EXPECT_EQ(t2->blocks().size(), 1);
	EXPECT_EQ(t2->track_length(), olive::core::Rational(4));

	cmd.undo_now();
	EXPECT_EQ(t1->track_length(), olive::core::Rational(4));
	EXPECT_EQ(t2->track_length(), olive::core::Rational(4));
}

TEST_F(TimelineUndoGeneralTest, InsertGapsAtOrBeyondEndDoesNothing)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	append_track_to_list(list, track);
	// Layout: a [0,2]

	// Exactly at the end of the last block no gap is needed
	olive::TrackListInsertGaps at_end(list, olive::core::Rational(2),
									  olive::core::Rational(2));
	at_end.redo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
	at_end.undo_now();
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));

	// Beyond all content there is nothing to split or extend
	olive::TrackListInsertGaps beyond(list, olive::core::Rational(5),
									  olive::core::Rational(2));
	beyond.redo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
	beyond.undo_now();
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
}

TEST_F(TimelineUndoGeneralTest, AddDefaultTransitionAddsInAndOutTransitions)
{
	olive::NodeFactory::initialize();

	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(c);
	append_track_to_list(list, track);
	// Layout: c [0,4]

	olive::TimelineAddDefaultTransitionCommand cmd(
		{ c }, olive::core::Rational(1, 30));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();

	// A lone clip gets an in transition and an out transition, each one second
	ASSERT_EQ(track->blocks().size(), 3);
	auto *in_transition =
		dynamic_cast<olive::TransitionBlock *>(track->blocks().at(0));
	auto *out_transition =
		dynamic_cast<olive::TransitionBlock *>(track->blocks().at(2));
	ASSERT_NE(in_transition, nullptr);
	ASSERT_NE(out_transition, nullptr);

	EXPECT_EQ(in_transition->in(), olive::core::Rational(0));
	EXPECT_EQ(in_transition->out(), olive::core::Rational(1));
	EXPECT_EQ(in_transition->connected_in_block(), c);
	EXPECT_EQ(in_transition->connected_out_block(), nullptr);
	EXPECT_FALSE(in_transition->is_dual_transition());

	EXPECT_EQ(c->length(), olive::core::Rational(2));
	EXPECT_EQ(c->media_in(), olive::core::Rational(1));
	EXPECT_EQ(c->in(), olive::core::Rational(1));
	EXPECT_EQ(c->out(), olive::core::Rational(3));

	EXPECT_EQ(out_transition->in(), olive::core::Rational(3));
	EXPECT_EQ(out_transition->out(), olive::core::Rational(4));
	EXPECT_EQ(out_transition->connected_out_block(), c);
	EXPECT_EQ(out_transition->connected_in_block(), nullptr);

	// The total length of the track is unchanged
	EXPECT_EQ(track->track_length(), olive::core::Rational(4));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->blocks().at(0), c);
	EXPECT_EQ(c->length(), olive::core::Rational(4));
	EXPECT_EQ(c->media_in(), olive::core::Rational(0));
	EXPECT_EQ(c->in(), olive::core::Rational(0));
	EXPECT_EQ(c->out(), olive::core::Rational(4));
	EXPECT_EQ(in_transition->project(), nullptr);
	EXPECT_EQ(out_transition->project(), nullptr);
}

TEST_F(TimelineUndoGeneralTest, AddDefaultTransitionAddsDualTransition)
{
	olive::NodeFactory::initialize();

	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(a);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,4], b [4,8]

	olive::TimelineAddDefaultTransitionCommand cmd(
		{ a, b }, olive::core::Rational(1, 30));

	cmd.redo_now();

	// Adjacent clips get an in transition on a, a dual transition between
	// them, and an out transition on b
	ASSERT_EQ(track->blocks().size(), 5);
	auto *in_transition =
		dynamic_cast<olive::TransitionBlock *>(track->blocks().at(0));
	auto *dual_transition =
		dynamic_cast<olive::TransitionBlock *>(track->blocks().at(2));
	auto *out_transition =
		dynamic_cast<olive::TransitionBlock *>(track->blocks().at(4));
	ASSERT_NE(in_transition, nullptr);
	ASSERT_NE(dual_transition, nullptr);
	ASSERT_NE(out_transition, nullptr);

	EXPECT_TRUE(dual_transition->is_dual_transition());
	EXPECT_EQ(dual_transition->connected_out_block(), a);
	EXPECT_EQ(dual_transition->connected_in_block(), b);
	// A centered dual transition overlaps each clip by half its length
	EXPECT_EQ(dual_transition->in_offset(), olive::core::Rational(1, 2));
	EXPECT_EQ(dual_transition->out_offset(), olive::core::Rational(1, 2));

	EXPECT_EQ(a->length(), olive::core::Rational(5, 2));
	EXPECT_EQ(a->in(), olive::core::Rational(1));
	EXPECT_EQ(a->out(), olive::core::Rational(7, 2));
	EXPECT_EQ(b->length(), olive::core::Rational(5, 2));
	EXPECT_EQ(b->media_in(), olive::core::Rational(1, 2));
	EXPECT_EQ(b->in(), olive::core::Rational(9, 2));
	EXPECT_EQ(b->out(), olive::core::Rational(7));

	EXPECT_EQ(dual_transition->in(), olive::core::Rational(7, 2));
	EXPECT_EQ(dual_transition->out(), olive::core::Rational(9, 2));
	EXPECT_EQ(track->track_length(), olive::core::Rational(8));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(a->media_in(), olive::core::Rational(0));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(8));
}

TEST_F(TimelineUndoGeneralTest, AddDefaultTransitionEmptyClipListIsHarmless)
{
	// A real timeline with two adjacent clips; the empty command must leave
	// every observable detail of it untouched
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(a);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,4], b [4,8]

	olive::TimelineAddDefaultTransitionCommand cmd(
		{}, olive::core::Rational(1, 30));
	EXPECT_EQ(cmd.get_relevant_project(), nullptr);

	// redo on an empty command adds no transitions and changes nothing
	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(a->media_in(), olive::core::Rational(0));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(8));
	EXPECT_EQ(track->track_length(), olive::core::Rational(8));

	// undo is equally a no-op
	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(a->media_in(), olive::core::Rational(0));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(8));
	EXPECT_EQ(track->track_length(), olive::core::Rational(8));
}
