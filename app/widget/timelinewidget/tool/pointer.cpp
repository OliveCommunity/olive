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

#include "widget/timelinewidget/timelinewidget.h"

#include <QDebug>
#include <QToolTip>

#include "oakutil/qtutils.h"
#include "oakutil/range.h"
#include "common/configwrapper.h"
#include "core.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "oakengine/timeline.h"
#include "pointer.h"
#include "widget/timeruler/timeruler.h"
#include "widget/timelinewidget/trackhandle.h"

namespace olive
{

namespace
{

/// Block::in() as rational seconds.
Rational block_in_rational(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_in_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::out() as rational seconds.
Rational block_out_rational(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_out_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/**
 * @brief ClipBlock::block_links() through the engine C ABI
 * (oakengine_block_link_count/at: Node::links() filtered to blocks, same
 * content and ordering for a ClipBlock).
 */
QVector<OakEngineBlock *> block_links_of(OakEngineBlock *block)
{
	QVector<OakEngineBlock *> links;
	const int n = oakengine_block_link_count(block);
	links.reserve(n);
	for (int i = 0; i < n; i++) {
		links.append(oakengine_block_link_at(block, i));
	}
	return links;
}

/**
 * @brief Track::to_reference() facade: the app TrackReference mirror of an
 * engine Track, through the C ABI (same pattern as
 * ghost_block_track_reference()). Type ordinals are pinned to the engine
 * Track::Type ordinals by the static_asserts in trackreferencehandle.h.
 */
TrackReference track_reference_of(OakEngineTrack *track)
{
	if (!track) {
		return TrackReference();
	}
	auto *h = reinterpret_cast<OakEngineNode *>(track);
	return TrackReference(
		static_cast<TrackReference::Type>(oakengine_track_get_type(h)),
		oakengine_track_get_index(h));
}

} // namespace

PointerTool::PointerTool(TimelineWidget *parent)
	: TimelineTool(parent)
	, movement_allowed_(true)
	, trimming_allowed_(true)
	, track_movement_allowed_(true)
	, gap_trimming_allowed_(false)
	, can_rubberband_select_(false)
	, rubberband_selecting_(false)
{
}

void PointerTool::mouse_press(TimelineViewMouseEvent *event)
{
	const TrackReference &track_ref = event->get_track();

	// Determine if item clicked on is selectable
	clicked_item_ = parent()->get_item_at_scene_pos(event->get_coordinates());
	OakEngineClip *clip_clicked_item =
		oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(clicked_item_)) ?
			reinterpret_cast<OakEngineClip *>(clicked_item_) :
			nullptr;

	can_rubberband_select_ = false;

	bool selectable_item =
		(clicked_item_ &&
		 !track_is_locked(parent()->get_track_from_reference(track_ref)));

	if (selectable_item) {
		// Cache the clip's type for use later
		drag_track_type_ = track_ref.type();

		// If we haven't started dragging yet, we'll initiate a drag here
		// Record where the drag started in timeline coordinates
		drag_start_ = event->get_coordinates();

		// Determine whether we're trimming or moving based on the position of the cursor
		drag_movement_mode_ =
			is_cursor_in_trim_handle(clicked_item_, event->get_scene_x());

		// If we're not in a trim mode, we must be in a move mode (provided the tool allows movement and
		// the block is not a gap)
		if (drag_movement_mode_ == TimelineApp::k_none && movement_allowed_ &&
			!oakengine_block_is_gap(
				reinterpret_cast<OakEngineBlock *>(clicked_item_))) {
			drag_movement_mode_ = TimelineApp::k_move;
		}

		// If this item is already selected, no further selection needs to be made
		if (parent()->is_block_selected(clicked_item_)) {
			// Collect item deselections
			QVector<OakEngineBlock *> deselected_blocks;

			// If shift is held, deselect it
			if (event->get_modifiers() & Qt::ShiftModifier) {
				parent()->remove_selection(clicked_item_);
				deselected_blocks.append(clicked_item_);

				// If not holding alt, deselect all links as well
				if (clip_clicked_item &&
					!(event->get_modifiers() & Qt::AltModifier)) {
					parent()->set_block_links_selected(clip_clicked_item, false);
					deselected_blocks.append(block_links_of(reinterpret_cast<OakEngineBlock *>(clip_clicked_item)));
				}
			}

			parent()->signal_deselected_blocks(deselected_blocks);

			return;
		}
	}

	// If not holding shift, deselect all clips
	if (!(event->get_modifiers() & Qt::ShiftModifier)) {
		parent()->deselect_all();
	}

	if (selectable_item) {
		// Collect item selections
		QVector<OakEngineBlock *> selected_blocks;

		// Select this item
		parent()->add_selection(clicked_item_);
		selected_blocks.append(clicked_item_);

		// If not holding alt, select all links as well
		if (clip_clicked_item && !(event->get_modifiers() & Qt::AltModifier)) {
			parent()->set_block_links_selected(clip_clicked_item, true);
			selected_blocks.append(block_links_of(reinterpret_cast<OakEngineBlock *>(clip_clicked_item)));
		}

		parent()->signal_selected_blocks(selected_blocks);
	}

	can_rubberband_select_ =
		(event->get_button() ==
			 Qt::LeftButton // Only rubberband select from the primary mouse button
		 &&
		 (!selectable_item ||
		  drag_movement_mode_ ==
			  TimelineApp::
				  k_none)); // And if no item was selected OR the item isn't draggable

	if (can_rubberband_select_) {
		drag_global_start_ = QCursor::pos();
	}

	// If we click anywhere other than a marker, deselect all markers
	parent()->ruler()->deselect_all_markers();
}

void PointerTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (can_rubberband_select_) {
		if (!rubberband_selecting_) {
			// If we clicked an item but are rubberband selecting anyway, deselect it now
			if (clicked_item_) {
				parent()->remove_selection(clicked_item_);
				parent()->signal_deselected_blocks({ clicked_item_ });
				clicked_item_ = nullptr;
			}

			parent()->start_rubber_band_select(drag_global_start_);

			rubberband_selecting_ = true;
		}

		// Process rubberband select
		parent()->move_rubber_band_select(true, !(event->get_modifiers() &
											   Qt::AltModifier));

	} else {
		// Process drag
		if (!dragging_) {
			// Now that the cursor has moved, we will assume the intention is to drag

			// Clear snap points
			snap_points_.clear();

			// If we're performing an action, we can initiate ghosts
			if (drag_movement_mode_ != TimelineApp::k_none) {
				initiate_drag(clicked_item_, drag_movement_mode_,
							 event->get_modifiers());
			}

			// Set dragging to true here so no matter what, the drag isn't re-initiated until it's completed
			dragging_ = true;
		}

		if (dragging_ && !parent()->get_ghost_items().isEmpty()) {
			// We're already dragging AND we have ghosts to work with
			process_drag(event->get_coordinates());
		}
	}
}

void PointerTool::mouse_release(TimelineViewMouseEvent *event)
{
	if (rubberband_selecting_) {
		// Finish rubberband select
		parent()->end_rubber_band_select();
		rubberband_selecting_ = false;
		return;
	}

	if (dragging_) {
		// If we were dragging, process the end of the drag
		if (!parent()->get_ghost_items().isEmpty()) {
			finish_drag(event);
		}

		// Clean up
		parent()->clear_ghosts();
		snap_points_.clear();

		dragging_ = false;
	}
}

void PointerTool::hover_move(TimelineViewMouseEvent *event)
{
	if (trimming_allowed_) {
		// No dragging, but we still want to process cursors
		OakEngineBlock *block_at_cursor =
			parent()->get_item_at_scene_pos(event->get_coordinates());

		if (block_at_cursor) {
			switch (is_cursor_in_trim_handle(block_at_cursor, event->get_scene_x())) {
			case TimelineApp::k_trim_in:
				parent()->setCursor(Qt::SizeHorCursor);
				break;
			case TimelineApp::k_trim_out:
				parent()->setCursor(Qt::SizeHorCursor);
				break;
			default:
				parent()->unsetCursor();
			}
		} else {
			parent()->unsetCursor();
		}
	} else {
		parent()->unsetCursor();
	}
}

void set_ghost_to_slide_mode(TimelineViewGhostItem *g)
{
	g->set_can_move_tracks(false);
	g->set_data(TimelineViewGhostItem::k_ghost_is_sliding, true);
}

void PointerTool::initiate_drag_internal(OakEngineBlock *clicked_item,
									   TimelineApp::MovementMode trim_mode,
									   Qt::KeyboardModifiers modifiers,
									   bool dont_roll_trims,
									   bool allow_nongap_rolling,
									   bool slide_instead_of_moving)
{
	// Get list of selected blocks
	QVector<OakEngineBlock *> clips = parent()->get_selected_blocks();

	if (trim_mode == TimelineApp::k_move) {
		// Gaps are not allowed to move, and since we only allow moving one block type at a time,
		// dragging a gap is a no-op
		if (oakengine_block_is_gap(clicked_item)) {
			return;
		}

		bool sliding_due_to_transition = false;

		if (!slide_instead_of_moving) {
			// If the user tries to move a transition without moving the clip it belongs to, we turn
			// this into a slide
			foreach (OakEngineBlock *block, clips) {
				if (oakengine_node_is_transition(
						reinterpret_cast<OakEngineNode *>(block))) {
					if (!can_transition_move(block, clips)) {
						slide_instead_of_moving = true;
						break;
					}
				} else if (oakengine_node_is_clip(
							   reinterpret_cast<OakEngineNode *>(block))) {
					OakEngineBlock *in_transit =
						oakengine_clip_in_transition(block);
					OakEngineBlock *out_transit =
						oakengine_clip_out_transition(block);
					if ((in_transit &&
						 !can_transition_move(in_transit, clips)) ||
						(out_transit &&
						 !can_transition_move(out_transit, clips))) {
						slide_instead_of_moving = true;
						break;
					}
				}
			}

			sliding_due_to_transition = slide_instead_of_moving;
		}

		if (slide_instead_of_moving) {
			// This is a slide. What we do here is move clips within their own track, between the clips
			// that they're already next to. We don't allow changing tracks or changing the order of
			// blocks.
			//
			// For slides to be legal, we make all blocks "contiguous". This means that only one series
			// of blocks can move at a time and prevents.

			QHash<OakEngineTrack *, OakEngineBlock *> earliest_block_on_track;
			QHash<OakEngineTrack *, OakEngineBlock *> latest_block_on_track;

			foreach (OakEngineBlock *this_block, clips) {
				OakEngineTrack *this_track =
					oakengine_block_get_track(this_block);
				OakEngineBlock *current_earliest =
					earliest_block_on_track.value(this_track, nullptr);
				if (!current_earliest ||
					block_in_rational(this_block) <
						block_in_rational(current_earliest)) {
					earliest_block_on_track.insert(this_track, this_block);
				}

				OakEngineBlock *current_latest =
					latest_block_on_track.value(this_track, nullptr);
				if (!current_latest ||
					block_out_rational(this_block) >
						block_out_rational(current_earliest)) {
					latest_block_on_track.insert(this_track, this_block);
				}
			}

			for (auto i = earliest_block_on_track.constBegin();
				 i != earliest_block_on_track.constEnd(); i++) {
				// Make a contiguous stream
				OakEngineTrack *track = i.key();
				OakEngineBlock *earliest = i.value();
				OakEngineBlock *latest = latest_block_on_track.value(i.key());

				OakEngineBlock *earliest_previous =
					oakengine_block_prev(earliest);
				OakEngineBlock *latest_next = oakengine_block_next(latest);

				// First we add the block that's out trimming, the one prior to the earliest
				{
					TimelineViewGhostItem *earliest_ghost;
					bool slide_with_earliest_previous = true;
					if (sliding_due_to_transition && earliest_previous) {
						if (oakengine_node_is_transition(
								reinterpret_cast<OakEngineNode *>(earliest))) {
							if (earliest_previous !=
								oakengine_transition_connected_out_block(
									earliest)) {
								slide_with_earliest_previous = false;
							}
						} else if (oakengine_node_is_clip(
									   reinterpret_cast<OakEngineNode *>(
										   earliest))) {
							if (earliest_previous !=
								oakengine_clip_in_transition(earliest)) {
								slide_with_earliest_previous = false;
							}
						}
					}

					if (earliest_previous && slide_with_earliest_previous) {
						earliest_ghost = add_ghost_from_block(earliest_previous,
														   TimelineApp::k_trim_out);
					} else {
						earliest_ghost = add_ghost_from_null(block_in_rational(earliest),
														  block_in_rational(earliest),
														  track_reference_of(track),
														  TimelineApp::k_trim_out);
					}
					set_ghost_to_slide_mode(earliest_ghost);
				}

				// Then we add the block that's in trimming, the one after the latest
				if (latest_next) {
					TimelineViewGhostItem *latest_ghost;

					bool slide_with_latest_next = true;
					if (sliding_due_to_transition) {
						if (oakengine_node_is_transition(
								reinterpret_cast<OakEngineNode *>(latest))) {
							if (latest_next !=
								oakengine_transition_connected_in_block(
									latest)) {
								slide_with_latest_next = false;
							}
						} else if (oakengine_node_is_clip(
									   reinterpret_cast<OakEngineNode *>(
										   latest))) {
							if (latest_next !=
								oakengine_clip_out_transition(latest)) {
								slide_with_latest_next = false;
							}
						}
					}

					if (slide_with_latest_next) {
						latest_ghost = add_ghost_from_block(latest_next,
														 TimelineApp::k_trim_in);
					} else {
						latest_ghost = add_ghost_from_null(block_out_rational(latest),
														block_out_rational(latest),
														track_reference_of(track),
														TimelineApp::k_trim_in);
					}
					set_ghost_to_slide_mode(latest_ghost);
				}

				// Finally, we add all of the moving blocks in between
				OakEngineBlock *b = nullptr;
				do {
					// On first run-through, set to earliest only. From then on, set to the next of the last
					// in the loop.
					if (b) {
						b = oakengine_block_next(b);
					} else {
						b = earliest;
					}

					TimelineViewGhostItem *between_ghost =
						add_ghost_from_block(b, TimelineApp::k_move);
					set_ghost_to_slide_mode(between_ghost);
				} while (b != latest);
			}
		} else {
			// Prepare for a standard pointer move by creating ghosts for them and any related blocks
			foreach (OakEngineBlock *block, clips) {
				if (oakengine_block_is_gap(block)) {
					continue;
				}

				// Create ghost for this block
				auto ghost = add_ghost_from_block(block, trim_mode, true);
				Q_UNUSED(ghost)

				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(block))) {
					if (OakEngineBlock *out_transit =
							oakengine_clip_out_transition(block)) {
						add_ghost_from_block(out_transit, trim_mode, true);
					}
					if (OakEngineBlock *in_transit =
							oakengine_clip_in_transition(block)) {
						add_ghost_from_block(in_transit, trim_mode, true);
					}
				}
			}
		}

	} else {
		// "Multi-trim" is trimming a clip on more than one track. Only the earliest (for in trimming)
		// or latest (for out trimming) clip on each track can be trimmed. Therefore, it's only enabled
		// if the clicked item is the earliest/latest on its track.
		bool multitrim_enabled =
			is_clip_trimmable(clicked_item, clips, trim_mode);

		// Create ghosts for trimming
		for (OakEngineBlock *clip_item : clips) {
			if (clip_item != clicked_item &&
				(!multitrim_enabled ||
				 !is_clip_trimmable(clip_item, clips, trim_mode))) {
				// Either multitrim is disabled or this clip is NOT the earliest/latest in its track. We
				// won't include it.
				continue;
			}

			OakEngineBlock *block = clip_item;

			// Create ghost for this block
			TimelineViewGhostItem *ghost = add_ghost_from_block(block, trim_mode);

			// If this side of the clip has a transition, we treat it more like a slide for that
			// transition than a trim/roll
			bool treat_trim_as_slide = false;

			if (oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(block))) {
				// See if this clip has a transition attached, and move it with the trim if so
				OakEngineBlock *connected_transition;

				// Get appropriate transition for the side of the clip
				if (trim_mode == TimelineApp::k_trim_in) {
					connected_transition = oakengine_clip_in_transition(block);
				} else {
					connected_transition = oakengine_clip_out_transition(block);
				}

				if (connected_transition) {
					// We found a transition, we'll make this a "slide" action
					TimelineViewGhostItem *transition_ghost = add_ghost_from_block(
						connected_transition, TimelineApp::k_move);

					// This will in effect be a slide with the transition moving between two other blocks
					set_ghost_to_slide_mode(ghost);
					set_ghost_to_slide_mode(transition_ghost);
					treat_trim_as_slide = true;

					// Further processing will apply to this transition rather than the clip
					block = connected_transition;
				}
			}

			// Standard pointer trimming in reality is a "roll" edit with an adjacent gap (one that may
			// or may not exist already)
			if (!dont_roll_trims) {
				OakEngineBlock *adjacent = nullptr;

				// Determine which block is adjacent
				if (trim_mode == TimelineApp::k_trim_in) {
					adjacent = oakengine_block_prev(block);
				} else {
					adjacent = oakengine_block_next(block);
				}

				// See if we can roll the adjacent or if we'll need to create our own gap
				bool block_is_transition = oakengine_node_is_transition(
					reinterpret_cast<OakEngineNode *>(block));
				if (!oakengine_block_is_gap(block) &&
					!allow_nongap_rolling && adjacent &&
					!oakengine_block_is_gap(adjacent) &&
					!(block_is_transition &&
					  ((trim_mode == TimelineApp::k_trim_in &&
						oakengine_transition_connected_out_block(block) ==
							adjacent) ||
					   (trim_mode == TimelineApp::k_trim_out &&
						oakengine_transition_connected_in_block(block) ==
							adjacent)))) {
					adjacent = nullptr;
				}

				TimelineApp::MovementMode flipped_mode = flip_trim_mode(trim_mode);
				QVector<TimelineViewGhostItem *> adjacent_ghosts;

				if (adjacent) {
					adjacent_ghosts.append(
						add_ghost_from_block(adjacent, flipped_mode));

					// Select adjacent's links if applicable
					// FIXME: The check for `clips.size() == 1` may not be necessary, but I don't know yet.
					//        I'm only including it to prevent any potentially unintended behavior.
					if (clips.size() == 1 && !(modifiers & Qt::AltModifier)) {
						if (oakengine_node_is_clip(
								reinterpret_cast<OakEngineNode *>(adjacent))) {
							for (OakEngineBlock *adjacent_link :
								 block_links_of(adjacent)) {
								adjacent_ghosts.append(add_ghost_from_block(
									adjacent_link, flipped_mode));
							}
						}
					}
				} else if (trim_mode == TimelineApp::k_trim_in ||
						   oakengine_block_next(block)) {
					Rational null_ghost_pos = (trim_mode == TimelineApp::k_trim_in) ?
												  block_in_rational(block) :
												  block_out_rational(block);

					adjacent_ghosts.append(add_ghost_from_null(
						null_ghost_pos, null_ghost_pos,
						track_reference_of(
							oakengine_block_get_track(clip_item)),
						flipped_mode));
				}

				// If we have an adjacent block (for any reason), this is a roll edit and the adjacent is
				// expected to fill the remaining space (no gap needs to be created)
				ghost->set_data(TimelineViewGhostItem::k_trim_is_a_roll_edit,
							   static_cast<bool>(adjacent));

				for (TimelineViewGhostItem *adjacent_ghost : adjacent_ghosts) {
					if (adjacent_ghost) {
						if (treat_trim_as_slide) {
							// We're sliding a transition rather than a pure trim/roll
							set_ghost_to_slide_mode(adjacent_ghost);
						} else if (oakengine_block_is_gap(block)) {
							ghost->set_data(
								TimelineViewGhostItem::k_trim_should_be_ignored,
								true);
						} else {
							adjacent_ghost->set_data(
								TimelineViewGhostItem::k_trim_should_be_ignored,
								true);
						}
					}
				}
			}
		}
	}
}

