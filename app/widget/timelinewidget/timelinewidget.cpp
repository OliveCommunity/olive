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

#include "timelinewidget.h"
#include "timelinewidgetwaveformsync.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

#include "audio/audiosynchronizer.h"
#include "audio/audiowaveformsync.h"
#include "codec/proxymanager.h"
#include "core.h"
#include "common/range.h"
#include "dialog/proxy/proxydialog.h"
#include "dialog/sequence/sequence.h"
#include "dialog/speedduration/speeddurationdialog.h"
#include "node/block/transition/transition.h"
#include "node/nodeundo.h"
#include "node/project/footage/footage.h"
#include "node/project/serializer/serializer.h"
#include "render/audiowaveformcache.h"
#include "task/project/import/import.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "timeline/timelineundoripple.h"
#include "timeline/timelineundoworkarea.h"
#include "tool/add.h"
#include "tool/beam.h"
#include "tool/edit.h"
#include "tool/pointer.h"
#include "tool/razor.h"
#include "tool/record.h"
#include "tool/ripple.h"
#include "tool/rolling.h"
#include "tool/slide.h"
#include "tool/slip.h"
#include "tool/trackselect.h"
#include "tool/transition.h"
#include "tool/zoom.h"
#include "tool/tool.h"
#include "trackview/trackview.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"
#include "widget/nodeparamview/nodeparamview.h"
#include "widget/timeruler/timeruler.h"

