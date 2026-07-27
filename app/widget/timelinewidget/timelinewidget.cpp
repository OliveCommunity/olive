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
#include <QInputDialog>
#include <QProcess>
#include <QSlider>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

#include "oakengine/audio.h"
#include "core.h"
#include "engineeventbridge.h"
#include "common/range.h"
#include "dialog/proxy/proxydialog.h"
#include "dialog/sequence/sequence.h"
#include "dialog/speedduration/speeddurationdialog.h"
#include "node/block/transition/transition.h"
#include "node/project/footage/footage.h"
#include "oakengine/serializer.h"
#include "oakengine/undo.h"
#include "oakengine/events.h"
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/proxy.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "render/audiowaveformcache.h"
#include "task/project/import/import.h"
#include "common/configwrapper.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "timeline/timelineundoripple.h"
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
#include "widget/timelinewidget/cliphandle.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"
#include "widget/nodeparamview/nodeparamview.h"
#include "widget/timeruler/timeruler.h"
#include "widget/toolbar/toolbar.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

#define super TimeBasedWidget

using namespace timeline_waveform_sync;

namespace
{

struct SourceSyncClip {
	ClipBlock *clip = nullptr;
	oak_audio_sync_source_clip source;
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
	const Rational source_start = footage->source_start_time();
	const Rational media_in = clip_media_in(clip);
	out->source.source_start_time_num = source_start.numerator();
	out->source.source_start_time_den = source_start.denominator();
	out->source.media_in_num = media_in.numerator();
	out->source.media_in_den = media_in.denominator();
	out->source.has_source_start_time = 1;
	out->source_head = source_start + media_in;
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
		oak_video_params _vp;
		if (!candidate ||
			oakengine_viewer_get_first_enabled_video_stream(
				reinterpret_cast<OakEngineNode *>(candidate), &_vp) < 0 ||
			!oakengine_video_params_is_valid(&_vp) ||
			footage.contains(candidate)) {
			continue;
		}

		footage.append(candidate);
	}
	return footage;
}

// Zoom slider <-> scale mapping (logarithmic: slider 0..1000 -> 0.1..10000 px/s)
constexpr double k_zoom_scale_min = 0.1;
constexpr double k_zoom_scale_max = 10000.0;

double zoom_slider_to_scale(int v)
{
	return k_zoom_scale_min *
		   std::pow(k_zoom_scale_max / k_zoom_scale_min, v / 1000.0);
}

int scale_to_zoom_slider(double s)
{
	if (s <= k_zoom_scale_min)
		return 0;
	if (s >= k_zoom_scale_max)
		return 1000;
	return qRound(1000.0 * std::log(s / k_zoom_scale_min) /
				  std::log(k_zoom_scale_max / k_zoom_scale_min));
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

	// Application toolbar row (31px) — replaces the dockable ToolPanel by
	// default. The dockable ToolPanel remains available via the Window menu.
	QHBoxLayout *toolbar_row = new QHBoxLayout();
	toolbar_row->setContentsMargins(0, 0, 0, 0);
	toolbar_row->setSpacing(4);

	Toolbar *toolbar = new Toolbar(this);
	toolbar->setFixedHeight(31);
	toolbar->set_tool(Core::instance()->tool());
	toolbar->set_snapping(Core::instance()->snapping());
	toolbar_row->addWidget(toolbar);

	connect(toolbar, &Toolbar::tool_changed, Core::instance(),
			&Core::set_tool);
	connect(Core::instance(), &Core::tool_changed, toolbar,
			&Toolbar::set_tool);
	connect(toolbar, &Toolbar::snapping_changed, Core::instance(),
			&Core::set_snapping);
	connect(Core::instance(), &Core::snapping_changed, toolbar,
			&Toolbar::set_snapping);
	connect(toolbar, &Toolbar::selected_transition_changed, Core::instance(),
			&Core::set_selected_transition_object);

	toolbar_row->addStretch();

	// Zoom slider (logarithmic)
	zoom_slider_ = new QSlider(Qt::Horizontal, this);
	zoom_slider_->setRange(0, 1000);
	zoom_slider_->setFixedWidth(120);
	zoom_slider_->setToolTip(tr("Zoom"));
	zoom_slider_->setValue(scale_to_zoom_slider(get_scale()));
	toolbar_row->addWidget(zoom_slider_);

	connect(zoom_slider_, &QSlider::valueChanged, this, [this](int v) {
		set_scale(zoom_slider_to_scale(v));
	});

	// Track height slider
	track_height_slider_ = new QSlider(Qt::Horizontal, this);
	track_height_slider_->setRange(0, 8);
	track_height_slider_->setFixedWidth(80);
	track_height_slider_->setToolTip(tr("Track Height"));
	track_height_slider_->setValue(3);
	toolbar_row->addWidget(track_height_slider_);

	connect(track_height_slider_, &QSlider::valueChanged, this, [this](int v) {
		if (!get_connected_node()) {
			return;
		}
		double h = oakengine_track_height_minimum() +
				   v * oakengine_track_height_interval();
		foreach (Track *t, sequence()->get_tracks()) {
			oakengine_track_set_height(
				reinterpret_cast<OakEngineSequence *>(sequence()), t->type(),
				t->index(), h);
		}
	});

	vert_layout->addLayout(toolbar_row);

	QHBoxLayout *ruler_and_time_layout = new QHBoxLayout();
	vert_layout->addLayout(ruler_and_time_layout);

	timecode_label_ = new RationalSlider();
	timecode_label_->set_alignment(Qt::AlignCenter);
	timecode_label_->set_display_type(slider::k_time);
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
				oakengine_viewer_set_playhead(
					reinterpret_cast<OakEngineNode *>(get_connected_node()),
					start.numerator(), start.denominator());
			}
		}

		QVector<OakEngineBlock *> oak_blocks;
		oak_blocks.reserve(selected_blocks_.size());
		for (Block *b : selected_blocks_) {
			oak_blocks.append(reinterpret_cast<OakEngineBlock *>(b));
		}
		emit block_selection_changed(oak_blocks);
	});
}

TimelineWidget::~TimelineWidget()
{
	// Ensure no blocks are selected before any child widgets are destroyed (prevents corrupt ViewSelectionChanged() signal)
	connect_viewer_node(nullptr);

	clear();

	qDeleteAll(tools_);

	oakengine_undo_command_free(subtitle_show_command_);
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

	// Keep zoom slider in sync when scale changes elsewhere (ctrl+wheel etc.)
	zoom_slider_->blockSignals(true);
	zoom_slider_->setValue(scale_to_zoom_slider(scale));
	zoom_slider_->blockSignals(false);

	if (rubberband_.isVisible()) {
		QMetaObject::invokeMethod(this, &TimelineWidget::force_update_rubber_band,
								  Qt::QueuedConnection);
	}
}