bool PointerTool::can_transition_move(OakEngineBlock *transit,
									const QVector<OakEngineBlock *> &clips)
{
	OakEngineBlock *out = oakengine_transition_connected_out_block(transit);
	OakEngineBlock *in = oakengine_transition_connected_in_block(transit);

	if ((out && !clips.contains(out)) || (in && !clips.contains(in))) {
		return false;
	}

	return true;
}

void PointerTool::process_drag(const TimelineCoordinate &mouse_pos)
{
	// Calculate track movement
	int track_movement =
		track_movement_allowed_ ?
			mouse_pos.get_track().index() - drag_start_.get_track().index() :
			0;

	// Determine frame movement
	Rational time_movement = mouse_pos.get_frame() - drag_start_.get_frame();

	// Validate movement (enforce all ghosts moving in legal ways)
	time_movement = validate_time_movement(time_movement);
	time_movement = validate_in_trimming(time_movement);
	time_movement = validate_out_trimming(time_movement);

	// Perform snapping if enabled (adjusts time_movement if it's close to any potential snap points)
	if (Core::instance()->snapping()) {
		parent()->snap_point(snap_points_, &time_movement);

		time_movement = validate_time_movement(time_movement);
		time_movement = validate_in_trimming(time_movement);
		time_movement = validate_out_trimming(time_movement);
	}

	// Validate ghosts that are being moved (clips from other track types do NOT get moved)
	if (track_movement != 0) {
		QVector<TimelineViewGhostItem *> validate_track_ghosts =
			parent()->get_ghost_items();
		for (int i = 0; i < validate_track_ghosts.size(); i++) {
			if (validate_track_ghosts.at(i)->get_track().type() !=
				drag_track_type_) {
				validate_track_ghosts.removeAt(i);
				i--;
			}
		}
		track_movement =
			validate_track_movement(track_movement, validate_track_ghosts);
	}

	// Perform movement
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		switch (ghost->get_mode()) {
		case TimelineApp::k_none:
			break;
		case TimelineApp::k_trim_in:
			ghost->set_in_adjustment(time_movement);
			ghost->set_media_in_adjustment(time_movement);
			break;
		case TimelineApp::k_trim_out:
			ghost->set_out_adjustment(time_movement);
			break;
		case TimelineApp::k_move: {
			ghost->set_in_adjustment(time_movement);
			ghost->set_out_adjustment(time_movement);

			// Track movement is only legal for moving, not for trimming
			// Also, we only move the clips on the same track type that the drag started from
			if (ghost->get_track().type() == drag_track_type_) {
				ghost->set_track_adjustment(track_movement);
			}
			break;
		}
		}
	}

	// Regenerate tooltip and force it to update (otherwise the tooltip won't move as written in the
	// documentation, and could get in the way of the cursor)
	Rational tooltip_timebase =
		parent()->get_timebase_for_track_type(drag_start_.get_track().type());
	QToolTip::hideText();
	QToolTip::showText(QCursor::pos(),
					   QString::fromStdString(Timecode::time_to_timecode(
						   time_movement, tooltip_timebase,
						   Core::instance()->get_timecode_display(), true)),
					   parent());
}