namespace olive
{

#define super TimeBasedWidget

using namespace timeline_waveform_sync;

namespace
{

struct SourceSyncClip {
	ClipBlock *clip = nullptr;
	AudioSynchronizer::SourceClip source;
	Rational source_head;
};

bool get_source_sync_clip(Block *block, SourceSyncClip *out)
{
	ClipBlock *clip = dynamic_cast<ClipBlock *>(block);
	if (!clip) {
		return false;
	}

	Footage *footage = dynamic_cast<Footage *>(clip->connected_viewer());
	if (!footage || !footage->has_source_start_time()) {
		return false;
	}

	out->clip = clip;
	out->source.source_start_time = footage->source_start_time();
	out->source.media_in = clip->media_in();
	out->source.has_source_start_time = true;
	out->source_head = out->source.source_start_time + out->source.media_in;
	return true;
}

QVector<SourceSyncClip>
get_selected_source_sync_clips(const QVector<Block *> &blocks)
{
	QVector<SourceSyncClip> clips;
	for (Block *block : blocks) {
		SourceSyncClip sync_clip;
		if (get_source_sync_clip(block, &sync_clip)) {
			clips.append(sync_clip);
		}
	}
	return clips;
}

QVector<Footage *> get_selected_proxy_footage(const QVector<Block *> &blocks)
{
	QVector<Footage *> footage;
	for (Block *block : blocks) {
		ClipBlock *clip = dynamic_cast<ClipBlock *>(block);
		if (!clip) {
			continue;
		}

		Footage *candidate = dynamic_cast<Footage *>(clip->connected_viewer());
		if (!candidate || !candidate->get_first_enabled_video_stream().is_valid() ||
			footage.contains(candidate)) {
			continue;
		}

		footage.append(candidate);
	}
	return footage;
}

} // namespace

TimelineWidget::TimelineWidget(QWidget *parent)
	: super(true, true, parent)
	, rubberband_(QRubberBand::Rectangle, this)
	, active_tool_(nullptr)
	, use_audio_time_units_(false)
	, subtitle_show_command_(nullptr)
	, subtitle_tentative_track_(nullptr)
{
	QVBoxLayout *vert_layout = new QVBoxLayout(this);
	vert_layout->setSpacing(0);
	vert_layout->setContentsMargins(0, 0, 0, 0);

	QHBoxLayout *ruler_and_time_layout = new QHBoxLayout();
	vert_layout->addLayout(ruler_and_time_layout);

	timecode_label_ = new RationalSlider();
	timecode_label_->set_alignment(Qt::AlignCenter);
	timecode_label_->set_display_type(RationalSlider::k_time);
	timecode_label_->setVisible(false);
	timecode_label_->set_minimum(0);
	ruler_and_time_layout->addWidget(timecode_label_);

	ruler_and_time_layout->addWidget(ruler());

	ruler()->setFocusPolicy(Qt::TabFocus);
	QWidget::setTabOrder(ruler(), timecode_label_);

	// Create list of TimelineViews - these MUST correspond to the ViewType enum

	view_splitter_ = new QSplitter(Qt::Vertical);
	vert_layout->addWidget(view_splitter_);

	// Video view
	views_.append(add_timeline_and_track_view(Qt::AlignBottom));

	// Audio view
	views_.append(add_timeline_and_track_view(Qt::AlignTop));

	// Subtitle view
	views_.append(add_timeline_and_track_view(Qt::AlignTop));

	// Create tools
	tools_.resize(olive::Tool::k_count);
	tools_.fill(nullptr);

	tools_.replace(olive::Tool::k_pointer, new PointerTool(this));
	tools_.replace(olive::Tool::k_track_select, new TrackSelectTool(this));
	tools_.replace(olive::Tool::k_edit, new EditTool(this));
	tools_.replace(olive::Tool::k_ripple, new RippleTool(this));
	tools_.replace(olive::Tool::k_rolling, new RollingTool(this));
	tools_.replace(olive::Tool::k_razor, new RazorTool(this));
	tools_.replace(olive::Tool::k_slip, new SlipTool(this));
	tools_.replace(olive::Tool::k_slide, new SlideTool(this));
	tools_.replace(olive::Tool::k_zoom, new ZoomTool(this));
	tools_.replace(olive::Tool::k_transition, new TransitionTool(this));
	tools_.replace(olive::Tool::k_record, new RecordTool(this));
	tools_.replace(olive::Tool::k_add, new AddTool(this));

	import_tool_ = new ImportTool(this);

	// We add this to the list to make deleting all tools easier, it should never get accessed through the list under
	// normal circumstances (but *technically* its index would be Tool::kCount)
	tools_.append(import_tool_);

	// Global scrollbar
	connect(views_.first()->view()->horizontalScrollBar(),
			&QScrollBar::rangeChanged, scrollbar(), &QScrollBar::setRange);
	vert_layout->addWidget(scrollbar());

	foreach (TimelineAndTrackView *tview, views_) {
		TimelineView *view = tview->view();

		view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		view->set_snap_service(this);
		view->set_selection_list(&selections_);
		view->set_ghost_list(&ghost_items_);

		view_splitter_->addWidget(tview);

		connect_timeline_view(view);

		connect(view, &TimelineView::customContextMenuRequested, this,
				&TimelineWidget::show_context_menu);

		connect(view, &TimelineView::mouse_pressed, this,
				&TimelineWidget::view_mouse_pressed);
		connect(view, &TimelineView::mouse_moved, this,
				&TimelineWidget::view_mouse_moved);
		connect(view, &TimelineView::mouse_released, this,
				&TimelineWidget::view_mouse_released);
		connect(view, &TimelineView::mouse_double_clicked, this,
				&TimelineWidget::view_mouse_double_clicked);
		connect(view, &TimelineView::drag_entered, this,
				&TimelineWidget::view_drag_entered);
		connect(view, &TimelineView::drag_moved, this,
				&TimelineWidget::view_drag_moved);
		connect(view, &TimelineView::drag_left, this,
				&TimelineWidget::view_drag_left);
		connect(view, &TimelineView::drag_dropped, this,
				&TimelineWidget::view_drag_dropped);

		connect(tview->splitter(), &QSplitter::splitterMoved, this,
				&TimelineWidget::update_horizontal_splitters);
	}

	// Split viewer 50/50
	QList<int> view_sizes;
	view_sizes.reserve(views_.size());
	view_sizes.append(height() / 2); // Video
	view_sizes.append(height() / 2); // Audio
	view_sizes.append(0); // Subtitle (hidden by default)
	view_splitter_->setSizes(view_sizes);

	// Video and audio are not collapsible, subtitle is
	view_splitter_->setCollapsible(Track::k_video, false);
	view_splitter_->setCollapsible(Track::k_audio, false);
	view_splitter_->setCollapsible(Track::k_subtitle, true);

	// FIXME: Magic number
	SetScale(90.0);

	set_auto_set_timebase(false);

	connect(Core::instance(), &Core::tool_changed, this,
			&TimelineWidget::tool_changed);
	connect(Core::instance(), &Core::addable_object_changed, this,
			&TimelineWidget::addable_object_changed);

	signal_block_change_timer_ = new QTimer(this);
	signal_block_change_timer_->setInterval(1);
	signal_block_change_timer_->setSingleShot(true);
	connect(signal_block_change_timer_, &QTimer::timeout, this, [this] {
		signal_block_change_timer_->stop();

		if (OAK_CONFIG("SelectAlsoSeeks").toBool()) {
			Rational start = RATIONAL_MAX;
			for (Block *b : selected_blocks_) {
				start = std::min(start, b->in());
			}
			if (start != RATIONAL_MAX) {
				get_connected_node()->set_playhead(start);
			}
		}

		emit block_selection_changed(selected_blocks_);
	});
}

TimelineWidget::~TimelineWidget()
{
	// Ensure no blocks are selected before any child widgets are destroyed (prevents corrupt ViewSelectionChanged() signal)
	connect_viewer_node(nullptr);

	clear();

	qDeleteAll(tools_);

	delete subtitle_show_command_;
}

void TimelineWidget::clear()
{
	// Emit that we've deselected any selected blocks
	signal_deselected_all_blocks();

	// Set null timebase
	SetTimebase(0);
}

void TimelineWidget::TimebaseChangedEvent(const Rational &timebase)
{
	super::TimebaseChangedEvent(timebase);

	timecode_label_->set_timebase(timebase);

	timecode_label_->setVisible(!timebase.isNull());

	update_view_timebases();
}

void TimelineWidget::resizeEvent(QResizeEvent *event)
{
	super::resizeEvent(event);

	// Update timecode label size
	update_timecode_width_from_splitters(views_.first()->splitter());
}

void TimelineWidget::TimeChangedEvent(const Rational &t)
{
	if (OAK_CONFIG("SeekAlsoSelects").toBool()) {
		TimelineWidgetSelections sels;

		QVector<Block *> new_blocks;

		for (auto it = sequence()->get_tracks().cbegin();
			 it != sequence()->get_tracks().cend(); it++) {
			Track *track = *it;
			if (track->is_locked()) {
				continue;
			}

			Block *b = track->visible_block_at_time(sequence()->get_playhead());
			if (!b || dynamic_cast<GapBlock *>(b)) {
				continue;
			}

			new_blocks.push_back(b);
			sels[track->to_reference()].insert(b->range());
		}

		if (selected_blocks_ != new_blocks) {
			selected_blocks_ = new_blocks;
			set_selections(sels, false);
			signal_block_selection_change();
		}
	}
}

void TimelineWidget::ScaleChangedEvent(const double &scale)
{
	super::ScaleChangedEvent(scale);

	foreach (TimelineAndTrackView *view, views_) {
		view->view()->set_scale(scale);
	}

	if (rubberband_.isVisible()) {
		QMetaObject::invokeMethod(this, &TimelineWidget::force_update_rubber_band,
								  Qt::QueuedConnection);
	}
}

void TimelineWidget::ConnectNodeEvent(ViewerOutput *n)
{
	Sequence *s = static_cast<Sequence *>(n);

	connect(s, &Sequence::track_added, this, &TimelineWidget::add_track);
	connect(s, &Sequence::track_removed, this, &TimelineWidget::remove_track);
	connect(s, &Sequence::frame_rate_changed, this,
			&TimelineWidget::frame_rate_changed);
	connect(s, &Sequence::sample_rate_changed, this,
			&TimelineWidget::sample_rate_changed);

	connect(timecode_label_, &RationalSlider::value_changed, s,
			&Sequence::set_playhead);
	connect(s, &Sequence::playhead_changed, timecode_label_,
			&RationalSlider::set_value);
	timecode_label_->set_value(s->get_playhead());

	ruler()->set_playback_cache(n->video_frame_cache());

	SetTimebase(n->get_video_params().frame_rate_as_time_base());

	for (int i = 0; i < views_.size(); i++) {
		Track::Type track_type = static_cast<Track::Type>(i);
		TimelineView *view = views_.at(i)->view();
		TrackList *track_list = s->track_list(track_type);
		TrackView *track_view = views_.at(i)->track_view();

		track_view->connect_track_list(track_list);
		view->connect_track_list(track_list);

		// Defer to the track to make all the block UI items necessary
		const QVector<Track *> tracks = s->track_list(track_type)->get_tracks();
		foreach (Track *track, tracks) {
			add_track(track);
		}
	}
}

void TimelineWidget::DisconnectNodeEvent(ViewerOutput *n)
{
	Sequence *s = static_cast<Sequence *>(n);

	disconnect(s, &Sequence::track_added, this, &TimelineWidget::add_track);
	disconnect(s, &Sequence::track_removed, this, &TimelineWidget::remove_track);
	disconnect(s, &Sequence::frame_rate_changed, this,
			   &TimelineWidget::frame_rate_changed);
	disconnect(s, &Sequence::sample_rate_changed, this,
			   &TimelineWidget::sample_rate_changed);

	disconnect(timecode_label_, &RationalSlider::value_changed, s,
			   &Sequence::set_playhead);

	deselect_all();

	foreach (Track *track, s->get_tracks()) {
		remove_track(track);
	}

	ruler()->set_playback_cache(nullptr);

	SetTimebase(0);

	clear();

	foreach (TimelineAndTrackView *tview, views_) {
		tview->track_view()->disconnect_track_list();
		tview->view()->connect_track_list(nullptr);
	}
}

void TimelineWidget::SendCatchUpScrollEvent()
{
	super::SendCatchUpScrollEvent();

	if (rubberband_.isVisible()) {
		this->force_update_rubber_band();
	}
}

void TimelineWidget::select_all()
{
	QVector<Block *> newly_selected_blocks;

	foreach (Block *block, added_blocks_) {
		if (!selected_blocks_.contains(block)) {
			newly_selected_blocks.append(block);
			add_selection(block);
		}
	}

	signal_selected_blocks(newly_selected_blocks, false);
}

void TimelineWidget::deselect_all()
{
	// Clear selections
	selections_.clear();

	// Update all viewports
	update_viewports();

	// Clear list and emit signal
	signal_deselected_all_blocks();
}

void TimelineWidget::ripple_to_in()
{
	ripple_to(Timeline::k_trim_in);
}

void TimelineWidget::ripple_to_out()
{
	ripple_to(Timeline::k_trim_out);
}

void TimelineWidget::edit_to_in()
{
	edit_to(Timeline::k_trim_in);
}

void TimelineWidget::edit_to_out()
{
	edit_to(Timeline::k_trim_out);
}

void TimelineWidget::split_at_playhead()
{
	if (!get_connected_node()) {
		return;
	}

	const Rational &playhead_time = get_connected_node()->get_playhead();

	QVector<Block *> selected_blocks = get_selected_blocks();

	// Prioritize blocks that are selected and overlap the playhead
	QVector<Block *> blocks_to_split;
	QVector<bool> block_is_selected;

	bool some_blocks_are_selected = false;

	// Get all blocks at the playhead
	foreach (Track *track, sequence()->get_tracks()) {
		if (track->is_locked()) {
			continue;
		}

		Block *b = track->block_containing_time(playhead_time);

		if (dynamic_cast<ClipBlock *>(b)) {
			bool selected = false;

			// See if this block is selected
			foreach (Block *item, selected_blocks) {
				if (item == b) {
					some_blocks_are_selected = true;
					selected = true;
					break;
				}
			}

			blocks_to_split.append(b);
			block_is_selected.append(selected);
		}
	}

	// If some blocks are selected, we prioritize those and don't split the blocks that aren't
	if (some_blocks_are_selected) {
		for (int i = 0; i < block_is_selected.size(); i++) {
			if (!block_is_selected.at(i)) {
				blocks_to_split.removeAt(i);
				block_is_selected.removeAt(i);
				i--;
			}
		}
	}

	if (!blocks_to_split.isEmpty()) {
		Core::instance()->undo_stack()->push(
			new BlockSplitPreservingLinksCommand(blocks_to_split,
												 { playhead_time }),
			tr("Split Clips At Playhead"));
	}
}

void TimelineWidget::replace_blocks_with_gaps(const QVector<Block *> &blocks,
										   bool remove_from_graph,
										   MultiUndoCommand *command,
										   bool handle_transitions)
{
	foreach (Block *b, blocks) {
		if (dynamic_cast<GapBlock *>(b)) {
			// No point in replacing a gap with a gap, and TrackReplaceBlockWithGapCommand will clear
			// up any extraneous gaps
			continue;
		}

		Track *original_track = b->track();

		command->add_child(new TrackReplaceBlockWithGapCommand(
			original_track, b, handle_transitions));

		if (remove_from_graph) {
			command->add_child(
				new NodeRemoveWithExclusiveDependenciesAndDisconnect(b));
		}
	}
}

void TimelineWidget::DeleteSelected(bool ripple)
{
	if (ruler()->has_items_selected()) {
		ruler()->delete_selected();
		return;
	}

	QVector<Block *> selected_list = get_selected_blocks();

	// No-op if nothing is selected
	if (selected_list.isEmpty()) {
		return;
	}

	QVector<Block *> clips_to_delete;
	QVector<TransitionBlock *> transitions_to_delete;

	bool all_gaps = true;

	foreach (Block *b, selected_list) {
		if (!dynamic_cast<GapBlock *>(b)) {
			all_gaps = false;
		}

		if (dynamic_cast<ClipBlock *>(b)) {
			clips_to_delete.append(b);
		} else if (dynamic_cast<TransitionBlock *>(b)) {
			transitions_to_delete.append(static_cast<TransitionBlock *>(b));
		}
	}

	if (all_gaps) {
		ripple = true;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	// Remove all selections
	command->add_child(new SetSelectionsCommand(
		this, TimelineWidgetSelections(), get_selections()));

	// For transitions, remove them but extend their attached blocks to fill their place
	foreach (TransitionBlock *transition, transitions_to_delete) {
		TransitionRemoveCommand *trc =
			new TransitionRemoveCommand(transition, true);

		// Perform the transition removal now so that replacing blocks with gaps below won't get confused
		trc->redo_now();

		command->add_child(trc);
	}

	// Replace clips with gaps (effectively deleting them)
	replace_blocks_with_gaps(clips_to_delete, true, command, false);

	// Insert ripple command now that it's all cleaned up gaps
	TimelineRippleDeleteGapsAtRegionsCommand *ripple_command = nullptr;
	Rational new_playhead = RATIONAL_MAX;
	if (ripple) {
		TimelineRippleDeleteGapsAtRegionsCommand::RangeList range_list;

		foreach (Block *b, selected_list) {
			range_list.append({ b->track(), b->range() });
			new_playhead = qMin(new_playhead, b->in());
		}

		ripple_command = new TimelineRippleDeleteGapsAtRegionsCommand(
			sequence(), range_list);
		command->add_child(ripple_command);
	}

	Core::instance()->undo_stack()->push(command, tr("Deleted Clips"));

	// Ensures any current drag operations are cancelled
	clear_ghosts();

	if (ripple_command && ripple_command->has_commands() &&
		new_playhead != RATIONAL_MAX) {
		get_connected_node()->set_playhead(new_playhead);
	}
}

void TimelineWidget::increase_track_height()
{
	if (!get_connected_node()) {
		return;
	}

	// Increase the height of each track by one "unit"
	foreach (Track *t, sequence()->get_tracks()) {
		t->set_track_height(t->get_track_height() + Track::k_track_height_interval);
	}
}

void TimelineWidget::decrease_track_height()
{
	if (!get_connected_node()) {
		return;
	}

	// Decrease the height of each track by one "unit"
	foreach (Track *t, sequence()->get_tracks()) {
		t->set_track_height(
			qMax(t->get_track_height() - Track::k_track_height_interval,
				 Track::k_track_height_minimum));
	}
}

void TimelineWidget::insert_footage_at_playhead(
	const QVector<ViewerOutput *> &footage)
{
	auto command = new MultiUndoCommand();
	import_tool_->place_at(footage, get_connected_node()->get_playhead(), true,
						  command, 0, true);
	Core::instance()->undo_stack()->push(command,
										 tr("Inserted Footage At Playhead"));
}

void TimelineWidget::overwrite_footage_at_playhead(
	const QVector<ViewerOutput *> &footage)
{
	auto command = new MultiUndoCommand();
	import_tool_->place_at(footage, get_connected_node()->get_playhead(), false,
						  command, 0, true);
	Core::instance()->undo_stack()->push(command,
										 tr("Overwrote Footage At Playhead"));
}

void TimelineWidget::toggle_links_on_selected()
{
	QVector<Node *> blocks;
	bool link = true;

	foreach (Block *item, get_selected_blocks()) {
		// Only clips can be linked
		if (!dynamic_cast<ClipBlock *>(item)) {
			continue;
		}

		// Prioritize unlinking, if any block has links, assume we're unlinking
		if (link && item->has_links()) {
			link = false;
		}

		blocks.append(item);
	}

	if (blocks.isEmpty()) {
		return;
	}

	Core::instance()->undo_stack()->push(new NodeLinkManyCommand(blocks, link),
										 tr("Linked Clips"));
}

void TimelineWidget::add_default_transitions_to_selected()
{
	QVector<ClipBlock *> blocks;

	foreach (Block *item, get_selected_blocks()) {
		// Only clips can be linked
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(item)) {
			blocks.append(clip);
		}
	}

	if (!blocks.isEmpty()) {
		Core::instance()->undo_stack()->push(
			new TimelineAddDefaultTransitionCommand(blocks, timebase()),
			tr("Added Default Transitions"));
	}
}

bool TimelineWidget::copy_selected(bool cut)
{
	if (super::copy_selected(cut)) {
		return true;
	}

	if (!get_connected_node() || selected_blocks_.isEmpty()) {
		return false;
	}

	QVector<Node *> selected_nodes;

	foreach (Block *block, selected_blocks_) {
		selected_nodes.append(block);

		QVector<Node *> deps = block->get_dependencies();

		foreach (Node *d, deps) {
			if (!selected_nodes.contains(d)) {
				selected_nodes.append(d);
			}
		}
	}

	ProjectSerializer::SaveData sdata(ProjectSerializer::k_only_clips);
	sdata.set_only_serialize_nodes_and_resolve_groups(selected_nodes);

	// Cache the earliest in point so all copied clips have a "relative" in point that can be pasted anywhere
	Rational earliest_in = RATIONAL_MAX;
	ProjectSerializer::SerializedProperties properties;

	foreach (Block *block, selected_blocks_) {
		earliest_in = qMin(earliest_in, block->in());
	}

	foreach (Block *block, selected_blocks_) {
		properties[block][QStringLiteral("in")] =
			QString::fromStdString((block->in() - earliest_in).to_string());
		properties[block][QStringLiteral("track")] =
			block->track()->to_reference().to_string();
	}

	sdata.set_properties(properties);

	ProjectSerializer::copy(sdata);

	if (cut) {
		DeleteSelected();
	}

	return true;
}

bool TimelineWidget::paste()
{
	// TimeRuler gets first chance (markers, etc.)
	if (super::paste()) {
		return true;
	}

	// Ensure we have a connected node
	if (!get_connected_node()) {
		return false;
	}

	// Attempt regular clip pasting
	if (paste_internal(false)) {
		return true;
	}

	// Give last chance to NodeParamView
	return NodeParamView::paste(
		this, std::bind(&TimelineWidget::generate_existing_paste_map, this,
						std::placeholders::_1));
}

void TimelineWidget::paste_insert()
{
	paste_internal(true);
}

void TimelineWidget::delete_in_to_out(bool ripple)
{
	if (!get_connected_node() || !get_connected_node()->get_work_area()->enabled()) {
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	if (ripple) {
		command->add_child(new TimelineRippleRemoveAreaCommand(
			sequence(), get_connected_node()->get_work_area()->in(),
			get_connected_node()->get_work_area()->out()));

	} else {
		QVector<Track *> unlocked_tracks = sequence()->get_unlocked_tracks();

		foreach (Track *track, unlocked_tracks) {
			GapBlock *gap = new GapBlock();

			gap->set_length_and_media_out(
				get_connected_node()->get_work_area()->length());

			command->add_child(new NodeAddCommand(
				static_cast<Project *>(track->parent()), gap));

			command->add_child(new TrackPlaceBlockCommand(
				sequence()->track_list(track->type()), track->index(), gap,
				get_connected_node()->get_work_area()->in()));
		}
	}

	// Clear workarea after this
	command->add_child(new WorkareaSetEnabledCommand(
		get_connected_node()->project(), get_connected_node()->get_work_area(),
		false));

	if (ripple) {
		get_connected_node()->set_playhead(
			get_connected_node()->get_work_area()->in());
	}

	Core::instance()->undo_stack()->push(command, tr("Deleted In To Out"));
}

void TimelineWidget::toggle_selected_enabled()
{
	QVector<Block *> items = get_selected_blocks();

	if (items.isEmpty()) {
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	foreach (Block *i, items) {
		command->add_child(new BlockEnableDisableCommand(i, !i->is_enabled()));
	}

	Core::instance()->undo_stack()->push(command, tr("Toggled Clips Enabled"));
}

void TimelineWidget::set_color_label(int index)
{
	MultiUndoCommand *command = new MultiUndoCommand();

	foreach (Block *b, selected_blocks_) {
		command->add_child(new NodeOverrideColorCommand(b, index));
	}

	Core::instance()->undo_stack()->push(
		command, tr("Set Colors of %1 Clips").arg(selected_blocks_.size()));
}

void TimelineWidget::nudge_left()
{
	if (get_connected_node()) {
		nudge_internal(-timebase());
	}
}

void TimelineWidget::nudge_right()
{
	if (get_connected_node()) {
		nudge_internal(timebase());
	}
}

void TimelineWidget::move_in_to_playhead()
{
	move_to_playhead_internal(false);
}

void TimelineWidget::move_out_to_playhead()
{
	move_to_playhead_internal(true);
}

void TimelineWidget::show_speed_duration_dialog_for_selected_clips()
{
	QVector<ClipBlock *> clips;

	foreach (Block *b, selected_blocks_) {
		ClipBlock *c = dynamic_cast<ClipBlock *>(b);
		if (c) {
			clips.append(c);
		}
	}

	if (!clips.isEmpty()) {
		SpeedDurationDialog sdd(clips, timebase(), this);
		sdd.exec();
	}
}

void TimelineWidget::synchronize_selected_clips_by_source_time()
{
	if (!get_connected_node()) {
		return;
	}

	const QVector<SourceSyncClip> sync_clips =
		get_selected_source_sync_clips(get_selected_blocks());
	if (sync_clips.size() < 2) {
		return;
	}

	SourceSyncClip reference = sync_clips.first();
	Rational anchor_timeline_in = sync_clips.first().clip->in();
	for (const SourceSyncClip &sync_clip : sync_clips) {
		if (sync_clip.source_head < reference.source_head) {
			reference = sync_clip;
		}
		if (sync_clip.clip->in() < anchor_timeline_in) {
			anchor_timeline_in = sync_clip.clip->in();
		}
	}

	struct SyncPlacement {
		ClipBlock *clip = nullptr;
		Rational timeline_in;
	};

	QVector<SyncPlacement> placements;
	for (const SourceSyncClip &sync_clip : sync_clips) {
		const AudioSynchronizer::Placement placement =
			AudioSynchronizer::place_by_source_time(
				reference.source, sync_clip.source, anchor_timeline_in);
		if (placement.valid) {
			placements.append({ sync_clip.clip, placement.timeline_in });
		}
	}

	if (placements.size() < 2) {
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();
	for (const SyncPlacement &placement : placements) {
		command->add_child(new TrackReplaceBlockWithGapCommand(
			placement.clip->track(), placement.clip, false));
	}

	TimelineWidgetSelections new_selections;
	for (const SyncPlacement &placement : placements) {
		command->add_child(new TrackPlaceBlockCommand(
			sequence()->track_list(placement.clip->track()->type()),
			placement.clip->track()->index(), placement.clip,
			placement.timeline_in));

		new_selections[placement.clip->track()->to_reference()].insert(
			TimeRange(placement.timeline_in,
					  placement.timeline_in + placement.clip->length()));
	}

	command->add_child(
		new SetSelectionsCommand(this, new_selections, get_selections()));

	Core::instance()->undo_stack()->push(
		command, tr("Synchronize Clips by Source Time"));
}

void TimelineWidget::synchronize_selected_clips_by_waveform()
{
	synchronize_selected_clips_by_waveform_internal(false);
}

void TimelineWidget::synchronize_selected_clips_by_waveform_with_speed()
{
	synchronize_selected_clips_by_waveform_internal(true);
}

void TimelineWidget::synchronize_selected_clips_by_waveform_internal(
	bool allow_speed)
{
	if (!get_connected_node()) {
		return;
	}

	const QVector<WaveformSyncClip> sync_clips =
		get_selected_waveform_sync_clips(get_selected_blocks());
	qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform:"
			 << sync_clips.size() << "sync clip(s) selected";
	if (sync_clips.size() < 2) {
		Core::instance()->show_status_bar_message(
			tr("Select at least 2 clips with cached waveforms to sync by waveform"));
		return;
	}

	WaveformSyncClip reference = sync_clips.first();
	for (const WaveformSyncClip &sync_clip : sync_clips) {
		if (sync_clip.clip->in() < reference.clip->in()) {
			reference = sync_clip;
		}
	}

	const int sample_rate = reference.sample_rate;
	const size_t window_samples =
		static_cast<size_t>(std::max(1, sample_rate / 20));
	const int64_t max_offset_samples =
		static_cast<int64_t>(sample_rate) * 10 * 60;
	const int64_t max_offset_windows =
		max_offset_samples / static_cast<int64_t>(window_samples);

	QVector<bool> reference_valid;
	const QVector<double> reference_envelope = extract_waveform_cache_envelope(
		reference, sample_rate, window_samples, &reference_valid);

	qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: sample_rate="
			 << sample_rate << "window_samples=" << window_samples
			 << "max_offset_windows=" << max_offset_windows
			 << "reference_envelope_size=" << reference_envelope.size();

	struct SyncPlacement {
		ClipBlock *clip = nullptr;
		Rational timeline_in;
		double speed = 1.0;
	};

	QVector<SyncPlacement> placements;
	placements.append({ reference.clip, reference.clip->in(), 1.0 });
	for (const WaveformSyncClip &sync_clip : sync_clips) {
		if (sync_clip.clip == reference.clip) {
			continue;
		}

		QVector<bool> candidate_valid;
		const QVector<double> candidate_envelope = extract_waveform_cache_envelope(
			sync_clip, sample_rate, window_samples, &candidate_valid);

		// Skip uncached (zero-filled) windows on both sides so partially
		// cached waveforms don't drag the correlation down
		AudioWaveformSync::OffsetResult offset =
			AudioWaveformSync::estimate_envelope_offset(
				reference_envelope, candidate_envelope, reference_valid,
				candidate_valid, window_samples, max_offset_windows);

		double speed = 1.0;

		if (allow_speed && (!offset.valid || offset.confidence < 0.6)) {
			// Plain offset alignment is inconclusive; the clips may run at
			// different speeds (e.g. 24fps vs 25fps pull-down). Search a
			// rate range with a tighter offset radius to keep the search
			// interactive.
			const int64_t stretch_radius_windows = std::min<int64_t>(
				max_offset_windows,
				(static_cast<int64_t>(sample_rate) * 30) /
					static_cast<int64_t>(window_samples));
			const AudioWaveformSync::StretchOffsetResult stretch =
				AudioWaveformSync::estimate_stretch_and_offset(
					reference_envelope, candidate_envelope, reference_valid,
					candidate_valid, window_samples, stretch_radius_windows,
					0.75, 1.34, 0.005);
			qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: "
						"stretch estimate valid="
					 << stretch.valid << "rate=" << stretch.rate
					 << "confidence=" << stretch.confidence;

			if (stretch.valid &&
				stretch.confidence > (offset.valid ? offset.confidence : 0.0)) {
				speed = stretch.rate;
				offset.valid = true;
				offset.confidence = stretch.confidence;
				offset.offset_samples = stretch.offset_samples;
			}
		}

		qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: candidate"
				 << sync_clip.clip << "envelope_size="
				 << candidate_envelope.size() << "offset_valid=" << offset.valid
				 << "offset_samples=" << offset.offset_samples
				 << "confidence=" << offset.confidence << "speed=" << speed;
		if (!offset.valid) {
			continue;
		}

		const AudioSynchronizer::Placement placement =
			AudioSynchronizer::place_by_waveform_offset(
				reference.clip->in(), offset.offset_samples, sample_rate);
		qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: placement"
				 << "valid=" << placement.valid << "timeline_in="
				 << placement.timeline_in.to_double();
		if (placement.valid && placement.timeline_in >= 0) {
			placements.append({ sync_clip.clip, placement.timeline_in, speed });
		}
	}

	if (placements.size() < 2) {
		qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: no usable"
				 << "offsets found";
		Core::instance()->show_status_bar_message(
			tr("Could not find a usable waveform offset for the selected clips"));
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();
	for (const SyncPlacement &placement : placements) {
		command->add_child(new TrackReplaceBlockWithGapCommand(
			placement.clip->track(), placement.clip, false));

		if (placement.speed != 1.0) {
			command->add_child(new NodeParamSetStandardValueCommand(
				NodeKeyframeTrackReference(
					NodeInput(placement.clip, ClipBlock::k_speed_input)),
				placement.clip->speed() * placement.speed));
		}
	}

	TimelineWidgetSelections new_selections;
	for (const SyncPlacement &placement : placements) {
		command->add_child(new TrackPlaceBlockCommand(
			sequence()->track_list(placement.clip->track()->type()),
			placement.clip->track()->index(), placement.clip,
			placement.timeline_in));

		// A speed change scales the clip's timeline length accordingly
		const Rational placed_length =
			placement.speed == 1.0 ?
				placement.clip->length() :
				Rational::from_double(placement.clip->length().to_double() /
									 placement.speed);
		new_selections[placement.clip->track()->to_reference()].insert(
			TimeRange(placement.timeline_in,
					  placement.timeline_in + placed_length));
	}

	command->add_child(
		new SetSelectionsCommand(this, new_selections, get_selections()));

	Core::instance()->undo_stack()->push(command,
													 tr("Synchronize Clips by Waveform"));
	Core::instance()->show_status_bar_message(
		tr("Synchronized %1 clip(s) by waveform").arg(placements.size()));
}

void TimelineWidget::generate_proxies_for_selected_clips()
{
	if (!ProxyManager::instance() || !sequence()) {
		qWarning()
			<< "GenerateProxiesForSelectedClips: ProxyManager or sequence unavailable";
		return;
	}

	const QVector<Footage *> footage =
		get_selected_proxy_footage(selected_blocks_);
	qDebug() << "GenerateProxiesForSelectedClips: starting proxy generation for"
			 << footage.size() << "footage item(s)";
	for (Footage *item : footage) {
		const VideoParams video = item->get_first_enabled_video_stream();
		if (!video.is_valid()) {
			qWarning()
				<< "GenerateProxiesForSelectedClips: skipping item with no valid video stream"
				<< item->filename();
			continue;
		}

		ProxyManager::ProxyParams params = item->get_effective_proxy_params();
		const ProxyManager::Proxy proxy =
			ProxyManager::instance()->get_or_start_proxy(
				item->project()->cache_path(), item->filename(),
				video.stream_index(), params);
		qDebug() << "GenerateProxiesForSelectedClips: proxy state="
				 << ProxyManager::proxy_state_to_string(proxy.state)
				 << "file=" << proxy.filename
				 << "cache=" << item->project()->cache_path();
		item->set_proxy(proxy.filename, proxy.state, video.stream_index(),
					   params.version, true);
		item->invalidate_all(Footage::k_filename_input);
	}
}

void TimelineWidget::set_selected_clips_proxy_enabled(bool enabled)
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(selected_blocks_);
	qDebug() << "TimelineWidget::SetSelectedClipsProxyEnabled:" << enabled
			 << "footage count=" << footage.size();
	for (Footage *item : footage) {
		if (item->proxy_path().isEmpty()) {
			qDebug()
				<< "  skipping item with empty proxy path" << item->filename();
			continue;
		}

		item->set_proxy_enabled(enabled);
		item->invalidate_all(Footage::k_filename_input);
	}
}

void TimelineWidget::reveal_proxy_for_selected_clips()
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(selected_blocks_);
	for (Footage *item : footage) {
		if (item->proxy_path().isEmpty()) {
			continue;
		}

#if defined(Q_OS_WINDOWS)
		QStringList args;
		args << "/select," << QDir::toNativeSeparators(item->proxy_path());
		QProcess::startDetached(QStringLiteral("explorer"), args);
#elif defined(Q_OS_MAC)
		QStringList args;
		args << "-e";
		args << "tell application \"Finder\"";
		args << "-e";
		args << "activate";
		args << "-e";
		args << "select POSIX file \"" + item->proxy_path() + "\"";
		args << "-e";
		args << "end tell";
		QProcess::startDetached(QStringLiteral("osascript"), args);
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QFileInfo(item->proxy_path()).dir().absolutePath()));
#endif
		return;
	}
}

void TimelineWidget::delete_proxies_for_selected_clips()
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(selected_blocks_);
	for (Footage *item : footage) {
		if (item->proxy_path().isEmpty()) {
			continue;
		}

		QFile::remove(item->proxy_path());
		QFile::remove(
			ProxyManager::get_working_proxy_filename(item->proxy_path()));
		item->clear_proxy();
		item->invalidate_all(Footage::k_filename_input);
	}
}