void TimelineWidget::ConnectNodeEvent(ViewerOutput *n)
{
	Sequence *s = static_cast<Sequence *>(n);
	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(s);

	// Track add/remove are now received via bridge sequence_track_* signals
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED);

	connect(bridge_, &EngineEventBridge::sequence_track_added, this,
			[this](OakEngineTrack *track, int track_type) {
				Track *t = reinterpret_cast<Track *>(track);
				add_track(t);
				// Update the TrackView UI for this track type
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->track_view()->insert_track(t);
				}
			});
	connect(bridge_, &EngineEventBridge::sequence_track_removed, this,
			[this](OakEngineTrack *track, int track_type) {
				Track *t = reinterpret_cast<Track *>(track);
				remove_track(t);
				// Update the TrackView UI for this track type
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->track_view()->remove_track(t);
				}
			});

	connect(bridge_, &EngineEventBridge::sequence_track_list_changed, this,
			[this](OakEngineSequence *, int track_type) {
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->view()->track_list_changed();
				}
			});
	connect(bridge_, &EngineEventBridge::sequence_track_height_changed, this,
			[this](OakEngineSequence *, OakEngineTrack *, int track_type, int) {
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->view()->track_list_changed();
				}
			});

	// Subscribe to track-level events via bridge (subscriptions in add_track)
	connect(bridge_, &EngineEventBridge::track_index_changed, this,
			[this](OakEngineTrack *source, int old_index, int new_index) {
				track_updated(static_cast<Track::Type>(
					oakengine_track_type(source)));
				track_index_changed(reinterpret_cast<Track *>(source),
									old_index, new_index);
			});
	connect(bridge_, &EngineEventBridge::track_height_changed, this,
			[this](OakEngineTrack *source, double) {
				track_updated(static_cast<Track::Type>(
					oakengine_track_type(source)));
			});
	connect(bridge_, &EngineEventBridge::track_blocks_refreshed, this,
			[this](OakEngineTrack *source) {
				track_updated(static_cast<Track::Type>(
					oakengine_track_type(source)));
			});
	connect(bridge_, &EngineEventBridge::track_block_added, this,
			[this](OakEngineBlock *block, qint64, qint64) {
				add_block(reinterpret_cast<Block *>(block));
			});
	connect(bridge_, &EngineEventBridge::track_block_removed, this,
			[this](OakEngineBlock *block, qint64, qint64) {
				remove_block(reinterpret_cast<Block *>(block));
			});

	// Block-level change notifications via bridge (replaces direct
	// connect(block, &Block::..._changed, ...) to avoid pulling Block's
	// staticMetaObject across the C ABI boundary).
	connect(bridge_, &EngineEventBridge::block_enabled_changed, this,
			[this](OakEngineBlock *) { block_updated(); });
	connect(bridge_, &EngineEventBridge::block_preview_changed, this,
			[this](OakEngineBlock *) { block_updated(); });
	connect(bridge_, &EngineEventBridge::node_label_changed, this,
			[this](OakEngineNode *) { block_updated(); });
	connect(bridge_, &EngineEventBridge::node_links_changed, this,
			[this](OakEngineNode *) { block_updated(); });
	connect(bridge_, &EngineEventBridge::node_color_changed, this,
			[this](OakEngineNode *) { block_updated(); });

	// Subscribe to viewer events via bridge
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED);

	connect(bridge_, &EngineEventBridge::viewer_frame_rate_changed, this,
			[this](OakEngineNode *, qint64, qint64) {
				frame_rate_changed();
			});
	connect(bridge_, &EngineEventBridge::viewer_sample_rate_changed, this,
			[this](OakEngineNode *, int) {
				sample_rate_changed();
			});
	connect(bridge_, &EngineEventBridge::viewer_playhead_changed, this,
			[this](OakEngineNode *, qint64 num, qint64 den) {
				this->timecode_label_->set_value(Rational(num, den));
			});

	connect(timecode_label_, &RationalSlider::value_changed, this,
			[handle](const Rational &time) {
				oakengine_viewer_set_playhead(
					handle, time.numerator(), time.denominator());
			});
	{
		int64_t pn, pd;
		oakengine_viewer_get_playhead(handle, &pn, &pd);
		timecode_label_->set_value(Rational(pn, pd));
	}

	ruler()->set_playback_cache(
		reinterpret_cast<PlaybackCache *>(
			oakengine_viewer_get_playback_cache(handle)));

	{
		oak_video_params vp;
		oakengine_viewer_get_video_params(handle, 0, &vp);
		// time_base is the frame duration (e.g. 1/25); reconstruct it as
		// Rational(num, den). (Previously num/den were swapped here, which
		// produced the frame rate and triggered "INVALID TIMEBASE".)
		SetTimebase(Rational(vp.time_base_num, vp.time_base_den));
	}

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

	// Bridge subscriptions and connections are cleaned up by
	// TimeBasedWidget::connect_viewer_node (disconnect(bridge_, nullptr, this, nullptr))

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
		// Split through the liboakengine C ABI facade: one undoable,
		// link-preserving command with the same semantics as the old
		// app-side BlockSplitPreservingLinksCommand push.
		QVector<OakEngineClip *> clips;
		clips.reserve(blocks_to_split.size());
		foreach (Block *b, blocks_to_split) {
			clips.append(reinterpret_cast<OakEngineClip *>(
				static_cast<ClipBlock *>(b)));
		}
		oakengine_sequence_split_clips(
			reinterpret_cast<OakEngineSequence *>(sequence()), clips.data(),
			clips.size(),
			Timecode::time_to_timestamp(playhead_time, timebase(),
										Timecode::k_round));
	}
}

