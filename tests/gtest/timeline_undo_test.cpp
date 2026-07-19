#include <gtest/gtest.h>

#include <memory>

#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundocommon.h"
#include "timeline/timelineundopointer.h"
#include "timeline/timelineundoripple.h"
#include "timeline/timelineundosplit.h"
#include "timeline/timelineundotrack.h"
#include "timeline/timelineundoworkarea.h"

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

void append_track_to_list(olive::TrackList *list, olive::Track *track)
{
	list->array_append();
	olive::Node::connect_edge(track,
							 list->track_input(list->array_size() - 1));
}

} // namespace

class TimelineUndoTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	std::unique_ptr<olive::Project> project_;
};

//
// timelineundotrack.h
//
TEST_F(TimelineUndoTest, RippleRemoveBlockCommandRemovesAndRestores)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	// Layout: a [0,2], b [2,5], c [5,6]

	olive::TrackRippleRemoveBlockCommand cmd(track, b);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), c);
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(c->in(), olive::core::Rational(2));
	EXPECT_EQ(c->out(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->track(), track);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));
}

TEST_F(TimelineUndoTest, PrependBlockCommandInsertsAndRemoves)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(b);

	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(1));
	olive::TrackPrependBlockCommand cmd(track, a);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->blocks().at(0), b);
	EXPECT_EQ(a->track(), nullptr);
	EXPECT_EQ(b->in(), olive::core::Rational(0));
	EXPECT_EQ(b->out(), olive::core::Rational(2));
}

TEST_F(TimelineUndoTest, InsertBlockAfterCommandInsertsAndRemoves)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(1));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(c);

	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	olive::TrackInsertBlockAfterCommand cmd(track, b, a);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(c->in(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(4));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(c->in(), olive::core::Rational(1));
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
}

TEST_F(TimelineUndoTest, ReplaceBlockCommandSwapsAndRestores)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);

	olive::ClipBlock *r = create_clip(project_.get(), olive::core::Rational(3));
	olive::TrackReplaceBlockCommand cmd(track, b, r);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), r);
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(r->in(), olive::core::Rational(2));
	EXPECT_EQ(r->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(r->track(), nullptr);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

//
// timelineundoworkarea.h
//
TEST_F(TimelineUndoTest, WorkareaSetEnabledCommandToggles)
{
	olive::TimelineWorkArea workarea;
	ASSERT_FALSE(workarea.enabled());

	olive::WorkareaSetEnabledCommand cmd(project_.get(), &workarea, true);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_TRUE(workarea.enabled());

	cmd.undo_now();
	EXPECT_FALSE(workarea.enabled());
}

TEST_F(TimelineUndoTest, WorkareaSetRangeCommandSetsAndRestores)
{
	olive::TimelineWorkArea workarea;
	const olive::core::TimeRange old_range(olive::core::Rational(2),
										   olive::core::Rational(6));
	const olive::core::TimeRange new_range(olive::core::Rational(3),
										   olive::core::Rational(9));
	workarea.set_range(old_range);

	// The two-argument form captures the workarea's current range as the old one
	olive::WorkareaSetRangeCommand cmd(&workarea, new_range);

	cmd.redo_now();
	EXPECT_EQ(workarea.range(), new_range);

	cmd.undo_now();
	EXPECT_EQ(workarea.range(), old_range);
}

TEST_F(TimelineUndoTest, WorkareaSetRangeCommandExplicitOldRange)
{
	olive::TimelineWorkArea workarea;
	const olive::core::TimeRange old_range(olive::core::Rational(0),
										   olive::core::Rational(10));
	const olive::core::TimeRange new_range(olive::core::Rational(4),
										   olive::core::Rational(5));
	workarea.set_range(old_range);

	olive::WorkareaSetRangeCommand cmd(&workarea, new_range, old_range);

	cmd.redo_now();
	EXPECT_EQ(workarea.range(), new_range);
	EXPECT_EQ(workarea.in(), olive::core::Rational(4));
	EXPECT_EQ(workarea.out(), olive::core::Rational(5));

	cmd.undo_now();
	EXPECT_EQ(workarea.range(), old_range);
}

//
// timelineundocommon.h
//
TEST_F(TimelineUndoTest, NodeCanBeRemovedReflectsConnections)
{
	olive::ClipBlock *clip = create_clip(project_.get(), olive::core::Rational(2));

	// An unconnected node has no output connections and can be removed
	EXPECT_TRUE(olive::node_can_be_removed(clip));

	olive::Track *track = create_track(project_.get());
	track->append_block(clip);
	EXPECT_FALSE(olive::node_can_be_removed(clip));

	track->ripple_remove_block(clip);
	EXPECT_TRUE(olive::node_can_be_removed(clip));
}

TEST_F(TimelineUndoTest, CreateAndRunRemoveCommandRemovesFromGraph)
{
	auto *node = new olive::MathNode();
	node->setParent(project_.get());
	ASSERT_EQ(node->project(), project_.get());

	olive::UndoCommand *cmd = olive::create_and_run_remove_command(node);
	EXPECT_EQ(node->project(), nullptr);
	EXPECT_FALSE(project_->nodes().contains(node));

	cmd->undo_now();
	EXPECT_EQ(node->project(), project_.get());
	EXPECT_TRUE(project_->nodes().contains(node));

	delete cmd;
}

//
// timelineundosplit.h
//
TEST_F(TimelineUndoTest, BlockSplitCommandSplitsAndMerges)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(a);
	// Layout: a [0,4]

	olive::BlockSplitCommand cmd(a, olive::core::Rational(1));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();

	olive::Block *split = cmd.new_block();
	ASSERT_NE(split, nullptr);
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), split);

	EXPECT_EQ(a->length(), olive::core::Rational(1));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(1));

	EXPECT_EQ(split->length(), olive::core::Rational(3));
	EXPECT_EQ(split->in(), olive::core::Rational(1));
	EXPECT_EQ(split->out(), olive::core::Rational(4));
	// The second half's media in point is offset by the split time
	EXPECT_EQ(static_cast<olive::ClipBlock *>(split)->media_in(),
			  olive::core::Rational(1));

	EXPECT_EQ(track->track_length(), olive::core::Rational(4));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(split->track(), nullptr);
}