struct GhostBlockPair {
	TimelineViewGhostItem *ghost;
	OakEngineBlock *block;
};

void PointerTool::finish_drag(TimelineViewMouseEvent *event)
{
	QList<GhostBlockPair> blocks_moving;
	QList<GhostBlockPair> blocks_sliding;
	QList<GhostBlockPair> blocks_trimming;

	// Sort ghosts depending on which ones are trimming, which are moving, and which are sliding
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (ghost->has_been_adjusted()) {
			OakEngineBlock *b = QtUtils::value_to_ptr<OakEngineBlock>(
				ghost->get_data(TimelineViewGhostItem::k_attached_block));

			if (ghost->get_data(TimelineViewGhostItem::k_ghost_is_sliding).toBool()) {
				blocks_sliding.append({ ghost, b });
			} else if (ghost->get_mode() == TimelineApp::k_move) {
				blocks_moving.append({ ghost, b });
			} else if (TimelineApp::is_a_trim_mode(ghost->get_mode())) {
				blocks_trimming.append({ ghost, b });
			}
		}
	}

	if (blocks_moving.isEmpty() && blocks_trimming.isEmpty() &&
		blocks_sliding.isEmpty()) {
		// No blocks were adjusted, so nothing to do
		return;
	}

	void *command = oakengine_undo_command_create_multi();

	if (!blocks_trimming.isEmpty()) {
		foreach (const GhostBlockPair &p, blocks_trimming) {
			TimelineViewGhostItem *ghost = p.ghost;

			if (!ghost->get_data(TimelineViewGhostItem::k_trim_should_be_ignored)
					 .toBool()) {
				// Must be an ordinary trim/roll
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_block_trim_command(
						reinterpret_cast<void *>(
							parent()->get_track_from_reference(
								ghost->get_adjusted_track())),
						reinterpret_cast<void *>(p.block),
						ghost->get_adjusted_length().numerator(),
						ghost->get_adjusted_length().denominator(),
						ghost->get_mode(),
						ghost->get_data(TimelineViewGhostItem::k_trim_is_a_roll_edit)
							.toBool()
							? 1
							: 0));
			}
		}

		if (blocks_moving.isEmpty() && blocks_sliding.isEmpty()) {
			// Trim selections (deferring to moving/sliding blocks when necessary)
			TimelineWidgetSelections new_sel = parent()->get_selections();
			TimelineViewGhostItem *reference_ghost =
				blocks_trimming.first().ghost;
			if (reference_ghost->get_mode() == TimelineApp::k_trim_in) {
				new_sel.trim_in(reference_ghost->get_in_adjustment());
			} else {
				new_sel.trim_out(reference_ghost->get_out_adjustment());
			}
			oakengine_undo_command_multi_add_child(command, parent()->create_set_selections_command(new_sel, parent()->get_selections()));
		}
	}

	if (!blocks_moving.isEmpty()) {
		// See if we're duplicated because ALT is held (only moved blocks can duplicate)
		bool duplicate_clips = (event->get_modifiers() & Qt::AltModifier);
		bool inserting = (event->get_modifiers() & Qt::ControlModifier);

		// If we're not duplicating, "remove" the clips and replace them with gaps
		if (!duplicate_clips) {
			QVector<OakEngineBlock *> blocks_to_delete(blocks_moving.size());

			for (int i = 0; i < blocks_moving.size(); i++) {
				blocks_to_delete[i] = blocks_moving.at(i).block;
			}

			parent()->replace_blocks_with_gaps(blocks_to_delete, false, command,
											false);
		}

		if (inserting) {
			// If we're inserting, ripple everything at the destination with gaps
			insert_gaps_at_ghost_destination(command);
		}

		QMap<OakEngineBlock *, OakEngineBlock *> relinks;

		// Now we can re-add each clip
		foreach (const GhostBlockPair &p, blocks_moving) {
			OakEngineBlock *block = p.block;

			if (duplicate_clips) {
				// Duplicate rather than move
				// Place the copy instead of the original block
				OakEngineBlock *new_block =
					reinterpret_cast<OakEngineBlock *>(oakengine_node_copy_in_graph(
						reinterpret_cast<OakEngineNode*>(block), command));
				relinks.insert(block, new_block);
				block = new_block;

				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(block))) {
					oakengine_clip_add_cache_passthrough(
						reinterpret_cast<OakEngineClip *>(block),
						reinterpret_cast<OakEngineClip *>(p.block));
				}
			}

			const TrackReference track_ref = p.ghost->get_adjusted_track();
			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), track_ref.type())), track_ref.index(), reinterpret_cast<void *>(block), core::Timecode::time_to_timestamp(p.ghost->get_adjusted_in(), parent()->timebase())));
		}

		if (!relinks.empty()) {
			for (auto it = relinks.cbegin(); it != relinks.cend(); it++) {
				// Re-connect links on duplicate clips (block links, same
				// content as Node::links() for a ClipBlock)
				for (OakEngineBlock *link : block_links_of(it.key())) {
					OakEngineBlock *copy_link = relinks.value(link);
					if (copy_link) {
					oakengine_undo_command_multi_add_child(command, (void *)(oakengine_node_link_command(
								reinterpret_cast<OakEngineNode*>(it.value()),
								reinterpret_cast<OakEngineNode*>(copy_link), 1)));
					}
				}

				// Re-connect transitions where applicable
				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(it.key()))) {
					OakEngineBlock *og_in_transition =
						oakengine_clip_in_transition(it.key());
					OakEngineBlock *og_out_transition =
						oakengine_clip_out_transition(it.key());

					if (og_in_transition &&
						relinks.contains(og_in_transition)) {
						OakEngineBlock *cp_in_transition =
							relinks.value(og_in_transition);
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_connect_command(
								reinterpret_cast<OakEngineNode *>(it.value()),
								reinterpret_cast<OakEngineNode *>(cp_in_transition),
								QLatin1String(oakengine_transition_in_block_input_id()).toUtf8().constData(),
								-1));
					}

					if (og_out_transition &&
						relinks.contains(og_out_transition)) {
						OakEngineBlock *cp_out_transition =
							relinks.value(og_out_transition);
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_connect_command(
								reinterpret_cast<OakEngineNode *>(it.value()),
								reinterpret_cast<OakEngineNode *>(cp_out_transition),
								QLatin1String(oakengine_transition_out_block_input_id()).toUtf8().constData(),
								-1));
					}
				}
			}
		}

		// Adjust selections
		TimelineWidgetSelections new_sel = parent()->get_selections();
		new_sel.shift_time(blocks_moving.first().ghost->get_in_adjustment());
		new_sel.shift_tracks(drag_track_type_,
							blocks_moving.first().ghost->get_track_adjustment());
		oakengine_undo_command_multi_add_child(command, parent()->create_set_selections_command(new_sel, parent()->get_selections()));
	}

	if (!blocks_sliding.isEmpty()) {
		// Assume that the blocks are contiguous per track as set up in InitiateGhostsInternal()

		// All we need to do is sort them by track and order them
		QHash<TrackReference, QList<OakEngineBlock *>> slide_info;
		QHash<TrackReference, OakEngineBlock *> in_adjacents;
		QHash<TrackReference, OakEngineBlock *> out_adjacents;
		Rational movement;

		foreach (const GhostBlockPair &p, blocks_sliding) {
			const TrackReference &track = p.ghost->get_track();

			switch (p.ghost->get_mode()) {
			case TimelineApp::k_none:
				break;
			case TimelineApp::k_move: {
				// These all should have moved uniformly, so as long as this is set, it should be fine
				movement = p.ghost->get_in_adjustment();

				QList<OakEngineBlock *> &blocks_on_this_track = slide_info[track];
				bool inserted = false;

				for (int i = 0; i < blocks_on_this_track.size(); i++) {
					if (block_in_rational(blocks_on_this_track.at(i)) >
						block_in_rational(p.block)) {
						blocks_on_this_track.insert(i, p.block);
						inserted = true;
						break;
					}
				}

				if (!inserted) {
					blocks_on_this_track.append(p.block);
				}
				break;
			}
			case TimelineApp::k_trim_in:
				out_adjacents.insert(track, p.block);
				break;
			case TimelineApp::k_trim_out:
				in_adjacents.insert(track, p.block);
				break;
			}
		}

		if (!movement.isNull()) {
			for (auto i = slide_info.constBegin(); i != slide_info.constEnd();
				 i++) {
				const QList<OakEngineBlock *> &moving_blocks = i.value();
				QVector<void *> slide_blocks;
				slide_blocks.reserve(moving_blocks.size());
				for (OakEngineBlock *b : moving_blocks) {
					slide_blocks.append(reinterpret_cast<void *>(b));
				}
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_track_slide_command(
						reinterpret_cast<void *>(
							parent()->get_track_from_reference(i.key())),
						slide_blocks.constData(), slide_blocks.size(),
						reinterpret_cast<void *>(in_adjacents.value(i.key())),
						reinterpret_cast<void *>(out_adjacents.value(i.key())),
						movement.numerator(), movement.denominator()));
			}

			// Adjust selections
			TimelineWidgetSelections new_sel = parent()->get_selections();
			new_sel.shift_time(movement);
			oakengine_undo_command_multi_add_child(command, parent()->create_set_selections_command(new_sel, parent()->get_selections()));
		}
	}

	oakengine_undo_push(
		command, qApp->translate("PointerTool", "Moved Clips").toUtf8().constData());
}

