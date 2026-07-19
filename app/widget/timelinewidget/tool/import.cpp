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

#include "import.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QMimeData>
#include <QToolTip>

#include "config/config.h"
#include "common/qtutils.h"
#include "core.h"
#include "dialog/sequence/sequence.h"
#include "node/audio/volume/volume.h"
#include "node/block/subtitle/subtitle.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/generator/matrix/matrix.h"
#include "node/math/math/math.h"
#include "node/nodeundo.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundopointer.h"
#include "window/mainwindow/mainwindow.h"
#include "window/mainwindow/mainwindowundo.h"

namespace olive
{

ImportTool::ImportTool(TimelineWidget *parent)
	: TimelineTool(parent)
{
	// Calculate width used for importing to give ghosts a slight lead-in so the ghosts aren't right on the cursor
	import_pre_buffer_ =
		QtUtils::q_font_metrics_width(parent->fontMetrics(), "HHHHHHHH");
}

void ImportTool::drag_enter(TimelineViewMouseEvent *event)
{
	QStringList mime_formats = event->get_mime_data()->formats();

	// Listen for MIME data from a ProjectViewModel
	if (mime_formats.contains(Project::k_item_mime_type)) {
		// Data is drag/drop data from a ProjectViewModel
		QByteArray model_data =
			event->get_mime_data()->data(Project::k_item_mime_type);

		// Use QDataStream to deserialize the data
		QDataStream stream(&model_data, QIODevice::ReadOnly);

		// Variables to deserialize into
		quintptr item_ptr;
		QVector<Track::Reference> enabled_streams;

		// Set drag start position
		drag_start_ = event->get_coordinates();

		snap_points_.clear();

		while (!stream.atEnd()) {
			stream >> enabled_streams >> item_ptr;

			// Get Item object
			Node *item = reinterpret_cast<Node *>(item_ptr);

			// Check if Item is Footage
			ViewerOutput *f = dynamic_cast<ViewerOutput *>(item);

			if (f && f->get_total_stream_count()) {
				// If the Item is Footage, we can create a Ghost from it
				dragged_footage_.append({ f, enabled_streams });
			}
		}

		// Create a reasonable amount of space to inset the cursor by when importing
		ghost_offset_ = drag_start_.get_frame();

		if (!event->get_bypass_import_buffer()) {
			ghost_offset_ -= parent()->scene_to_time(import_pre_buffer_);
		}

		prep_ghosts(ghost_offset_, drag_start_.get_track().index());

		if (parent()->has_ghosts() || !parent()->get_connected_node()) {
			// We only clear the tentative track if the mimedata is about to be destroyed (i.e. the drag
			// is cancelled). If we do this in DragLeave, it leads to undesirable behavior if the cursor
			// is going between views (subtitle track rapidly appearing and disappearing)
			QObject::connect(event->get_mime_data(), &QObject::destroyed,
							 parent(),
							 &TimelineWidget::clear_tentative_subtitle_track);

			event->accept();
		} else {
			event->ignore();
		}
	} else {
		// FIXME: Implement dropping from file
		event->ignore();
	}
}

void ImportTool::drag_move(TimelineViewMouseEvent *event)
{
	if (!dragged_footage_.isEmpty()) {
		if (parent()->has_ghosts()) {
			Rational time_movement = event->get_frame() - drag_start_.get_frame();

			// Keep ghost offset no lower than 0
			if (ghost_offset_ + time_movement < 0) {
				time_movement = -ghost_offset_;
			}

			int track_movement =
				event->get_track().index() - drag_start_.get_track().index();

			time_movement = validate_time_movement(time_movement);
			track_movement = validate_track_movement(track_movement,
												   parent()->get_ghost_items());

			// If snapping is enabled, check for snap points
			if (Core::instance()->snapping()) {
				parent()->snap_point(snap_points_, &time_movement);

				time_movement = validate_time_movement(time_movement);
				track_movement = validate_track_movement(
					track_movement, parent()->get_ghost_items());
			}

			Rational earliest_ghost = RATIONAL_MAX;

			// Move ghosts to the mouse cursor
			foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
				ghost->set_in_adjustment(time_movement);
				ghost->set_out_adjustment(time_movement);
				ghost->set_track_adjustment(track_movement);

				earliest_ghost = qMin(earliest_ghost, ghost->get_adjusted_in());
			}

			// Generate tooltip (showing earliest in point of imported clip)
			Rational tooltip_timebase =
				parent()->get_timebase_for_track_type(event->get_track().type());
			QString tooltip_text =
				QString::fromStdString(Timecode::time_to_timecode(
					earliest_ghost, tooltip_timebase,
					Core::instance()->get_timecode_display()));

			// Force tooltip to update (otherwise the tooltip won't move as written in the documentation, and could get in the way
			// of the cursor)
			QToolTip::hideText();
			QToolTip::showText(QCursor::pos(), tooltip_text, parent());
		}

		event->accept();
	} else {
		event->ignore();
	}
}