TEST_F(TimelineUndoTest, BlockSplitCommandMovesOutTransitionToNewBlock)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	auto *transition = new olive::CrossDissolveTransition();
	transition->setParent(project_.get());
	transition->set_length_and_media_out(olive::core::Rational(2));
	track->append_block(a);
	track->append_block(transition);
	olive::Node::connect_edge(
		a, olive::NodeInput(transition, olive::TransitionBlock::k_out_block_input));
	ASSERT_EQ(transition->connected_out_block(), a);
	// Layout: a [0,4], transition [4,6]

	olive::BlockSplitCommand cmd(a, olive::core::Rational(2));
	cmd.redo_now();

	olive::Block *split = cmd.new_block();
	ASSERT_NE(split, nullptr);
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(split->in(), olive::core::Rational(2));
	EXPECT_EQ(split->out(), olive::core::Rational(4));
	EXPECT_EQ(transition->in(), olive::core::Rational(4));

	// The out transition moved from the original block to the split block
	EXPECT_EQ(transition->connected_out_block(), split);
	EXPECT_EQ(olive::NodeInput(transition,
							   olive::TransitionBlock::k_out_block_input)
				  .get_connected_output(),
			  split);

	cmd.undo_now();

	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(transition->connected_out_block(), a);
	EXPECT_EQ(transition->in(), olive::core::Rational(4));
}

TEST_F(TimelineUndoTest, BlockSplitPreservingLinksCommandSplitsLinkedBlocks)
{
	olive::Track *video_track = create_track(project_.get());
	olive::Track *audio_track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(4));
	video_track->append_block(a);
	audio_track->append_block(b);
	olive::Node::link(a, b);

	olive::BlockSplitPreservingLinksCommand cmd(
		{ a, b }, { olive::core::Rational(2) });
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();

	ASSERT_EQ(video_track->blocks().size(), 2);
	ASSERT_EQ(audio_track->blocks().size(), 2);
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(b->length(), olive::core::Rational(2));

	olive::Block *a_split = cmd.get_split(a, 0);
	olive::Block *b_split = cmd.get_split(b, 0);
	ASSERT_NE(a_split, nullptr);
	ASSERT_NE(b_split, nullptr);
	EXPECT_EQ(a_split->in(), olive::core::Rational(2));
	EXPECT_EQ(b_split->in(), olive::core::Rational(2));

	// The original link survives and the new halves are linked together too
	EXPECT_TRUE(olive::Node::are_linked(a, b));
	EXPECT_TRUE(olive::Node::are_linked(a_split, b_split));

	// Invalid lookups return null instead of crashing
	EXPECT_EQ(cmd.get_split(a, 1), nullptr);
	EXPECT_EQ(cmd.get_split(a, -1), nullptr);
	olive::ClipBlock *stray =
		create_clip(project_.get(), olive::core::Rational(1));
	EXPECT_EQ(cmd.get_split(stray, 0), nullptr);

	cmd.undo_now();

	ASSERT_EQ(video_track->blocks().size(), 1);
	ASSERT_EQ(audio_track->blocks().size(), 1);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_TRUE(olive::Node::are_linked(a, b));
}