void TimelineWidget::show_proxy_dialog_for_selected_clips()
{
	ProxyDialog d(this, get_selected_proxy_footage(selected_blocks_));
	d.exec();
}

void TimelineWidget::recording_callback(const QString &filename,
									   const TimeRange &time,
									   const Track::Reference &track)
{
	ProjectImportTask task(get_connected_node()->project()->root(), { filename });
	task.start();

	auto subimport_command = task.get_command();

	if (task.get_imported_footage().empty()) {
		qCritical() << "Failed to import recorded audio file" << filename;
		delete subimport_command;
	} else {
		subimport_command->redo_now();

		auto import_command = new MultiUndoCommand();
		import_command->add_child(subimport_command);

		import_tool_->place_at({ task.get_imported_footage().front() }, time.in(),
							  false, import_command, track.index());
		Core::instance()->undo_stack()->push(import_command,
											 tr("Recorded Audio Clip"));
	}
}

void TimelineWidget::enable_recording_overlay(const TimelineCoordinate &coord)
{
	foreach (TimelineAndTrackView *tview, views_) {
		tview->view()->enable_recording_overlay(coord);
	}
}

void TimelineWidget::disable_recording_overlay()
{
	foreach (TimelineAndTrackView *tview, views_) {
		tview->view()->disable_recording_overlay();
	}
}