void TimelineWidget::replace_blocks_with_gaps(const QVector<Block *> &blocks,
										   bool remove_from_graph,
										   void *command,
										   bool handle_transitions)
{
	foreach (Block *b, blocks) {
		if (dynamic_cast<GapBlock *>(b)) {
			// No point in replacing a gap with a gap, and TrackReplaceBlockWithGapCommand will clear
			// up any extraneous gaps
			continue;
		}

		Track *original_track = b->track();

		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(original_track), reinterpret_cast<void *>(b), handle_transitions ? 1 : 0));

		if (remove_from_graph) {
			void *remove_cmd = oakengine_undo_command_create_multi();
			oakengine_undo_command_multi_add_child(
				remove_cmd,
				oakengine_node_remove_and_disconnect_command(
					reinterpret_cast<void *>(b)));
			for (Node *dep : b->get_exclusive_dependencies()) {
				oakengine_undo_command_multi_add_child(
					remove_cmd,
					oakengine_node_remove_and_disconnect_command(
						reinterpret_cast<void *>(dep)));
			}
			oakengine_undo_command_multi_add_child(command, remove_cmd);
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

	void *command = oakengine_undo_command_create_multi();

	// Remove all selections
	oakengine_undo_command_multi_add_child(command, create_set_selections_command(TimelineWidgetSelections(), get_selections()));

	// For transitions, remove them but extend their attached blocks to fill their place
	foreach (TransitionBlock *transition, transitions_to_delete) {
		void *trc = oakengine_transition_remove_command(
			reinterpret_cast<void *>(transition), 1);

		// Perform the transition removal now so that replacing blocks with gaps below won't get confused
		oakengine_undo_command_redo_now(trc);

		oakengine_undo_command_multi_add_child(command, trc);
	}

	// Selection clearing and transition removal stay app-side (selection
	// state and transition commands have no facade equivalent); the clip
	// deletion core below goes through the facade and lands as one undoable
	// command right after this one, keeping the undo order intact.
	oakengine_undo_push(command, tr("Deleted Clips").toUtf8().constData());

	// Delete the clips through the liboakengine C ABI facade (gap
	// replacement + graph removal, optionally rippling the selected ranges
	// closed), same semantics as the old in-command children.
	QVector<OakEngineClip *> facade_clips;
	facade_clips.reserve(clips_to_delete.size());
	foreach (Block *b, clips_to_delete) {
		facade_clips.append(
			reinterpret_cast<OakEngineClip *>(static_cast<ClipBlock *>(b)));
	}

	Rational new_playhead = RATIONAL_MAX;
	QVector<int64_t> ripple_ranges;
	if (ripple) {
		foreach (Block *b, selected_list) {
			ripple_ranges.append(int64_t(b->track()->type()));
			ripple_ranges.append(b->track()->index());
			ripple_ranges.append(Timecode::time_to_timestamp(
				b->in(), timebase(), Timecode::k_round));
			ripple_ranges.append(Timecode::time_to_timestamp(
				b->out(), timebase(), Timecode::k_round));
			new_playhead = qMin(new_playhead, b->in());
		}
	}

	int rippled = 0;
	oakengine_sequence_delete_clips(
		reinterpret_cast<OakEngineSequence *>(sequence()),
		facade_clips.data(), facade_clips.size(), ripple ? 1 : 0,
		ripple ? ripple_ranges.constData() : nullptr,
		ripple ? ripple_ranges.size() / 4 : 0, &rippled);

	// Ensures any current drag operations are cancelled
	clear_ghosts();

	if (ripple && rippled && new_playhead != RATIONAL_MAX) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			new_playhead.numerator(), new_playhead.denominator());
	}
}

void TimelineWidget::increase_track_height()
{
	if (!get_connected_node()) {
		return;
	}

	// Increase the height of each track by one "unit"
	foreach (Track *t, sequence()->get_tracks()) {
		double h;
		oakengine_track_get_height(
			reinterpret_cast<OakEngineSequence *>(sequence()),
			t->type(), t->index(), &h);
		oakengine_track_set_height(
			reinterpret_cast<OakEngineSequence *>(sequence()),
			t->type(), t->index(),
			h + oakengine_track_height_interval());
	}
}

void TimelineWidget::decrease_track_height()
{
	if (!get_connected_node()) {
		return;
	}

	// Decrease the height of each track by one "unit"
	foreach (Track *t, sequence()->get_tracks()) {
		double h;
		oakengine_track_get_height(
			reinterpret_cast<OakEngineSequence *>(sequence()),
			t->type(), t->index(), &h);
		oakengine_track_set_height(
			reinterpret_cast<OakEngineSequence *>(sequence()),
			t->type(), t->index(),
			qMax(h - oakengine_track_height_interval(),
				 oakengine_track_height_minimum()));
	}
}

void TimelineWidget::insert_footage_at_playhead(
	const QVector<OakEngineNode *> &footage)
{
	auto command = oakengine_undo_command_create_multi();
	QVector<ViewerOutput *> viewer_footage;
	viewer_footage.reserve(footage.size());
	foreach (OakEngineNode *handle, footage) {
		viewer_footage.append(reinterpret_cast<ViewerOutput *>(handle));
	}
	import_tool_->place_at(viewer_footage, get_connected_node()->get_playhead(), true,
						  command, 0, true);
	oakengine_undo_push(command,
										 tr("Inserted Footage At Playhead").toUtf8().constData());
}

void TimelineWidget::overwrite_footage_at_playhead(
	const QVector<OakEngineNode *> &footage)
{
	auto command = oakengine_undo_command_create_multi();
	QVector<ViewerOutput *> viewer_footage;
	viewer_footage.reserve(footage.size());
	foreach (OakEngineNode *handle, footage) {
		viewer_footage.append(reinterpret_cast<ViewerOutput *>(handle));
	}
	import_tool_->place_at(viewer_footage, get_connected_node()->get_playhead(), false,
						  command, 0, true);
	oakengine_undo_push(command,
										 tr("Overwrote Footage At Playhead").toUtf8().constData());
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

	// Link/unlink through the liboakengine C ABI facade (one undoable
	// command, same as the old NodeLinkManyCommand push).
	QVector<OakEngineClip *> clips;
	clips.reserve(blocks.size());
	foreach (Node *n, blocks) {
		clips.append(
			reinterpret_cast<OakEngineClip *>(static_cast<ClipBlock *>(n)));
	}
	oakengine_clip_set_linked(clips.data(), clips.size(), link ? 1 : 0);
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
		// Through the liboakengine C ABI facade (one undoable command with
		// the same engine semantics as the old app-side push).
		QVector<OakEngineClip *> clips;
		clips.reserve(blocks.size());
		foreach (ClipBlock *clip, blocks) {
			clips.append(reinterpret_cast<OakEngineClip *>(clip));
		}
		oakengine_sequence_add_default_transition(
			reinterpret_cast<OakEngineSequence *>(sequence()), clips.data(),
			clips.size());
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

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_CLIPS, nullptr, nullptr);

	oakengine_clipboard_set_nodes(
		cb,
		reinterpret_cast<const OakEngineNode *const *>(
			selected_nodes.constData()),
		selected_nodes.size());

	// Cache the earliest in point so all copied clips have a "relative" in point that can be pasted anywhere
	Rational earliest_in = RATIONAL_MAX;

	foreach (Block *block, selected_blocks_) {
		earliest_in = qMin(earliest_in, block->in());
	}

	foreach (Block *block, selected_blocks_) {
		oakengine_clipboard_set_property(
			cb, reinterpret_cast<OakEngineNode *>(block), "in",
			(block->in() - earliest_in).to_string().c_str());
		QString track_ref = block->track()->to_reference().to_string();
		oakengine_clipboard_set_property(
			cb, reinterpret_cast<OakEngineNode *>(block), "track",
			track_ref.toUtf8().constData());
	}

	oakengine_clipboard_copy(cb);
	oakengine_clipboard_free(cb);

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

	// Compound through the liboakengine C ABI facade (ripple removal or
	// per-track gap fill + workarea disable, one undoable command with the
	// same semantics as the old app-side assembly).
	const Rational wa_in = get_connected_node()->get_work_area()->in();
	const Rational wa_out = get_connected_node()->get_work_area()->out();
	oakengine_sequence_ripple_delete_in_to_out(
		reinterpret_cast<OakEngineSequence *>(sequence()), ripple ? 1 : 0,
		Timecode::time_to_timestamp(wa_in, timebase(), Timecode::k_round),
		Timecode::time_to_timestamp(wa_out, timebase(), Timecode::k_round));

	// Playhead move is not undoable and stays here (same as before).
	if (ripple) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			wa_in.numerator(), wa_in.denominator());
	}
}