TEST_F(TimelineUndoTest, TrackSplitAtTimeCommandSplitsContainingBlock)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	olive::TrackSplitAtTimeCommand cmd(track, olive::core::Rational(3));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(track->blocks().at(2)->in(), olive::core::Rational(3));
	EXPECT_EQ(track->blocks().at(2)->out(), olive::core::Rational(5));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, TrackSplitAtTimeCommandNoOpOutsideBlocks)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	// A time exactly on a block boundary is not contained by any block
	olive::TrackSplitAtTimeCommand on_edge(track, olive::core::Rational(2));
	on_edge.redo_now();
	EXPECT_EQ(track->blocks().size(), 2);
	on_edge.undo_now();
	EXPECT_EQ(track->blocks().size(), 2);

	// A time past the end of the track contains nothing either
	olive::TrackSplitAtTimeCommand past_end(track, olive::core::Rational(10));
	past_end.redo_now();
	EXPECT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	past_end.undo_now();
	EXPECT_EQ(track->blocks().size(), 2);
}

//
// timelineundoripple.h
//
TEST_F(TimelineUndoTest, RippleRemoveAreaRemovesMiddleBlock)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	// Layout: a [0,2], b [2,5], c [5,6]

	olive::TrackRippleRemoveAreaCommand cmd(
		track, olive::core::TimeRange(olive::core::Rational(2),
									  olive::core::Rational(5)));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), c);
	EXPECT_EQ(c->in(), olive::core::Rational(2));
	EXPECT_EQ(c->out(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));

	// The removed block was taken out of the graph entirely
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(b->project(), nullptr);

	// An insertion would go after the block preceding the removed area
	EXPECT_EQ(cmd.get_insertion_index(), a);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->project(), project_.get());
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));
}

TEST_F(TimelineUndoTest, RippleRemoveAreaRemovesMultipleBlocks)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(1));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(1));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	olive::ClipBlock *d = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	track->append_block(d);
	// Layout: a [0,1], b [1,2], c [2,3], d [3,4]

	olive::TrackRippleRemoveAreaCommand cmd(
		track, olive::core::TimeRange(olive::core::Rational(1),
									  olive::core::Rational(3)));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), d);
	EXPECT_EQ(d->in(), olive::core::Rational(1));
	EXPECT_EQ(d->out(), olive::core::Rational(2));
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 4);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(track->blocks().at(2), c);
	EXPECT_EQ(track->blocks().at(3), d);
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(c->in(), olive::core::Rational(2));
	EXPECT_EQ(d->in(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(4));
}

TEST_F(TimelineUndoTest, RippleRemoveAreaTrimsBothEnds)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(4));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(4));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,4], b [4,8]

	olive::TrackRippleRemoveAreaCommand cmd(
		track, olive::core::TimeRange(olive::core::Rational(2),
									  olive::core::Rational(6)));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	// a is out-trimmed to the range start, b is in-trimmed to the range end
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(4));
	EXPECT_EQ(track->track_length(), olive::core::Rational(4));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(b->length(), olive::core::Rational(4));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(8));
}

