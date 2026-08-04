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

#include "common/configwrapper.h"
#include "common/subtitleapp.h"
#include "oakutil/oaknode.h"
#include "oakutil/qtutils.h"
#include "core.h"
#include "dialog/sequence/sequence.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "oakengine/viewer.h"
#include "playback/playbackcontroller.h"
#include "oakengine/project.h"
#include "widget/timelinewidget/cliphandle.h"
#include "window/mainwindow/mainwindow.h"
#include "window/mainwindow/mainwindowundo.h"

#include "widget/viewer/vieweroutpututils.h"
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
	if (mime_formats.contains(QString::fromUtf8(oakengine_project_item_mime_type()))) {
		// Data is drag/drop data from a ProjectViewModel
		QByteArray model_data =
			event->get_mime_data()->data(QString::fromUtf8(oakengine_project_item_mime_type()));

		// Use QDataStream to deserialize the data
		QDataStream stream(&model_data, QIODevice::ReadOnly);

		// Variables to deserialize into
		quintptr item_ptr;
		QVector<TrackReference> enabled_streams;

		// Set drag start position
		drag_start_ = event->get_coordinates();

		snap_points_.clear();

		while (!stream.atEnd()) {
			stream >> enabled_streams >> item_ptr;

			// Get Item object
			OakEngineNode *item = reinterpret_cast<OakEngineNode *>(item_ptr);

			// Check if Item is a viewer (Footage or Sequence) with streams
			if (oakengine_node_is_viewer_output(item) &&
				oakengine_viewer_get_video_stream_count(item) +
						oakengine_viewer_get_audio_stream_count(item) +
						oakengine_viewer_get_subtitle_stream_count(item) >
					0) {
				// If the Item is Footage, we can create a Ghost from it
				dragged_footage_.append({ item, enabled_streams });
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
		auto command = oakengine_undo_command_create_multi();
		drop_ghosts(event->get_modifiers() & Qt::ControlModifier, command);
		oakengine_undo_push(
			command,
			qApp->translate("ImportTool", "Dropped Footage Into Sequence").toUtf8().constData());

		event->accept();
	} else {
		event->ignore();
	}
}

void ImportTool::place_at(const QVector<OakEngineNode *> &footage,
						 const Rational &start, bool insert,
						 void *command, int track_offset,
						 bool jump_to_end)
{
	DraggedFootageData refs;

	foreach (OakEngineNode *f, footage) {
		// ViewerOutput::get_enabled_streams_as_references() via the oak::
		// wrapper (C ABI): (track_type, index) pairs with ordinals matching
		// TrackReference::Type (see common/trackreferencehandle.h)
		QVector<TrackReference> enabled_streams;
		const QVector<QPair<int, int>> streams =
			oak::Node(f).enabled_streams();
		enabled_streams.reserve(streams.size());
		for (const QPair<int, int> &s : streams) {
			enabled_streams.append(TrackReference(
				static_cast<TrackReference::Type>(s.first), s.second));
		}
		refs.append({ f, enabled_streams });
	}

	place_at(refs, start, insert, command, track_offset, jump_to_end);
}

void ImportTool::place_at(const DraggedFootageData &footage,
						 const Rational &start, bool insert,
						 void *command, int track_offset,
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
		PlaybackController::instance()->set_playhead(
			reinterpret_cast<OakEngineNode *>(this->sequence()),
			max.numerator(), max.denominator());
	}
}

