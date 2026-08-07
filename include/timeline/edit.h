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

#ifndef OAK_EDITOR_TIMELINE_EDIT_H
#define OAK_EDITOR_TIMELINE_EDIT_H

#include "node/block.h"
#include "node/sequence.h"
#include "node/track.h"
#include "timeline/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Timeline edit primitives (M4 §2.3).
 *
 * The timeline undo command classes stay inside oaktimeline (01 §5);
 * consumers create commands through these factories, receiving base
 * OakUndoCommand handles (owned; free with oakundo_command_free()).
 * Redo a command directly or push it on an undo stack.
 */

/** @brief olive::TimelineAddTrackCommand. */
OakUndoCommand oaktimeline_add_track_command(OakNodeTrackList *list);

/** @brief olive::TimelineRemoveTrackCommand. */
OakUndoCommand oaktimeline_remove_track_command(OakNodeTrack *track);

/** @brief olive::TrackPlaceBlockCommand. */
OakUndoCommand oaktimeline_place_block_command(OakNodeTrackList *list,
												int track_index,
												OakNodeBlock *block,
												int64_t in_num,
												int64_t in_den);

/** @brief olive::TrackReplaceBlockWithGapCommand. */
OakUndoCommand oaktimeline_replace_block_with_gap_command(
	OakNodeTrack *track, OakNodeBlock *block);

/**
 * @brief olive::BlockTrimCommand. `mode` is an OakTimelineMovementMode
 * value (k_trim_in / k_trim_out).
 */
OakUndoCommand oaktimeline_trim_command(OakNodeTrack *track,
										 OakNodeBlock *block,
										 int64_t new_length_num,
										 int64_t new_length_den, int mode);

/** @brief olive::BlockSplitCommand on a set of blocks at one point. */
OakUndoCommand oaktimeline_split_command(OakNodeBlock *const *blocks,
										  int count, int64_t point_num,
										  int64_t point_den);

/** @brief olive::BlockSplitPreservingLinksCommand. */
OakUndoCommand oaktimeline_split_preserving_links_command(
	OakNodeBlock *const *blocks, int count, const int64_t *point_nums,
	const int64_t *point_dens, int time_count);

/** @brief olive::TimelineRippleDeleteGapsAtRegionsCommand. */
OakUndoCommand oaktimeline_ripple_delete_gaps_command(
	OakNodeSequence *sequence, const int64_t *in_nums,
	const int64_t *in_dens, const int64_t *out_nums,
	const int64_t *out_dens, OakNodeTrack *const *tracks, int range_count);

/** @brief olive::TrackSlideCommand. */
OakUndoCommand oaktimeline_slide_command(
	OakNodeTrack *track, OakNodeBlock *const *blocks, int block_count,
	OakNodeBlock *in_adjacent, OakNodeBlock *out_adjacent,
	int64_t movement_num, int64_t movement_den);

/** @brief olive::TrackRippleRemoveAreaCommand. */
OakUndoCommand oaktimeline_ripple_remove_area_command(
	OakNodeTrack *track, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den);

/** @brief olive::TrackListInsertGaps. */
OakUndoCommand oaktimeline_insert_gaps_command(OakNodeTrackList *list,
												int64_t point_num,
												int64_t point_den,
												int64_t length_num,
												int64_t length_den);

/** @brief Movement modes (olive::Timeline::MovementMode). */
enum OakTimelineMovementMode {
	OAKTIMELINE_MOVEMENT_NONE = 0,
	OAKTIMELINE_MOVEMENT_MOVE = 1,
	OAKTIMELINE_MOVEMENT_TRIM_IN = 2,
	OAKTIMELINE_MOVEMENT_TRIM_OUT = 3
};

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TIMELINE_EDIT_H
