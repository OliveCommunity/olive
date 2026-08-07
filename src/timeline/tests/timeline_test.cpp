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

#include "common/xmlutils.h"
#include "node/project.h"
#include "node/sequence.h"
#include "timeline/edit.h"
#include "timeline/marker.h"
#include "timeline/workarea.h"
#include "undo/undocommand.h"

#include "../../node/c_api/nodehandle.h"

namespace
{

class TimelineSequenceFixture : public ::testing::Test {
protected:
	void SetUp() override
	{
		project_ = oaknode_project_init();
		ASSERT_NE(project_.ctx, nullptr);
		sequence_ = oaknode_sequence_create();
		ASSERT_NE(sequence_.ctx, nullptr);
		node_ = oaknode_sequence_as_node(sequence_);
		OakNodeProject owner = {};
		ASSERT_EQ(oaknode_node_get_project(node_, &owner), OAKNODE_OK);
		if (!owner.ctx) {
			ASSERT_EQ(oaknode_project_add_node(project_, node_),
					  OAKNODE_OK);
		}
	}

	void TearDown() override
	{
		// The project owns the sequence and everything in its graph
		oaknode_project_free(&project_);
	}

	OakNodeProject project_ = {};
	OakNodeSequence sequence_ = {};
	OakNodeNode node_ = {};
};

// ---- marker ---------------------------------------------------------------

TEST_F(TimelineSequenceFixture, MarkerListOfReturnsList)
{
	EXPECT_NE(oaktimeline_marker_list_of(node_).ctx, nullptr);
	EXPECT_EQ(oaktimeline_marker_list_of(OakNodeNode{}).ctx, nullptr);
}

TEST_F(TimelineSequenceFixture, MarkerAddCountAtRemove)
{
	OakTimelineMarkerList list = oaktimeline_marker_list_of(node_);
	ASSERT_NE(list.ctx, nullptr);

	int count = -1;
	EXPECT_EQ(oaktimeline_marker_count(list, &count), OAKTIMELINE_OK);
	EXPECT_EQ(count, 0);
	EXPECT_EQ(oaktimeline_marker_count(OakTimelineMarkerList{}, &count),
			  OAKTIMELINE_E_INVALID);
	EXPECT_EQ(oaktimeline_marker_count(list, nullptr),
			  OAKTIMELINE_E_INVALID);

	OakUndoCommand cmd =
		oaktimeline_marker_add_command(list, 1, 2, 3, 4, "mark", 5);
	ASSERT_NE(cmd.ctx, nullptr);
	oakundo_command_redo_now(cmd);
	oakundo_command_free(&cmd);

	EXPECT_EQ(oaktimeline_marker_count(list, &count), OAKTIMELINE_OK);
	ASSERT_EQ(count, 1);

	int in_n = 0, in_d = 0, out_n = 0, out_d = 0, color = 0;
	char name[32];
	int needed = oaktimeline_marker_at(list, 0, &in_n, &in_d, &out_n,
									   &out_d, &color, name, sizeof(name));
	ASSERT_GT(needed, 0);
	EXPECT_EQ(in_n, 1);
	EXPECT_EQ(in_d, 2);
	EXPECT_EQ(out_n, 3);
	EXPECT_EQ(out_d, 4);
	EXPECT_EQ(color, 5);
	EXPECT_STREQ(name, "mark");

	EXPECT_EQ(oaktimeline_marker_at(list, 9, &in_n, &in_d, &out_n,
									&out_d, &color, name, sizeof(name)),
			  OAKTIMELINE_E_NOT_FOUND);
	EXPECT_EQ(oaktimeline_marker_at(OakTimelineMarkerList{}, 0, &in_n,
									&in_d, &out_n, &out_d, &color, name,
									sizeof(name)),
			  OAKTIMELINE_E_INVALID);

	OakUndoCommand rm = oaktimeline_marker_remove_at_command(list, 0);
	ASSERT_NE(rm.ctx, nullptr);
	oakundo_command_redo_now(rm);
	oakundo_command_free(&rm);

	EXPECT_EQ(oaktimeline_marker_count(list, &count), OAKTIMELINE_OK);
	EXPECT_EQ(count, 0);

	EXPECT_EQ(oaktimeline_marker_remove_at_command(list, 4).ctx, nullptr);
}

TEST_F(TimelineSequenceFixture, MarkerSetTimeAndPropsUndo)
{
	OakTimelineMarkerList list = oaktimeline_marker_list_of(node_);
	ASSERT_NE(list.ctx, nullptr);

	OakUndoCommand add =
		oaktimeline_marker_add_command(list, 0, 1, 1, 1, "a", 1);
	ASSERT_NE(add.ctx, nullptr);
	oakundo_command_redo_now(add);
	oakundo_command_free(&add);

	OakUndoCommand set_time =
		oaktimeline_marker_set_time_command(list, 0, 10, 1, 20, 1);
	ASSERT_NE(set_time.ctx, nullptr);
	oakundo_command_redo_now(set_time);

	int in_n = 0;
	EXPECT_GT(oaktimeline_marker_at(list, 0, &in_n, NULL, NULL, NULL,
									NULL, NULL, 0),
			  0);
	EXPECT_EQ(in_n, 10);

	oakundo_command_undo_now(set_time);
	EXPECT_GT(oaktimeline_marker_at(list, 0, &in_n, NULL, NULL, NULL,
									NULL, NULL, 0),
			  0);
	EXPECT_EQ(in_n, 0);
	oakundo_command_free(&set_time);

	OakUndoCommand props =
		oaktimeline_marker_set_props_command(list, 0, 7, "renamed");
	ASSERT_NE(props.ctx, nullptr);
	oakundo_command_redo_now(props);
	oakundo_command_free(&props);

	int color = 0;
	char name[32];
	EXPECT_GT(oaktimeline_marker_at(list, 0, NULL, NULL, NULL, NULL,
									&color, name, sizeof(name)),
			  0);
	EXPECT_EQ(color, 7);
	EXPECT_STREQ(name, "renamed");

	EXPECT_EQ(oaktimeline_marker_set_props_command(list, 0, -1, NULL)
				  .ctx,
			  nullptr);
	EXPECT_EQ(oaktimeline_marker_set_time_command(list, 9, 0, 1, 1, 1)
				  .ctx,
			  nullptr);
}

TEST_F(TimelineSequenceFixture, MarkerListXmlRoundTrip)
{
	OakTimelineMarkerList list = oaktimeline_marker_list_of(node_);
	ASSERT_NE(list.ctx, nullptr);

	OakUndoCommand add =
		oaktimeline_marker_add_command(list, 1, 3, 2, 3, "xml", 4);
	ASSERT_NE(add.ctx, nullptr);
	oakundo_command_redo_now(add);
	oakundo_command_free(&add);

	OakXmlWriter writer = oakcommon_xml_writer_init();
	ASSERT_NE(writer.ctx, nullptr);
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(writer, "markers"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oaktimeline_marker_list_save(list, writer), OAKTIMELINE_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_end_element(writer),
			  OAKCOMMON_OK);

	char xml[1024];
	int needed = oakcommon_xml_writer_output(writer, xml, sizeof(xml));
	ASSERT_GT(needed, 0);
	ASSERT_LT(needed, int(sizeof(xml)));
	oakcommon_xml_writer_free(&writer);

	// Load into a fresh sequence's marker list
	OakNodeSequence seq2 = oaknode_sequence_create();
	ASSERT_NE(seq2.ctx, nullptr);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_sequence_as_node(seq2)),
			  OAKNODE_OK);
	OakTimelineMarkerList list2 =
		oaktimeline_marker_list_of(oaknode_sequence_as_node(seq2));
	ASSERT_NE(list2.ctx, nullptr);

	OakXmlReader reader = oakcommon_xml_reader_init(xml);
	ASSERT_NE(reader.ctx, nullptr);
	int start = 0;
	ASSERT_EQ(oakcommon_xml_reader_read_next_start_element(reader, &start),
			  OAKCOMMON_OK);
	ASSERT_NE(start, 0);
	EXPECT_EQ(oaktimeline_marker_list_load(list2, reader), OAKTIMELINE_OK);
	oakcommon_xml_reader_free(&reader);

	int count = 0;
	EXPECT_EQ(oaktimeline_marker_count(list2, &count), OAKTIMELINE_OK);
	ASSERT_EQ(count, 1);

	int color = 0;
	char name[32];
	EXPECT_GT(oaktimeline_marker_at(list2, 0, NULL, NULL, NULL, NULL,
									&color, name, sizeof(name)),
			  0);
	EXPECT_EQ(color, 4);
	EXPECT_STREQ(name, "xml");

	EXPECT_EQ(oaktimeline_marker_list_load(OakTimelineMarkerList{}, reader),
			  OAKTIMELINE_E_INVALID);

}