void ImportTool::drag_leave(QDragLeaveEvent *event)
{
	if (!dragged_footage_.isEmpty()) {
		parent()->clear_ghosts();
		dragged_footage_.clear();

		event->accept();
	} else {
		event->ignore();
	}
}

void ImportTool::drag_drop(TimelineViewMouseEvent *event)
{
	if (!dragged_footage_.isEmpty()) {
		auto command = new MultiUndoCommand();
		drop_ghosts(event->get_modifiers() & Qt::ControlModifier, command);
		Core::instance()->undo_stack()->push(
			command,
			qApp->translate("ImportTool", "Dropped Footage Into Sequence"));

		event->accept();
	} else {
		event->ignore();
	}
}

void ImportTool::place_at(const QVector<ViewerOutput *> &footage,
						 const Rational &start, bool insert,
						 MultiUndoCommand *command, int track_offset,
						 bool jump_to_end)
{
	DraggedFootageData refs;

	foreach (ViewerOutput *f, footage) {
		refs.append({ f, f->get_enabled_streams_as_references() });
	}

	place_at(refs, start, insert, command, track_offset, jump_to_end);
}

void ImportTool::place_at(const DraggedFootageData &footage,
						 const Rational &start, bool insert,
						 MultiUndoCommand *command, int track_offset,
						 bool jump_to_end)
{
	dragged_footage_ = footage;

	if (dragged_footage_.isEmpty()) {
		return;
	}

	prep_ghosts(start, track_offset);

	Rational max(0);
	if (jump_to_end) {
		for (TimelineViewGhostItem *ghost : parent()->get_ghost_items()) {
			max = std::max(max, ghost->get_adjusted_out());
		}
	}

	drop_ghosts(insert, command);

	if (jump_to_end) {
		this->sequence()->set_playhead(max);
	}
}

void ImportTool::footage_to_ghosts(Rational ghost_start,
								 const DraggedFootageData &sorted,
								 const Rational &dest_tb,
								 const int &track_start)
{
	for (auto it = sorted.cbegin(); it != sorted.cend(); it++) {
		ViewerOutput *footage = it->first;

		if (footage == sequence() ||
			(sequence() && footage->inputs_from(sequence(), true))) {
			// Prevent cyclical dependency
			continue;
		}

		// Each stream is offset by one track per track "type", we keep track of them in this vector
		QVector<int> track_offsets(Track::k_count);
		track_offsets.fill(track_start);

		Rational footage_duration;
		Rational ghost_in;

		TimelineWorkArea *wk = footage->get_work_area();
		if (wk->enabled()) {
			footage_duration = wk->length();
			ghost_in = wk->in();
		} else {
			footage_duration = footage->get_length();

			if (footage_duration.isNull()) {
				// Fallback to still length if legngth was 0
				footage_duration =
					OAK_CONFIG("DefaultStillLength").value<Rational>();
			}
		}

		// Snap footage duration to timebase
		Rational snap_mvmt =
			snap_movement_to_timebase(footage_duration, 0, dest_tb);
		if (!snap_mvmt.isNull()) {
			footage_duration += snap_mvmt;
		}

		// Create ghosts
		foreach (const Track::Reference &ref, it->second) {
			Track::Type track_type = ref.type();
			Track::Reference dest_track(track_type,
										track_offsets.at(track_type));

			if (track_type == Track::k_video || track_type == Track::k_audio) {
				auto ghost = create_ghost(
					TimeRange(ghost_start, ghost_start + footage_duration),
					ghost_in, dest_track);

				// Increment track count for this track type
				track_offsets[track_type]++;

				TimelineViewGhostItem::AttachedFootage af = { it->first,
															  ref.to_string() };
				ghost->set_data(TimelineViewGhostItem::k_attached_footage,
							   QVariant::fromValue(af));
			} else if (track_type == Track::k_subtitle) {
				SubtitleParams sp = footage->get_subtitle_params(ref.index());

				for (const Subtitle &sub : sp) {
					auto ghost =
						create_ghost(sub.time() + ghost_start, 0, dest_track);

					ghost->set_data(TimelineViewGhostItem::k_attached_footage,
								   QVariant::fromValue(sub));
				}

				parent()->add_tentative_subtitle_track();
			}
		}

		// Stack each ghost one after the other
		ghost_start += footage_duration;
	}
}

void ImportTool::prep_ghosts(const Rational &frame, const int &track_index)
{
	if (parent()->get_connected_node()) {
		footage_to_ghosts(
			frame, dragged_footage_,
			parent()->get_connected_node()->get_video_params().time_base(),
			track_index);
	}
}