TimelineApp::MovementMode PointerTool::is_cursor_in_trim_handle(OakEngineBlock *block,
														 qreal cursor_x)
{
	const double k_trim_handle =
		QtUtils::q_font_metrics_width(parent()->fontMetrics(), "H");

	double block_left = parent()->time_to_scene(block_in_rational(block));
	double block_right = parent()->time_to_scene(block_out_rational(block));
	double block_width = block_right - block_left;

	// Block is too narrow, no trimming allowed
	if (block_width <= k_trim_handle * 2) {
		return TimelineApp::k_none;
	}

	if (trimming_allowed_ && cursor_x <= block_left + k_trim_handle) {
		return TimelineApp::k_trim_in;
	} else if (trimming_allowed_ && cursor_x >= block_right - k_trim_handle) {
		return TimelineApp::k_trim_out;
	} else {
		return TimelineApp::k_none;
	}
}

void PointerTool::initiate_drag(OakEngineBlock *clicked_item,
							   TimelineApp::MovementMode trim_mode,
							   Qt::KeyboardModifiers modifiers)
{
	initiate_drag_internal(clicked_item, trim_mode, modifiers, false, false,
						 false);
}

TimelineViewGhostItem *PointerTool::get_existing_ghost_from_block(OakEngineBlock *block)
{
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (QtUtils::value_to_ptr<OakEngineBlock>(ghost->get_data(
				TimelineViewGhostItem::k_attached_block)) == block) {
			return ghost;
		}
	}

	return nullptr;
}

