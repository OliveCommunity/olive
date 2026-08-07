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

#include "timeline/edit.h"

#include <new>
#include <vector>

#include "../src/timelineundogeneral.h"
#include "../src/timelineundopointer.h"
#include "../src/timelineundoripple.h"
#include "../src/timelineundosplit.h"
#include "../../undo/c_api/commandhandle.h"

namespace
{

OakUndoCommand wrap_command(olive::UndoCommand *command)
{
	if (!command) {
		return OakUndoCommand{};
	}
	return oakundo_capi::make_command_handle(command, true);
}

olive::core::Rational rat(int64_t n, int64_t d)
{
	return olive::core::Rational(int(n), int(d));
}

olive::Timeline::MovementMode to_mode(int mode)
{
	return static_cast<olive::Timeline::MovementMode>(mode);
}

} // namespace

OakUndoCommand oaktimeline_add_track_command(OakNodeTrackList list)
{
	if (!list.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::TimelineAddTrackCommand(list));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_remove_track_command(OakNodeTrack track)
{
	if (!track.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::TimelineRemoveTrackCommand(track));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_place_block_command(OakNodeTrackList list,
												int track_index,
												OakNodeBlock block,
												int64_t in_num,
												int64_t in_den)
{
	if (!list.ctx || !block.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::TrackPlaceBlockCommand(
			list, track_index, block, rat(in_num, in_den)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_replace_block_with_gap_command(
	OakNodeTrack track, OakNodeBlock block)
{
	if (!track.ctx || !block.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(
			new olive::TrackReplaceBlockWithGapCommand(track, block));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_trim_command(OakNodeTrack track,
										 OakNodeBlock block,
										 int64_t new_length_num,
										 int64_t new_length_den, int mode)
{
	if (!track.ctx || !block.ctx || (mode != OAKTIMELINE_MOVEMENT_TRIM_IN &&
									 mode != OAKTIMELINE_MOVEMENT_TRIM_OUT)) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(
			new olive::BlockTrimCommand(track, block,
										rat(new_length_num, new_length_den),
										to_mode(mode)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_split_command(const OakNodeBlock *blocks,
										  int count, int64_t point_num,
										  int64_t point_den)
{
	if (!blocks || count <= 0) {
		return OakUndoCommand{};
	}

	try {
		auto *multi = new olive::MultiUndoCommand();
		for (int i = 0; i < count; i++) {
			if (blocks[i].ctx) {
				multi->add_child(new olive::BlockSplitCommand(
					blocks[i], rat(point_num, point_den)));
			}
		}
		return wrap_command(multi);
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_split_preserving_links_command(
	const OakNodeBlock *blocks, int count, const int64_t *point_nums,
	const int64_t *point_dens, int time_count)
{
	if (!blocks || count <= 0 || !point_nums || !point_dens ||
		time_count <= 0) {
		return OakUndoCommand{};
	}

	try {
		std::vector<OakNodeBlock> block_vec(blocks, blocks + count);
		std::vector<olive::core::Rational> times;
		times.reserve(time_count);
		for (int i = 0; i < time_count; i++) {
			times.push_back(rat(point_nums[i], point_dens[i]));
		}
		return wrap_command(new olive::BlockSplitPreservingLinksCommand(
			block_vec, times));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_ripple_delete_gaps_command(
	OakNodeSequence sequence, const int64_t *in_nums,
	const int64_t *in_dens, const int64_t *out_nums,
	const int64_t *out_dens, const OakNodeTrack *tracks, int range_count)
{
	if (!sequence.ctx || !in_nums || !in_dens || !out_nums || !out_dens ||
		!tracks || range_count <= 0) {
		return OakUndoCommand{};
	}

	try {
		olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList regions;
		regions.reserve(range_count);
		for (int i = 0; i < range_count; i++) {
			regions.emplace_back(
				tracks[i],
				olive::core::TimeRange(rat(in_nums[i], in_dens[i]),
									   rat(out_nums[i], out_dens[i])));
		}
		return wrap_command(
			new olive::TimelineRippleDeleteGapsAtRegionsCommand(sequence,
																regions));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_slide_command(
	OakNodeTrack track, const OakNodeBlock *blocks, int block_count,
	OakNodeBlock in_adjacent, OakNodeBlock out_adjacent,
	int64_t movement_num, int64_t movement_den)
{
	if (!track.ctx || !blocks || block_count <= 0) {
		return OakUndoCommand{};
	}

	try {
		std::vector<OakNodeBlock> block_vec(blocks, blocks + block_count);
		return wrap_command(new olive::TrackSlideCommand(
			track, block_vec, in_adjacent, out_adjacent,
			rat(movement_num, movement_den)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_ripple_remove_area_command(
	OakNodeTrack track, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den)
{
	if (!track.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::TrackRippleRemoveAreaCommand(
			track, olive::core::TimeRange(rat(in_num, in_den),
										  rat(out_num, out_den))));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_insert_gaps_command(OakNodeTrackList list,
												int64_t point_num,
												int64_t point_den,
												int64_t length_num,
												int64_t length_den)
{
	if (!list.ctx) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(
			new olive::TrackListInsertGaps(list, rat(point_num, point_den),
										   rat(length_num, length_den)));
	} catch (...) {
		return OakUndoCommand{};
	}
}
