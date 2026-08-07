/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "timelineundogeneral.h"

#include <vector>

#include "common/config.h"
#include "node/factory.h"
#include "node/node.h"
#include "node/project.h"
#include "timelineundocommon.h"
#include "timelineundotrack.h"
#include "timelineutil.h"
#include "undo/undocommand.h"

namespace olive
{

namespace
{

const char *k_merge_node_id = "org.olivevideoeditor.Olive.merge";
const char *k_math_node_id = "org.olivevideoeditor.Olive.math";
const char *k_merge_base_input = "base_in";
const char *k_merge_blend_input = "blend_in";
const char *k_math_param_a_input = "param_a_in";
const char *k_math_param_b_input = "param_b_in";

void add_multi_child(OakUndoCommand multi, OakUndoCommand child)
{
	if (multi.ctx && child.ctx) {
		oakundo_command_multi_add_child(multi, child);
	}
}

} // namespace

//
// BlockResizeCommand
//
void BlockResizeCommand::redo()
{
	old_length_ = block_length(block_);
	block_set_length_and_media_out(block_, new_length_);
}

void BlockResizeCommand::undo()
{
	block_set_length_and_media_out(block_, old_length_);
}

//
// BlockResizeWithMediaInCommand
//
void BlockResizeWithMediaInCommand::redo()
{
	old_length_ = block_length(block_);
	block_set_length_and_media_in(block_, new_length_);
}

void BlockResizeWithMediaInCommand::undo()
{
	block_set_length_and_media_in(block_, old_length_);
}

//
// BlockSetMediaInCommand
//
void BlockSetMediaInCommand::redo()
{
	int n, d;
	oaknode_clip_get_media_in(block_, &n, &d);
	old_media_in_ = Rational(n, d);
	rat_nd(new_media_in_, &n, &d);
	oaknode_clip_set_media_in(block_, n, d);
}

void BlockSetMediaInCommand::undo()
{
	int n, d;
	rat_nd(old_media_in_, &n, &d);
	oaknode_clip_set_media_in(block_, n, d);
}

//
// TimelineAddTrackCommand
//
TimelineAddTrackCommand::TimelineAddTrackCommand(OakNodeTrackList timeline)
	: TimelineAddTrackCommand(
		timeline,
		oakcommon_config_get_bool(NULL, "AutoMergeTracks", 0) != 0)
{
}

TimelineAddTrackCommand::TimelineAddTrackCommand(
	OakNodeTrackList timeline, bool automerge_tracks)
	: timeline_(timeline)
	, track_{}
	, merge_{}
	, position_command_({})
	, automerge_tracks_(automerge_tracks)
	, track_orphaned_(false)
	, merge_orphaned_(false)
{
	// Create new track
	int type = OAKNODE_TRACK_TYPE_NONE;
	oaknode_tracklist_get_type(timeline_, &type);
	track_ = oaknode_track_create(type);
	track_orphaned_ = true;

	// Determine what input to connect it to
	if (type == OAKNODE_TRACK_TYPE_VIDEO) {
		direct_input_ = OAKNODE_SEQUENCE_TEXTURE_INPUT;
	} else if (type == OAKNODE_TRACK_TYPE_AUDIO) {
		direct_input_ = OAKNODE_SEQUENCE_SAMPLES_INPUT;
	}

	// If we have an input to connect to, check if something is already connected
	if (!direct_input_.empty() && automerge_tracks_) {
		OakNodeSequence sequence = {};
		oaknode_tracklist_get_sequence(timeline_, &sequence);

		int connected = 0;
		oaknode_node_input_is_connected(oaknode_sequence_as_node(sequence),
										direct_input_.c_str(), &connected);
		if (connected) {
			if (type == OAKNODE_TRACK_TYPE_VIDEO) {
				// Use merge for video
				merge_ = oaknode_factory_create_from_id(k_merge_node_id);
				base_input_ = k_merge_base_input;
				blend_input_ = k_merge_blend_input;
			} else if (type == OAKNODE_TRACK_TYPE_AUDIO) {
				// Use math (add) for audio
				merge_ = oaknode_factory_create_from_id(k_math_node_id);
				base_input_ = k_math_param_a_input;
				blend_input_ = k_math_param_b_input;
			}

			if (merge_.ctx) {
				merge_orphaned_ = true;
			}
		}
	}
}

TimelineAddTrackCommand::~TimelineAddTrackCommand()
{
	if (position_command_.ctx) {
		oakundo_command_free(&position_command_);
	}
	if (track_orphaned_) {
		free_detached_handle(&track_);
	}
	if (merge_orphaned_) {
		free_detached_handle(&merge_);
	}
}

void TimelineAddTrackCommand::redo()
{
	// Get sequence
	OakNodeSequence sequence = {};
	oaknode_tracklist_get_sequence(timeline_, &sequence);
	OakNodeNode sequence_node = oaknode_sequence_as_node(sequence);
	OakNodeNode track_node = oaknode_track_as_node(track_);

	OakNodeProject project = {};
	oaknode_node_get_project(sequence_node, &project);

	// Add track to sequence's graph
	if (project.ctx) {
		oaknode_project_add_node(project, track_node);
	}
	track_orphaned_ = false;

	int track_count = 0;
	oaknode_tracklist_get_track_count(timeline_, &track_count);
	if (track_count > 0) {
		OakNodeTrack last = {};
		oaknode_tracklist_get_track_at(timeline_, track_count - 1, &last);
		double height = 0;
		if (last.ctx && oaknode_track_get_height(last, &height) == OAKNODE_OK) {
			oaknode_track_set_height(track_, height);
		}
	}

	char input_id[64];
	oaknode_tracklist_get_track_input_id(timeline_, input_id,
										 sizeof(input_id));

	oaknode_tracklist_array_append(timeline_);
	int array_size = 0;
	oaknode_tracklist_get_array_size(timeline_, &array_size);
	oaknode_node_connect_element(track_node, sequence_node, input_id,
								 array_size - 1);

	int type = OAKNODE_TRACK_TYPE_NONE;
	oaknode_tracklist_get_type(timeline_, &type);

	double position_factor = 0.5;
	if (type == OAKNODE_TRACK_TYPE_VIDEO) {
		position_factor = -position_factor;
	}
	bool create_pos_command =
		(!position_command_.ctx && (type == OAKNODE_TRACK_TYPE_VIDEO ||
								type == OAKNODE_TRACK_TYPE_AUDIO));
	if (create_pos_command) {
		position_command_ = oakundo_command_init_multi();
	}

	// Add merge if applicable
	if (merge_.ctx) {
		// Determine what was previously connected
		OakNodeNode previous_connection = {};
		oaknode_node_input_get_connected_node(sequence_node,
											  direct_input_.c_str(),
											  &previous_connection);

		// Add merge to graph
		if (project.ctx) {
			oaknode_project_add_node(project, merge_);
		}
		merge_orphaned_ = false;

		// Connect merge between what used to be here
		oaknode_node_disconnect(sequence_node, direct_input_.c_str());
		oaknode_node_connect(merge_, sequence_node, direct_input_.c_str());
		if (previous_connection.ctx) {
			oaknode_node_connect(previous_connection, merge_,
								 base_input_.c_str());
		}
		oaknode_node_connect(track_node, merge_, blend_input_.c_str());

		if (create_pos_command && position_command_.ctx) {
			double sx = 0, sy = 0;
			oaknode_node_get_context_position(sequence_node, sequence_node,
											  &sx, &sy, NULL);

			OakUndoCommand child = {};
			if (oaknode_node_set_context_position_undoable(
					track_node, sequence_node, sx - 1,
					sy - position_factor, 0,
					&child) == OAKNODE_OK) {
				add_multi_child(position_command_, child);
			}
			child = OakUndoCommand{};
			if (oaknode_node_set_context_position_undoable(
					merge_, sequence_node, sx, sy, 0, &child) == OAKNODE_OK) {
				add_multi_child(position_command_, child);
			}

			int count_for_pos = 0;
			oaknode_tracklist_get_track_count(timeline_, &count_for_pos);
			child = oaknode_command_create_set_position_recursive(
				merge_, sequence_node, sx - 1,
				sy + position_factor * count_for_pos);
			add_multi_child(position_command_, child);
		}
	} else if (!direct_input_.empty()) {
		int connected = 0;
		oaknode_node_input_is_connected(sequence_node, direct_input_.c_str(),
										&connected);
		if (!connected) {
			// If no merge, we have a direct connection, and nothing else is
			// connected, connect this
			oaknode_node_connect(track_node, sequence_node,
								 direct_input_.c_str());

			if (create_pos_command && position_command_.ctx) {
				// Just position directly next to the context node
				double sx = 0, sy = 0;
				oaknode_node_get_context_position(sequence_node,
												  sequence_node, &sx, &sy,
												  NULL);

				OakUndoCommand child = {};
				if (oaknode_node_set_context_position_undoable(
						track_node, sequence_node, sx - 1,
						sy + position_factor, 0,
						&child) == OAKNODE_OK) {
					add_multi_child(position_command_, child);
				}
			}
		}
	}

	// Run position command if we created one
	if (position_command_.ctx) {
		oakundo_command_redo_now(position_command_);
	}
}

void TimelineAddTrackCommand::undo()
{
	if (position_command_.ctx) {
		oakundo_command_undo_now(position_command_);
	}

	OakNodeSequence sequence = {};
	oaknode_tracklist_get_sequence(timeline_, &sequence);
	OakNodeNode sequence_node = oaknode_sequence_as_node(sequence);
	OakNodeNode track_node = oaknode_track_as_node(track_);

	OakNodeProject project = {};
	oaknode_node_get_project(sequence_node, &project);

	// Remove merge if applicable
	if (merge_.ctx) {
		OakNodeNode previous_connection = {};
		oaknode_node_input_get_connected_node(merge_, base_input_.c_str(),
											  &previous_connection);

		oaknode_node_disconnect(merge_, blend_input_.c_str());
		oaknode_node_disconnect(merge_, base_input_.c_str());
		oaknode_node_disconnect(sequence_node, direct_input_.c_str());
		if (previous_connection.ctx) {
			oaknode_node_connect(previous_connection, sequence_node,
								 direct_input_.c_str());
		}

		if (project.ctx) {
			oaknode_project_remove_node(project, merge_);
		}
		merge_orphaned_ = true;
	} else if (!direct_input_.empty()) {
		OakNodeNode connected_output = {};
		oaknode_node_input_get_connected_node(sequence_node,
											  direct_input_.c_str(),
											  &connected_output);
		if (same_node(connected_output, track_node)) {
			oaknode_node_disconnect(sequence_node, direct_input_.c_str());
		}
	}

	// Remove track
	char input_id[64];
	oaknode_tracklist_get_track_input_id(timeline_, input_id,
										 sizeof(input_id));
	int array_size = 0;
	oaknode_tracklist_get_array_size(timeline_, &array_size);
	oaknode_node_disconnect_element(sequence_node, input_id, array_size - 1);
	oaknode_tracklist_array_remove_last(timeline_);

	if (project.ctx) {
		oaknode_project_remove_node(project, track_node);
	}
	track_orphaned_ = true;
}

//
// TransitionRemoveCommand
//
TransitionRemoveCommand::~TransitionRemoveCommand()
{
	if (remove_command_.ctx) {
		free_command_handle(&remove_command_);
	}
}

void TransitionRemoveCommand::redo()
{
	oaknode_block_get_track(block_, &track_);
	oaknode_transition_get_connected_out_block(block_, &out_block_);
	oaknode_transition_get_connected_in_block(block_, &in_block_);

	int n, d;

	if (in_block_.ctx) {
		oaknode_transition_get_in_offset(block_, &n, &d);
		block_set_length_and_media_in(
			in_block_, block_length(in_block_) + Rational(n, d));
	}

	if (out_block_.ctx) {
		oaknode_transition_get_out_offset(block_, &n, &d);
		block_set_length_and_media_out(
			out_block_, block_length(out_block_) + Rational(n, d));
	}

	if (in_block_.ctx) {
		oaknode_node_disconnect(oaknode_block_as_node(block_),
								OAKNODE_TRANSITION_IN_BLOCK_INPUT);
	}

	if (out_block_.ctx) {
		oaknode_node_disconnect(oaknode_block_as_node(block_),
								OAKNODE_TRANSITION_OUT_BLOCK_INPUT);
	}

	oaknode_track_ripple_remove_block(track_, block_);

	if (remove_from_graph_) {
		if (!remove_command_.ctx) {
			remove_command_ = create_remove_command(block_);
		}

		oakundo_command_redo_now(remove_command_);
	}
}

void TransitionRemoveCommand::undo()
{
	if (remove_from_graph_) {
		oakundo_command_undo_now(remove_command_);
	}

	if (in_block_.ctx) {
		oaknode_track_insert_block_before(track_, block_, in_block_);
	} else {
		oaknode_track_insert_block_after(track_, block_, out_block_);
	}

	if (in_block_.ctx) {
		oaknode_node_connect(oaknode_block_as_node(in_block_),
							 oaknode_block_as_node(block_),
							 OAKNODE_TRANSITION_IN_BLOCK_INPUT);
	}

	if (out_block_.ctx) {
		oaknode_node_connect(oaknode_block_as_node(out_block_),
							 oaknode_block_as_node(block_),
							 OAKNODE_TRANSITION_OUT_BLOCK_INPUT);
	}

	int n, d;

	// These if statements must be separated because in_offset and out_offset report different things
	// if only one block is connected vs two. So we have to connect the blocks first before we have
	// an accurate return value from these offset functions.
	if (in_block_.ctx) {
		oaknode_transition_get_in_offset(block_, &n, &d);
		block_set_length_and_media_in(
			in_block_, block_length(in_block_) - Rational(n, d));
	}

	if (out_block_.ctx) {
		oaknode_transition_get_out_offset(block_, &n, &d);
		block_set_length_and_media_out(
			out_block_, block_length(out_block_) - Rational(n, d));
	}
}

//
// TrackListInsertGaps
//
TrackListInsertGaps::~TrackListInsertGaps()
{
	delete split_command_;
	for (AddGap &add_gap : gaps_added_) {
		if (add_gap.orphaned) {
			free_detached_handle(&add_gap.gap);
		}
	}
}

void TrackListInsertGaps::prepare()
{
	// Determine if all tracks will be affected, which will allow us to make some optimizations
	int track_count = 0;
	oaknode_tracklist_get_track_count(track_list_, &track_count);
	for (int i = 0; i < track_count; i++) {
		OakNodeTrack track = {};
		oaknode_tracklist_get_track_at(track_list_, i, &track);
		if (!track.ctx) {
			continue;
		}

		int locked = 0;
		oaknode_track_get_locked(track, &locked);
		if (locked) {
			continue;
		}

		working_tracks_.push_back(track);
	}

	std::vector<OakNodeBlock> blocks_to_split;
	std::vector<OakNodeBlock> blocks_to_append_gap_to;
	std::vector<OakNodeTrack> tracks_to_append_gap_to;

	for (OakNodeTrack track : working_tracks_) {
		int block_count = 0;
		oaknode_track_get_block_count(track, &block_count);
		for (int i = 0; i < block_count; i++) {
			OakNodeBlock b = {};
			oaknode_track_get_block_at(track, i, &b);
			if (!b.ctx) {
				continue;
			}

			int kind = OAKNODE_BLOCK_OTHER;
			oaknode_block_get_kind(b, &kind);

			if (kind == OAKNODE_BLOCK_GAP && block_in(b) <= point_ &&
				block_out(b) >= point_) {
				// Found a gap at the location
				gaps_to_extend_.push_back(b);
				break;
			} else if (kind == OAKNODE_BLOCK_CLIP && block_out(b) >= point_) {
				bool append_gap = true;

				if (block_in(b) == point_) {
					// The only reason we should be here is if this block is at the start of the track,
					// in which case no split needs to occur
					b = OakNodeBlock{};
				} else if (block_out(b) > point_) {
					// Block must be split as well as having a gap appended to it
					blocks_to_split.push_back(b);
				} else if (!block_next(b).ctx) {
					// At the end of a track, no gap needs to be added at all
					append_gap = false;
				}

				if (append_gap) {
					tracks_to_append_gap_to.push_back(track);
					blocks_to_append_gap_to.push_back(b);
				}
				break;
			}
		}
	}

	if (!blocks_to_split.empty()) {
		split_command_ = new BlockSplitPreservingLinksCommand(blocks_to_split,
															{ point_ });
	}

	for (size_t i = 0; i < blocks_to_append_gap_to.size(); i++) {
		OakNodeBlock gap = oaknode_block_gap_create();
		block_set_length_and_media_out(gap, length_);
		gaps_added_.push_back({ gap, true, blocks_to_append_gap_to.at(i),
								tracks_to_append_gap_to.at(i) });
	}
}

void TrackListInsertGaps::redo()
{
	for (OakNodeBlock gap : gaps_to_extend_) {
		block_set_length_and_media_out(gap, block_length(gap) + length_);
	}

	if (split_command_) {
		split_command_->redo_now();
	}

	for (AddGap &add_gap : gaps_added_) {
		block_add_to_graph(add_gap.gap, add_gap.track);
		oaknode_track_insert_block_after(add_gap.track, add_gap.gap,
										 add_gap.before);
		add_gap.orphaned = false;
	}
}

void TrackListInsertGaps::undo()
{
	// Remove added gaps
	for (AddGap &add_gap : gaps_added_) {
		OakNodeTrack t = block_track(add_gap.gap);
		if (t.ctx) {
			oaknode_track_ripple_remove_block(t, add_gap.gap);
			block_remove_from_graph(add_gap.gap, t);
		}
		add_gap.orphaned = true;
	}

	// Un-split blocks
	if (split_command_) {
		split_command_->undo_now();
	}

	// Restore original length of gaps
	for (OakNodeBlock gap : gaps_to_extend_) {
		block_set_length_and_media_out(gap, block_length(gap) - length_);
	}
}

//
// TrackReplaceBlockWithGapCommand
//
TrackReplaceBlockWithGapCommand::~TrackReplaceBlockWithGapCommand()
{
	for (TransitionRemoveCommand *c : transition_remove_commands_) {
		delete c;
	}
	if (our_gap_orphaned_) {
		free_detached_handle(&our_gap_);
	}
	if (merged_gap_orphaned_) {
		free_detached_handle(&existing_merged_gap_);
	}
}

void TrackReplaceBlockWithGapCommand::redo()
{
	// Determine if this block is connected to any transitions that should also be removed by this operation
	if (handle_transitions_ && transition_remove_commands_.empty()) {
		create_remove_transition_command_if_necessary(false);
		create_remove_transition_command_if_necessary(true);
	}
	for (TransitionRemoveCommand *c : transition_remove_commands_) {
		c->redo_now();
	}

	if (block_next(block_).ctx) {
		// Block has a next, which means it's NOT at the end of the sequence and thus requires a gap
		Rational new_gap_length = block_length(block_);

		OakNodeBlock previous = block_previous(block_);
		OakNodeBlock next = block_next(block_);

		int prev_kind = OAKNODE_BLOCK_OTHER;
		int next_kind = OAKNODE_BLOCK_OTHER;
		if (previous.ctx) {
			oaknode_block_get_kind(previous, &prev_kind);
		}
		if (next.ctx) {
			oaknode_block_get_kind(next, &next_kind);
		}
		bool previous_is_a_gap = (prev_kind == OAKNODE_BLOCK_GAP);
		bool next_is_a_gap = (next_kind == OAKNODE_BLOCK_GAP);

		if (previous_is_a_gap && next_is_a_gap) {
			// Clip is preceded and followed by a gap, so we'll merge the two
			existing_gap_ = previous;

			existing_merged_gap_ = next;
			new_gap_length += block_length(existing_merged_gap_);
			oaknode_track_ripple_remove_block(track_, existing_merged_gap_);
			block_remove_from_graph(existing_merged_gap_, track_);
			merged_gap_orphaned_ = true;
		} else if (previous_is_a_gap) {
			// Extend this gap to fill space left by block
			existing_gap_ = previous;
		} else if (next_is_a_gap) {
			// Extend this gap to fill space left by block
			existing_gap_ = next;
		}

		if (existing_gap_.ctx) {
			// Extend an existing gap
			new_gap_length += block_length(existing_gap_);
			block_set_length_and_media_out(existing_gap_, new_gap_length);
			oaknode_track_ripple_remove_block(track_, block_);

			existing_gap_precedes_ = same_block(existing_gap_, previous);
		} else {
			// No gap exists to fill this space, create a new one and swap it in
			if (!our_gap_.ctx) {
				our_gap_ = oaknode_block_gap_create();
				block_set_length_and_media_out(our_gap_, new_gap_length);
				our_gap_orphaned_ = true;
			}

			block_add_to_graph(our_gap_, track_);
			oaknode_track_replace_block(track_, block_, our_gap_);
			our_gap_orphaned_ = false;
		}

	} else {
		// Block is at the end of the track, simply remove it
		OakNodeBlock preceding = block_previous(block_);
		oaknode_track_ripple_remove_block(track_, block_);

		// Determine if it's preceded by a gap, and remove that gap if so
		int kind = OAKNODE_BLOCK_OTHER;
		if (preceding.ctx) {
			oaknode_block_get_kind(preceding, &kind);
		}
		if (kind == OAKNODE_BLOCK_GAP) {
			oaknode_track_ripple_remove_block(track_, preceding);
			block_remove_from_graph(preceding, track_);

			existing_merged_gap_ = preceding;
			merged_gap_orphaned_ = true;
		}
	}
}

void TrackReplaceBlockWithGapCommand::undo()
{
	if (our_gap_.ctx || existing_gap_.ctx) {
		if (our_gap_.ctx) {
			// We made this gap, simply swap our gap back
			oaknode_track_replace_block(track_, our_gap_, block_);
			block_remove_from_graph(our_gap_, track_);
			our_gap_orphaned_ = true;

		} else {
			// If we're here, assume that we extended an existing gap
			Rational original_gap_length =
				block_length(existing_gap_) - block_length(block_);

			// If we merged two gaps together, restore the second one now
			if (existing_merged_gap_.ctx) {
				original_gap_length -= block_length(existing_merged_gap_);
				block_add_to_graph(existing_merged_gap_, track_);
				oaknode_track_insert_block_after(track_, existing_merged_gap_,
												 existing_gap_);
				merged_gap_orphaned_ = false;
				existing_merged_gap_ = OakNodeBlock{};
			}

			// Restore original block
			if (existing_gap_precedes_) {
				oaknode_track_insert_block_after(track_, block_,
												 existing_gap_);
			} else {
				oaknode_track_insert_block_before(track_, block_,
												  existing_gap_);
			}

			// Restore gap's original length
			block_set_length_and_media_out(existing_gap_,
										   original_gap_length);

			existing_gap_ = OakNodeBlock{};
		}

	} else {
		// Our gap and existing gap were both null, our block must have been at the end and thus
		// required no gap extension/replacement

		// However, we may have removed an unnecessary gap that preceded it
		if (existing_merged_gap_.ctx) {
			block_add_to_graph(existing_merged_gap_, track_);
			oaknode_track_append_block(track_, existing_merged_gap_);
			merged_gap_orphaned_ = false;
			existing_merged_gap_ = OakNodeBlock{};
		}

		// Restore block
		oaknode_track_append_block(track_, block_);
	}

	for (auto it = transition_remove_commands_.rbegin();
		 it != transition_remove_commands_.rend(); it++) {
		(*it)->undo_now();
	}
}

void TrackReplaceBlockWithGapCommand::create_remove_transition_command_if_necessary(
	bool next)
{
	OakNodeBlock relevant_block =
		next ? block_next(block_) : block_previous(block_);

	int kind = OAKNODE_BLOCK_OTHER;
	if (relevant_block.ctx) {
		oaknode_block_get_kind(relevant_block, &kind);
	}
	if (kind != OAKNODE_BLOCK_TRANSITION) {
		return;
	}

	OakNodeBlock connected_out = {};
	OakNodeBlock connected_in = {};
	oaknode_transition_get_connected_out_block(relevant_block,
											   &connected_out);
	oaknode_transition_get_connected_in_block(relevant_block, &connected_in);

	if ((next && same_block(connected_out, block_) && !connected_in.ctx) ||
		(!next && same_block(connected_in, block_) && !connected_out.ctx)) {
		transition_remove_commands_.push_back(
			new TransitionRemoveCommand(relevant_block, true));
	}
}

//
// TimelineRemoveTrackCommand
//
TimelineRemoveTrackCommand::~TimelineRemoveTrackCommand()
{
	if (remove_command_.ctx) {
		free_command_handle(&remove_command_);
	}
}

void TimelineRemoveTrackCommand::prepare()
{
	OakNodeSequence sequence = {};
	oaknode_track_get_sequence(track_, &sequence);

	int type = OAKNODE_TRACK_TYPE_NONE;
	oaknode_track_get_type(track_, &type);
	oaknode_sequence_get_track_list(sequence, type, &list_);

	int cache_index = 0;
	oaknode_track_get_index(track_, &cache_index);
	oaknode_tracklist_get_array_index_from_cache_index(list_, cache_index,
													   &index_);

	remove_command_ = create_remove_command(oaknode_track_as_node(track_));
}

void TimelineRemoveTrackCommand::redo()
{
	oakundo_command_redo_now(remove_command_);

	OakNodeSequence sequence = {};
	oaknode_tracklist_get_sequence(list_, &sequence);

	char input_id[64];
	oaknode_tracklist_get_track_input_id(list_, input_id, sizeof(input_id));
	oaknode_node_input_array_remove(oaknode_sequence_as_node(sequence),
									input_id, index_);
}

void TimelineRemoveTrackCommand::undo()
{
	OakNodeSequence sequence = {};
	oaknode_tracklist_get_sequence(list_, &sequence);

	char input_id[64];
	oaknode_tracklist_get_track_input_id(list_, input_id, sizeof(input_id));
	oaknode_node_input_array_insert(oaknode_sequence_as_node(sequence),
									input_id, index_);

	oakundo_command_undo_now(remove_command_);
}

//
// TimelineAddDefaultTransitionCommand
//
namespace
{

std::string config_get_string(const char *key)
{
	int needed = oakcommon_config_get(NULL, key, NULL, 0);
	if (needed <= 0) {
		return std::string();
	}
	std::vector<char> buf(needed);
	if (oakcommon_config_get(NULL, key, buf.data(), needed) < 0) {
		return std::string();
	}
	return std::string(buf.data());
}

} // namespace

void TimelineAddDefaultTransitionCommand::prepare()
{
	for (OakNodeBlock c : clips_) {
		OakNodeBlock previous = block_previous(c);
		OakNodeBlock next = block_next(c);

		auto is_clip_in_selection = [this](OakNodeBlock b) {
			if (!b.ctx) {
				return false;
			}
			for (OakNodeBlock clip : clips_) {
				if (same_block(clip, b)) {
					return true;
				}
			}
			return false;
		};

		int prev_kind = OAKNODE_BLOCK_OTHER;
		int next_kind = OAKNODE_BLOCK_OTHER;
		if (previous.ctx) {
			oaknode_block_get_kind(previous, &prev_kind);
		}
		if (next.ctx) {
			oaknode_block_get_kind(next, &next_kind);
		}

		// Handle in transition
		if (is_clip_in_selection(previous)) {
			// Do nothing, assume this will be handled by a dual transition from that clip
		} else if (prev_kind == OAKNODE_BLOCK_GAP || !previous.ctx) {
			// Create in transition
			add_transition(c, k_in);
		}

		// Handle out transition
		if (is_clip_in_selection(next)) {
			add_transition(c, k_out_dual);
		} else if (next_kind == OAKNODE_BLOCK_GAP || !next.ctx) {
			// Create out transition
			add_transition(c, k_out);
		}
	}
}

void TimelineAddDefaultTransitionCommand::add_transition(
	OakNodeBlock c, CreateTransitionMode mode)
{
	OakNodeTrack t = block_track(c);
	if (!t.ctx) {
		return;
	}

	int type = OAKNODE_TRACK_TYPE_NONE;
	oaknode_track_get_type(t, &type);

	OakNodeNode p = {};
	if (type == OAKNODE_TRACK_TYPE_VIDEO) {
		std::string id = config_get_string("DefaultVideoTransition");
		if (!id.empty()) {
			p = oaknode_factory_create_from_id(id.c_str());
		}
	} else if (type == OAKNODE_TRACK_TYPE_AUDIO) {
		std::string id = config_get_string("DefaultAudioTransition");
		if (!id.empty()) {
			p = oaknode_factory_create_from_id(id.c_str());
		}
	}

	std::string len_str = config_get_string("DefaultTransitionLength");
	Rational transition_length =
		len_str.empty() ? Rational() : Rational::from_string(len_str);

	// Resize original clip
	switch (mode) {
	case k_in:
		validate_transition_length(c, transition_length);

		if (transition_length > 0) {
			adjust_clip_length(c, transition_length, false);
		}
		break;
	case k_out:
		validate_transition_length(c, transition_length);

		if (transition_length > 0) {
			adjust_clip_length(c, transition_length, true);
		}
		break;
	case k_out_dual: {
		Rational half_length = transition_length / 2;

		validate_transition_length(block_next(c), half_length);
		validate_transition_length(c, half_length);

		transition_length = half_length * 2;

		if (transition_length > 0) {
			adjust_clip_length(block_next(c), half_length, false);
			adjust_clip_length(c, half_length, true);
		}
		break;
	}
	}

	if (transition_length <= 0) {
		if (p.ctx) {
			oaknode_node_free(&p);
		}
		return;
	}

	OakNodeBlock transition = oaknode_block_from_node(p);
	int kind = OAKNODE_BLOCK_OTHER;
	if (transition.ctx) {
		oaknode_block_get_kind(transition, &kind);
	}
	if (kind != OAKNODE_BLOCK_TRANSITION) {
		if (p.ctx) {
			oaknode_node_free(&p);
		}
		return;
	}

	block_set_length_and_media_out(transition, transition_length);

	// Add transition
	OakNodeProject project = {};
	oaknode_node_get_project(oaknode_block_as_node(c), &project);
	commands_.push_back(
		new CHandleCommandWrapper(oaknode_command_create_add_node(project, p)));

	// Insert block
	OakNodeBlock insert_after = (mode == k_in) ? block_previous(c) : c;
	commands_.push_back(
		new TrackInsertBlockAfterCommand(t, transition, insert_after));

	// Connect
	OakUndoCommand edge_command = {};
	switch (mode) {
	case k_in:
		if (oaknode_node_connect_undoable(
				oaknode_block_as_node(c), p,
				OAKNODE_TRANSITION_IN_BLOCK_INPUT,
				&edge_command) == OAKNODE_OK) {
			commands_.push_back(new CHandleCommandWrapper(edge_command));
		}
		break;
	case k_out_dual:
		if (oaknode_node_connect_undoable(
				oaknode_block_as_node(block_next(c)), p,
				OAKNODE_TRANSITION_IN_BLOCK_INPUT,
				&edge_command) == OAKNODE_OK) {
			commands_.push_back(new CHandleCommandWrapper(edge_command));
		}
		/* fall through */
	case k_out:
		edge_command = OakUndoCommand{};
		if (oaknode_node_connect_undoable(
				oaknode_block_as_node(c), p,
				OAKNODE_TRANSITION_OUT_BLOCK_INPUT,
				&edge_command) == OAKNODE_OK) {
			commands_.push_back(new CHandleCommandWrapper(edge_command));
		}
		break;
	}
}

void TimelineAddDefaultTransitionCommand::adjust_clip_length(
	OakNodeBlock c, const Rational &transition_length, bool out)
{
	Rational cur_len = lengths_.count(c) ? lengths_[c] : block_length(c);
	Rational new_len = cur_len - transition_length;
	if (out) {
		commands_.push_back(new BlockResizeCommand(c, new_len));
	} else {
		commands_.push_back(new BlockResizeWithMediaInCommand(c, new_len));
	}
	lengths_[c] = new_len;
}

void TimelineAddDefaultTransitionCommand::validate_transition_length(
	OakNodeBlock c, Rational &transition_length)
{
	if (!c.ctx) {
		return;
	}
	Rational cur_len = lengths_.count(c) ? lengths_[c] : block_length(c);
	Rational half_cur_len = cur_len / 2;
	if (transition_length >= half_cur_len) {
		transition_length = half_cur_len - timebase_;
	}
}

}