void TimelineWidget::add_tentative_subtitle_track()
{
	if (!subtitle_show_command_) {
		// Determine if we need to do anything
		QList<int> sz = view_splitter_->sizes();
		bool should_adjust_splitter = (sz[Track::k_subtitle] == 0);
		bool should_add_sub_track =
			(sequence() &&
			 sequence()->track_list(Track::k_subtitle)->get_track_count() == 0);

		if (should_adjust_splitter || should_add_sub_track) {
			// Create command
			subtitle_show_command_ = new MultiUndoCommand();

			if (should_adjust_splitter) {
				sz[Track::k_subtitle] = height() / Track::k_count;
				subtitle_show_command_->add_child(
					new SetSplitterSizesCommand(view_splitter_, sz));
			}

			if (should_add_sub_track) {
				TimelineAddTrackCommand *track_add_cmd =
					new TimelineAddTrackCommand(
						sequence()->track_list(Track::k_subtitle));
				subtitle_tentative_track_ = track_add_cmd->track();
				subtitle_show_command_->add_child(track_add_cmd);
			}

			subtitle_show_command_->redo_now();
		}
	}
}

void TimelineWidget::nest_selected_clips()
{
	if (!get_connected_node()) {
		return;
	}

	QVector<Block *> blocks = this->selected_blocks_;
	if (blocks.empty()) {
		return;
	}

	QVector<Track::Reference> tracks(blocks.size());
	QVector<TimeRange> times(blocks.size());
	QVector<int> track_offset(Track::k_count, INT_MAX);
	Rational start_time = RATIONAL_MAX;
	Rational end_time = RATIONAL_MIN;
	for (int i = 0; i < blocks.size(); i++) {
		Block *b = blocks.at(i);

		Track::Reference tf = b->track()->to_reference();
		;
		tracks[i] = tf;
		times[i] = b->range();

		int &to = track_offset[tf.type()];
		to = std::min(to, tf.index());

		start_time = std::min(start_time, b->in());
		end_time = std::max(end_time, b->out());
	}

	auto move_to_nest_command = new MultiUndoCommand();

	// Remove blocks from this sequence
	replace_blocks_with_gaps(blocks, false, move_to_nest_command);

	// Create new sequence
	Project *project = this->get_connected_node()->project();
	Sequence *nest =
		Core::create_new_sequence_for_project(tr("Nested Sequence %1"), project);
	nest->set_video_params(get_connected_node()->get_video_params());
	nest->set_audio_params(get_connected_node()->get_audio_params());
	move_to_nest_command->add_child(new NodeAddCommand(project, nest));

	// Add to same folder
	move_to_nest_command->add_child(
		new FolderAddChild(this->get_connected_node()->folder(), nest));

	// Place blocks in new sequence
	for (int i = 0; i < blocks.size(); i++) {
		Block *b = blocks.at(i);

		const TimeRange &range = times.at(i);
		Track::Reference track = tracks.at(i);

		move_to_nest_command->add_child(new TrackPlaceBlockCommand(
			nest->track_list(track.type()),
			track.index() - track_offset.at(track.type()), b,
			range.in() - start_time));
	}

	// Do this command now, because we later do checks and actions that rely on these having been done
	move_to_nest_command->redo_now();

	auto meta_command = new MultiUndoCommand();
	meta_command->add_child(move_to_nest_command);

	// Find first free track index
	bool empty = false;
	int index = -1;
	while (!empty) {
		index++;
		empty = true;
		for (int i = 0; i < Track::k_count; i++) {
			if (track_offset.at(i) == INT_MAX) {
				// No clips on this track
				continue;
			}

			TrackList *list =
				sequence()->track_list(static_cast<Track::Type>(i));
			if (index < list->get_track_count() &&
				!list->get_track_at(index)->is_range_free(
					TimeRange(start_time, end_time))) {
				empty = false;
				break;
			}
		}
	}

	// Place new sequence in this sequence
	import_tool_->place_at({ nest }, start_time, false, meta_command, index);

	Core::instance()->undo_stack()->push(meta_command, tr("Nested Clips"));
}