TEST_F(TimelineUndoTest, RippleRemoveAreaSplicesBlock)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(10));
	track->append_block(a);
	// Layout: a [0,10]

	olive::TrackRippleRemoveAreaCommand cmd(
		track, olive::core::TimeRange(olive::core::Rational(3),
									  olive::core::Rational(6)));

	cmd.redo_now();

	// The block is split around the removed range and the second half is
	// in-trimmed by the range length
	olive::Block *spliced = cmd.get_spliced_block();
	ASSERT_NE(spliced, nullptr);
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(a->length(), olive::core::Rational(3));
	EXPECT_EQ(a->out(), olive::core::Rational(3));
	EXPECT_EQ(spliced->in(), olive::core::Rational(3));
	EXPECT_EQ(spliced->out(), olive::core::Rational(7));
	EXPECT_EQ(spliced->length(), olive::core::Rational(4));
	EXPECT_EQ(static_cast<olive::ClipBlock *>(spliced)->media_in(),
			  olive::core::Rational(6));
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(a->length(), olive::core::Rational(10));
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(10));
}

TEST_F(TimelineUndoTest, RippleRemoveAreaTrimsGapWhenSplittingGapsDisabled)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(5));
	track->append_block(g);
	// Layout: gap [0,5]

	olive::TrackRippleRemoveAreaCommand cmd(
		track, olive::core::TimeRange(olive::core::Rational(2),
									  olive::core::Rational(4)));
	ASSERT_EQ(cmd.get_spliced_block(), nullptr);

	cmd.redo_now();
	// Gaps are not spliced by default, the gap is just trimmed by the range
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(g->length(), olive::core::Rational(3));
	EXPECT_EQ(g->in(), olive::core::Rational(0));
	EXPECT_EQ(g->out(), olive::core::Rational(3));
	EXPECT_EQ(cmd.get_spliced_block(), nullptr);

	cmd.undo_now();
	EXPECT_EQ(g->length(), olive::core::Rational(5));
	EXPECT_EQ(g->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, RippleRemoveAreaNoOpOnEmptyRange)
{
	olive::Track *track = create_track(project_.get());

	// Empty track: nothing to remove
	olive::TrackRippleRemoveAreaCommand empty_track(
		track, olive::core::TimeRange(olive::core::Rational(0),
									  olive::core::Rational(5)));
	empty_track.redo_now();
	EXPECT_TRUE(track->blocks().isEmpty());
	empty_track.undo_now();
	EXPECT_TRUE(track->blocks().isEmpty());

	// Range fully beyond the track's content: also nothing to remove
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	olive::TrackRippleRemoveAreaCommand past_end(
		track, olive::core::TimeRange(olive::core::Rational(5),
									  olive::core::Rational(7)));
	past_end.redo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
	past_end.undo_now();
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
}

TEST_F(TimelineUndoTest, TrackListRippleRemoveAreaAffectsAllTracks)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *t1 = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	t1->append_block(a);
	t1->append_block(b);
	append_track_to_list(list, t1);

	olive::Track *t2 = create_track(project_.get());
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *d = create_clip(project_.get(), olive::core::Rational(3));
	t2->append_block(c);
	t2->append_block(d);
	append_track_to_list(list, t2);
	// t1: a [0,2], b [2,5] / t2: c [0,3], d [3,6]

	olive::TrackListRippleRemoveAreaCommand cmd(list, olive::core::Rational(2),
												olive::core::Rational(4));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	// t1: b is in-trimmed by the range
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	// t2: c is out-trimmed and d is in-trimmed
	EXPECT_EQ(c->length(), olive::core::Rational(2));
	EXPECT_EQ(d->length(), olive::core::Rational(2));
	EXPECT_EQ(d->in(), olive::core::Rational(2));
	EXPECT_EQ(d->out(), olive::core::Rational(4));

	cmd.undo_now();
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->length(), olive::core::Rational(3));
	EXPECT_EQ(d->length(), olive::core::Rational(3));
	EXPECT_EQ(d->in(), olive::core::Rational(3));
	EXPECT_EQ(d->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoTest, TrackListRippleRemoveAreaSkipsLockedTracks)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *t1 = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	t1->append_block(a);
	t1->append_block(b);
	append_track_to_list(list, t1);

	olive::Track *t2 = create_track(project_.get());
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(3));
	t2->append_block(c);
	append_track_to_list(list, t2);
	t2->set_locked(true);

	olive::TrackListRippleRemoveAreaCommand cmd(list, olive::core::Rational(2),
												olive::core::Rational(4));

	cmd.redo_now();
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	// The locked track is untouched
	EXPECT_EQ(c->length(), olive::core::Rational(3));
	EXPECT_EQ(c->in(), olive::core::Rational(0));
	EXPECT_EQ(c->out(), olive::core::Rational(3));

	cmd.undo_now();
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(c->length(), olive::core::Rational(3));
}