void TimelineWidget::toggle_selected_enabled()
{
	QVector<Block *> items = get_selected_blocks();

	if (items.isEmpty()) {
		return;
	}

	// Flip each selected clip through the liboakengine C ABI facade (one
	// undoable command, same per-block inversion as the old children).
	QVector<OakEngineClip *> clips;
	clips.reserve(items.size());
	foreach (Block *i, items) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(i)) {
			clips.append(reinterpret_cast<OakEngineClip *>(clip));
		}
	}
	oakengine_clip_toggle_enabled(clips.data(), clips.size());
}

void TimelineWidget::set_color_label(int index)
{
	// Batch color labels through the liboakengine C ABI facade (one
	// undoable command, same as the old per-block children).
	QVector<OakEngineNode *> nodes;
	nodes.reserve(selected_blocks_.size());
	foreach (Block *b, selected_blocks_) {
		nodes.append(reinterpret_cast<OakEngineNode *>(b));
	}
	oakengine_node_set_color_label(nodes.data(), nodes.size(), index);
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
		oak_audio_sync_placement placement;
		if (oakengine_audio_sync_place_by_source_time(
				&reference.source, &sync_clip.source,
				anchor_timeline_in.numerator(),
				anchor_timeline_in.denominator(),
				&placement) == OAKENGINE_OK &&
			placement.valid) {
			placements.append({ sync_clip.clip,
								Rational(int(placement.timeline_in_num),
										 int(placement.timeline_in_den)) });
		}
	}

	if (placements.size() < 2) {
		return;
	}

	void *command = oakengine_undo_command_create_multi();
	for (const SyncPlacement &placement : placements) {
		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(placement.clip->track()), reinterpret_cast<void *>(placement.clip), false ? 1 : 0));
	}

	TimelineWidgetSelections new_selections;
	for (const SyncPlacement &placement : placements) {
		oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(sequence()->track_list(placement.clip->track()->type())), placement.clip->track()->index(), reinterpret_cast<void *>(placement.clip), core::Timecode::time_to_timestamp(placement.timeline_in, timebase())));

		new_selections[placement.clip->track()->to_reference()].insert(
			TimeRange(placement.timeline_in,
					  placement.timeline_in + placement.clip->length()));
	}

	oakengine_undo_command_multi_add_child(command, create_set_selections_command(new_selections, get_selections()));

	oakengine_undo_push(
		command, tr("Synchronize Clips by Source Time").toUtf8().constData());
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
		oak_audio_waveform_offset offset;
		oakengine_audio_estimate_envelope_offset(
			reference_envelope.constData(), reference_envelope.size(),
			candidate_envelope.constData(), candidate_envelope.size(),
			reference_valid.isEmpty() ? nullptr : reference_valid.constData(),
			reference_valid.size(),
			candidate_valid.isEmpty() ? nullptr : candidate_valid.constData(),
			candidate_valid.size(), window_samples, max_offset_windows,
			&offset);

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
			oak_audio_waveform_stretch_offset stretch;
			oakengine_audio_estimate_stretch_and_offset(
				reference_envelope.constData(), reference_envelope.size(),
				candidate_envelope.constData(), candidate_envelope.size(),
				reference_valid.isEmpty() ? nullptr : reference_valid.constData(),
				reference_valid.size(),
				candidate_valid.isEmpty() ? nullptr : candidate_valid.constData(),
				candidate_valid.size(), window_samples, stretch_radius_windows,
				0.75, 1.34, 0.005, &stretch);
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

		oak_audio_sync_placement placement;
		oakengine_audio_sync_place_by_waveform_offset(
			reference.clip->in().numerator(), reference.clip->in().denominator(),
			offset.offset_samples, sample_rate, &placement);
		qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: placement"
				 << "valid=" << placement.valid << "timeline_in="
				 << (placement.valid ? Rational(int(placement.timeline_in_num),
											int(placement.timeline_in_den))
									 .to_double()
									 : 0.0);
		if (placement.valid) {
			const Rational timeline_in(int(placement.timeline_in_num),
								   int(placement.timeline_in_den));
			if (timeline_in >= 0) {
				placements.append({ sync_clip.clip, timeline_in, speed });
			}
		}
	}

	if (placements.size() < 2) {
		qDebug() << "TimelineWidget::SynchronizeSelectedClipsByWaveform: no usable"
				 << "offsets found";
		Core::instance()->show_status_bar_message(
			tr("Could not find a usable waveform offset for the selected clips"));
		return;
	}

	void *command = oakengine_undo_command_create_multi();
	for (const SyncPlacement &placement : placements) {
		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(placement.clip->track()), reinterpret_cast<void *>(placement.clip), false ? 1 : 0));

		if (placement.speed != 1.0) {
			oak_node_value v{};
			v.type = OAK_NODE_VALUE_FLOAT;
			v.f[0] = clip_speed(placement.clip) * placement.speed;
			oakengine_undo_command_multi_add_child(
				command,
				oakengine_node_set_standard_value_command(
					reinterpret_cast<OakEngineNode *>(placement.clip),
					oakengine_clip_speed_input_id(), 0, 0, &v));
		}
	}

	TimelineWidgetSelections new_selections;
	for (const SyncPlacement &placement : placements) {
		oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(sequence()->track_list(placement.clip->track()->type())), placement.clip->track()->index(), reinterpret_cast<void *>(placement.clip), core::Timecode::time_to_timestamp(placement.timeline_in, timebase())));

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

	oakengine_undo_command_multi_add_child(command, create_set_selections_command(new_selections, get_selections()));

	oakengine_undo_push(command,
													 tr("Synchronize Clips by Waveform").toUtf8().constData());
	Core::instance()->show_status_bar_message(
		tr("Synchronized %1 clip(s) by waveform").arg(placements.size()));
}