void TimelineWidget::clear_tentative_subtitle_track()
{
	if (subtitle_show_command_) {
		subtitle_show_command_->undo_now();
		delete subtitle_show_command_;
		subtitle_show_command_ = nullptr;
		subtitle_tentative_track_ = nullptr;
	}
}

void TimelineWidget::insert_gaps_at(const Rational &earliest_point,
								  const Rational &insert_length,
								  MultiUndoCommand *command)
{
	for (int i = 0; i < Track::k_count; i++) {
		command->add_child(new TrackListInsertGaps(
			sequence()->track_list(static_cast<Track::Type>(i)), earliest_point,
			insert_length));
	}
}

Track *TimelineWidget::get_track_from_reference(const Track::Reference &ref) const
{
	return sequence()->track_list(ref.type())->get_track_at(ref.index());
}

int TimelineWidget::get_track_y(const Track::Reference &ref)
{
	return views_.at(ref.type())->view()->get_track_y(ref.index());
}

int TimelineWidget::get_track_height(const Track::Reference &ref)
{
	return views_.at(ref.type())->view()->get_track_height(ref.index());
}

void TimelineWidget::center_on(qreal scene_pos)
{
	scrollbar()->setValue(qRound(scene_pos - scrollbar()->width() / 2));
}

void TimelineWidget::clear_ghosts()
{
	if (!ghost_items_.isEmpty()) {
		foreach (TimelineViewGhostItem *ghost, ghost_items_) {
			delete ghost;
		}

		ghost_items_.clear();
	}

	hide_snaps();
}

TimelineTool *TimelineWidget::get_active_tool()
{
	return tools_.at(Core::instance()->tool());
}

void TimelineWidget::view_mouse_pressed(TimelineViewMouseEvent *event)
{
	active_tool_ = get_active_tool();

	if (get_connected_node() && active_tool_ != nullptr) {
		active_tool_->mouse_press(event);
		update_viewports();
	}

	if (event->get_button() != Qt::LeftButton) {
		// Suspend tool immediately if the cursor isn't the primary button
		active_tool_->mouse_release(event);
		update_viewports();
		active_tool_ = nullptr;
	}
}

void TimelineWidget::view_mouse_moved(TimelineViewMouseEvent *event)
{
	if (get_connected_node()) {
		if (active_tool_) {
			active_tool_->mouse_move(event);

			update_viewports();

			set_catch_up_scroll_value(event->get_screen_pos().x());
		} else {
			// Mouse is not down, attempt a hover event
			TimelineTool *hover_tool = get_active_tool();

			if (hover_tool) {
				hover_tool->hover_move(event);
			}
		}
	}
}

void TimelineWidget::view_mouse_released(TimelineViewMouseEvent *event)
{
	stop_catch_up_scroll_timer();

	if (active_tool_) {
		if (get_connected_node()) {
			active_tool_->mouse_release(event);
			update_viewports();
		}

		active_tool_ = nullptr;
	}
}

void TimelineWidget::view_mouse_double_clicked(TimelineViewMouseEvent *event)
{
	// kHand tool will return nullptr
	if (!get_active_tool()) {
		// Only kHand should return a nullptr
		Q_ASSERT(Core::instance()->tool() == olive::Tool::k_hand);
		return;
	}
	if (get_connected_node()) {
		get_active_tool()->mouse_double_click(event);
		update_viewports();
	}
}

void TimelineWidget::view_drag_entered(TimelineViewMouseEvent *event)
{
	import_tool_->drag_enter(event);
	update_viewports();
}

void TimelineWidget::view_drag_moved(TimelineViewMouseEvent *event)
{
	import_tool_->drag_move(event);
	update_viewports();

	set_catch_up_scroll_value(event->get_screen_pos().x());
}

void TimelineWidget::view_drag_left(QDragLeaveEvent *event)
{
	stop_catch_up_scroll_timer();

	import_tool_->drag_leave(event);
	update_viewports();
}

void TimelineWidget::view_drag_dropped(TimelineViewMouseEvent *event)
{
	stop_catch_up_scroll_timer();

	import_tool_->drag_drop(event);
	update_viewports();
}

void TimelineWidget::add_block(Block *block)
{
	// Set up clip with view parameters (clip item will automatically size its rect accordingly)
	if (!added_blocks_.contains(block)) {
		connect(block, &Block::links_changed, this,
				&TimelineWidget::block_updated);
		connect(block, &Block::label_changed, this,
				&TimelineWidget::block_updated);
		connect(block, &Block::color_changed, this,
				&TimelineWidget::block_updated);
		connect(block, &Block::enabled_changed, this,
				&TimelineWidget::block_updated);
		connect(block, &Block::preview_changed, this,
				&TimelineWidget::block_updated);

		added_blocks_.append(block);

		if (selections_[block->track()->to_reference()].contains(
				block->range()) &&
			!selected_blocks_.contains(block)) {
			selected_blocks_.append(block);
		}
	}
}

void TimelineWidget::remove_block(Block *block)
{
	// Disconnect all signals
	disconnect(block, &Block::links_changed, this,
			   &TimelineWidget::block_updated);
	disconnect(block, &Block::label_changed, this,
			   &TimelineWidget::block_updated);
	disconnect(block, &Block::color_changed, this,
			   &TimelineWidget::block_updated);
	disconnect(block, &Block::enabled_changed, this,
			   &TimelineWidget::block_updated);
	disconnect(block, &Block::preview_changed, this,
			   &TimelineWidget::block_updated);

	// Take item from map
	added_blocks_.removeOne(block);

	// If selected, deselect it
	int select_index = selected_blocks_.indexOf(block);
	if (select_index > -1) {
		selected_blocks_.removeAt(select_index);
		remove_selection(block);

		signal_block_selection_change();
	}
}

void TimelineWidget::add_track(Track *track)
{
	foreach (Block *b, track->blocks()) {
		add_block(b);
	}

	connect(track, &Track::index_changed, this, &TimelineWidget::track_updated);
	connect(track, &Track::index_changed, this,
			&TimelineWidget::track_index_changed);
	connect(track, &Track::blocks_refreshed, this,
			&TimelineWidget::track_updated);
	connect(track, &Track::track_height_changed, this,
			&TimelineWidget::track_updated);
	connect(track, &Track::block_added, this, &TimelineWidget::add_block);
	connect(track, &Track::block_removed, this, &TimelineWidget::remove_block);
}

void TimelineWidget::remove_track(Track *track)
{
	disconnect(track, &Track::index_changed, this,
			   &TimelineWidget::track_updated);
	disconnect(track, &Track::index_changed, this,
			   &TimelineWidget::track_index_changed);
	disconnect(track, &Track::blocks_refreshed, this,
			   &TimelineWidget::track_updated);
	disconnect(track, &Track::track_height_changed, this,
			   &TimelineWidget::track_updated);
	disconnect(track, &Track::block_added, this, &TimelineWidget::add_block);
	disconnect(track, &Track::block_removed, this, &TimelineWidget::remove_block);

	remove_selection(TimeRange(0, RATIONAL_MAX), track->to_reference());

	foreach (Block *b, track->blocks()) {
		remove_block(b);
	}
}

void TimelineWidget::track_updated()
{
	update_viewports(static_cast<Track *>(sender())->type());
}

void TimelineWidget::block_updated()
{
	update_viewports(static_cast<Block *>(sender())->track()->type());
}

void TimelineWidget::update_horizontal_splitters()
{
	QSplitter *sender_splitter = static_cast<QSplitter *>(sender());

	foreach (TimelineAndTrackView *tview, views_) {
		QSplitter *recv_splitter = tview->splitter();

		if (recv_splitter != sender_splitter) {
			recv_splitter->blockSignals(true);
			recv_splitter->setSizes(sender_splitter->sizes());
			recv_splitter->blockSignals(false);
		}
	}

	update_timecode_width_from_splitters(sender_splitter);
}

void TimelineWidget::update_timecode_width_from_splitters(QSplitter *s)
{
	timecode_label_->setFixedWidth(s->sizes().first() + s->handleWidth());
}