TEST_F(TimelineUndoTest, TimelineRippleRemoveAreaAffectsAllTrackTypes)
{
	olive::Sequence *sequence = create_sequence(project_.get());

	olive::Track *video = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	video->append_block(a);
	video->append_block(b);
	append_track_to_list(sequence->track_list(olive::Track::k_video), video);

	olive::Track *audio = create_track(project_.get());
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(3));
	audio->append_block(c);
	append_track_to_list(sequence->track_list(olive::Track::k_audio), audio);
	// video: a [0,2], b [2,5] / audio: c [0,3]

	olive::TimelineRippleRemoveAreaCommand cmd(sequence,
											   olive::core::Rational(1),
											   olive::core::Rational(3));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(1));
	EXPECT_EQ(b->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(c->length(), olive::core::Rational(1));
	EXPECT_EQ(c->out(), olive::core::Rational(1));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->length(), olive::core::Rational(3));
	EXPECT_EQ(c->out(), olive::core::Rational(3));
}

TEST_F(TimelineUndoTest, RippleToolCommandTrimsBlockAndRipples)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,2], b [2,5]

	QHash<olive::Track *, olive::TrackListRippleToolCommand::RippleInfo> info;
	info.insert(track, { a, false });
	olive::TrackListRippleToolCommand cmd(list, info, olive::core::Rational(1),
										  olive::Timeline::k_trim_out);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	// a extends by one, pushing b later
	EXPECT_EQ(a->length(), olive::core::Rational(3));
	EXPECT_EQ(a->out(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, RippleToolCommandAppendsGap)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,2], b [2,5]

	// Rather than resizing a block, a gap of the movement length is inserted
	QHash<olive::Track *, olive::TrackListRippleToolCommand::RippleInfo> info;
	info.insert(track, { b, true });
	olive::TrackListRippleToolCommand cmd(list, info, olive::core::Rational(1),
										  olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	olive::Block *gap = track->blocks().at(1);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->length(), olive::core::Rational(1));
	EXPECT_EQ(gap->in(), olive::core::Rational(2));
	EXPECT_EQ(gap->out(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(6));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, RippleToolCommandRemovesZeroLengthGap)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	append_track_to_list(list, track);
	// Layout: a [0,2], gap [2,4], b [4,6]

	// Trimming the gap by its entire length removes it from the track and graph
	QHash<olive::Track *, olive::TrackListRippleToolCommand::RippleInfo> info;
	info.insert(track, { g, false });
	olive::TrackListRippleToolCommand cmd(list, info,
										  olive::core::Rational(-2),
										  olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(4));
	EXPECT_EQ(g->track(), nullptr);
	EXPECT_EQ(g->project(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), g);
	EXPECT_EQ(g->project(), project_.get());
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoTest, RippleDeleteGapsAtRegionsRemovesGap)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	append_track_to_list(sequence->track_list(olive::Track::k_video), track);
	// Layout: a [0,2], gap [2,4], b [4,6]

	olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList regions;
	regions.append({ track, olive::core::TimeRange(olive::core::Rational(2),
												   olive::core::Rational(4)) });
	olive::TimelineRippleDeleteGapsAtRegionsCommand cmd(sequence, regions);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	// commands_ is populated by prepare(), which runs on the first redo
	cmd.redo_now();
	EXPECT_TRUE(cmd.has_commands());
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(4));
	EXPECT_EQ(g->track(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), g);
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
}

TEST_F(TimelineUndoTest, RippleDeleteGapsAtRegionsIgnoresNonGapRegion)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	append_track_to_list(sequence->track_list(olive::Track::k_video), track);
	// Layout: a [0,2]

	// The region covers a clip rather than a gap, so there is nothing to do
	olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList regions;
	regions.append({ track, olive::core::TimeRange(olive::core::Rational(0),
												   olive::core::Rational(2)) });
	olive::TimelineRippleDeleteGapsAtRegionsCommand cmd(sequence, regions);

	// prepare() finds no gap for the region, so no sub-commands are created
	cmd.redo_now();
	EXPECT_FALSE(cmd.has_commands());
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(a->length(), olive::core::Rational(2));

	cmd.undo_now();
	EXPECT_EQ(track->track_length(), olive::core::Rational(2));
}