// ---- workarea -------------------------------------------------------------

TEST_F(TimelineSequenceFixture, WorkareaGetSetLive)
{
	OakTimelineWorkArea w = oaktimeline_workarea_of(node_);
	ASSERT_NE(w.ctx, nullptr);
	EXPECT_EQ(oaktimeline_workarea_of(OakNodeNode{}).ctx, nullptr);

	int in_n = 0, in_d = 0, out_n = 0, out_d = 0, enabled = -1;
	EXPECT_EQ(oaktimeline_workarea_get(w, &in_n, &in_d, &out_n, &out_d,
									   &enabled),
			  OAKTIMELINE_OK);
	EXPECT_EQ(enabled, 0);

	EXPECT_EQ(oaktimeline_workarea_set_range(w, 1, 4, 3, 4),
			  OAKTIMELINE_OK);
	EXPECT_EQ(oaktimeline_workarea_get(w, &in_n, &in_d, &out_n, &out_d,
									   &enabled),
			  OAKTIMELINE_OK);
	EXPECT_EQ(in_n, 1);
	EXPECT_EQ(in_d, 4);
	EXPECT_EQ(out_n, 3);
	EXPECT_EQ(out_d, 4);

	EXPECT_EQ(oaktimeline_workarea_set_range(OakTimelineWorkArea{}, 0, 1, 1,
											 1),
			  OAKTIMELINE_E_INVALID);
	EXPECT_EQ(oaktimeline_workarea_get(OakTimelineWorkArea{}, &in_n, &in_d,
									   &out_n, &out_d, &enabled),
			  OAKTIMELINE_E_INVALID);
}