//#define HIDE_GAP_GHOSTS

TimelineViewGhostItem *
PointerTool::add_ghost_from_block(OakEngineBlock *block, TimelineApp::MovementMode mode,
							   bool check_if_exists)
{
	// Ignore null blocks or blocks that aren't attached to a track because there's nothing we can
	// do with either of those
	if (!block || !oakengine_block_get_track(block)) {
		return nullptr;
	}

	TimelineViewGhostItem *ghost;

	// Check if we've already made a ghost for this block
	if (check_if_exists) {
		if ((ghost = get_existing_ghost_from_block(block))) {
			return ghost;
		}
	}

	// Otherwise, it's time to make a ghost for this block
	ghost = TimelineViewGhostItem::from_block(block);

#ifdef HIDE_GAP_GHOSTS
	if (oakengine_block_is_gap(block)) {
		ghost->set_invisible(true);
	}
#endif

	add_ghost_internal(ghost, mode);

	return ghost;
}

TimelineViewGhostItem *
PointerTool::add_ghost_from_null(const Rational &in, const Rational &out,
							  const TrackReference &track,
							  TimelineApp::MovementMode mode)
{
	TimelineViewGhostItem *ghost = new TimelineViewGhostItem();

	ghost->set_in(in);
	ghost->set_out(out);
	ghost->set_track(track);

#ifdef HIDE_GAP_GHOSTS
	ghost->SetInvisible(true);
#endif

	add_ghost_internal(ghost, mode);

	return ghost;
}

