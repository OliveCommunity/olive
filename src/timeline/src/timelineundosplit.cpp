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

#include "timelineundosplit.h"

#include <cassert>

#include "node/node.h"
#include "timelineutil.h"
#include "undo/undostack.h"

namespace olive
{

namespace
{

/**
 * @brief oaknode-block-link undo command (replaces NodeLinkCommand from
 * oaknode's nodeundo, which does not cross the module boundary)
 */
class BlockLinkUndoCommand : public UndoCommand {
public:
	BlockLinkUndoCommand(OakNodeBlock a, OakNodeBlock b, bool link)
		: a_(a)
		, b_(b)
		, link_(link)
	{
	}

protected:
	virtual void redo() override
	{
		if (link_) {
			oaknode_block_link(a_, b_);
		} else {
			oaknode_block_unlink(a_, b_);
		}
	}

	virtual void undo() override
	{
		if (link_) {
			oaknode_block_unlink(a_, b_);
		} else {
			oaknode_block_link(a_, b_);
		}
	}

private:
	OakNodeBlock a_;
	OakNodeBlock b_;
	bool link_;
};

} // namespace

//
// BlockSplitCommand
//
BlockSplitCommand::~BlockSplitCommand()
{
	if (reconnect_tree_command_.ctx) {
		oakundo_command_free(&reconnect_tree_command_);
	}
}

void BlockSplitCommand::prepare()
{
	OakNodeNode copy = oaknode_node_copy_in_graph(
		oaknode_block_as_node(block_), &reconnect_tree_command_);
	new_block_ = oaknode_block_from_node(copy);
}

void BlockSplitCommand::redo()
{
	int n, d;

	oaknode_block_get_length(block_, &n, &d);
	old_length_ = Rational(n, d);

	int block_in_n, block_in_d, block_out_n, block_out_d;
	oaknode_block_get_in(block_, &block_in_n, &block_in_d);
	oaknode_block_get_out(block_, &block_out_n, &block_out_d);
	Rational block_in(block_in_n, block_in_d);
	Rational block_out(block_out_n, block_out_d);

	assert(point_ > block_in && point_ < block_out);

	if (reconnect_tree_command_.ctx) {
		oakundo_command_redo_now(reconnect_tree_command_);
	}

	// Determine our new lengths
	Rational new_length = point_ - block_in;
	Rational new_part_length = block_out - point_;

	// Begin an operation
	OakNodeTrack track = {};
	oaknode_block_get_track(block_, &track);

	// Set lengths
	rat_nd(new_length, &n, &d);
	oaknode_block_set_length_and_media_out(block_, n, d);
	rat_nd(new_part_length, &n, &d);
	oaknode_block_set_length_and_media_in(new_block_, n, d);

	// Insert new block
	oaknode_track_insert_block_after(track, new_block_, block_);

	oaknode_clip_add_cache_passthrough_from(new_block_, block_);

	// If the block had an out transition, we move it to the new block
	moved_transition_ = OakNodeBlock{};
	moved_transition_input_.clear();

	OakNodeBlock next = {};
	oaknode_block_get_next(new_block_, &next);
	int next_kind = OAKNODE_BLOCK_OTHER;
	if (next.ctx) {
		oaknode_block_get_kind(next, &next_kind);
	}
	if (next_kind == OAKNODE_BLOCK_TRANSITION) {
		// A transition is a node connected to the old block's output
		int conn_count = 0;
		oaknode_node_output_connection_count(
			oaknode_block_as_node(block_), &conn_count);
		for (int i = 0; i < conn_count; i++) {
			OakNodeNode conn_node = {};
			oaknode_node_output_connection_node_at(
				oaknode_block_as_node(block_), i, &conn_node);
			if (!same_block(oaknode_block_from_node(conn_node), next)) {
				continue;
			}

			char id_buf[64];
			if (oaknode_node_output_connection_input_id_at(
					oaknode_block_as_node(block_), i, id_buf,
					sizeof(id_buf)) < 0) {
				continue;
			}

			moved_transition_ = next;
			moved_transition_input_ = id_buf;
			oaknode_node_disconnect(oaknode_block_as_node(next), id_buf);
			oaknode_node_connect(oaknode_block_as_node(new_block_),
							 oaknode_block_as_node(next),
								 id_buf);
			break;
		}
	}
}

void BlockSplitCommand::undo()
{
	OakNodeTrack track = {};
	oaknode_block_get_track(block_, &track);

	if (moved_transition_.ctx) {
		oaknode_node_disconnect(oaknode_block_as_node(moved_transition_),
								moved_transition_input_.c_str());
		oaknode_node_connect(oaknode_block_as_node(block_),
							 oaknode_block_as_node(moved_transition_),
							 moved_transition_input_.c_str());
	}

	int n, d;
	rat_nd(old_length_, &n, &d);
	oaknode_block_set_length_and_media_out(block_, n, d);
	oaknode_track_ripple_remove_block(track, new_block_);

	// If we ran a reconnect command, disconnect now
	if (reconnect_tree_command_.ctx) {
		oakundo_command_undo_now(reconnect_tree_command_);
	}
}

//
// BlockSplitPreservingLinksCommand
//
BlockSplitPreservingLinksCommand::~BlockSplitPreservingLinksCommand()
{
	for (UndoCommand *c : commands_) {
		delete c;
	}
}

OakNodeBlock
BlockSplitPreservingLinksCommand::get_split(OakNodeBlock original,
											int time_index) const
{
	if (time_index >= 0 && time_index < int(times_.size())) {
		for (size_t i = 0; i < blocks_.size(); i++) {
			if (same_block(blocks_[i], original)) {
				return splits_.at(time_index).at(i);
			}
		}
	}

	return OakNodeBlock{};
}

void BlockSplitPreservingLinksCommand::prepare()
{
	splits_.resize(times_.size());

	for (size_t i = 0; i < times_.size(); i++) {
		const Rational &time = times_.at(i);

		// FIXME: I realize this isn't going to work if the times aren't ordered. I'm lazy so rather
		//        than writing in a sorting algorithm here, I'll just put an assert as a reminder
		//        if this ever becomes an issue.
		assert(i == 0 || time > times_.at(i - 1));

		std::vector<OakNodeBlock> splits(blocks_.size(), OakNodeBlock{});

		for (size_t j = 0; j < blocks_.size(); j++) {
			OakNodeBlock b = blocks_.at(j);

			int in_n, in_d, out_n, out_d;
			oaknode_block_get_in(b, &in_n, &in_d);
			oaknode_block_get_out(b, &out_n, &out_d);
			Rational b_in(in_n, in_d);
			Rational b_out(out_n, out_d);

			if (b_in < time && b_out > time) {
				BlockSplitCommand *split_command =
					new BlockSplitCommand(b, time);
				split_command->redo_now();
				splits[j] = split_command->new_block();
				commands_.push_back(split_command);
			}
		}

		splits_[i] = splits;
	}

	// Now that we've determined all the splits, we can relink everything
	for (size_t i = 0; i < blocks_.size(); i++) {
		OakNodeBlock a = blocks_.at(i);

		for (size_t j = 0; j < blocks_.size(); j++) {
			if (i == j) {
				continue;
			}

			OakNodeBlock b = blocks_.at(j);

			int linked = 0;
			oaknode_block_are_linked(a, b, &linked);
			if (linked) {
				// These blocks are linked, ensure all the splits are linked too
				for (const std::vector<OakNodeBlock> &split_list :
					 splits_) {
					if (!split_list.at(i).ctx || !split_list.at(j).ctx) {
						continue;
					}
					BlockLinkUndoCommand *blc = new BlockLinkUndoCommand(
						split_list.at(i), split_list.at(j), true);
					blc->redo_now();
					commands_.push_back(blc);
				}
			}
		}
	}
}

//
// TrackSplitAtTimeCommand
//
void TrackSplitAtTimeCommand::prepare()
{
	// Find Block that contains this time
	int n, d;
	rat_nd(point_, &n, &d);

	OakNodeBlock b = {};
	oaknode_track_get_block_containing_time(track_, n, d, &b);

	if (b.ctx) {
		command_ = new BlockSplitCommand(b, point_);
	}
}

}