TEST_F(TimelineSequenceFixture, WorkareaUndoCommands)
{
	OakTimelineWorkArea w = oaktimeline_workarea_of(node_);
	ASSERT_NE(w.ctx, nullptr);

	OakUndoCommand range_cmd = oaktimeline_workarea_set_range_command(
		w, 1, 2, 1, 1, 0, 1, 1, 1);
	ASSERT_NE(range_cmd.ctx, nullptr);
	oakundo_command_redo_now(range_cmd);

	int in_n = 0, out_n = 0;
	EXPECT_EQ(oaktimeline_workarea_get(w, &in_n, NULL, &out_n, NULL, NULL),
			  OAKTIMELINE_OK);
	EXPECT_EQ(in_n, 1);
	EXPECT_EQ(out_n, 1);

	oakundo_command_undo_now(range_cmd);
	EXPECT_EQ(oaktimeline_workarea_get(w, &in_n, NULL, &out_n, NULL, NULL),
			  OAKTIMELINE_OK);
	EXPECT_EQ(in_n, 0);
	oakundo_command_free(&range_cmd);

	OakUndoCommand enable_cmd =
		oaktimeline_workarea_set_enabled_command(w, 1);
	ASSERT_NE(enable_cmd.ctx, nullptr);
	oakundo_command_redo_now(enable_cmd);

	int enabled = 0;
	EXPECT_EQ(oaktimeline_workarea_get(w, NULL, NULL, NULL, NULL,
									   &enabled),
			  OAKTIMELINE_OK);
	EXPECT_EQ(enabled, 1);

	oakundo_command_undo_now(enable_cmd);
	EXPECT_EQ(oaktimeline_workarea_get(w, NULL, NULL, NULL, NULL,
									   &enabled),
			  OAKTIMELINE_OK);
	EXPECT_EQ(enabled, 0);
	oakundo_command_free(&enable_cmd);

	EXPECT_EQ(oaktimeline_workarea_set_range_command(OakTimelineWorkArea{},
													 0, 1, 1, 1, 0, 1, 1, 1)
				  .ctx,
			  nullptr);
	EXPECT_EQ(oaktimeline_workarea_set_enabled_command(OakTimelineWorkArea{},
													   1)
				  .ctx,
			  nullptr);
}