void TimelineWidget::show_context_menu()
{
	Menu menu(this);

	QVector<Block *> selected = get_selected_blocks();

	if (!selected.isEmpty()) {
		MenuShared::instance()->add_items_for_edit_menu(&menu, true);

		menu.addSeparator();

		MenuShared::instance()->add_color_coding_menu(&menu);

		menu.addSeparator();

		QAction *sync_by_source_time =
			menu.addAction(tr("Synchronize by Source Time"));
		sync_by_source_time->setEnabled(
			get_selected_source_sync_clips(selected).size() >= 2);
		connect(sync_by_source_time, &QAction::triggered, this,
				&TimelineWidget::synchronize_selected_clips_by_source_time);

		QAction *sync_by_waveform =
			menu.addAction(tr("Synchronize by Waveform"));
		sync_by_waveform->setEnabled(
			get_selected_waveform_sync_clips(selected).size() >= 2);
		sync_by_waveform->setShortcut(
			QKeySequence(QStringLiteral("Ctrl+Shift+W")));
		sync_by_waveform->setShortcutContext(Qt::WidgetShortcut);
		this->addAction(sync_by_waveform);
		connect(sync_by_waveform, &QAction::triggered, this,
				&TimelineWidget::synchronize_selected_clips_by_waveform);

		QAction *sync_by_waveform_speed =
			menu.addAction(tr("Synchronize by Waveform (Adjust Speed)"));
		sync_by_waveform_speed->setEnabled(
			get_selected_waveform_sync_clips(selected).size() >= 2);
		connect(sync_by_waveform_speed, &QAction::triggered, this,
				&TimelineWidget::synchronize_selected_clips_by_waveform_with_speed);

		menu.addSeparator();

		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(selected.first())) {
			{
				Menu *cache_menu = new Menu(tr("Cache"), &menu);
				menu.addMenu(cache_menu);

				QAction *autocache_action =
					cache_menu->addAction(tr("Auto-Cache"));
				autocache_action->setCheckable(true);
				autocache_action->setChecked(clip->is_autocaching());
				connect(autocache_action, &QAction::triggered, this,
						&TimelineWidget::set_selected_clips_autocaching);

				cache_menu->addSeparator();

				auto cache_clip = cache_menu->addAction(tr("Cache All"));
				connect(cache_clip, &QAction::triggered, this,
						&TimelineWidget::cache_clips);

				auto cache_inout = cache_menu->addAction(tr("Cache In/Out"));
				connect(cache_inout, &QAction::triggered, this,
						&TimelineWidget::cache_clips_in_out);

				auto cache_discard = cache_menu->addAction(tr("Discard"));
				connect(cache_discard, &QAction::triggered, this,
						&TimelineWidget::cache_discard);
			}

			{
				const QVector<Footage *> proxy_footage =
					get_selected_proxy_footage(selected);
				Menu *proxy_menu = new Menu(tr("Proxy"), &menu);
				menu.addMenu(proxy_menu);

				QAction *generate_proxy =
					proxy_menu->addAction(tr("Generate Proxy"));
				generate_proxy->setEnabled(!proxy_footage.isEmpty());
				connect(generate_proxy, &QAction::triggered, this,
						&TimelineWidget::generate_proxies_for_selected_clips);

				QAction *use_proxy = proxy_menu->addAction(tr("Use Proxy"));
				use_proxy->setCheckable(true);
				use_proxy->setEnabled(!proxy_footage.isEmpty());
				use_proxy->setChecked(
					!proxy_footage.isEmpty() &&
					std::all_of(proxy_footage.cbegin(), proxy_footage.cend(),
								[](const Footage *footage) {
									return footage->proxy_enabled();
								}));
				connect(use_proxy, &QAction::triggered, this,
						&TimelineWidget::set_selected_clips_proxy_enabled);

				QAction *reveal_proxy =
					proxy_menu->addAction(tr("Reveal Proxy"));
				reveal_proxy->setEnabled(
					std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
								[](const Footage *footage) {
									return !footage->proxy_path().isEmpty();
								}));
				connect(reveal_proxy, &QAction::triggered, this,
						&TimelineWidget::reveal_proxy_for_selected_clips);

				QAction *delete_proxy =
					proxy_menu->addAction(tr("Delete Proxy"));
				delete_proxy->setEnabled(
					std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
								[](const Footage *footage) {
									return !footage->proxy_path().isEmpty();
								}));
				connect(delete_proxy, &QAction::triggered, this,
						&TimelineWidget::delete_proxies_for_selected_clips);

				QAction *proxy_settings =
					proxy_menu->addAction(tr("Proxy Settings..."));
				connect(proxy_settings, &QAction::triggered, this,
						&TimelineWidget::show_proxy_dialog_for_selected_clips);
			}

			if (clip->connected_viewer()) {
				QAction *reveal_in_footage_viewer =
					menu.addAction(tr("Reveal in Footage Viewer"));
				reveal_in_footage_viewer->setData(
					reinterpret_cast<quintptr>(clip->connected_viewer()));
				reveal_in_footage_viewer->setProperty(
					"range", QVariant::fromValue(clip->media_range()));
				connect(reveal_in_footage_viewer, &QAction::triggered, this,
						&TimelineWidget::reveal_in_footage_viewer);

				QAction *reveal_in_project =
					menu.addAction(tr("Reveal in Project"));
				reveal_in_project->setData(
					reinterpret_cast<quintptr>(clip->connected_viewer()));
				connect(reveal_in_project, &QAction::triggered, this,
						&TimelineWidget::reveal_in_project);

				if (Sequence *sequence =
						dynamic_cast<Sequence *>(clip->connected_viewer())) {
					QAction *multicam_enabled = menu.addAction(tr("Multi-Cam"));
					multicam_enabled->setCheckable(true);

					MultiCamNode *mcn = nullptr;
					auto paths = clip->find_ways_node_arrives_here(sequence);

					for (const NodeInput &i : paths) {
						if ((mcn = dynamic_cast<MultiCamNode *>(i.node()))) {
							break;
						}
					}

					multicam_enabled->setChecked(mcn);

					connect(multicam_enabled, &QAction::triggered, this,
							&TimelineWidget::multicam_enabled_triggered);
				}
			}
		}

		menu.addSeparator();

		QAction *properties_action = menu.addAction(tr("Properties"));
		connect(properties_action, &QAction::triggered, this,
				&TimelineWidget::show_speed_duration_dialog_for_selected_clips);
	}

	if (selected.isEmpty()) {
		QAction *toggle_audio_units =
			menu.addAction(tr("Use Audio Time Units"));
		toggle_audio_units->setCheckable(true);
		toggle_audio_units->setChecked(use_audio_time_units_);
		connect(toggle_audio_units, &QAction::triggered, this,
				&TimelineWidget::set_use_audio_time_units);

		{
			Menu *thumbnail_menu = new Menu(tr("Show Thumbnails"), &menu);
			menu.addMenu(thumbnail_menu);

			thumbnail_menu->add_action_with_data(
				tr("Disabled"), Timeline::k_thumbnail_off,
				OAK_CONFIG("TimelineThumbnailMode"));
			thumbnail_menu->add_action_with_data(
				tr("Only At In Points"), Timeline::k_thumbnail_in_out,
				OAK_CONFIG("TimelineThumbnailMode"));
			thumbnail_menu->add_action_with_data(
				tr("Enabled"), Timeline::k_thumbnail_on,
				OAK_CONFIG("TimelineThumbnailMode"));

			connect(thumbnail_menu, &Menu::triggered, this,
					&TimelineWidget::set_view_thumbnails_enabled);
		}

		QAction *show_waveforms = menu.addAction(tr("Show Waveforms"));
		show_waveforms->setCheckable(true);
		show_waveforms->setChecked(
			OAK_CONFIG("TimelineWaveformMode").toInt() ==
			Timeline::k_waveforms_enabled);
		connect(show_waveforms, &QAction::triggered, this,
				&TimelineWidget::set_view_waveforms_enabled);

		menu.addSeparator();

		QAction *properties_action = menu.addAction(tr("Properties"));
		connect(properties_action, &QAction::triggered, this,
				&TimelineWidget::show_sequence_dialog);
	}

	menu.exec(QCursor::pos());
}

void TimelineWidget::DeferredScrollAction()
{
	scrollbar()->setValue(deferred_scroll_value_);
}

void TimelineWidget::show_sequence_dialog()
{
	if (!get_connected_node()) {
		return;
	}

	SequenceDialog sd(sequence(), SequenceDialog::k_existing, this);
	sd.exec();
}

void TimelineWidget::set_use_audio_time_units(bool use)
{
	use_audio_time_units_ = use;

	// Update timebases
	update_view_timebases();
}

void TimelineWidget::tool_changed()
{
	hide_snaps();
	set_view_beam_cursor(TimelineCoordinate(0, Track::k_none, -1));
	set_view_transition_overlay(nullptr, nullptr);

	addable_object_changed();
}

void TimelineWidget::addable_object_changed()
{
	// Special cast for subtitle adding - ensure section is visible
	if (Core::instance()->tool() == Tool::k_add &&
		Core::instance()->get_selected_addable_object() ==
			Tool::k_addable_subtitle) {
		add_tentative_subtitle_track();
	} else {
		clear_tentative_subtitle_track();
	}
}

void TimelineWidget::set_view_waveforms_enabled(bool e)
{
	OAK_CONFIG("TimelineWaveformMode") = e ? Timeline::k_waveforms_enabled :
											   Timeline::k_waveforms_disabled;
	update_viewports();
}

void TimelineWidget::set_view_thumbnails_enabled(QAction *action)
{
	OAK_CONFIG("TimelineThumbnailMode") = action->data();
	update_viewports();
}

void TimelineWidget::frame_rate_changed()
{
	SetTimebase(get_connected_node()->get_video_params().frame_rate_as_time_base());
}

void TimelineWidget::sample_rate_changed()
{
	update_view_timebases();
}

void TimelineWidget::track_index_changed(int old, int now)
{
	Track *track = static_cast<Track *>(sender());

	Track::Reference old_ref(track->type(), old);
	Track::Reference new_ref(track->type(), now);

	auto track_selections = selections_.take(old_ref);
	if (!track_selections.isEmpty()) {
		selections_.insert(new_ref, track_selections);
	}
}

void TimelineWidget::signal_block_selection_change()
{
	signal_block_change_timer_->stop();
	signal_block_change_timer_->start();
}

void TimelineWidget::reveal_in_footage_viewer()
{
	QAction *a = static_cast<QAction *>(sender());

	ViewerOutput *item_to_reveal =
		reinterpret_cast<ViewerOutput *>(a->data().value<quintptr>());
	TimeRange r = a->property("range").value<TimeRange>();

	emit reveal_viewer_in_footage_viewer(item_to_reveal, r);
}

void TimelineWidget::reveal_in_project()
{
	QAction *a = static_cast<QAction *>(sender());

	ViewerOutput *item_to_reveal =
		reinterpret_cast<ViewerOutput *>(a->data().value<quintptr>());

	emit reveal_viewer_in_project(item_to_reveal);
}

void TimelineWidget::rename_selected_blocks()
{
	MultiUndoCommand *command = new MultiUndoCommand();
	QVector<Node *> nodes(selected_blocks_.size());

	for (int i = 0; i < nodes.size(); i++) {
		nodes[i] = selected_blocks_[i];
	}

	Core::instance()->label_nodes(nodes);
	Core::instance()->undo_stack()->push(
		command, tr("Renamed %1 Clip(s)").arg(nodes.size()));
}

void TimelineWidget::track_about_to_be_deleted(Track *track)
{
	if (track == subtitle_tentative_track_) {
		// User is deleting the tentative subtitle track. Technically they shouldn't do this, but they
		// might if they misinterpret it as permanent. If so, we handle it cleanly by pushing our
		// command as if the action really were permanent.
		Core::instance()->undo_stack()->push(take_subtitle_section_command(),
											 tr("Created Subtitle Track"));
	}
}

void TimelineWidget::set_selected_clips_autocaching(bool e)
{
	MultiUndoCommand *command = new MultiUndoCommand();

	for (Block *b : selected_blocks_) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
			command->add_child(new NodeParamSetStandardValueCommand(
				NodeKeyframeTrackReference(
					NodeInput(clip, ClipBlock::k_auto_cache_input)),
				e));
		}
	}

	Core::instance()->undo_stack()->push(
		command, e ? tr("Enabled Auto-Caching On %1 Clip(s)")
						 .arg(selected_blocks_.size()) :
					 tr("Disabled Auto-Caching On %1 Clip(s)")
						 .arg(selected_blocks_.size()));
}

void TimelineWidget::cache_clips()
{
	for (Block *b : selected_blocks_) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
			clip->request_invalidated_from_connected(true);
		}
	}
}

void TimelineWidget::cache_clips_in_out()
{
	if (!this->sequence() || !this->sequence()->get_work_area()->enabled()) {
		return;
	}

	TimeTargetObject tto;
	tto.set_time_target(this->sequence());

	const TimeRange &r = this->sequence()->get_work_area()->range();
	for (Block *b : qAsConst(selected_blocks_)) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
			if (Node *connected = clip->get_connected_output(clip->k_buffer_in)) {
				TimeRange adjusted =
					tto.get_adjusted_time(this->sequence(), connected, r,
										Node::k_transform_towards_input);
				clip->request_invalidated_from_connected(true, adjusted);
			}
		}
	}
}

void TimelineWidget::cache_discard()
{
	if (QMessageBox::question(
			this, tr("Discard Cache"),
			tr("This will discard all cache for this clip. "
			   "If the clip has auto-cache enabled, it will be recached immediately. "
			   "This cannot be undone.\n\n"
			   "Do you wish to continue?"),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		for (Block *b : selected_blocks_) {
			if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
				clip->discard_cache();
			}
		}
	}
}