void TimelineWidget::generate_proxies_for_selected_clips()
{
	if (!sequence()) {
		qWarning()
			<< "GenerateProxiesForSelectedClips: sequence unavailable";
		return;
	}

	const QVector<Footage *> footage =
		get_selected_proxy_footage(selected_blocks_);
	qDebug() << "GenerateProxiesForSelectedClips: starting proxy generation for"
			 << footage.size() << "footage item(s)";
	for (Footage *item : footage) {
		const VideoParams video = item->get_first_enabled_video_stream();
		oak_video_params _vp;
		oakengine_viewer_get_first_enabled_video_stream(
			reinterpret_cast<OakEngineNode *>(item), &_vp);
		if (!oakengine_video_params_is_valid(&_vp)) {
			qWarning()
				<< "GenerateProxiesForSelectedClips: skipping item with no valid video stream"
				<< item->filename();
			continue;
		}

		oak_proxy_params params;
		oakengine_footage_get_effective_proxy_params(
			reinterpret_cast<OakEngineFootage *>(item), &params);
		oak_proxy_result proxy;
		char cache_buf[512];
		oakengine_project_cache_path(
			reinterpret_cast<OakEngineProject *>(item->project()),
			cache_buf, sizeof(cache_buf));
		int ret = oakengine_proxy_get_or_start(
			cache_buf,
			item->filename().toUtf8().constData(),
			video.stream_index(), &params, &proxy);
		if (ret != 0) {
			qWarning() << "GenerateProxiesForSelectedClips: failed to get/start proxy for"
					   << item->filename();
			continue;
		}
		qDebug() << "GenerateProxiesForSelectedClips: proxy state="
				 << proxy.state
				 << "file=" << QString::fromUtf8(proxy.filename)
				 << "cache=" << cache_buf;
		oakengine_footage_set_proxy(reinterpret_cast<OakEngineFootage *>(item),
								   proxy.filename, proxy.state,
								   video.stream_index(), 1, params.version);
		oakengine_footage_invalidate(reinterpret_cast<OakEngineFootage *>(item));
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

		oakengine_footage_proxy_set_enabled(
			reinterpret_cast<OakEngineFootage *>(item), enabled ? 1 : 0);
		oakengine_footage_invalidate(reinterpret_cast<OakEngineFootage *>(item));
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
		{
			char wbuf[4096];
			int wlen = oakengine_proxy_get_working_filename(
				item->proxy_path().toUtf8().constData(), wbuf, sizeof(wbuf));
			if (wlen > 0) {
				QFile::remove(QString::fromUtf8(wbuf, wlen));
			}
		}
		oakengine_footage_clear_proxy(reinterpret_cast<OakEngineFootage *>(item));
		oakengine_footage_invalidate(reinterpret_cast<OakEngineFootage *>(item));
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
	OakEngineNode *root = reinterpret_cast<OakEngineNode *>(
		get_connected_node()->project()->root());
	const char *url = filename.toUtf8().constData();
	OakEngineTask *task = oakengine_task_create_project_import(root, &url, 1);
	if (!task) {
		qCritical() << "Failed to create import task for" << filename;
		return;
	}
	oakengine_task_start_sync(task);

	void *subimport_command = oakengine_task_import_get_command(task);

	if (oakengine_task_import_footage_count(task) == 0) {
		qCritical() << "Failed to import recorded audio file" << filename;
		if (subimport_command) {
			oakengine_undo_command_free(subimport_command);
		}
	} else {
		oakengine_undo_command_redo_now(subimport_command);

		auto import_command = oakengine_undo_command_create_multi();
		oakengine_undo_command_multi_add_child(import_command, static_cast<void *>(subimport_command));

		OakEngineNode *front = oakengine_task_import_footage_at(task, 0);
		import_tool_->place_at({ reinterpret_cast<Footage *>(front) }, time.in(),
							  false, import_command, track.index());
		oakengine_undo_push(import_command,
											 tr("Recorded Audio Clip").toUtf8().constData());
	}
	oakengine_task_free(task);
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
			subtitle_show_command_ = oakengine_undo_command_create_multi();

			if (should_adjust_splitter) {
				sz[Track::k_subtitle] = height() / Track::k_count;
				oakengine_undo_command_multi_add_child(
					subtitle_show_command_,
					make_splitter_sizes_command(view_splitter_, sz));
			}

			if (should_add_sub_track) {
				void *track_add_cmd = oakengine_sequence_add_track_command(
					reinterpret_cast<OakEngineSequence *>(sequence()),
					OAKENGINE_TRACK_TYPE_SUBTITLE, 0,
					&subtitle_tentative_track_);
				oakengine_undo_command_multi_add_child(
					subtitle_show_command_, track_add_cmd);
			}

			oakengine_undo_command_redo_now(subtitle_show_command_);
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

	auto move_to_nest_command = oakengine_undo_command_create_multi();

	// Remove blocks from this sequence
	replace_blocks_with_gaps(blocks, false, move_to_nest_command);

	// Create new sequence
	Project *project = this->get_connected_node()->project();
	Sequence *nest = reinterpret_cast<Sequence *>(
		Core::instance()->create_new_sequence_for_project(
			tr("Nested Sequence %1"),
			reinterpret_cast<OakEngineProject *>(project)));
	{
		oak_video_params vpod;
		oakengine_viewer_get_video_params(
			reinterpret_cast<const OakEngineNode *>(get_connected_node()),
			0, &vpod);
		oakengine_viewer_set_video_params(
			reinterpret_cast<OakEngineNode *>(nest), &vpod, 0);
	}
	{
		int sr = 0, fmt = 0;
		uint64_t cl = 0;
		oakengine_viewer_get_audio_params(
			reinterpret_cast<const OakEngineNode *>(get_connected_node()),
			0, &sr, &cl, &fmt);
		oakengine_viewer_set_audio_params(
			reinterpret_cast<OakEngineNode *>(nest), sr, cl, fmt, 0);
	}
	oakengine_undo_command_multi_add_child(move_to_nest_command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(project),
			reinterpret_cast<OakEngineNode *>(nest)));

	// Add to same folder
	oakengine_folder_add_child(
		reinterpret_cast<OakEngineNode *>(this->get_connected_node()->folder()),
		reinterpret_cast<OakEngineNode *>(nest));

	// Place blocks in new sequence
	for (int i = 0; i < blocks.size(); i++) {
		Block *b = blocks.at(i);

		const TimeRange &range = times.at(i);
		Track::Reference track = tracks.at(i);

		oakengine_undo_command_multi_add_child(move_to_nest_command, oakengine_track_place_block_command(reinterpret_cast<void *>(nest->track_list(track.type())), track.index() - track_offset.at(track.type()), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(range.in() - start_time, sequence_timebase(nest))));
	}

	// Do this command now, because we later do checks and actions that rely on these having been done
	oakengine_undo_command_redo_now(move_to_nest_command);

	auto meta_command = oakengine_undo_command_create_multi();
	oakengine_undo_command_multi_add_child(meta_command, move_to_nest_command);

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

	oakengine_undo_push(meta_command, tr("Nested Clips").toUtf8().constData());
}