//
// timelineundopointer.h
//
TEST_F(TimelineUndoTest, BlockTrimCommandTrimOutCreatesGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	// Trimming a shorter with a clip adjacent creates a gap to fill the space
	olive::BlockTrimCommand cmd(track, a, olive::core::Rational(1),
								olive::Timeline::k_trim_out);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(a->length(), olive::core::Rational(1));
	EXPECT_EQ(a->out(), olive::core::Rational(1));
	olive::Block *gap = track->blocks().at(1);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->in(), olive::core::Rational(1));
	EXPECT_EQ(gap->out(), olive::core::Rational(2));
	// b is unaffected
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, BlockTrimCommandTrimOutIntoGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	// Layout: a [0,2], gap [2,4], b [4,6]

	// Trimming a longer consumes time from the adjacent gap
	olive::BlockTrimCommand cmd(track, a, olive::core::Rational(3),
								olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(a->length(), olive::core::Rational(3));
	EXPECT_EQ(a->out(), olive::core::Rational(3));
	EXPECT_EQ(g->length(), olive::core::Rational(1));
	EXPECT_EQ(g->in(), olive::core::Rational(3));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(6));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(g->length(), olive::core::Rational(2));
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
}

TEST_F(TimelineUndoTest, BlockTrimCommandTrimOutConsumesWholeGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	// Layout: a [0,2], gap [2,4], b [4,6]

	// Trimming a longer by the exact gap length removes the gap entirely
	olive::BlockTrimCommand cmd(track, a, olive::core::Rational(4),
								olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(a->length(), olive::core::Rational(4));
	EXPECT_EQ(a->out(), olive::core::Rational(4));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
	// By default the consumed gap is removed from the graph too
	EXPECT_EQ(g->track(), nullptr);
	EXPECT_EQ(g->project(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), g);
	EXPECT_EQ(g->project(), project_.get());
	EXPECT_EQ(g->length(), olive::core::Rational(2));
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
}

TEST_F(TimelineUndoTest, BlockTrimCommandConsumedGapCanStayInGraph)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::GapBlock *g = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	// Layout: a [0,2], gap [2,4], b [4,6]

	olive::BlockTrimCommand cmd(track, a, olive::core::Rational(4),
								olive::Timeline::k_trim_out);
	cmd.set_remove_zero_length_from_graph(false);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	// The gap is off the track but remains in the graph when asked to stay
	EXPECT_EQ(g->track(), nullptr);
	EXPECT_EQ(g->project(), project_.get());

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(1), g);
	EXPECT_EQ(g->in(), olive::core::Rational(2));
	EXPECT_EQ(g->out(), olive::core::Rational(4));
	EXPECT_EQ(a->length(), olive::core::Rational(2));
}

TEST_F(TimelineUndoTest, BlockTrimCommandTrimInCreatesGap)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	// Trimming b's in point shorter with a clip before it creates a gap
	olive::BlockTrimCommand cmd(track, b, olive::core::Rational(2),
								olive::Timeline::k_trim_in);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(0), a);
	olive::Block *gap = track->blocks().at(1);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->in(), olive::core::Rational(2));
	EXPECT_EQ(gap->out(), olive::core::Rational(3));
	EXPECT_EQ(b->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(b->media_in(), olive::core::Rational(1));
	// a is unaffected
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(2));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
}

TEST_F(TimelineUndoTest, BlockTrimCommandTrimInRollEdit)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	// A roll edit extends the adjacent clip instead of creating a gap
	olive::BlockTrimCommand cmd(track, b, olive::core::Rational(2),
								olive::Timeline::k_trim_in);
	cmd.set_trim_is_a_roll_edit(true);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(a->length(), olive::core::Rational(3));
	EXPECT_EQ(a->out(), olive::core::Rational(3));
	EXPECT_EQ(b->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(b->media_in(), olive::core::Rational(1));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(b->media_in(), olive::core::Rational(0));
}