void ImportTool::footage_to_ghosts(Rational ghost_start,
								 const DraggedFootageData &sorted,
								 const Rational &dest_tb,
								 const int &track_start)
{
	for (auto it = sorted.cbegin(); it != sorted.cend(); it++) {
		OakEngineNode *footage = it->first;

		if (footage ==
				reinterpret_cast<OakEngineNode *>(sequence()) ||
			(sequence() &&
			 oakengine_node_inputs_from(
				 footage,
				 reinterpret_cast<OakEngineNode *>(sequence()), 1))) {
			// Prevent cyclical dependency
			continue;
		}

		// Each stream is offset by one track per track "type", we keep track of them in this vector
		QVector<int> track_offsets(TrackReference::k_count);
		track_offsets.fill(track_start);

		Rational footage_duration;
		Rational ghost_in;

		oakengine_viewer_workarea wk;
		oakengine_viewer_get_workarea(
			footage, &wk);
		if (wk.enabled) {
			footage_duration =
				Rational(int(wk.out_num), int(wk.out_den)) -
				Rational(int(wk.in_num), int(wk.in_den));
			ghost_in = Rational(int(wk.in_num), int(wk.in_den));
		} else {
			int64_t len_num = 0, len_den = 1;
			oakengine_viewer_get_length(
				footage, &len_num,
				&len_den);
			footage_duration = Rational(int(len_num), int(len_den));

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
		foreach (const TrackReference &ref, it->second) {
			TrackReference::Type track_type = ref.type();
			TrackReference dest_track(track_type,
									  track_offsets.at(track_type));

			if (track_type == TrackReference::k_video ||
				track_type == TrackReference::k_audio) {
				auto ghost = create_ghost(
					TimeRange(ghost_start, ghost_start + footage_duration),
					ghost_in, dest_track);

				// Increment track count for this track type
				track_offsets[track_type]++;

				TimelineViewGhostItem::AttachedFootage af = { it->first,
															  ref.to_string() };
				ghost->set_data(TimelineViewGhostItem::k_attached_footage,
							   QVariant::fromValue(af));
			} else if (track_type == TrackReference::k_subtitle) {
				int sub_count = oakengine_viewer_get_subtitle_count(
					footage,
					ref.index());

				for (int si = 0; si < sub_count; si++) {
					// Points at an engine Subtitle; read through the
					// layout-identical app mirror (common/subtitleapp.h)
					const SubtitleApp *sub = static_cast<const SubtitleApp *>(
						oakengine_viewer_get_subtitle_at(
							footage,
							ref.index(), si));
					auto ghost =
						create_ghost(sub->time() + ghost_start, 0, dest_track);

					ghost->set_data(TimelineViewGhostItem::k_attached_footage,
								   QVariant::fromValue(*sub));
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
			viewer_output_video_params(parent()->get_connected_node()).time_base(),
			track_index);
	}
}

void ImportTool::drop_ghosts(bool insert, void *parent_command)
{
	auto command = oakengine_undo_command_create_multi();

	if (void *c = parent()->take_subtitle_section_command()) {
		oakengine_undo_command_multi_add_child(command, c);
	}

	OakEngineProject *dst_graph = nullptr;
	OakEngineSequence *sequence = this->sequence();
	bool open_sequence = false;

	if (sequence) {
		dst_graph = oakengine_node_parent(
			reinterpret_cast<OakEngineNode *>(sequence));
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
			OakEngineProject *active_project = Core::instance()->get_active_project();

			if (active_project) {
				OakEngineSequence *new_sequence =
					Core::instance()->create_new_sequence_for_project(
						active_project);

				oakengine_viewer_set_default_parameters(
			reinterpret_cast<OakEngineNode *>(new_sequence));

				bool sequence_is_valid = true;

				// Even if the user selected manual, set from footage anyway so the user has a useful
				// starting point
				QVector<OakEngineNode *> footage_only;

				for (auto it = dragged_footage_.cbegin();
					 it != dragged_footage_.cend(); it++) {
					if (!footage_only.contains(it->first)) {
						footage_only.append(it->first);
					}
				}

				QVector<OakEngineNode *> _footage_nodes;
		for (auto *f : footage_only) {
			_footage_nodes.append(f);
		}
		oakengine_viewer_set_parameters_from_footage(
			reinterpret_cast<OakEngineNode *>(new_sequence),
			_footage_nodes.data(), _footage_nodes.size());

				// If the user selected manual, show them a dialog with parameters
				if (behavior == k_dws_manual) {
					SequenceDialog sd(reinterpret_cast<OakEngineNode *>(new_sequence), SequenceDialog::k_new,
									  parent());
					sd.set_undoable(false);

					if (sd.exec() != QDialog::Accepted) {
						sequence_is_valid = false;
					}
				}

				if (sequence_is_valid) {
					dst_graph = Core::instance()->get_active_project();

					oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			dst_graph,
			reinterpret_cast<OakEngineNode *>(new_sequence)));
					oakengine_folder_add_child(
						reinterpret_cast<OakEngineNode *>(
							Core::instance()->get_selected_folder_in_active_project()),
						reinterpret_cast<OakEngineNode *>(new_sequence));
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(new_sequence), reinterpret_cast<void *>(new_sequence), 0, 0, 0));
					oakengine_sequence_add_default_nodes(
						new_sequence);

					footage_to_ghosts(0, dragged_footage_,
									viewer_output_video_params(new_sequence).time_base(),
									0);

					if (void *c =
							parent()->take_subtitle_section_command()) {
						oakengine_undo_command_multi_add_child(command, c);
					}

					sequence = new_sequence;

					// Set this as the sequence to open
					open_sequence = true;
				} else {
					// If the sequence is valid, ownership is passed to AddItemCommand.
					// Otherwise, we're responsible for deleting it.
					oakengine_node_free(
						reinterpret_cast<OakEngineNode *>(new_sequence));
				}
			}
		}
	}

	std::list<OakEngineBlock *> imported_clips;

	if (dst_graph) {
		QVector<OakEngineBlock *> block_items(parent()->get_ghost_items().size());

		// Check if we're inserting (only valid if we're not creating this sequence ourselves)
		if (insert && !open_sequence) {
			insert_gaps_at_ghost_destination(command);
		}

		for (int i = 0; i < parent()->get_ghost_items().size(); i++) {
			TimelineViewGhostItem *ghost = parent()->get_ghost_items().at(i);
			OakEngineBlock *block = nullptr;

			TrackReference::Type track_type = ghost->get_adjusted_track().type();
			if (track_type == TrackReference::k_video ||
				track_type == TrackReference::k_audio) {
				TimelineViewGhostItem::AttachedFootage footage_stream =
					ghost->get_data(TimelineViewGhostItem::k_attached_footage)
						.value<TimelineViewGhostItem::AttachedFootage>();

				OakEngineBlock *clip = clip_create_empty();
				block = clip;
				clip_set_media_in(clip, ghost->get_media_in());
				oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(dst_graph),
			reinterpret_cast<OakEngineNode *>(clip)));

				// Position clip in its own context
				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(clip), reinterpret_cast<void *>(clip), 0, 0, 0));

				int dep_pos = k_default_distance_from_output;

				// Position footage in its context
				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(footage_stream.footage), reinterpret_cast<void *>(clip), dep_pos, 0, 0));

				dep_pos++;

				switch (
					TrackReference::type_from_string(footage_stream.output)) {
				case TrackReference::k_video: {
					OakEngineNode *transform =
						oakengine_node_factory_create_from_id("org.olivevideoeditor.Olive.transformdistort");
					oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(dst_graph),
			reinterpret_cast<OakEngineNode *>(transform)));

					oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_set_value_hint_command(
				reinterpret_cast<void *>(transform),
				oakengine_transform_texture_input_id(), -1,
				OAK_NODE_VALUE_TEXTURE, -1,
				footage_stream.output.toUtf8().constData()));

					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							footage_stream.footage,
							reinterpret_cast<OakEngineNode *>(transform),
							oakengine_transform_texture_input_id(),
							-1));
					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							reinterpret_cast<OakEngineNode *>(transform),
							reinterpret_cast<OakEngineNode *>(clip),
							oakengine_clip_buffer_input_id(),
							-1));
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(transform), reinterpret_cast<void *>(clip), dep_pos, 0, 0));
					break;
				}
				case TrackReference::k_audio: {
					OakEngineNode *volume_node = oakengine_node_factory_create_from_id("org.olivevideoeditor.Olive.volume");
					oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(dst_graph),
			reinterpret_cast<OakEngineNode *>(volume_node)));

					oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_set_value_hint_command(
				reinterpret_cast<void *>(volume_node),
				oakengine_volume_samples_input_id(), -1,
				OAK_NODE_VALUE_SAMPLES, -1,
				footage_stream.output.toUtf8().constData()));

					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							footage_stream.footage,
							reinterpret_cast<OakEngineNode *>(volume_node),
							oakengine_volume_samples_input_id(),
							-1));
					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							reinterpret_cast<OakEngineNode *>(volume_node),
							reinterpret_cast<OakEngineNode *>(clip),
							oakengine_clip_buffer_input_id(),
							-1));
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(volume_node), reinterpret_cast<void *>(clip), dep_pos, 0, 0));
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
						oakengine_block_link(
							reinterpret_cast<void *>(block_items.at(j)),
							reinterpret_cast<void *>(clip), 1);
					}
				}

				imported_clips.push_back(clip);
			} else if (track_type == TrackReference::k_subtitle) {
				SubtitleApp src =
					ghost->get_data(TimelineViewGhostItem::k_attached_footage)
						.value<SubtitleApp>();
				OakEngineBlock *sub = reinterpret_cast<OakEngineBlock *>(
					oakengine_node_factory_create_from_id(
						"org.olivevideoeditor.Olive.subtitle"));
				oakengine_subtitle_set_text(
					reinterpret_cast<OakEngineNode *>(sub),
					src.text().toUtf8().constData());
				block = sub;

				oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(dst_graph),
			reinterpret_cast<OakEngineNode *>(sub)));
				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(sub), reinterpret_cast<void *>(sub), 0, 0, 0));
			}

			// Set the block's length before placement through a trim command
			// child (children redo in order, so the length is set before
			// the block is placed): oakengine_block_set_length_and_media_out()
			// itself requires the block to already be on a track
			// (OAKENGINE_E_STATE), and pre-placement there are no adjacent
			// blocks, so a trim-out command reduces to
			// Block::set_length_and_media_out().
			oakengine_undo_command_multi_add_child(command,
				oakengine_block_trim_command(
					reinterpret_cast<void *>(oakengine_sequence_track_at(
						sequence,
						ghost->get_adjusted_track().type(),
						ghost->get_adjusted_track().index())),
					reinterpret_cast<void *>(block),
					ghost->get_length().numerator(),
					ghost->get_length().denominator(),
					OAKENGINE_MOVEMENT_MODE_TRIM_OUT, 0));

			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence, static_cast<int>(ghost->get_adjusted_track().type()))), ghost->get_adjusted_track().index(), reinterpret_cast<void *>(block), core::Timecode::time_to_timestamp(ghost->get_adjusted_in(), parent()->timebase())));

			block_items.replace(i, block);
		}
	}

	if (open_sequence) {
		oakengine_undo_command_multi_add_child(command, make_open_sequence_command(reinterpret_cast<OakEngineNode *>(sequence)));
	}

	// Do command now because RequestInvalidatedFromConnected relies on track type, which will be
	// "none" before this command is done because it won't be connected to any track
	oakengine_undo_command_redo_now(command);
	oakengine_undo_command_multi_add_child(parent_command, command);

	while (!imported_clips.empty()) {
		clip_request_invalidate_connected(imported_clips.front());
		imported_clips.pop_front();
	}

	parent()->clear_ghosts();
	dragged_footage_.clear();
}

TimelineViewGhostItem *ImportTool::create_ghost(const TimeRange &range,
											   const Rational &media_in,
											   const TrackReference &track)
{
	TimelineViewGhostItem *ghost = new TimelineViewGhostItem();

	ghost->set_in(range.in());
	ghost->set_out(range.out());
	ghost->set_media_in(media_in);
	ghost->set_track(track);

	snap_points_.push_back(ghost->get_in());
	snap_points_.push_back(ghost->get_out());

	ghost->set_mode(TimelineApp::k_move);

	parent()->add_ghost(ghost);

	return ghost;
}

}