TEST_F(TimelineSequenceFixture, WorkareaResetSentinels)
{
	int in_n = 0, in_d = 0, out_n = 0, out_d = 0;
	EXPECT_EQ(oaktimeline_workarea_reset(&in_n, &in_d, &out_n, &out_d),
			  OAKTIMELINE_OK);
	EXPECT_EQ(in_n, 0);
	EXPECT_EQ(out_n, INT32_MAX);

	EXPECT_EQ(oaktimeline_workarea_reset(nullptr, &in_d, &out_n, &out_d),
			  OAKTIMELINE_E_INVALID);
}

TEST_F(TimelineSequenceFixture, WorkareaXmlRoundTrip)
{
	OakTimelineWorkArea w = oaktimeline_workarea_of(node_);
	ASSERT_NE(w.ctx, nullptr);
	EXPECT_EQ(oaktimeline_workarea_set_range(w, 1, 3, 2, 3),
			  OAKTIMELINE_OK);

	OakXmlWriter writer = oakcommon_xml_writer_init();
	ASSERT_NE(writer.ctx, nullptr);
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(writer, "workarea"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oaktimeline_workarea_save(w, writer), OAKTIMELINE_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_end_element(writer),
			  OAKCOMMON_OK);

	char xml[1024];
	int needed = oakcommon_xml_writer_output(writer, xml, sizeof(xml));
	ASSERT_GT(needed, 0);
	ASSERT_LT(needed, int(sizeof(xml)));
	oakcommon_xml_writer_free(&writer);

	OakNodeSequence seq2 = oaknode_sequence_create();
	ASSERT_NE(seq2.ctx, nullptr);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_sequence_as_node(seq2)),
			  OAKNODE_OK);
	OakTimelineWorkArea w2 =
		oaktimeline_workarea_of(oaknode_sequence_as_node(seq2));
	ASSERT_NE(w2.ctx, nullptr);

	OakXmlReader reader = oakcommon_xml_reader_init(xml);
	ASSERT_NE(reader.ctx, nullptr);
	int start = 0;
	ASSERT_EQ(oakcommon_xml_reader_read_next_start_element(reader, &start),
			  OAKCOMMON_OK);
	ASSERT_NE(start, 0);
	EXPECT_EQ(oaktimeline_workarea_load(w2, reader), OAKTIMELINE_OK);
	oakcommon_xml_reader_free(&reader);

	int in_n = 0, out_n = 0;
	EXPECT_EQ(oaktimeline_workarea_get(w2, &in_n, NULL, &out_n, NULL,
									   NULL),
			  OAKTIMELINE_OK);
	EXPECT_EQ(in_n, 1);
	EXPECT_EQ(out_n, 2);

}

// ---- edit -----------------------------------------------------------------

TEST_F(TimelineSequenceFixture, AddAndRemoveTrackCommands)
{
	OakNodeTrackList list = {};
	ASSERT_EQ(oaknode_sequence_get_track_list(
				  sequence_, OAKNODE_TRACK_TYPE_VIDEO, &list),
			  OAKNODE_OK);
	ASSERT_NE(list.ctx, nullptr);

	int count = -1;
	EXPECT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
	const int before = count;

	OakUndoCommand add = oaktimeline_add_track_command(list);
	ASSERT_NE(add.ctx, nullptr);
	oakundo_command_redo_now(add);

	EXPECT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
	EXPECT_EQ(count, before + 1);

	OakNodeTrack track = {};
	EXPECT_EQ(oaknode_tracklist_get_track_at(list, before, &track),
			  OAKNODE_OK);
	ASSERT_NE(track.ctx, nullptr);

	OakUndoCommand rm = oaktimeline_remove_track_command(track);
	ASSERT_NE(rm.ctx, nullptr);
	oakundo_command_redo_now(rm);

	EXPECT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
	EXPECT_EQ(count, before);

	oakundo_command_undo_now(rm);
	EXPECT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
	EXPECT_EQ(count, before + 1);

	oakundo_command_undo_now(add);
	EXPECT_EQ(oaknode_tracklist_get_track_count(list, &count), OAKNODE_OK);
	EXPECT_EQ(count, before);

	oakundo_command_free(&add);
	oakundo_command_free(&rm);

	EXPECT_EQ(oaktimeline_add_track_command(OakNodeTrackList{}).ctx, nullptr);
	EXPECT_EQ(oaktimeline_remove_track_command(OakNodeTrack{}).ctx, nullptr);
}