void PointerTool::add_ghost_internal(TimelineViewGhostItem *ghost,
								   TimelineApp::MovementMode mode)
{
	ghost->set_mode(mode);

	// Prepare snap points (optimizes snapping for later)
	switch (mode) {
	case TimelineApp::k_move:
		snap_points_.push_back(ghost->get_in());
		snap_points_.push_back(ghost->get_out());
		break;
	case TimelineApp::k_trim_in:
		snap_points_.push_back(ghost->get_in());
		break;
	case TimelineApp::k_trim_out:
		snap_points_.push_back(ghost->get_out());
		break;
	default:
		break;
	}

	parent()->add_ghost(ghost);
}

bool PointerTool::is_clip_trimmable(OakEngineBlock *clip, const QVector<OakEngineBlock *> &items,
								  const TimelineApp::MovementMode &mode)
{
	foreach (OakEngineBlock *compare, items) {
		if (oakengine_block_get_track(clip) ==
				oakengine_block_get_track(compare) &&
			clip != compare &&
			((block_in_rational(compare) < block_in_rational(clip) &&
			  mode == TimelineApp::k_trim_in) ||
			 (block_out_rational(compare) > block_out_rational(clip) &&
			  mode == TimelineApp::k_trim_out))) {
			return false;
		}
	}

	return true;
}