void TimelineWidget::clear_tentative_subtitle_track()
{
	if (subtitle_show_command_) {
		oakengine_undo_command_undo_now(subtitle_show_command_);
		oakengine_undo_command_free(subtitle_show_command_);
		subtitle_show_command_ = nullptr;
		subtitle_tentative_track_ = nullptr;
	}
}

void TimelineWidget::insert_gaps_at(const Rational &earliest_point,
								  const Rational &insert_length,
								  void *command)
{
	for (int i = 0; i < Track::k_count; i++) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_track_list_insert_gaps_command(
				reinterpret_cast<void *>(sequence()->track_list(
					static_cast<Track::Type>(i))),
				earliest_point.numerator(), earliest_point.denominator(),
				insert_length.numerator(), insert_length.denominator()));
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
		OakEngineNode *node = reinterpret_cast<OakEngineNode *>(block);
		QVector<int64_t> subs;
		subs.append(bridge_->subscribe(node, OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED));
		subs.append(bridge_->subscribe(node, OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED));
		subs.append(bridge_->subscribe(node, OAKENGINE_EVENT_NODE_LABEL_CHANGED));
		subs.append(bridge_->subscribe(node, OAKENGINE_EVENT_NODE_LINKS_CHANGED));
		subs.append(bridge_->subscribe(node, OAKENGINE_EVENT_NODE_COLOR_CHANGED));
		block_subscriptions_.insert(block, subs);

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
	// Unsubscribe bridge events for this block
	if (auto it = block_subscriptions_.find(block);
		it != block_subscriptions_.end()) {
		for (int64_t id : it.value()) {
			oakengine_event_unsubscribe(id);
		}
		block_subscriptions_.erase(it);
	}

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

	OakEngineTrack *h = reinterpret_cast<OakEngineTrack *>(track);
	bridge_->subscribe(h, OAKENGINE_EVENT_TRACK_INDEX_CHANGED);
	bridge_->subscribe(h, OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED);
	bridge_->subscribe(h, OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED);
	bridge_->subscribe(h, OAKENGINE_EVENT_TRACK_BLOCK_ADDED);
	bridge_->subscribe(h, OAKENGINE_EVENT_TRACK_BLOCK_REMOVED);
}

void TimelineWidget::remove_track(Track *track)
{
	// Bridge subscriptions auto-die with the observed engine object.
	// No per-track unsubscribe needed.

	remove_selection(TimeRange(0, RATIONAL_MAX), track->to_reference());

	foreach (Block *b, track->blocks()) {
		remove_block(b);
	}
}

void TimelineWidget::track_updated(Track::Type type)
{
	update_viewports(type);
}

void TimelineWidget::block_updated(OakEngineBlock *)
{
	// The old implementation used sender() to obtain the Block and then
	// queried its track type to update only one viewport.  With the bridge
	// approach sender() is the EngineEventBridge (not a Block), and the
	// OakEngineBlock handle is opaque -- there is no C API to retrieve its
	// track type.  Simply refresh all viewports; the cost is negligible
	// (just a repaint request).
	update_viewports();
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
				autocache_action->setChecked(clip_is_autocaching(clip));
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
					"range", QVariant::fromValue(clip_media_range(clip)));
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
	SetTimebase(viewer_output_video_params(get_connected_node()).frame_rate_as_time_base());
}

void TimelineWidget::sample_rate_changed()
{
	update_view_timebases();
}

void TimelineWidget::track_index_changed(Track *track, int old, int now)
{
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

	emit reveal_viewer_in_footage_viewer(reinterpret_cast<OakEngineNode *>(item_to_reveal), r);
}

void TimelineWidget::reveal_in_project()
{
	QAction *a = static_cast<QAction *>(sender());

	ViewerOutput *item_to_reveal =
		reinterpret_cast<ViewerOutput *>(a->data().value<quintptr>());

	emit reveal_viewer_in_project(reinterpret_cast<OakEngineNode *>(item_to_reveal));
}

void TimelineWidget::rename_selected_blocks()
{
	if (selected_blocks_.isEmpty()) {
		return;
	}

	// Same rename dialog as Core::label_nodes(), but the write goes
	// through the liboakengine C ABI facade (one undoable multi-node
	// rename command; the old code also pushed a stray empty command,
	// which is gone now).
	QString start_label = selected_blocks_.first()->get_label();
	for (int i = 1; i < selected_blocks_.size(); i++) {
		if (selected_blocks_.at(i)->get_label() != start_label) {
			start_label.clear();
			break;
		}
	}

	bool ok;
	const QString s = QInputDialog::getText(this, tr("Label Node"),
											tr("Set node label"),
											QLineEdit::Normal, start_label,
											&ok);
	if (!ok) {
		return;
	}

	QVector<OakEngineNode *> nodes;
	nodes.reserve(selected_blocks_.size());
	foreach (Block *b, selected_blocks_) {
		nodes.append(reinterpret_cast<OakEngineNode *>(b));
	}
	oakengine_node_set_label_many(nodes.data(), nodes.size(),
								  s.toUtf8().constData());
}

void TimelineWidget::track_about_to_be_deleted(OakEngineTrack *track)
{
	if (track == subtitle_tentative_track_) {
		// User is deleting the tentative subtitle track. Technically they shouldn't do this, but they
		// might if they misinterpret it as permanent. If so, we handle it cleanly by pushing our
		// command as if the action really were permanent.
		oakengine_undo_push(take_subtitle_section_command(),
											 tr("Created Subtitle Track").toUtf8().constData());
	}
}

void TimelineWidget::set_selected_clips_autocaching(bool e)
{
	void *command = oakengine_undo_command_create_multi();

	for (Block *b : selected_blocks_) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
			oak_node_value v{};
			v.type = OAK_NODE_VALUE_BOOL;
			v.num = e ? 1 : 0;
			oakengine_undo_command_multi_add_child(
				command,
				oakengine_node_set_standard_value_command(
					reinterpret_cast<OakEngineNode *>(clip),
					oakengine_clip_auto_cache_input_id(), 0, 0, &v));
		}
	}

	oakengine_undo_push(
		command, (e ? tr("Enabled Auto-Caching On %1 Clip(s)")
						 .arg(selected_blocks_.size()) :
					 tr("Disabled Auto-Caching On %1 Clip(s)")
						 .arg(selected_blocks_.size())).toUtf8().constData());
}