TEST_F(TimelineSequenceFixture, PlaceTrimSplitRemoveAreaCommands)
{
	OakNodeTrackList list = {};
	ASSERT_EQ(oaknode_sequence_get_track_list(
				  sequence_, OAKNODE_TRACK_TYPE_VIDEO, &list),
			  OAKNODE_OK);

	OakNodeTrack track = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(track.ctx, nullptr);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_track_as_node(track)),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_tracklist_add_track(list, track), OAKNODE_OK);

	OakNodeBlock clip = oaknode_block_clip_create();
	ASSERT_NE(clip.ctx, nullptr);
	EXPECT_EQ(oaknode_block_set_length_and_media_out(clip, 10, 1),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_block_as_node(clip)),
			  OAKNODE_OK);

	OakUndoCommand place =
		oaktimeline_place_block_command(list, 0, clip, 0, 1);
	ASSERT_NE(place.ctx, nullptr);
	oakundo_command_redo_now(place);

	int block_count = 0;
	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	ASSERT_EQ(block_count, 1);

	// Trim out to length 4
	OakUndoCommand trim = oaktimeline_trim_command(
		track, clip, 4, 1, OAKTIMELINE_MOVEMENT_TRIM_OUT);
	ASSERT_NE(trim.ctx, nullptr);
	oakundo_command_redo_now(trim);

	int n = 0, d = 0;
	EXPECT_EQ(oaknode_block_get_length(clip, &n, &d), OAKNODE_OK);
	EXPECT_EQ(n, 4);

	oakundo_command_undo_now(trim);
	EXPECT_EQ(oaknode_block_get_length(clip, &n, &d), OAKNODE_OK);
	EXPECT_EQ(n, 10);
	oakundo_command_free(&trim);

	// Split at 5: two blocks of 5
	OakNodeBlock blocks[] = { clip };
	OakUndoCommand split =
		oaktimeline_split_command(blocks, 1, 5, 1);
	ASSERT_NE(split.ctx, nullptr);
	oakundo_command_redo_now(split);

	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	ASSERT_EQ(block_count, 2);

	oakundo_command_undo_now(split);
	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	ASSERT_EQ(block_count, 1);
	oakundo_command_free(&split);

	// Ripple remove [3..5] then undo
	OakUndoCommand remove_area =
		oaktimeline_ripple_remove_area_command(track, 3, 1, 5, 1);
	ASSERT_NE(remove_area.ctx, nullptr);
	oakundo_command_redo_now(remove_area);

	// Removing [3..5] from [0..10] splices it into [0..3] + [5..10]
	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	ASSERT_EQ(block_count, 2);
	EXPECT_EQ(oaknode_block_get_length(clip, &n, &d), OAKNODE_OK);
	EXPECT_EQ(n, 3);

	OakNodeBlock second = {};
	EXPECT_EQ(oaknode_track_get_block_at(track, 1, &second), OAKNODE_OK);
	ASSERT_NE(second.ctx, nullptr);
	EXPECT_EQ(oaknode_block_get_length(second, &n, &d), OAKNODE_OK);
	EXPECT_EQ(n, 5);

	oakundo_command_undo_now(remove_area);
	EXPECT_EQ(oaknode_block_get_length(clip, &n, &d), OAKNODE_OK);
	EXPECT_EQ(n, 10);
	oakundo_command_free(&remove_area);

	oakundo_command_undo_now(place);
	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	EXPECT_EQ(block_count, 0);
	oakundo_command_free(&place);

	EXPECT_EQ(oaktimeline_place_block_command(OakNodeTrackList{}, 0, clip, 0, 1)
				  .ctx,
			  nullptr);
	EXPECT_EQ(oaktimeline_trim_command(track, clip, 1, 1, 99).ctx, nullptr);
	EXPECT_EQ(oaktimeline_split_command(blocks, 0, 1, 1).ctx, nullptr);
	EXPECT_EQ(oaktimeline_ripple_remove_area_command(OakNodeTrack{}, 0, 1, 1, 1)
				  .ctx,
			  nullptr);
}