TEST_F(TimelineUndoTest, BlockTrimCommandTrimOutLastBlockHasNoAdjacent)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	// Trimming the last block's out point shorter shortens the whole track
	olive::BlockTrimCommand cmd(track, b, olive::core::Rational(1),
								olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));

	cmd.undo_now();
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, BlockTrimCommandSameLengthDoesNothing)
{
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);

	// Trimming to the current length is a no-op
	olive::BlockTrimCommand cmd(track, a, olive::core::Rational(2),
								olive::Timeline::k_trim_out);

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));

	cmd.undo_now();
	EXPECT_EQ(a->length(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
}

TEST_F(TimelineUndoTest, TrackSlideCommandShiftsGaps)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g1 = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(3));
	olive::GapBlock *g2 = create_gap(project_.get(), olive::core::Rational(2));
	track->append_block(g1);
	track->append_block(a);
	track->append_block(g2);
	// Layout: gap [0,2], a [2,5], gap [5,7]

	// Sliding a one frame later grows the leading gap and shrinks the trailing one
	olive::TrackSlideCommand cmd(track, { a }, g1, g2,
								 olive::core::Rational(1));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(g1->length(), olive::core::Rational(3));
	EXPECT_EQ(g1->out(), olive::core::Rational(3));
	EXPECT_EQ(a->in(), olive::core::Rational(3));
	EXPECT_EQ(a->out(), olive::core::Rational(6));
	EXPECT_EQ(g2->length(), olive::core::Rational(1));
	EXPECT_EQ(g2->in(), olive::core::Rational(6));
	EXPECT_EQ(g2->out(), olive::core::Rational(7));
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));

	cmd.undo_now();
	EXPECT_EQ(g1->length(), olive::core::Rational(2));
	EXPECT_EQ(a->in(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(5));
	EXPECT_EQ(g2->length(), olive::core::Rational(2));
	EXPECT_EQ(g2->in(), olive::core::Rational(5));
	EXPECT_EQ(g2->out(), olive::core::Rational(7));
}

TEST_F(TimelineUndoTest, TrackSlideCommandRemovesOutAdjacent)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g1 = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(3));
	olive::GapBlock *g2 = create_gap(project_.get(), olive::core::Rational(2));
	track->append_block(g1);
	track->append_block(a);
	track->append_block(g2);
	// Layout: gap [0,2], a [2,5], gap [5,7]

	// Sliding right by the trailing gap's length consumes it
	olive::TrackSlideCommand cmd(track, { a }, g1, g2,
								 olive::core::Rational(2));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), g1);
	EXPECT_EQ(track->blocks().at(1), a);
	EXPECT_EQ(g1->length(), olive::core::Rational(4));
	EXPECT_EQ(g1->out(), olive::core::Rational(4));
	EXPECT_EQ(a->in(), olive::core::Rational(4));
	EXPECT_EQ(a->out(), olive::core::Rational(7));
	EXPECT_EQ(g2->track(), nullptr);
	EXPECT_EQ(g2->project(), nullptr);
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(0), g1);
	EXPECT_EQ(track->blocks().at(1), a);
	EXPECT_EQ(track->blocks().at(2), g2);
	EXPECT_EQ(g2->project(), project_.get());
	EXPECT_EQ(g1->length(), olive::core::Rational(2));
	EXPECT_EQ(g1->out(), olive::core::Rational(2));
	EXPECT_EQ(a->in(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(5));
	EXPECT_EQ(g2->in(), olive::core::Rational(5));
	EXPECT_EQ(g2->out(), olive::core::Rational(7));
}

TEST_F(TimelineUndoTest, TrackSlideCommandRemovesInAdjacent)
{
	olive::Track *track = create_track(project_.get());
	olive::GapBlock *g1 = create_gap(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(3));
	olive::GapBlock *g2 = create_gap(project_.get(), olive::core::Rational(2));
	track->append_block(g1);
	track->append_block(a);
	track->append_block(g2);
	// Layout: gap [0,2], a [2,5], gap [5,7]

	// Sliding left by the leading gap's length consumes it
	olive::TrackSlideCommand cmd(track, { a }, g1, g2,
								 olive::core::Rational(-2));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), g2);
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(3));
	EXPECT_EQ(g2->length(), olive::core::Rational(4));
	EXPECT_EQ(g2->in(), olive::core::Rational(3));
	EXPECT_EQ(g2->out(), olive::core::Rational(7));
	EXPECT_EQ(g1->track(), nullptr);
	EXPECT_EQ(g1->project(), nullptr);

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(0), g1);
	EXPECT_EQ(track->blocks().at(1), a);
	EXPECT_EQ(track->blocks().at(2), g2);
	EXPECT_EQ(g1->project(), project_.get());
	EXPECT_EQ(g1->in(), olive::core::Rational(0));
	EXPECT_EQ(g1->out(), olive::core::Rational(2));
	EXPECT_EQ(a->in(), olive::core::Rational(2));
	EXPECT_EQ(a->out(), olive::core::Rational(5));
	EXPECT_EQ(g2->length(), olive::core::Rational(2));
	EXPECT_EQ(g2->in(), olive::core::Rational(5));
	EXPECT_EQ(g2->out(), olive::core::Rational(7));
}