void TimelineWidget::cache_clips()
{
	for (Block *b : selected_blocks_) {
		if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
			clip_request_invalidate_connected(clip, true);
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
			if (Node *connected = clip->get_connected_output(oakengine_clip_buffer_input_id())) {
				TimeRange adjusted =
					tto.get_adjusted_time(this->sequence(), connected, r,
										Node::k_transform_towards_input);
				clip_request_invalidate_connected(clip, true, adjusted);
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
				oakengine_clip_discard_cache(
					reinterpret_cast<OakEngineClip *>(clip));
			}
		}
	}
}

void TimelineWidget::multicam_enabled_triggered(bool e)
{
	void *command = oakengine_undo_command_create_multi();

	for (Block *b : qAsConst(selected_blocks_)) {
		if (ClipBlock *c = dynamic_cast<ClipBlock *>(b)) {
			if (Sequence *s = dynamic_cast<Sequence *>(c->connected_viewer())) {
				if (e) {
					// Adding multicams
					// Create multicam node and add it to the graph
					MultiCamNode *n = reinterpret_cast<MultiCamNode *>(
						oakengine_project_add_node(
							reinterpret_cast<OakEngineProject *>(s->parent()),
							"org.olivevideoeditor.Olive.multicam"));
					{
						oak_node_value v;
						v.type = OAK_NODE_VALUE_INT;
						v.num = c->get_track_type();
						oakengine_node_set_input(
							reinterpret_cast<OakEngineNode *>(n),
							oakengine_multicam_input_sequence_type(), &v);
					}
					// Node was already added by oakengine_project_add_node

					// For each output the sequence has to this clip, disconnect it and
					// connect to the multicam instead
					QVector<NodeInput> inputs = c->find_ways_node_arrives_here(s);
					for (const NodeInput &i : inputs) {
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_disconnect_command(
								reinterpret_cast<OakEngineNode *>(i.node()),
								i.input().toUtf8().constData(),
								i.element()));
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_connect_command(
								reinterpret_cast<OakEngineNode *>(n),
								reinterpret_cast<OakEngineNode *>(i.node()),
								i.input().toUtf8().constData(),
								i.element()));
					}

					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							reinterpret_cast<OakEngineNode *>(s),
							reinterpret_cast<OakEngineNode *>(n),
							oakengine_multicam_input_sequence(),
							-1));

					// Move sequence node one unit back, and place multicam in sequence's spot
					QPointF sequence_pos = c->get_node_position_in_context(s);
					QPointF shifted_pos = sequence_pos - QPointF(1, 0);
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(s), reinterpret_cast<void *>(c), shifted_pos.x(), shifted_pos.y(), 0));
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(n), reinterpret_cast<void *>(c), sequence_pos.x(), sequence_pos.y(), 0));

				} else {
					// Removing multicams
					// Locate first multicam that specifically ends up at this clip
					QVector<NodeInput> inputs = c->find_ways_node_arrives_here(s);
					for (const NodeInput &i : inputs) {
						if (MultiCamNode *mcn =
								dynamic_cast<MultiCamNode *>(i.node())) {
							for (auto it = mcn->output_connections().cbegin();
								 it != mcn->output_connections().cend(); it++) {
								oakengine_undo_command_multi_add_child(
									command,
									oakengine_node_disconnect_command(
										reinterpret_cast<OakEngineNode *>(it->second.node()),
										it->second.input().toUtf8().constData(),
										it->second.element()));
								oakengine_undo_command_multi_add_child(
									command,
									oakengine_node_connect_command(
										reinterpret_cast<OakEngineNode *>(s),
										reinterpret_cast<OakEngineNode *>(it->second.node()),
										it->second.input().toUtf8().constData(),
										it->second.element()));
							}

							oakengine_undo_command_multi_add_child(command, oakengine_node_remove_and_disconnect_command(reinterpret_cast<void *>(mcn)));
						}
					}
				}
			}
		}
	}

	oakengine_undo_push(
		command,
		(e ? tr("Multi-Cam Enabled On %1 Clip(s)").arg(selected_blocks_.size()) :
			tr("Multi-Cam Disabled On %1 Clip(s)").arg(selected_blocks_.size())).toUtf8().constData());
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
				viewer_output_audio_params(get_connected_node()).sample_rate_as_time_base());
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

		void *command = oakengine_undo_command_create_multi();

		foreach (Block *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(b->track()), reinterpret_cast<void *>(b), false ? 1 : 0));
		}

		foreach (Block *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(sequence()->track_list(b->track()->type())), b->track()->index(), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(b->in() + amount, timebase())));
		}

		// Nudge selections
		TimelineWidgetSelections new_sel = get_selections();
		new_sel.shift_time(amount);
		oakengine_undo_command_multi_add_child(command, create_set_selections_command(new_sel, get_selections()));

		oakengine_undo_push(command, tr("Nudged Clips").toUtf8().constData());
	}
}

void TimelineWidget::move_to_playhead_internal(bool out)
{
	if (get_connected_node() && !selected_blocks_.isEmpty()) {
		void *command = oakengine_undo_command_create_multi();

		// Remove each block from the graph
		QHash<Track *, Rational> earliest_pts;
		foreach (Block *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(b->track()), reinterpret_cast<void *>(b), false ? 1 : 0));

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
					oakengine_undo_command_multi_add_child(command, oakengine_block_resize_with_media_in_command(reinterpret_cast<void *>(b), new_out.numerator(), new_out.denominator()));
					new_in = 0;
				}
			}

			if (can_shift) {
				oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(sequence()->track_list(b->track()->type())), b->track()->index(), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(new_in, timebase())));
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
		oakengine_undo_command_multi_add_child(command, create_set_selections_command(new_sel, get_selections()));

		oakengine_undo_push(command,
											 tr("Moved Clip(s) To Point").toUtf8().constData());
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

	// Ripple the region out through the liboakengine C ABI facade (one
	// undoable all-tracks ripple, same as the old
	// TimelineRippleRemoveAreaCommand push).
	oakengine_sequence_ripple_delete_range(
		reinterpret_cast<OakEngineSequence *>(sequence()),
		Timecode::time_to_timestamp(in_ripple, timebase(), Timecode::k_round),
		Timecode::time_to_timestamp(out_ripple, timebase(),
									Timecode::k_round));

	// If we rippled, ump to where new cut is if applicable
	if (mode == Timeline::k_trim_in) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			closest_point_to_playhead.numerator(),
			closest_point_to_playhead.denominator());
	} else if (mode == Timeline::k_trim_out &&
			   closest_point_to_playhead == get_connected_node()->get_playhead()) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			playhead_time.numerator(), playhead_time.denominator());
	}
}