TEST_F(TimelineSequenceFixture, ReplaceWithGapCommand)
{
	OakNodeTrackList list = {};
	ASSERT_EQ(oaknode_sequence_get_track_list(
				  sequence_, OAKNODE_TRACK_TYPE_VIDEO, &list),
			  OAKNODE_OK);

	OakNodeTrack track = oaknode_track_create(OAKNODE_TRACK_TYPE_VIDEO);
	ASSERT_NE(track.ctx, nullptr);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_track_as_node(track)),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_tracklist_add_track(list, track), OAKNODE_OK);

	OakNodeBlock clip = oaknode_block_clip_create();
	ASSERT_NE(clip.ctx, nullptr);
	EXPECT_EQ(oaknode_block_set_length_and_media_out(clip, 6, 1),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_block_as_node(clip)),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_track_append_block(track, clip), OAKNODE_OK);

	// Second clip so the first has a next (gap required)
	OakNodeBlock clip2 = oaknode_block_clip_create();
	ASSERT_NE(clip2.ctx, nullptr);
	EXPECT_EQ(oaknode_block_set_length_and_media_out(clip2, 6, 1),
			  OAKNODE_OK);
	ASSERT_EQ(oaknode_project_add_node(project_,
									 oaknode_block_as_node(clip2)),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_track_append_block(track, clip2), OAKNODE_OK);

	OakUndoCommand replace =
		oaktimeline_replace_block_with_gap_command(track, clip);
	ASSERT_NE(replace.ctx, nullptr);
	oakundo_command_redo_now(replace);

	int block_count = 0;
	EXPECT_EQ(oaknode_track_get_block_count(track, &block_count),
			  OAKNODE_OK);
	ASSERT_EQ(block_count, 2);

	OakNodeBlock first = {};
	EXPECT_EQ(oaknode_track_get_block_at(track, 0, &first), OAKNODE_OK);
	int kind = OAKNODE_BLOCK_OTHER;
	EXPECT_EQ(oaknode_block_get_kind(first, &kind), OAKNODE_OK);
	EXPECT_EQ(kind, OAKNODE_BLOCK_GAP);

	oakundo_command_undo_now(replace);
	EXPECT_EQ(oaknode_track_get_block_at(track, 0, &first), OAKNODE_OK);
	// Borrowed handles get a fresh box per call, so compare the wrapped
	// native objects
	EXPECT_EQ(oaknode_c_api::to_native<void>(first),
			  oaknode_c_api::to_native<void>(clip));
	oakundo_command_free(&replace);

	EXPECT_EQ(oaktimeline_replace_block_with_gap_command(OakNodeTrack{}, clip)
				  .ctx,
			  nullptr);

	EXPECT_EQ(oaknode_track_ripple_remove_block(track, clip), OAKNODE_OK);
	EXPECT_EQ(oaknode_track_ripple_remove_block(track, clip2), OAKNODE_OK);
}

TEST_F(TimelineSequenceFixture, SlideAndInsertGapsAndRippleDeleteGapsFactories)
{
	OakNodeBlock clip = oaknode_block_clip_create();
	EXPECT_EQ(oaktimeline_slide_command(OakNodeTrack{}, &clip, 1,
										OakNodeBlock{}, OakNodeBlock{}, 1, 1)
				  .ctx,
			  nullptr);

	OakNodeTrackList list = {};
	ASSERT_EQ(oaknode_sequence_get_track_list(
				  sequence_, OAKNODE_TRACK_TYPE_VIDEO, &list),
			  OAKNODE_OK);
	EXPECT_EQ(oaktimeline_insert_gaps_command(OakNodeTrackList{}, 0, 1, 1, 1).ctx,
			  nullptr);

	EXPECT_EQ(oaktimeline_ripple_delete_gaps_command(
				  sequence_, nullptr, nullptr, nullptr, nullptr, nullptr, 0)
				  .ctx,
			  nullptr);

	oaknode_block_free(&clip);
}

} // namespace