Rational PointerTool::validate_in_trimming(Rational movement)
{
	bool first_ghost = true;

	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (ghost->get_mode() != TimelineApp::k_trim_in) {
			continue;
		}

		Rational earliest_in = RATIONAL_MIN;
		Rational latest_in = ghost->get_out();

		Rational ghost_timebase =
			parent()->get_timebase_for_track_type(ghost->get_track().type());

		// If the ghost must be at least one frame in size, limit the latest allowed in point
		if (!ghost->can_have_zero_length()) {
			latest_in -= ghost_timebase;
		}

		// Clamp adjusted value between the earliest and latest values
		Rational adjusted = ghost->get_in() + movement;
		Rational clamped = std::clamp(adjusted, earliest_in, latest_in);

		if (clamped != adjusted) {
			movement = clamped - ghost->get_in();
		}

		if (first_ghost) {
			movement = snap_movement_to_timebase(ghost->get_in(), movement,
											  ghost_timebase);
			first_ghost = false;
		}
	}

	return movement;
}

Rational PointerTool::validate_out_trimming(Rational movement)
{
	bool first_ghost = true;

	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (ghost->get_mode() != TimelineApp::k_trim_out) {
			continue;
		}

		// Determine earliest and latest out points
		Rational earliest_out = ghost->get_in();

		Rational ghost_timebase =
			parent()->get_timebase_for_track_type(ghost->get_track().type());

		if (!ghost->can_have_zero_length()) {
			earliest_out += ghost_timebase;
		}

		Rational latest_out = RATIONAL_MAX;

		// Clamp adjusted value between the earliest and latest values
		Rational adjusted = ghost->get_out() + movement;
		Rational clamped = std::clamp(adjusted, earliest_out, latest_out);

		if (clamped != adjusted) {
			movement = clamped - ghost->get_out();
		}

		if (first_ghost) {
			movement = snap_movement_to_timebase(ghost->get_out(), movement,
											  ghost_timebase);
			first_ghost = false;
		}
	}

	return movement;
}

}