void ImportTool::drop_ghosts(bool insert, MultiUndoCommand *parent_command)
{
	auto command = new MultiUndoCommand();

	if (MultiUndoCommand *c = parent()->take_subtitle_section_command()) {
		command->add_child(c);
	}

	Project *dst_graph = nullptr;
	Sequence *sequence = this->sequence();
	bool open_sequence = false;

	if (sequence) {
		dst_graph = sequence->parent();
	} else {
		// There's no active timeline here, ask the user what to do

		DropWithoutSequenceBehavior behavior =
			static_cast<DropWithoutSequenceBehavior>(
				OAK_CONFIG("DropWithoutSequenceBehavior").toInt());

		if (behavior == k_dws_ask) {
			QCheckBox *dont_ask_again_box = new QCheckBox(
				QCoreApplication::translate("ImportTool",
											"Don't ask me again"));

			QMessageBox mbox(parent());

			mbox.setIcon(QMessageBox::Question);
			mbox.setWindowTitle(QCoreApplication::translate(
				"ImportTool", "No Active Sequence"));
			mbox.setText(QCoreApplication::translate(
				"ImportTool",
				"No sequence is currently open. Would you like to create one?"));
			mbox.setCheckBox(dont_ask_again_box);

			QPushButton *auto_params_btn = mbox.addButton(
				QCoreApplication::translate(
					"ImportTool",
					"Automatically Detect Parameters From Footage"),
				QMessageBox::YesRole);
			QPushButton *manual_params_btn =
				mbox.addButton(QCoreApplication::translate(
								   "ImportTool", "Set Parameters Manually"),
							   QMessageBox::NoRole);
			mbox.addButton(QMessageBox::Cancel);

			mbox.exec();

			if (mbox.clickedButton() == auto_params_btn) {
				behavior = k_dws_auto;
			} else if (mbox.clickedButton() == manual_params_btn) {
				behavior = k_dws_manual;
			} else {
				behavior = k_dws_disable;
			}

			if (behavior != k_dws_disable && dont_ask_again_box->isChecked()) {
				OAK_CONFIG("DropWithoutSequenceBehavior") = behavior;
			}
		}

		if (behavior != k_dws_disable) {
			Project *active_project = Core::instance()->get_active_project();

			if (active_project) {
				Sequence *new_sequence =
					Core::instance()->create_new_sequence_for_project(
						active_project);

				new_sequence->set_default_parameters();

				bool sequence_is_valid = true;

				// Even if the user selected manual, set from footage anyway so the user has a useful
				// starting point
				QVector<ViewerOutput *> footage_only;

				for (auto it = dragged_footage_.cbegin();
					 it != dragged_footage_.cend(); it++) {
					if (!footage_only.contains(it->first)) {
						footage_only.append(it->first);
					}
				}

				new_sequence->set_parameters_from_footage(footage_only);

				// If the user selected manual, show them a dialog with parameters
				if (behavior == k_dws_manual) {
					SequenceDialog sd(new_sequence, SequenceDialog::k_new,
									  parent());
					sd.set_undoable(false);

					if (sd.exec() != QDialog::Accepted) {
						sequence_is_valid = false;
					}
				}

				if (sequence_is_valid) {
					dst_graph = Core::instance()->get_active_project();

					command->add_child(
						new NodeAddCommand(dst_graph, new_sequence));
					command->add_child(new FolderAddChild(
						Core::instance()->get_selected_folder_in_active_project(),
						new_sequence));
					command->add_child(new NodeSetPositionCommand(
						new_sequence, new_sequence, QPointF(0, 0)));
					new_sequence->add_default_nodes(command);

					footage_to_ghosts(0, dragged_footage_,
									new_sequence->get_video_params().time_base(),
									0);

					if (MultiUndoCommand *c =
							parent()->take_subtitle_section_command()) {
						command->add_child(c);
					}

					sequence = new_sequence;

					// Set this as the sequence to open
					open_sequence = true;
				} else {
					// If the sequence is valid, ownership is passed to AddItemCommand.
					// Otherwise, we're responsible for deleting it.
					delete new_sequence;
				}
			}
		}
	}

	std::list<ClipBlock *> imported_clips;

	if (dst_graph) {
		QVector<Block *> block_items(parent()->get_ghost_items().size());

		// Check if we're inserting (only valid if we're not creating this sequence ourselves)
		if (insert && !open_sequence) {
			insert_gaps_at_ghost_destination(command);
		}

		for (int i = 0; i < parent()->get_ghost_items().size(); i++) {
			TimelineViewGhostItem *ghost = parent()->get_ghost_items().at(i);
			Block *block = nullptr;

			Track::Type track_type = ghost->get_adjusted_track().type();
			if (track_type == Track::k_video || track_type == Track::k_audio) {
				TimelineViewGhostItem::AttachedFootage footage_stream =
					ghost->get_data(TimelineViewGhostItem::k_attached_footage)
						.value<TimelineViewGhostItem::AttachedFootage>();

				ClipBlock *clip = new ClipBlock();
				block = clip;
				clip->set_media_in(ghost->get_media_in());
				command->add_child(new NodeAddCommand(dst_graph, clip));

				// Position clip in its own context
				command->add_child(
					new NodeSetPositionCommand(clip, clip, QPointF(0, 0)));

				int dep_pos = k_default_distance_from_output;

				// Position footage in its context
				command->add_child(new NodeSetPositionCommand(
					footage_stream.footage, clip, QPointF(dep_pos, 0)));

				dep_pos++;

				switch (
					Track::Reference::type_from_string(footage_stream.output)) {
				case Track::k_video: {
					TransformDistortNode *transform =
						new TransformDistortNode();
					command->add_child(
						new NodeAddCommand(dst_graph, transform));

					command->add_child(new NodeSetValueHintCommand(
						transform, TransformDistortNode::k_texture_input, -1,
						Node::ValueHint({ NodeValue::k_texture },
										footage_stream.output)));

					command->add_child(new NodeEdgeAddCommand(
						footage_stream.footage,
						NodeInput(transform,
								  TransformDistortNode::k_texture_input)));
					command->add_child(new NodeEdgeAddCommand(
						transform, NodeInput(clip, ClipBlock::k_buffer_in)));
					command->add_child(new NodeSetPositionCommand(
						transform, clip, QPointF(dep_pos, 0)));
					break;
				}
				case Track::k_audio: {
					VolumeNode *volume_node = new VolumeNode();
					command->add_child(
						new NodeAddCommand(dst_graph, volume_node));

					command->add_child(new NodeSetValueHintCommand(
						volume_node, VolumeNode::k_samples_input, -1,
						Node::ValueHint({ NodeValue::k_samples },
										footage_stream.output)));

					command->add_child(new NodeEdgeAddCommand(
						footage_stream.footage,
						NodeInput(volume_node, VolumeNode::k_samples_input)));
					command->add_child(new NodeEdgeAddCommand(
						volume_node, NodeInput(clip, ClipBlock::k_buffer_in)));
					command->add_child(new NodeSetPositionCommand(
						volume_node, clip, QPointF(dep_pos, 0)));
					break;
				}
				default:
					break;
				}

				// Link any clips so far that share the same Footage with this one
				for (int j = 0; j < i; j++) {
					TimelineViewGhostItem::AttachedFootage footage_compare =
						parent()
							->get_ghost_items()
							.at(j)
							->get_data(TimelineViewGhostItem::k_attached_footage)
							.value<TimelineViewGhostItem::AttachedFootage>();

					if (footage_compare.footage == footage_stream.footage) {
						Block::link(block_items.at(j), clip);
					}
				}

				imported_clips.push_back(clip);
			} else if (track_type == Track::k_subtitle) {
				Subtitle src =
					ghost->get_data(TimelineViewGhostItem::k_attached_footage)
						.value<Subtitle>();
				SubtitleBlock *sub = new SubtitleBlock();
				sub->set_text(src.text());
				block = sub;

				command->add_child(new NodeAddCommand(dst_graph, sub));
				command->add_child(
					new NodeSetPositionCommand(sub, sub, QPointF(0, 0)));
			}

			block->set_length_and_media_out(ghost->get_length());

			command->add_child(new TrackPlaceBlockCommand(
				sequence->track_list(ghost->get_adjusted_track().type()),
				ghost->get_adjusted_track().index(), block,
				ghost->get_adjusted_in()));

			block_items.replace(i, block);
		}
	}

	if (open_sequence) {
		command->add_child(new OpenSequenceCommand(sequence));
	}

	// Do command now because RequestInvalidatedFromConnected relies on track type, which will be
	// "none" before this command is done because it won't be connected to any track
	command->redo_now();
	parent_command->add_child(command);

	while (!imported_clips.empty()) {
		imported_clips.front()->request_invalidated_from_connected();
		imported_clips.pop_front();
	}

	parent()->clear_ghosts();
	dragged_footage_.clear();
}

TimelineViewGhostItem *ImportTool::create_ghost(const TimeRange &range,
											   const Rational &media_in,
											   const Track::Reference &track)
{
	TimelineViewGhostItem *ghost = new TimelineViewGhostItem();

	ghost->set_in(range.in());
	ghost->set_out(range.out());
	ghost->set_media_in(media_in);
	ghost->set_track(track);

	snap_points_.push_back(ghost->get_in());
	snap_points_.push_back(ghost->get_out());

	ghost->set_mode(Timeline::k_move);

	parent()->add_ghost(ghost);

	return ghost;
}

}