void TimelineWidget::multicam_enabled_triggered(bool e)
{
	MultiUndoCommand *command = new MultiUndoCommand();

	for (Block *b : qAsConst(selected_blocks_)) {
		if (ClipBlock *c = dynamic_cast<ClipBlock *>(b)) {
			if (Sequence *s = dynamic_cast<Sequence *>(c->connected_viewer())) {
				if (e) {
					// Adding multicams
					// Create multicam node and add it to the graph
					MultiCamNode *n = new MultiCamNode();
					n->set_sequence_type(c->get_track_type());
					command->add_child(new NodeAddCommand(s->parent(), n));

					// For each output the sequence has to this clip, disconnect it and
					// connect to the multicam instead
					QVector<NodeInput> inputs = c->find_ways_node_arrives_here(s);
					for (const NodeInput &i : inputs) {
						command->add_child(new NodeEdgeRemoveCommand(s, i));
						command->add_child(new NodeEdgeAddCommand(n, i));
					}

					command->add_child(new NodeEdgeAddCommand(
						s, NodeInput(n, n->k_sequence_input)));

					// Move sequence node one unit back, and place multicam in sequence's spot
					QPointF sequence_pos = c->get_node_position_in_context(s);
					command->add_child(new NodeSetPositionCommand(
						s, c, sequence_pos - QPointF(1, 0)));
					command->add_child(
						new NodeSetPositionCommand(n, c, sequence_pos));

				} else {
					// Removing multicams
					// Locate first multicam that specifically ends up at this clip
					QVector<NodeInput> inputs = c->find_ways_node_arrives_here(s);
					for (const NodeInput &i : inputs) {
						if (MultiCamNode *mcn =
								dynamic_cast<MultiCamNode *>(i.node())) {
							for (auto it = mcn->output_connections().cbegin();
								 it != mcn->output_connections().cend(); it++) {
								command->add_child(new NodeEdgeRemoveCommand(
									it->first, it->second));
								command->add_child(
									new NodeEdgeAddCommand(s, it->second));
							}

							command->add_child(
								new NodeRemoveAndDisconnectCommand(mcn));
						}
					}
				}
			}
		}
	}

	Core::instance()->undo_stack()->push(
		command,
		e ? tr("Multi-Cam Enabled On %1 Clip(s)").arg(selected_blocks_.size()) :
			tr("Multi-Cam Disabled On %1 Clip(s)").arg(selected_blocks_.size()));
}

void TimelineWidget::force_update_rubber_band()
{
	if (rubberband_.isVisible()) {
		this->move_rubber_band_select(rubberband_enable_selecting_,
								   rubberband_select_links_);
	}
}

void TimelineWidget::add_ghost(TimelineViewGhostItem *ghost)
{
	ghost_items_.append(ghost);

	update_viewports(ghost->get_track().type());
}

void TimelineWidget::update_view_timebases()
{
	for (int i = 0; i < views_.size(); i++) {
		TimelineAndTrackView *view = views_.at(i);

		if (get_connected_node() && use_audio_time_units_ && i == Track::k_audio) {
			view->view()->set_timebase(
				get_connected_node()->get_audio_params().sample_rate_as_time_base());
		} else {
			view->view()->set_timebase(timebase());
		}
	}
}

void TimelineWidget::nudge_internal(Rational amount)
{
	if (!selected_blocks_.isEmpty()) {
		// Validate
		foreach (Block *b, selected_blocks_) {
			if (b->in() + amount < 0) {
				amount = -b->in();
			}
		}

		if (amount.isNull()) {
			return;
		}

		MultiUndoCommand *command = new MultiUndoCommand();

		foreach (Block *b, selected_blocks_) {
			command->add_child(
				new TrackReplaceBlockWithGapCommand(b->track(), b, false));
		}

		foreach (Block *b, selected_blocks_) {
			command->add_child(new TrackPlaceBlockCommand(
				sequence()->track_list(b->track()->type()), b->track()->index(),
				b, b->in() + amount));
		}

		// Nudge selections
		TimelineWidgetSelections new_sel = get_selections();
		new_sel.shift_time(amount);
		command->add_child(new TimelineWidget::SetSelectionsCommand(
			this, new_sel, get_selections()));

		Core::instance()->undo_stack()->push(command, tr("Nudged Clips"));
	}
}

void TimelineWidget::move_to_playhead_internal(bool out)
{
	if (get_connected_node() && !selected_blocks_.isEmpty()) {
		MultiUndoCommand *command = new MultiUndoCommand();

		// Remove each block from the graph
		QHash<Track *, Rational> earliest_pts;
		foreach (Block *b, selected_blocks_) {
			command->add_child(
				new TrackReplaceBlockWithGapCommand(b->track(), b, false));

			Rational r = earliest_pts.value(b->track(),
											out ? RATIONAL_MIN : RATIONAL_MAX);
			Rational compare = out ? b->out() : b->in();
			if ((compare < r) == !out) {
				earliest_pts.insert(b->track(), compare);
			}
		}

		foreach (Block *b, selected_blocks_) {
			Rational shift_amt = get_connected_node()->get_playhead() -
								 earliest_pts.value(b->track());
			Rational new_in = b->in() + shift_amt;
			bool can_shift = true;

			if (new_in < 0) {
				// Handle clips threatening to go below 0
				Rational new_out = new_in + b->length();
				if (new_out <= 0) {
					can_shift = false;
				} else {
					command->add_child(
						new BlockResizeWithMediaInCommand(b, new_out));
					new_in = 0;
				}
			}

			if (can_shift) {
				command->add_child(new TrackPlaceBlockCommand(
					sequence()->track_list(b->track()->type()),
					b->track()->index(), b, new_in));
			}
		}

		// Shift selections
		TimelineWidgetSelections new_sel = get_selections();
		for (auto it = new_sel.begin(); it != new_sel.end(); it++) {
			Rational track_adj =
				get_connected_node()->get_playhead() -
				earliest_pts.value(get_track_from_reference(it.key()),
								   get_connected_node()->get_playhead());
			if (!track_adj.isNull()) {
				it.value().shift(track_adj);
			}
		}
		command->add_child(
			new SetSelectionsCommand(this, new_sel, get_selections()));

		Core::instance()->undo_stack()->push(command,
											 tr("Moved Clip(s) To Point"));
	}
}

void TimelineWidget::set_view_beam_cursor(const TimelineCoordinate &coord)
{
	foreach (TimelineAndTrackView *tview, views_) {
		tview->view()->set_beam_cursor(coord);
	}
}

void TimelineWidget::set_view_transition_overlay(ClipBlock *out, ClipBlock *in)
{
	foreach (TimelineAndTrackView *tview, views_) {
		tview->view()->set_transition_overlay(out, in);
	}
}

void TimelineWidget::set_block_links_selected(ClipBlock *block, bool selected)
{
	foreach (Block *link, block->block_links()) {
		if (selected) {
			add_selection(link);
		} else {
			remove_selection(link);
		}
	}
}

void TimelineWidget::queue_scroll(int value)
{
	// (using a hacky singleShot so the scroll occurs after the scene and its scrollbars have updated)
	deferred_scroll_value_ = value;

	QTimer::singleShot(0, this, &TimelineWidget::DeferredScrollAction);
}

TimelineView *TimelineWidget::get_first_timeline_view()
{
	return views_.first()->view();
}

Rational TimelineWidget::get_timebase_for_track_type(Track::Type type)
{
	return views_.at(type)->view()->timebase();
}

const QRect &TimelineWidget::get_rubber_band_geometry() const
{
	return rubberband_.geometry();
}

void TimelineWidget::signal_selected_blocks(QVector<Block *> input, bool filter)
{
	if (input.isEmpty()) {
		return;
	}

	if (filter) {
		// If filtering, remove all the blocks that are already selected
		for (int i = 0; i < input.size(); i++) {
			Block *b = input.at(i);

			if (selected_blocks_.contains(b)) {
				input.removeAt(i);
				i--;
			}
		}
	}

	selected_blocks_.append(input);

	signal_block_selection_change();
}

void TimelineWidget::signal_deselected_blocks(
	const QVector<Block *> &deselected_blocks)
{
	if (deselected_blocks.isEmpty()) {
		return;
	}

	foreach (Block *b, deselected_blocks) {
		selected_blocks_.removeOne(b);
	}

	signal_block_selection_change();
}

void TimelineWidget::signal_deselected_all_blocks()
{
	if (!selected_blocks_.isEmpty()) {
		selected_blocks_.clear();
		signal_block_selection_change();
	}
}

QVector<Timeline::EditToInfo>
TimelineWidget::get_edit_to_info(const Rational &playhead_time,
							  Timeline::MovementMode mode)
{
	// Get list of unlocked tracks
	QVector<Track *> tracks = sequence()->get_unlocked_tracks();

	// Create list to cache nearest times and the blocks at this point
	QVector<Timeline::EditToInfo> info_list(tracks.size());

	for (int i = 0; i < tracks.size(); i++) {
		Timeline::EditToInfo &info = info_list[i];

		Track *track = tracks.at(i);
		info.track = track;

		Block *b;

		// Determine what block is at this time (for "trim in", we want to catch blocks that start at
		// the time, for "trim out" we don't)
		if (mode == Timeline::k_trim_in) {
			b = track->nearest_block_before_or_at(playhead_time);
		} else {
			b = track->nearest_block_before(playhead_time);
		}

		// If we have a block here, cache how close it is to the track
		if (b) {
			Rational this_track_closest_point;

			if (mode == Timeline::k_trim_in) {
				this_track_closest_point = b->in();
			} else {
				this_track_closest_point = b->out();
			}

			info.nearest_time = this_track_closest_point;
		}

		info.nearest_block = b;
	}

	return info_list;
}