TEST_F(TimelineUndoTest, TrackPlaceBlockCommandAppendsToEmptyTrack)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	append_track_to_list(list, track);

	olive::ClipBlock *p = create_clip(project_.get(), olive::core::Rational(2));
	olive::TrackPlaceBlockCommand cmd(list, 0, p, olive::core::Rational(0));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->blocks().at(0), p);
	EXPECT_EQ(p->in(), olive::core::Rational(0));
	EXPECT_EQ(p->out(), olive::core::Rational(2));

	cmd.undo_now();
	EXPECT_TRUE(track->blocks().isEmpty());
	EXPECT_EQ(p->track(), nullptr);
}

TEST_F(TimelineUndoTest, TrackPlaceBlockCommandInsertsGapToReachPoint)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	append_track_to_list(list, track);

	// Placing past the end of the track pads with a gap
	olive::ClipBlock *p = create_clip(project_.get(), olive::core::Rational(2));
	olive::TrackPlaceBlockCommand cmd(list, 0, p, olive::core::Rational(3));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 2);
	olive::Block *gap = track->blocks().at(0);
	EXPECT_NE(dynamic_cast<olive::GapBlock *>(gap), nullptr);
	EXPECT_EQ(gap->in(), olive::core::Rational(0));
	EXPECT_EQ(gap->out(), olive::core::Rational(3));
	EXPECT_EQ(track->blocks().at(1), p);
	EXPECT_EQ(p->in(), olive::core::Rational(3));
	EXPECT_EQ(p->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	cmd.undo_now();
	EXPECT_TRUE(track->blocks().isEmpty());
	EXPECT_EQ(track->track_length(), olive::core::Rational(0));
}

TEST_F(TimelineUndoTest, TrackPlaceBlockCommandOverwritesMiddle)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(project_.get());
	olive::ClipBlock *a = create_clip(project_.get(), olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(project_.get(), olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(project_.get(), olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	append_track_to_list(list, track);
	// Layout: a [0,2], b [2,5], c [5,6]

	// Placing a two-second block at 2 trims b's head to make room
	olive::ClipBlock *p = create_clip(project_.get(), olive::core::Rational(2));
	olive::TrackPlaceBlockCommand cmd(list, 0, p, olive::core::Rational(2));

	cmd.redo_now();
	ASSERT_EQ(track->blocks().size(), 4);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), p);
	EXPECT_EQ(p->in(), olive::core::Rational(2));
	EXPECT_EQ(p->out(), olive::core::Rational(4));
	EXPECT_EQ(track->blocks().at(2), b);
	EXPECT_EQ(b->length(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(4));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(track->blocks().at(3), c);
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(c->out(), olive::core::Rational(6));

	cmd.undo_now();
	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(p->track(), nullptr);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(b->length(), olive::core::Rational(3));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
}

TEST_F(TimelineUndoTest, TrackPlaceBlockCommandAddsMissingTracks)
{
	olive::Sequence *sequence = create_sequence(project_.get());
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	ASSERT_EQ(list->get_track_count(), 0);

	// Placing on track index 1 of an empty list creates both tracks
	olive::ClipBlock *p = create_clip(project_.get(), olive::core::Rational(2));
	olive::TrackPlaceBlockCommand cmd(list, 1, p, olive::core::Rational(0));

	cmd.redo_now();
	ASSERT_EQ(list->get_track_count(), 2);
	olive::Track *placed_track = list->get_track_at(1);
	ASSERT_NE(placed_track, nullptr);
	ASSERT_EQ(placed_track->blocks().size(), 1);
	EXPECT_EQ(placed_track->blocks().at(0), p);
	EXPECT_EQ(p->in(), olive::core::Rational(0));
	EXPECT_EQ(p->out(), olive::core::Rational(2));

	cmd.undo_now();
	EXPECT_EQ(p->track(), nullptr);
	EXPECT_EQ(list->get_track_count(), 0);
}