void TimelineWidget::edit_to(Timeline::MovementMode mode)
{
	const Rational playhead_time = get_connected_node()->get_playhead();

	// Batch trim through the liboakengine C ABI facade (one undoable
	// command; the per-track nearest-block semantics of the old app-side
	// assembly live behind the facade now).
	oakengine_sequence_trim_clips_to(
		reinterpret_cast<OakEngineSequence *>(sequence()),
		(mode == Timeline::k_trim_in) ? 0 : 1,
		Timecode::time_to_timestamp(playhead_time, timebase(),
									Timecode::k_round));
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

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_CLIPS,
		reinterpret_cast<OakEngineProject *>(get_connected_node()->project()),
		nullptr);
	int result_code = OAKENGINE_SERIALIZER_NO_DATA;
	oakengine_clipboard_paste(cb, OAKENGINE_CLIPBOARD_CLIPS,
							reinterpret_cast<OakEngineProject *>(
								get_connected_node()->project()),
							&result_code, nullptr, 0);

	if (result_code != OAKENGINE_SERIALIZER_OK) {
		oakengine_clipboard_free(cb);
		return false;
	}

	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);
	if (node_count == 0) {
		oakengine_clipboard_free(cb);
		return false;
	}

	void *command = oakengine_undo_command_create_multi();

	Project *project = get_connected_node()->project();
	for (int i = 0; i < node_count; i++) {
		Node *n = reinterpret_cast<Node *>(
			oakengine_clipboard_get_loaded_node_at(cb, i));
		oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(project),
			reinterpret_cast<OakEngineNode *>(n)));
		if (n->is_item() && !n->folder()) {
			oakengine_folder_add_child(
			reinterpret_cast<OakEngineNode *>(project->root()),
			reinterpret_cast<OakEngineNode *>(n));
		}
	}

	// Collect connections
	struct Conn {
		Node *output;
		Node *input;
		QString input_id;
		int element;
	};
	QVector<Conn> conns;
	oakengine_clipboard_foreach_connection(
		cb,
		[](OakEngineNode *out, OakEngineNode *in, const char *input_id,
		   int element, void *userdata) -> int {
			auto *v = static_cast<QVector<Conn> *>(userdata);
			v->append({reinterpret_cast<Node *>(out),
					   reinterpret_cast<Node *>(in),
					   QString::fromUtf8(input_id), element});
			return 0;
		},
		&conns);

	for (const Conn &c : conns) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_connect_command(
				reinterpret_cast<OakEngineNode *>(c.output),
				reinterpret_cast<OakEngineNode *>(c.input),
				c.input_id.toUtf8().constData(),
				c.element));
	}

	Rational paste_start = get_connected_node()->get_playhead();

	struct PropertyCtx {
		Rational paste_start;
		Rational paste_end;
		bool insert;
		void *command;
		TimelineWidget *self;
	};

	PropertyCtx pctx;
	pctx.paste_start = paste_start;
	pctx.paste_end = paste_start;
	pctx.insert = insert;
	pctx.command = command;
	pctx.self = this;

	// First pass: compute paste_end for insert mode
	oakengine_clipboard_foreach_property(
		cb,
		[](OakEngineNode *node, const char *key, const char *value,
		   void *userdata) -> int {
			auto *ctx = static_cast<PropertyCtx *>(userdata);
			if (ctx->insert && std::strcmp(key, "in") == 0) {
				Block *block = static_cast<Block *>(
					reinterpret_cast<Node *>(node));
				Rational length = block->length();
				Rational in = Rational::from_string(value);
				Rational end = ctx->paste_start + in + length;
				if (end > ctx->paste_end) {
					ctx->paste_end = end;
				}
			}
			return 0;
		},
		&pctx);

	if (insert && pctx.paste_end != paste_start) {
		insert_gaps_at(paste_start, pctx.paste_end - paste_start, command);
	}

	// Collect all properties
	QHash<OakEngineNode *, QMap<QString, QString>> props;
	oakengine_clipboard_foreach_property(
		cb,
		[](OakEngineNode *node, const char *key, const char *value,
		   void *userdata) -> int {
			auto *m = static_cast<QHash<OakEngineNode *, QMap<QString, QString>> *>(userdata);
			(*m)[node][QString::fromUtf8(key)] = QString::fromUtf8(value);
			return 0;
		},
		&props);

	for (auto it = props.cbegin(); it != props.cend(); it++) {
		Block *block = static_cast<Block *>(
			reinterpret_cast<Node *>(it.key()));
		if (it.value().contains(QStringLiteral("in"))) {
			Rational in = Rational::from_string(
				it.value()[QStringLiteral("in")].toStdString());
			Track::Reference track = Track::Reference::from_string(
				it.value()[QStringLiteral("track")]);
			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(sequence()->track_list(track.type())), track.index(), reinterpret_cast<void *>(block), core::Timecode::time_to_timestamp(paste_start + in, timebase())));
		}
	}

	oakengine_undo_push(
		command,
		tr("Pasted %1 Clip(s)").arg(node_count).toUtf8().constData());

	oakengine_clipboard_free(cb);
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
TimelineWidget::generate_existing_paste_map(void *clipboard)
{
	QHash<Node *, Node *> m;
	OakEngineClipboard *cb = static_cast<OakEngineClipboard *>(clipboard);
	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);

	for (int i = 0; i < node_count; i++) {
		Node *n = reinterpret_cast<Node *>(
			oakengine_clipboard_get_loaded_node_at(cb, i));
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

namespace {

struct SetSelectionsCommandData {
	TimelineWidget *timeline;
	TimelineWidgetSelections now;
	TimelineWidgetSelections old;
	bool process_block_changes;
};

static void redo_set_selections(void *userdata)
{
	auto *d = static_cast<SetSelectionsCommandData *>(userdata);
	d->timeline->set_selections(d->now, d->process_block_changes);
}

static void undo_set_selections(void *userdata)
{
	auto *d = static_cast<SetSelectionsCommandData *>(userdata);
	d->timeline->set_selections(d->old, d->process_block_changes);
}

static void free_set_selections(void *userdata)
{
	delete static_cast<SetSelectionsCommandData *>(userdata);
}

} // namespace

void *TimelineWidget::create_set_selections_command(
	const TimelineWidgetSelections &now, const TimelineWidgetSelections &old,
	bool process_block_changes)
{
	auto *d = new SetSelectionsCommandData{this, now, old,
									   process_block_changes};
	return oakengine_undo_command_create(
		tr("Set Selections").toUtf8().constData(), redo_set_selections,
		undo_set_selections, free_set_selections, d);
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


}