void TimelineWidget::ripple_to(Timeline::MovementMode mode)
{
	if (!get_connected_node()) {
		return;
	}

	Rational playhead_time = get_connected_node()->get_playhead();

	QVector<Timeline::EditToInfo> tracks = get_edit_to_info(playhead_time, mode);

	if (tracks.isEmpty()) {
		return;
	}

	// Find each track's nearest point and determine the overall timeline's nearest point
	Rational closest_point_to_playhead =
		(mode == Timeline::k_trim_in) ? RATIONAL_MIN : RATIONAL_MAX;

	foreach (const Timeline::EditToInfo &info, tracks) {
		if (info.nearest_block) {
			if (mode == Timeline::k_trim_in) {
				closest_point_to_playhead =
					qMax(info.nearest_time, closest_point_to_playhead);
			} else {
				closest_point_to_playhead =
					qMin(info.nearest_time, closest_point_to_playhead);
			}
		}
	}

	if (closest_point_to_playhead == RATIONAL_MIN ||
		closest_point_to_playhead == RATIONAL_MAX) {
		// Assume no blocks will be acted upon
		return;
	}

	// If we're not inserting gaps and the edit point is right on the nearest in point, we enter a
	// single-frame mode where we remove one frame only
	if (closest_point_to_playhead == playhead_time) {
		if (mode == Timeline::k_trim_in) {
			playhead_time += timebase();
		} else {
			playhead_time -= timebase();
		}
	}

	// For standard rippling, we can cache here the region that will be rippled out
	Rational in_ripple = qMin(closest_point_to_playhead, playhead_time);
	Rational out_ripple = qMax(closest_point_to_playhead, playhead_time);

	TimelineRippleRemoveAreaCommand *c =
		new TimelineRippleRemoveAreaCommand(sequence(), in_ripple, out_ripple);

	Core::instance()->undo_stack()->push(c, tr("Rippled Clip(s) To Point"));

	// If we rippled, ump to where new cut is if applicable
	if (mode == Timeline::k_trim_in) {
		get_connected_node()->set_playhead(closest_point_to_playhead);
	} else if (mode == Timeline::k_trim_out &&
			   closest_point_to_playhead == get_connected_node()->get_playhead()) {
		get_connected_node()->set_playhead(playhead_time);
	}
}

void TimelineWidget::edit_to(Timeline::MovementMode mode)
{
	const Rational playhead_time = get_connected_node()->get_playhead();

	// Get list of unlocked tracks
	QVector<Timeline::EditToInfo> tracks = get_edit_to_info(playhead_time, mode);

	if (tracks.isEmpty()) {
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	foreach (const Timeline::EditToInfo &info, tracks) {
		if (info.nearest_block &&
			!dynamic_cast<GapBlock *>(info.nearest_block) &&
			info.nearest_time != playhead_time) {
			Rational new_len;

			if (mode == Timeline::k_trim_in) {
				new_len = playhead_time - info.nearest_time;
			} else {
				new_len = info.nearest_time - playhead_time;
			}
			new_len = info.nearest_block->length() - new_len;

			command->add_child(new BlockTrimCommand(
				info.track, info.nearest_block, new_len, mode));
		}
	}

	Core::instance()->undo_stack()->push(command, tr("Cut Clip(s) To Point"));
}

void TimelineWidget::update_viewports(const Track::Type &type)
{
	if (type == Track::k_none) {
		foreach (TimelineAndTrackView *tview, views_) {
			tview->view()->viewport()->update();
		}
	} else {
		views_.at(type)->view()->viewport()->update();
	}
}

bool TimelineWidget::paste_internal(bool insert)
{
	if (!get_connected_node()) {
		return false;
	}

	ProjectSerializer::Result res = ProjectSerializer::paste(
		ProjectSerializer::k_only_clips, get_connected_node()->project());
	if (res.get_load_data().nodes.isEmpty()) {
		return false;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	Project *project = get_connected_node()->project();
	foreach (Node *n, res.get_load_data().nodes) {
		command->add_child(new NodeAddCommand(project, n));
		if (n->is_item() && !n->folder()) {
			command->add_child(new FolderAddChild(project->root(), n));
		}
	}

	for (auto it = res.get_load_data().promised_connections.cbegin();
		 it != res.get_load_data().promised_connections.cend(); it++) {
		auto oc = *it;
		command->add_child(new NodeEdgeAddCommand(oc.first, oc.second));
	}

	Rational paste_start = get_connected_node()->get_playhead();

	if (insert) {
		Rational paste_end = paste_start;

		for (auto it = res.get_load_data().properties.cbegin();
			 it != res.get_load_data().properties.cend(); it++) {
			Rational length = static_cast<Block *>(it.key())->length();
			Rational in = Rational::from_string(
				it.value()[QStringLiteral("in")].toStdString());

			paste_end = qMax(paste_end, paste_start + in + length);
		}

		if (paste_end != paste_start) {
			insert_gaps_at(paste_start, paste_end - paste_start, command);
		}
	}

	for (auto it = res.get_load_data().properties.cbegin();
		 it != res.get_load_data().properties.cend(); it++) {
		Block *block = static_cast<Block *>(it.key());
		Rational in = Rational::from_string(
			it.value()[QStringLiteral("in")].toStdString());
		Track::Reference track =
			Track::Reference::from_string(it.value()[QStringLiteral("track")]);

		command->add_child(
			new TrackPlaceBlockCommand(sequence()->track_list(track.type()),
									   track.index(), block, paste_start + in));
	}

	Core::instance()->undo_stack()->push(
		command,
		tr("Pasted %1 Clip(s)").arg(res.get_load_data().properties.size()));

	return true;
}

TimelineAndTrackView *
TimelineWidget::add_timeline_and_track_view(Qt::Alignment alignment)
{
	TimelineAndTrackView *v = new TimelineAndTrackView(alignment);
	connect(v->track_view(), &TrackView::about_to_delete_track, this,
			&TimelineWidget::track_about_to_be_deleted);
	return v;
}

QHash<Node *, Node *>
TimelineWidget::generate_existing_paste_map(const ProjectSerializer::Result &r)
{
	QHash<Node *, Node *> m;

	for (Node *n : r.get_load_data().nodes) {
		for (Block *b : qAsConst(this->selected_blocks_)) {
			for (auto it = b->get_context_positions().cbegin();
				 it != b->get_context_positions().cend(); it++) {
				if (it.key()->id() == n->id() && !m.contains(it.key())) {
					m.insert(it.key(), n);
					break;
				}
			}
		}
	}

	return m;
}

QByteArray TimelineWidget::save_splitter_state() const
{
	return view_splitter_->saveState();
}

void TimelineWidget::restore_splitter_state(const QByteArray &state)
{
	view_splitter_->restoreState(state);
}

void TimelineWidget::start_rubber_band_select(const QPoint &global_cursor_start)
{
	// Store scene positions for each view
	rubberband_scene_pos_.resize(views_.size());
	for (int i = 0; i < rubberband_scene_pos_.size(); i++) {
		TimelineView *v = views_.at(i)->view();
		rubberband_scene_pos_[i] = v->unscale_point(
			v->mapToScene(v->mapFromGlobal(global_cursor_start)));
	}

	rubberband_.show();

	// We don't touch any blocks that are already selected. If you want these to be deselected by
	// default, call DeselectAll() before calling StartRubberBandSelect()
	rubberband_old_selections_ = selections_;
}

void TimelineWidget::move_rubber_band_select(bool enable_selecting,
										  bool select_links)
{
	QPoint rubberband_now = QCursor::pos();

	TimelineView *fv = views_.first()->view();
	const QPointF &rubberband_scene_start = rubberband_scene_pos_.at(0);
	QPointF rubberband_now_scaled =
		fv->unscale_point(fv->mapToScene(fv->mapFromGlobal(rubberband_now)));

	QPoint rubberband_local_start = fv->mapTo(
		this, fv->mapFromScene(fv->scale_point(rubberband_scene_start)));
	QPoint rubberband_local_now = fv->mapTo(
		this, fv->mapFromScene(fv->scale_point(rubberband_now_scaled)));

	rubberband_.setGeometry(
		QRect(rubberband_local_start, rubberband_local_now).normalized());

	rubberband_enable_selecting_ = enable_selecting;
	rubberband_select_links_ = select_links;

	if (!enable_selecting) {
		return;
	}

	// Get current items in rubberband
	QVector<Block *> items_in_rubberband;

	for (int i = 0; i < views_.size(); i++) {
		TimelineView *v = views_.at(i)->view();
		QRectF r = QRectF(v->scale_point(rubberband_scene_pos_.at(i)),
						  v->mapToScene(v->mapFromGlobal(rubberband_now)))
					   .normalized();
		items_in_rubberband.append(v->get_items_at_scene_rect(r));
	}

	// Reset selection to whatever it was before
	set_selections(rubberband_old_selections_, false);

	// Add any blocks in rubberband
	rubberband_now_selected_.clear();

	foreach (Block *b, items_in_rubberband) {
		if (dynamic_cast<GapBlock *>(b)) {
			continue;
		}

		Track *t = b->track();
		if (t->is_locked()) {
			continue;
		}

		if (!rubberband_now_selected_.contains(b)) {
			add_selection(b);
			rubberband_now_selected_.append(b);
		}

		ClipBlock *c = dynamic_cast<ClipBlock *>(b);
		if (c && select_links) {
			foreach (Block *link, c->block_links()) {
				if (!rubberband_now_selected_.contains(link)) {
					add_selection(link);
					rubberband_now_selected_.append(link);
				}
			}
		}
	}
}

void TimelineWidget::end_rubber_band_select()
{
	rubberband_.hide();

	// Emit any blocks that were newly selected
	signal_selected_blocks(rubberband_now_selected_);

	rubberband_now_selected_.clear();
	rubberband_old_selections_.clear();
}

void TimelineWidget::add_selection(const TimeRange &time,
								  const Track::Reference &track)
{
	selections_[track].insert(time);

	update_viewports(track.type());
}

void TimelineWidget::add_selection(Block *item)
{
	if (item->track()) {
		add_selection(item->range(), item->track()->to_reference());
	}
}

void TimelineWidget::remove_selection(const TimeRange &time,
									 const Track::Reference &track)
{
	selections_[track].remove(time);

	update_viewports(track.type());
}

void TimelineWidget::remove_selection(Block *item)
{
	if (item->track()) {
		remove_selection(item->range(), item->track()->to_reference());
	}
}

void TimelineWidget::set_selections(const TimelineWidgetSelections &s,
								   bool process_block_changes)
{
	if (selections_ == s) {
		return;
	}

	if (!get_connected_node()) {
		return;
	}

	if (process_block_changes) {
		QVector<Block *> deselected;
		QVector<Block *> selected;

		foreach (Block *b, selected_blocks_) {
			if (!s[b->track()->to_reference()].contains(b->range())) {
				deselected.append(b);
			}
		}

		// NOTE: This loop could do with some optimization
		for (auto it = s.cbegin(); it != s.cend(); it++) {
			Track *track = get_track_from_reference(it.key());
			if (track) {
				const TimeRangeList &ranges = it.value();

				foreach (Block *b, track->blocks()) {
					if (!selected_blocks_.contains(b) &&
						ranges.contains(b->range())) {
						selected.append(b);
					}
				}
			}
		}

		signal_deselected_blocks(deselected);
		signal_selected_blocks(selected);
	}

	selections_ = s;

	update_viewports();
}

Block *TimelineWidget::get_item_at_scene_pos(const TimelineCoordinate &coord)
{
	return views_.at(coord.get_track().type())
		->view()
		->get_item_at_scene_pos(coord.get_frame(), coord.get_track().index());
}

void TimelineWidget::SetSplitterSizesCommand::redo()
{
	old_sizes_ = splitter_->sizes();
	splitter_->setSizes(new_sizes_);
}

void TimelineWidget::SetSplitterSizesCommand::undo()
{
	splitter_->setSizes(old_sizes_);
}

}
