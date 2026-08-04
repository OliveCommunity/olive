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

#include <utility>
#include "timelinewidget.h"
#include "timelinewidgetwaveformsync.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
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
#include "oakutil/range.h"
#include "dialog/proxy/proxydialog.h"
#include "dialog/sequence/sequence.h"
#include "dialog/speedduration/speeddurationdialog.h"
#include "oakutil/oaknode.h"
#include "oakengine/serializer.h"
#include "oakengine/undo.h"
#include "oakengine/events.h"
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/proxy.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "common/configwrapper.h"
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
#include "widget/timelinewidget/trackhandle.h"
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

/**
 * @brief Track::to_reference() directly as the app TrackReference mirror
 * (common/trackreferencehandle.h), resolved through the C ABI. Type
 * ordinals are identical, pinned by the static_asserts in the mirror
 * header; update both sides together.
 */
TrackReference track_app_ref(OakEngineTrack *track)
{
	return TrackReference(static_cast<TrackReference::Type>(track_type_of(track)),
						  track_index_of(track));
}

/// Clip-style predicate for block handles (replaces dynamic_cast<ClipBlock*>
/// now that the engine class definitions are no longer visible here).
OakEngineBlock *block_as_clip(OakEngineBlock *block)
{
	return (block &&
			oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(block)))
			   ? block
			   : nullptr;
}

/// Block::track() as a track handle (oakengine_block_get_track facade).
OakEngineTrack *block_track(OakEngineBlock *block)
{
	return oakengine_block_get_track(block);
}

/**
 * @brief Sequence::get_tracks() through the C ABI (per-type count + indexed
 * access; type ordinals match TrackReference::Type/OAKENGINE_TRACK_TYPE_*).
 *
 * WRAPPER-GAP: no tracklist enumeration facade
 * (oakengine_sequence_track_list() returns an opaque handle only).
 */
QVector<OakEngineTrack *> sequence_all_tracks(OakEngineSequence *seq)
{
	int counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(seq, &counts[0], &counts[1], &counts[2]);
	QVector<OakEngineTrack *> tracks;
	for (int type = 0; type < 3; type++) {
		for (int i = 0; i < counts[type]; i++) {
			if (OakEngineTrack *t = oakengine_sequence_track_at(seq, type, i)) {
				tracks.append(t);
			}
		}
	}
	return tracks;
}

/// Track::blocks() through the C ABI (count + indexed access).
QVector<OakEngineBlock *> track_all_blocks(OakEngineTrack *track)
{
	QVector<OakEngineBlock *> blocks;
	const int n = oakengine_track_block_count(track);
	blocks.reserve(n);
	for (int i = 0; i < n; i++) {
		if (OakEngineBlock *b = oakengine_track_block_at(track, i)) {
			blocks.append(b);
		}
	}
	return blocks;
}

/// Block::block_links() through the C ABI (count + indexed access).
QVector<OakEngineBlock *> block_all_links(OakEngineBlock *block)
{
	QVector<OakEngineBlock *> links;
	const int n = oakengine_block_link_count(block);
	links.reserve(n);
	for (int i = 0; i < n; i++) {
		if (OakEngineBlock *l = oakengine_block_link_at(block, i)) {
			links.append(l);
		}
	}
	return links;
}

/// Block::range() as rational seconds (C ABI rational getters).
TimeRange block_range(OakEngineBlock *block)
{
	auto *node = reinterpret_cast<const OakEngineNode *>(block);
	int in_num = 0, in_den = 1, out_num = 0, out_den = 1;
	oakengine_block_get_in_rational(node, &in_num, &in_den);
	oakengine_block_get_out_rational(node, &out_num, &out_den);
	return TimeRange(Rational(in_num, in_den), Rational(out_num, out_den));
}

/// Block::length() as rational seconds.
Rational block_length(OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_length_rational(reinterpret_cast<const OakEngineNode *>(block),
										&num, &den);
	return Rational(num, den);
}

/// Block::in() as rational seconds.
Rational block_in(OakEngineBlock *block)
{
	return block_range(block).in();
}

/// Block::out() as rational seconds.
Rational block_out(OakEngineBlock *block)
{
	return block_range(block).out();
}

/// Node::get_exclusive_dependencies() through the C ABI.
QVector<OakEngineNode *> block_exclusive_dependencies(OakEngineBlock *block)
{
	QVector<OakEngineNode *> deps;
	auto *node = reinterpret_cast<OakEngineNode *>(block);
	const int n = oakengine_node_get_exclusive_dependency_count(node);
	deps.reserve(n);
	for (int i = 0; i < n; i++) {
		if (OakEngineNode *d = oakengine_node_get_exclusive_dependency_at(node, i)) {
			deps.append(d);
		}
	}
	return deps;
}

/// Node::get_dependencies() through the C ABI (recursive input-connection
/// walk, mirroring the engine's get_dependencies_recursively()).
void collect_dependencies(QVector<OakEngineNode *> &list, OakEngineNode *node)
{
	oak::Node n(node);
	const int count = n.input_connection_count_all();
	for (int i = 0; i < count; i++) {
		const oak::NodeConnection conn = n.input_connection_at_all(i);
		OakEngineNode *dep = conn.source_node.handle();
		if (dep && !list.contains(dep)) {
			list.append(dep);
			collect_dependencies(list, dep);
		}
	}
}

/// Node::find_ways_node_arrives_here() through the C ABI (recursive
/// input-connection walk, mirroring the engine implementation).
void find_ways_node_arrives_here(OakEngineNode *output, OakEngineNode *input,
								 QVector<oak::Input> &v)
{
	oak::Node n(input);
	const int count = n.input_connection_count_all();
	for (int i = 0; i < count; i++) {
		const oak::NodeConnection conn = n.input_connection_at_all(i);
		if (conn.source_node.handle() == output) {
			v.append(oak::Input(input, conn.input_id, conn.element));
		} else if (!conn.source_node.is_null()) {
			find_ways_node_arrives_here(output, conn.source_node.handle(), v);
		}
	}
}

/// ViewerOutput::get_playhead() through the C ABI (the connected viewer is
/// only available as an opaque handle here).
Rational viewer_playhead(OakEngineNode *viewer)
{
	int64_t num = 0, den = 1;
	oakengine_viewer_get_playhead(viewer, &num, &den);
	return Rational(num, den);
}

struct SourceSyncClip {
	OakEngineBlock *clip = nullptr;
	oak_audio_sync_source_clip source;
	Rational source_head;
};

bool get_source_sync_clip(OakEngineBlock *block, SourceSyncClip *out)
{
	OakEngineBlock *clip = block_as_clip(block);
	if (!clip) {
		return false;
	}

	OakEngineNode *viewer = oakengine_clip_get_connected_viewer(block);
	if (!viewer || !oakengine_node_is_footage(viewer)) {
		return false;
	}

	// WRAPPER-GAP: oak::Footage lacks source_start_time(); query the C ABI
	// directly on a borrowed handle.
	oak::Footage footage = oak::Footage::borrow(viewer);
	int sst_num = 0, sst_den = 1;
	if (!footage ||
		oakengine_footage_get_source_start_time(footage.handle(), &sst_num,
												&sst_den) != 1) {
		return false;
	}

	out->clip = clip;
	const Rational source_start(sst_num, sst_den);
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
get_selected_source_sync_clips(const QVector<OakEngineBlock *> &blocks)
{
	QVector<SourceSyncClip> clips;
	for (OakEngineBlock *block : blocks) {
		SourceSyncClip sync_clip;
		if (get_source_sync_clip(block, &sync_clip)) {
			clips.append(sync_clip);
		}
	}
	return clips;
}

QVector<OakEngineNode *> get_selected_proxy_footage(const QVector<OakEngineBlock *> &blocks)
{
	QVector<OakEngineNode *> footage;
	for (OakEngineBlock *block : blocks) {
		if (!block_as_clip(block)) {
			continue;
		}

		OakEngineNode *viewer = oakengine_clip_get_connected_viewer(block);
		oak_video_params _vp;
		if (!viewer || !oakengine_node_is_footage(viewer) ||
			oakengine_viewer_get_first_enabled_video_stream(viewer, &_vp) < 0 ||
			!oakengine_video_params_is_valid(&_vp) ||
			footage.contains(viewer)) {
			continue;
		}

		footage.append(viewer);
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
		foreach (OakEngineTrack *t, sequence_all_tracks(sequence())) {
			oakengine_track_set_height(
				sequence(),
				track_type_of(t), track_index_of(t), h);
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
	view_splitter_->setCollapsible(TrackReference::k_video, false);
	view_splitter_->setCollapsible(TrackReference::k_audio, false);
	view_splitter_->setCollapsible(TrackReference::k_subtitle, true);

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
			for (OakEngineBlock *b : selected_blocks_) {
				start = std::min(start, block_in(b));
			}
			if (start != RATIONAL_MAX) {
				oakengine_viewer_set_playhead(
					reinterpret_cast<OakEngineNode *>(get_connected_node()),
					start.numerator(), start.denominator());
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

		QVector<OakEngineBlock *> new_blocks;

		const QVector<OakEngineTrack *> tracks = sequence_all_tracks(sequence());
		const int64_t playhead_ts = core::Timecode::time_to_timestamp(
			viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())),
			sequence_timebase(sequence()));
		for (OakEngineTrack *track : tracks) {
			if (track_is_locked(track)) {
				continue;
			}

			OakEngineBlock *b =
				oakengine_track_visible_block_at_time(track, playhead_ts);
			if (!b || oakengine_block_is_gap(b)) {
				continue;
			}

			new_blocks.push_back(b);
			sels[track_app_ref(track)].insert(block_range(b));
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

void TimelineWidget::ConnectNodeEvent(OakEngineNode *n)
{
	OakEngineNode *handle = n;

	// Track add/remove are now received via bridge sequence_track_* signals
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED);

	connect(bridge_, &EngineEventBridge::sequence_track_added, this,
			[this](OakEngineTrack *track, int track_type) {
				add_track(track);
				// Update the TrackView UI for this track type
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->track_view()->insert_track(track);
				}
			});
	connect(bridge_, &EngineEventBridge::sequence_track_removed, this,
			[this](OakEngineTrack *track, int track_type) {
				remove_track(track);
				// Update the TrackView UI for this track type
				if (track_type >= 0 && track_type < views_.size()) {
					views_.at(track_type)->track_view()->remove_track(track);
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
				track_updated(static_cast<TrackReference::Type>(
					oakengine_track_type(source)));
				track_index_changed(source, old_index, new_index);
			});
	connect(bridge_, &EngineEventBridge::track_height_changed, this,
			[this](OakEngineTrack *source, double) {
				track_updated(static_cast<TrackReference::Type>(
					oakengine_track_type(source)));
			});
	connect(bridge_, &EngineEventBridge::track_blocks_refreshed, this,
			[this](OakEngineTrack *source) {
				track_updated(static_cast<TrackReference::Type>(
					oakengine_track_type(source)));
			});
	connect(bridge_, &EngineEventBridge::track_block_added, this,
			[this](OakEngineBlock *block, qint64, qint64) {
				add_block(block);
			});
	connect(bridge_, &EngineEventBridge::track_block_removed, this,
			[this](OakEngineBlock *block, qint64, qint64) {
				remove_block(block);
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

	ruler()->set_playback_cache(oakengine_viewer_get_playback_cache(handle));

	{
		oak_video_params vp;
		oakengine_viewer_get_video_params(handle, 0, &vp);
		// time_base is the frame duration (e.g. 1/25); reconstruct it as
		// Rational(num, den). (Previously num/den were swapped here, which
		// produced the frame rate and triggered "INVALID TIMEBASE".)
		SetTimebase(Rational(vp.time_base_num, vp.time_base_den));
	}

	for (int i = 0; i < views_.size(); i++) {
		TimelineView *view = views_.at(i)->view();
		TrackView *track_view = views_.at(i)->track_view();

		// TrackView holds (sequence, type) and queries tracks via the C ABI
		track_view->connect_track_list(
			reinterpret_cast<OakEngineSequence *>(handle), i);
		view->connect_track_list(reinterpret_cast<OakEngineSequence *>(handle),
								 i);

		// Defer to the track to make all the block UI items necessary
		int type_counts[3] = { 0, 0, 0 };
		oakengine_sequence_track_count(
			reinterpret_cast<OakEngineSequence *>(handle), &type_counts[0],
			&type_counts[1], &type_counts[2]);
		for (int ti = 0; ti < type_counts[i]; ti++) {
			add_track(oakengine_sequence_track_at(
				reinterpret_cast<OakEngineSequence *>(handle), i, ti));
		}
	}
}

void TimelineWidget::DisconnectNodeEvent(OakEngineNode *n)
{
	// Bridge subscriptions and connections are cleaned up by
	// TimeBasedWidget::connect_viewer_node (disconnect(bridge_, nullptr, this, nullptr))

	deselect_all();

	foreach (OakEngineTrack *track,
			 sequence_all_tracks(reinterpret_cast<OakEngineSequence *>(n))) {
		remove_track(track);
	}

	ruler()->set_playback_cache(nullptr);

	SetTimebase(0);

	clear();

	foreach (TimelineAndTrackView *tview, views_) {
		tview->track_view()->disconnect_track_list();
		tview->view()->connect_track_list(nullptr, 0);
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
	QVector<OakEngineBlock *> newly_selected_blocks;

	foreach (OakEngineBlock *block, added_blocks_) {
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
	ripple_to(TimelineApp::k_trim_in);
}

void TimelineWidget::ripple_to_out()
{
	ripple_to(TimelineApp::k_trim_out);
}

void TimelineWidget::edit_to_in()
{
	edit_to(TimelineApp::k_trim_in);
}

void TimelineWidget::edit_to_out()
{
	edit_to(TimelineApp::k_trim_out);
}

void TimelineWidget::split_at_playhead()
{
	if (!get_connected_node()) {
		return;
	}

	const Rational playhead_time =
		viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node()));

	QVector<OakEngineBlock *> selected_blocks = get_selected_blocks();

	// Prioritize blocks that are selected and overlap the playhead
	QVector<OakEngineBlock *> blocks_to_split;
	QVector<bool> block_is_selected;

	bool some_blocks_are_selected = false;

	// Get all blocks at the playhead
	const int64_t playhead_ts = core::Timecode::time_to_timestamp(
		playhead_time, sequence_timebase(sequence()));
	foreach (OakEngineTrack *track, sequence_all_tracks(sequence())) {
		if (track_is_locked(track)) {
			continue;
		}

		OakEngineBlock *b = oakengine_track_block_at_time(track, playhead_ts);

		if (block_as_clip(b)) {
			bool selected = false;

			// See if this block is selected
			foreach (OakEngineBlock *item, selected_blocks) {
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
		foreach (OakEngineBlock *b, blocks_to_split) {
			clips.append(reinterpret_cast<OakEngineClip *>(b));
		}
		oakengine_sequence_split_clips(
			sequence(), clips.data(),
			clips.size(),
			Timecode::time_to_timestamp(playhead_time, timebase(),
										Timecode::k_round));
	}
}

void TimelineWidget::replace_blocks_with_gaps(const QVector<OakEngineBlock *> &blocks,
										   bool remove_from_graph,
										   void *command,
										   bool handle_transitions)
{
	foreach (OakEngineBlock *b, blocks) {
		if (oakengine_block_is_gap(b)) {
			// No point in replacing a gap with a gap, and TrackReplaceBlockWithGapCommand will clear
			// up any extraneous gaps
			continue;
		}

		OakEngineTrack *original_track = block_track(b);

		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(original_track), reinterpret_cast<void *>(b), handle_transitions ? 1 : 0));

		if (remove_from_graph) {
			void *remove_cmd = oakengine_undo_command_create_multi();
			oakengine_undo_command_multi_add_child(
				remove_cmd,
				oakengine_node_remove_and_disconnect_command(
					reinterpret_cast<void *>(b)));
			for (OakEngineNode *dep : block_exclusive_dependencies(b)) {
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

	QVector<OakEngineBlock *> selected_list = get_selected_blocks();

	// No-op if nothing is selected
	if (selected_list.isEmpty()) {
		return;
	}

	QVector<OakEngineBlock *> clips_to_delete;
	QVector<OakEngineBlock *> transitions_to_delete;

	bool all_gaps = true;

	foreach (OakEngineBlock *b, selected_list) {
		if (!oakengine_block_is_gap(b)) {
			all_gaps = false;
		}

		if (block_as_clip(b)) {
			clips_to_delete.append(b);
		} else if (oakengine_node_is_transition(
					   reinterpret_cast<OakEngineNode *>(b))) {
			transitions_to_delete.append(b);
		}
	}

	if (all_gaps) {
		ripple = true;
	}

	void *command = oakengine_undo_command_create_multi();

	// Remove all selections
	oakengine_undo_command_multi_add_child(command, create_set_selections_command(TimelineWidgetSelections(), get_selections()));

	// For transitions, remove them but extend their attached blocks to fill their place
	foreach (OakEngineBlock *transition, transitions_to_delete) {
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
	foreach (OakEngineBlock *b, clips_to_delete) {
		facade_clips.append(reinterpret_cast<OakEngineClip *>(b));
	}

	Rational new_playhead = RATIONAL_MAX;
	QVector<int64_t> ripple_ranges;
	if (ripple) {
		foreach (OakEngineBlock *b, selected_list) {
			ripple_ranges.append(int64_t(track_type_of(block_track(b))));
			ripple_ranges.append(track_index_of(block_track(b)));
			ripple_ranges.append(Timecode::time_to_timestamp(
				block_in(b), timebase(), Timecode::k_round));
			ripple_ranges.append(Timecode::time_to_timestamp(
				block_out(b), timebase(), Timecode::k_round));
			new_playhead = qMin(new_playhead, block_in(b));
		}
	}

	int rippled = 0;
	oakengine_sequence_delete_clips(
		sequence(),
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
	foreach (OakEngineTrack *t, sequence_all_tracks(sequence())) {
		double h;
		oakengine_track_get_height(
			sequence(),
			track_type_of(t), track_index_of(t), &h);
		oakengine_track_set_height(
			sequence(),
			track_type_of(t), track_index_of(t),
			h + oakengine_track_height_interval());
	}
}

void TimelineWidget::decrease_track_height()
{
	if (!get_connected_node()) {
		return;
	}

	// Decrease the height of each track by one "unit"
	foreach (OakEngineTrack *t, sequence_all_tracks(sequence())) {
		double h;
		oakengine_track_get_height(
			sequence(),
			track_type_of(t), track_index_of(t), &h);
		oakengine_track_set_height(
			sequence(),
			track_type_of(t), track_index_of(t),
			qMax(h - oakengine_track_height_interval(),
				 oakengine_track_height_minimum()));
	}
}

void TimelineWidget::insert_footage_at_playhead(
	const QVector<OakEngineNode *> &footage)
{
	auto command = oakengine_undo_command_create_multi();
	import_tool_->place_at(footage,
						  viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())),
						  true, command, 0, true);
	oakengine_undo_push(command,
										 tr("Inserted Footage At Playhead").toUtf8().constData());
}

void TimelineWidget::overwrite_footage_at_playhead(
	const QVector<OakEngineNode *> &footage)
{
	auto command = oakengine_undo_command_create_multi();
	import_tool_->place_at(footage,
						  viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())),
						  false, command, 0, true);
	oakengine_undo_push(command,
										 tr("Overwrote Footage At Playhead").toUtf8().constData());
}

void TimelineWidget::toggle_links_on_selected()
{
	QVector<OakEngineNode *> blocks;
	bool link = true;

	foreach (OakEngineBlock *item, get_selected_blocks()) {
		// Only clips can be linked
		if (!block_as_clip(item)) {
			continue;
		}

		// Prioritize unlinking, if any block has links, assume we're unlinking
		if (link && oakengine_block_link_count(item) > 0) {
			link = false;
		}

		blocks.append(reinterpret_cast<OakEngineNode *>(item));
	}

	if (blocks.isEmpty()) {
		return;
	}

	// Link/unlink through the liboakengine C ABI facade (one undoable
	// command, same as the old NodeLinkManyCommand push).
	QVector<OakEngineClip *> clips;
	clips.reserve(blocks.size());
	foreach (OakEngineNode *n, blocks) {
		clips.append(reinterpret_cast<OakEngineClip *>(n));
	}
	oakengine_clip_set_linked(clips.data(), clips.size(), link ? 1 : 0);
}

void TimelineWidget::add_default_transitions_to_selected()
{
	QVector<OakEngineClip *> blocks;

	foreach (OakEngineBlock *item, get_selected_blocks()) {
		// Only clips can be linked
		if (OakEngineBlock *clip = block_as_clip(item)) {
			blocks.append(reinterpret_cast<OakEngineClip *>(clip));
		}
	}

	if (!blocks.isEmpty()) {
		// Through the liboakengine C ABI facade (one undoable command with
		// the same engine semantics as the old app-side push).
		oakengine_sequence_add_default_transition(
			sequence(), blocks.data(),
			blocks.size());
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

	QVector<OakEngineNode *> selected_nodes;

	foreach (OakEngineBlock *block, selected_blocks_) {
		selected_nodes.append(reinterpret_cast<OakEngineNode *>(block));

		QVector<OakEngineNode *> deps;
		collect_dependencies(deps, reinterpret_cast<OakEngineNode *>(block));

		foreach (OakEngineNode *d, deps) {
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

	foreach (OakEngineBlock *block, selected_blocks_) {
		earliest_in = qMin(earliest_in, block_in(block));
	}

	foreach (OakEngineBlock *block, selected_blocks_) {
		oakengine_clipboard_set_property(
			cb, reinterpret_cast<OakEngineNode *>(block), "in",
			(block_in(block) - earliest_in).to_string().c_str());
		QString track_ref = track_app_ref(block_track(block)).to_string();
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
	oakengine_viewer_workarea wa;
	if (!get_connected_node() ||
		oakengine_viewer_get_workarea(
			reinterpret_cast<OakEngineNode *>(get_connected_node()), &wa) !=
			0 ||
		!wa.enabled) {
		return;
	}

	// Compound through the liboakengine C ABI facade (ripple removal or
	// per-track gap fill + workarea disable, one undoable command with the
	// same semantics as the old app-side assembly).
	const Rational wa_in(int(wa.in_num), int(wa.in_den));
	const Rational wa_out(int(wa.out_num), int(wa.out_den));
	oakengine_sequence_ripple_delete_in_to_out(
		sequence(), ripple ? 1 : 0,
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
	QVector<OakEngineBlock *> items = get_selected_blocks();

	if (items.isEmpty()) {
		return;
	}

	// Flip each selected clip through the liboakengine C ABI facade (one
	// undoable command, same per-block inversion as the old children).
	QVector<OakEngineClip *> clips;
	clips.reserve(items.size());
	foreach (OakEngineBlock *i, items) {
		if (OakEngineBlock *clip = block_as_clip(i)) {
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
	foreach (OakEngineBlock *b, selected_blocks_) {
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
	QVector<OakEngineBlock *> clips;

	foreach (OakEngineBlock *b, selected_blocks_) {
		if (OakEngineBlock *c = block_as_clip(b)) {
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
	Rational anchor_timeline_in = block_in(sync_clips.first().clip);
	for (const SourceSyncClip &sync_clip : sync_clips) {
		if (sync_clip.source_head < reference.source_head) {
			reference = sync_clip;
		}
		if (block_in(sync_clip.clip) < anchor_timeline_in) {
			anchor_timeline_in = block_in(sync_clip.clip);
		}
	}

	struct SyncPlacement {
		OakEngineBlock *clip = nullptr;
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
		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(block_track(placement.clip)), reinterpret_cast<void *>(placement.clip), false ? 1 : 0));
	}

	TimelineWidgetSelections new_selections;
	for (const SyncPlacement &placement : placements) {
		oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), track_type_of(block_track(placement.clip)))), track_index_of(block_track(placement.clip)), reinterpret_cast<void *>(placement.clip), core::Timecode::time_to_timestamp(placement.timeline_in, timebase())));

		new_selections[track_app_ref(block_track(placement.clip))].insert(
			TimeRange(placement.timeline_in,
					  placement.timeline_in + block_length(placement.clip)));
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
		if (block_in(sync_clip.clip) < block_in(reference.clip)) {
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
		OakEngineBlock *clip = nullptr;
		Rational timeline_in;
		double speed = 1.0;
	};

	QVector<SyncPlacement> placements;
	placements.append({ reference.clip, block_in(reference.clip), 1.0 });
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
		const Rational reference_in = block_in(reference.clip);
		oakengine_audio_sync_place_by_waveform_offset(
			reference_in.numerator(), reference_in.denominator(),
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
		oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(block_track(placement.clip)), reinterpret_cast<void *>(placement.clip), false ? 1 : 0));

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
		oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), track_type_of(block_track(placement.clip)))), track_index_of(block_track(placement.clip)), reinterpret_cast<void *>(placement.clip), core::Timecode::time_to_timestamp(placement.timeline_in, timebase())));

		// A speed change scales the clip's timeline length accordingly
		const Rational placed_length =
			placement.speed == 1.0 ?
				block_length(placement.clip) :
				Rational::from_double(block_length(placement.clip).to_double() /
									 placement.speed);
		new_selections[track_app_ref(block_track(placement.clip))].insert(
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

	const QVector<OakEngineNode *> footage =
		get_selected_proxy_footage(selected_blocks_);
	qDebug() << "GenerateProxiesForSelectedClips: starting proxy generation for"
			 << footage.size() << "footage item(s)";
	for (OakEngineNode *item : footage) {
		// WRAPPER-GAP: oak::Footage::{get_effective_proxy_params,set_proxy}
		oak::Footage f = oak::Footage::borrow(item);
		oak_video_params _vp;
		oakengine_viewer_get_first_enabled_video_stream(item, &_vp);
		if (!oakengine_video_params_is_valid(&_vp)) {
			qWarning()
				<< "GenerateProxiesForSelectedClips: skipping item with no valid video stream"
				<< f.filename();
			continue;
		}

		// Index of the first enabled video stream
		// (VideoParams::stream_index() has no facade equivalent).
		int stream_index = -1;
		const QVector<QPair<int, int>> streams =
			oak::Node(item).enabled_streams();
		for (const QPair<int, int> &s : streams) {
			if (s.first == OAKENGINE_TRACK_TYPE_VIDEO) {
				stream_index = s.second;
				break;
			}
		}
		if (stream_index < 0) {
			continue;
		}

		oak_proxy_params params;
		oakengine_footage_get_effective_proxy_params(f.handle(), &params);
		oak_proxy_result proxy;
		char cache_buf[512];
		oakengine_project_cache_path(oakengine_node_get_project(item),
									 cache_buf, sizeof(cache_buf));
		int ret = oakengine_proxy_get_or_start(
			cache_buf,
			f.filename().toUtf8().constData(),
			stream_index, &params, &proxy);
		if (ret != 0) {
			qWarning() << "GenerateProxiesForSelectedClips: failed to get/start proxy for"
					   << f.filename();
			continue;
		}
		qDebug() << "GenerateProxiesForSelectedClips: proxy state="
				 << proxy.state
				 << "file=" << QString::fromUtf8(proxy.filename)
				 << "cache=" << cache_buf;
		oakengine_footage_set_proxy(f.handle(),
								   proxy.filename, proxy.state,
								   stream_index, 1, params.version);
		f.invalidate();
	}
}

void TimelineWidget::set_selected_clips_proxy_enabled(bool enabled)
{
	const QVector<OakEngineNode *> footage =
		get_selected_proxy_footage(selected_blocks_);
	qDebug() << "TimelineWidget::SetSelectedClipsProxyEnabled:" << enabled
			 << "footage count=" << footage.size();
	for (OakEngineNode *item : footage) {
		oak::Footage f = oak::Footage::borrow(item);
		if (f.proxy_path().isEmpty()) {
			qDebug()
				<< "  skipping item with empty proxy path" << f.filename();
			continue;
		}

		f.set_proxy_enabled(enabled);
		f.invalidate();
	}
}

void TimelineWidget::reveal_proxy_for_selected_clips()
{
	const QVector<OakEngineNode *> footage =
		get_selected_proxy_footage(selected_blocks_);
	for (OakEngineNode *item : footage) {
		oak::Footage f = oak::Footage::borrow(item);
		if (f.proxy_path().isEmpty()) {
			continue;
		}

#if defined(Q_OS_WINDOWS)
		QStringList args;
		args << "/select," << QDir::toNativeSeparators(f.proxy_path());
		QProcess::startDetached(QStringLiteral("explorer"), args);
#elif defined(Q_OS_MAC)
		QStringList args;
		args << "-e";
		args << "tell application \"Finder\"";
		args << "-e";
		args << "activate";
		args << "-e";
		args << "select POSIX file \"" + f.proxy_path() + "\"";
		args << "-e";
		args << "end tell";
		QProcess::startDetached(QStringLiteral("osascript"), args);
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QFileInfo(f.proxy_path()).dir().absolutePath()));
#endif
		return;
	}
}

void TimelineWidget::delete_proxies_for_selected_clips()
{
	const QVector<OakEngineNode *> footage =
		get_selected_proxy_footage(selected_blocks_);
	for (OakEngineNode *item : footage) {
		// WRAPPER-GAP: oak::Footage::clear_proxy
		oak::Footage f = oak::Footage::borrow(item);
		if (f.proxy_path().isEmpty()) {
			continue;
		}

		QFile::remove(f.proxy_path());
		{
			char wbuf[4096];
			int wlen = oakengine_proxy_get_working_filename(
				f.proxy_path().toUtf8().constData(), wbuf, sizeof(wbuf));
			if (wlen > 0) {
				QFile::remove(QString::fromUtf8(wbuf, wlen));
			}
		}
		oakengine_footage_clear_proxy(f.handle());
		f.invalidate();
	}
}

void TimelineWidget::show_proxy_dialog_for_selected_clips()
{
	const QVector<OakEngineNode *> handles =
		get_selected_proxy_footage(selected_blocks_);
	ProxyDialog d(this, handles);
	d.exec();
}

void TimelineWidget::recording_callback(const QString &filename,
									   const TimeRange &time,
									   const TrackReference &track)
{
	OakEngineNode *root = oakengine_project_root(
		oakengine_node_get_project(
			reinterpret_cast<OakEngineNode *>(get_connected_node())));
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
		import_tool_->place_at({ front },
							  time.in(), false, import_command, track.index());
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
		bool should_adjust_splitter = (sz[TrackReference::k_subtitle] == 0);
		int subtitle_track_count = 0;
		if (sequence()) {
			oakengine_sequence_track_count(
				sequence(), nullptr,
				nullptr, &subtitle_track_count);
		}
		bool should_add_sub_track =
			(sequence() && subtitle_track_count == 0);

		if (should_adjust_splitter || should_add_sub_track) {
			// Create command
			subtitle_show_command_ = oakengine_undo_command_create_multi();

			if (should_adjust_splitter) {
				sz[TrackReference::k_subtitle] = height() / TrackReference::k_count;
				oakengine_undo_command_multi_add_child(
					subtitle_show_command_,
					make_splitter_sizes_command(view_splitter_, sz));
			}

			if (should_add_sub_track) {
				void *track_add_cmd = oakengine_sequence_add_track_command(
					sequence(),
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

	QVector<OakEngineBlock *> blocks = this->selected_blocks_;
	if (blocks.empty()) {
		return;
	}

	QVector<TrackReference> tracks(blocks.size());
	QVector<TimeRange> times(blocks.size());
	QVector<int> track_offset(TrackReference::k_count, INT_MAX);
	Rational start_time = RATIONAL_MAX;
	Rational end_time = RATIONAL_MIN;
	for (int i = 0; i < blocks.size(); i++) {
		OakEngineBlock *b = blocks.at(i);

		TrackReference tf = track_app_ref(block_track(b));
		tracks[i] = tf;
		times[i] = block_range(b);

		int &to = track_offset[tf.type()];
		to = std::min(to, tf.index());

		start_time = std::min(start_time, block_in(b));
		end_time = std::max(end_time, block_out(b));
	}

	auto move_to_nest_command = oakengine_undo_command_create_multi();

	// Remove blocks from this sequence
	replace_blocks_with_gaps(blocks, false, move_to_nest_command);

	// Create new sequence
	OakEngineProject *project = oakengine_node_get_project(
		reinterpret_cast<OakEngineNode *>(this->get_connected_node()));
	OakEngineSequence *nest =
		Core::instance()->create_new_sequence_for_project(
			tr("Nested Sequence %1"), project);
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
			project,
			reinterpret_cast<OakEngineNode *>(nest)));

	// Add to same folder
	oakengine_folder_add_child(
		oakengine_node_folder(
			reinterpret_cast<OakEngineNode *>(this->get_connected_node())),
		reinterpret_cast<OakEngineNode *>(nest));

	// Place blocks in new sequence
	for (int i = 0; i < blocks.size(); i++) {
		OakEngineBlock *b = blocks.at(i);

		const TimeRange &range = times.at(i);
		const TrackReference &track = tracks.at(i);

		oakengine_undo_command_multi_add_child(move_to_nest_command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(nest, track.type())), track.index() - track_offset.at(track.type()), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(range.in() - start_time, sequence_timebase(nest))));
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
		for (int i = 0; i < TrackReference::k_count; i++) {
			if (track_offset.at(i) == INT_MAX) {
				// No clips on this track
				continue;
			}

			int type_counts[3] = { 0, 0, 0 };
			oakengine_sequence_track_count(
				sequence(),
				&type_counts[0], &type_counts[1], &type_counts[2]);
			const int64_t start_ts = core::Timecode::time_to_timestamp(
				start_time, sequence_timebase(sequence()));
			const int64_t end_ts = core::Timecode::time_to_timestamp(
				end_time, sequence_timebase(sequence()));
			if (index < type_counts[i] &&
				oakengine_track_is_range_free(
					sequence(), i,
					index, start_ts, end_ts) != 1) {
				empty = false;
				break;
			}
		}
	}

	// Place new sequence in this sequence
	import_tool_->place_at({ reinterpret_cast<OakEngineNode *>(nest) },
						  start_time, false, meta_command, index);

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
	for (int i = 0; i < TrackReference::k_count; i++) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_track_list_insert_gaps_command(
				reinterpret_cast<void *>(oakengine_sequence_track_list(
					sequence(), i)),
				earliest_point.numerator(), earliest_point.denominator(),
				insert_length.numerator(), insert_length.denominator()));
	}
}

OakEngineTrack *TimelineWidget::get_track_from_reference(const TrackReference &ref) const
{
	if (!sequence()) {
		return nullptr;
	}
	return oakengine_sequence_track_at(
		sequence(),
		static_cast<int>(ref.type()), ref.index());
}

int TimelineWidget::get_track_y(const TrackReference &ref)
{
	return views_.at(ref.type())->view()->get_track_y(ref.index());
}

int TimelineWidget::get_track_height(const TrackReference &ref)
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

void TimelineWidget::add_block(OakEngineBlock *block)
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

		if (selections_[track_app_ref(block_track(block))].contains(
				block_range(block)) &&
			!selected_blocks_.contains(block)) {
			selected_blocks_.append(block);
		}
	}
}

void TimelineWidget::remove_block(OakEngineBlock *block)
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

void TimelineWidget::add_track(OakEngineTrack *track)
{
	foreach (OakEngineBlock *b, track_all_blocks(track)) {
		add_block(b);
	}

	bridge_->subscribe(track, OAKENGINE_EVENT_TRACK_INDEX_CHANGED);
	bridge_->subscribe(track, OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED);
	bridge_->subscribe(track, OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED);
	bridge_->subscribe(track, OAKENGINE_EVENT_TRACK_BLOCK_ADDED);
	bridge_->subscribe(track, OAKENGINE_EVENT_TRACK_BLOCK_REMOVED);
}

void TimelineWidget::remove_track(OakEngineTrack *track)
{
	// Bridge subscriptions auto-die with the observed engine object.
	// No per-track unsubscribe needed.

	remove_selection(TimeRange(0, RATIONAL_MAX), track_app_ref(track));

	foreach (OakEngineBlock *b, track_all_blocks(track)) {
		remove_block(b);
	}
}

void TimelineWidget::track_updated(TrackReference::Type type)
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

	QVector<OakEngineBlock *> selected = get_selected_blocks();

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

		if (OakEngineBlock *clip = block_as_clip(selected.first())) {
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
				const QVector<OakEngineNode *> proxy_footage =
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
								[](OakEngineNode *footage) {
									oak::Footage f =
										oak::Footage::borrow(footage);
									return f.proxy_enabled();
								}));
				connect(use_proxy, &QAction::triggered, this,
						&TimelineWidget::set_selected_clips_proxy_enabled);

				QAction *reveal_proxy =
					proxy_menu->addAction(tr("Reveal Proxy"));
				reveal_proxy->setEnabled(
					std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
								[](OakEngineNode *footage) {
									oak::Footage f =
										oak::Footage::borrow(footage);
									return !f.proxy_path().isEmpty();
								}));
				connect(reveal_proxy, &QAction::triggered, this,
						&TimelineWidget::reveal_proxy_for_selected_clips);

				QAction *delete_proxy =
					proxy_menu->addAction(tr("Delete Proxy"));
				delete_proxy->setEnabled(
					std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
								[](OakEngineNode *footage) {
									oak::Footage f =
										oak::Footage::borrow(footage);
									return !f.proxy_path().isEmpty();
								}));
				connect(delete_proxy, &QAction::triggered, this,
						&TimelineWidget::delete_proxies_for_selected_clips);

				QAction *proxy_settings =
					proxy_menu->addAction(tr("Proxy Settings..."));
				connect(proxy_settings, &QAction::triggered, this,
						&TimelineWidget::show_proxy_dialog_for_selected_clips);
			}

			OakEngineNode *connected_viewer =
				oakengine_clip_get_connected_viewer(
					reinterpret_cast<OakEngineBlock *>(clip));
			if (connected_viewer) {
				QAction *reveal_in_footage_viewer =
					menu.addAction(tr("Reveal in Footage Viewer"));
				reveal_in_footage_viewer->setData(
					reinterpret_cast<quintptr>(connected_viewer));
				reveal_in_footage_viewer->setProperty(
					"range", QVariant::fromValue(clip_media_range(clip)));
				connect(reveal_in_footage_viewer, &QAction::triggered, this,
						&TimelineWidget::reveal_in_footage_viewer);

				QAction *reveal_in_project =
					menu.addAction(tr("Reveal in Project"));
				reveal_in_project->setData(
					reinterpret_cast<quintptr>(connected_viewer));
				connect(reveal_in_project, &QAction::triggered, this,
						&TimelineWidget::reveal_in_project);

				OakEngineSequence *connected_sequence =
					oakengine_node_is_sequence(connected_viewer) ?
						reinterpret_cast<OakEngineSequence *>(connected_viewer) :
						nullptr;
				if (connected_sequence) {
					QAction *multicam_enabled = menu.addAction(tr("Multi-Cam"));
					multicam_enabled->setCheckable(true);

					bool has_multicam = false;
					QVector<oak::Input> paths;
					find_ways_node_arrives_here(
						reinterpret_cast<OakEngineNode *>(connected_sequence),
						reinterpret_cast<OakEngineNode *>(clip), paths);

					for (const oak::Input &i : paths) {
						if (oakengine_node_is_multicam(i.node_handle())) {
							has_multicam = true;
							break;
						}
					}

					multicam_enabled->setChecked(has_multicam);

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
				tr("Disabled"), TimelineApp::k_thumbnail_off,
				OAK_CONFIG("TimelineThumbnailMode"));
			thumbnail_menu->add_action_with_data(
				tr("Only At In Points"), TimelineApp::k_thumbnail_in_out,
				OAK_CONFIG("TimelineThumbnailMode"));
			thumbnail_menu->add_action_with_data(
				tr("Enabled"), TimelineApp::k_thumbnail_on,
				OAK_CONFIG("TimelineThumbnailMode"));

			connect(thumbnail_menu, &Menu::triggered, this,
					&TimelineWidget::set_view_thumbnails_enabled);
		}

		QAction *show_waveforms = menu.addAction(tr("Show Waveforms"));
		show_waveforms->setCheckable(true);
		show_waveforms->setChecked(
			OAK_CONFIG("TimelineWaveformMode").toInt() ==
			TimelineApp::k_waveforms_enabled);
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

	SequenceDialog sd(reinterpret_cast<OakEngineNode *>(sequence()), SequenceDialog::k_existing, this);
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
	set_view_beam_cursor(TimelineCoordinate(0, TrackReference::k_none, -1));
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
	OAK_CONFIG("TimelineWaveformMode") = e ? TimelineApp::k_waveforms_enabled :
											   TimelineApp::k_waveforms_disabled;
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

void TimelineWidget::track_index_changed(OakEngineTrack *track, int old, int now)
{
	TrackReference old_ref(static_cast<TrackReference::Type>(oakengine_track_type(track)),
						   old);
	TrackReference new_ref(static_cast<TrackReference::Type>(oakengine_track_type(track)),
						   now);

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

	OakEngineNode *item_to_reveal =
		reinterpret_cast<OakEngineNode *>(a->data().value<quintptr>());
	TimeRange r = a->property("range").value<TimeRange>();

	emit reveal_viewer_in_footage_viewer(item_to_reveal, r);
}

void TimelineWidget::reveal_in_project()
{
	QAction *a = static_cast<QAction *>(sender());

	OakEngineNode *item_to_reveal =
		reinterpret_cast<OakEngineNode *>(a->data().value<quintptr>());

	emit reveal_viewer_in_project(item_to_reveal);
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
	auto block_label = [](OakEngineBlock *b) {
		char buf[512];
		buf[0] = '\0';
		oakengine_node_get_label(reinterpret_cast<OakEngineNode *>(b), buf,
								 sizeof(buf));
		return QString::fromUtf8(buf);
	};
	QString start_label = block_label(selected_blocks_.first());
	for (int i = 1; i < selected_blocks_.size(); i++) {
		if (block_label(selected_blocks_.at(i)) != start_label) {
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
	foreach (OakEngineBlock *b, selected_blocks_) {
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

	for (OakEngineBlock *b : selected_blocks_) {
		if (OakEngineBlock *clip = block_as_clip(b)) {
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
	for (OakEngineBlock *b : selected_blocks_) {
		if (OakEngineBlock *clip = block_as_clip(b)) {
			clip_request_invalidate_connected(clip, true);
		}
	}
}

void TimelineWidget::cache_clips_in_out()
{
	if (!this->sequence()) {
		return;
	}

	oakengine_viewer_workarea wa;
	if (oakengine_viewer_get_workarea(
			reinterpret_cast<OakEngineNode *>(this->sequence()), &wa) != 0 ||
		!wa.enabled) {
		return;
	}

	TimeTargetObject tto;
	tto.set_time_target(reinterpret_cast<OakEngineNode *>(this->sequence()));

	const TimeRange r(Rational(int(wa.in_num), int(wa.in_den)),
					  Rational(int(wa.out_num), int(wa.out_den)));
	for (OakEngineBlock *b : std::as_const(selected_blocks_)) {
		if (OakEngineBlock *clip = block_as_clip(b)) {
			if (OakEngineNode *connected = clip_connected_node(clip)) {
				TimeRange adjusted =
					tto.get_adjusted_time(
						reinterpret_cast<OakEngineNode *>(this->sequence()),
						connected,
						r, k_transform_towards_input);
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
		for (OakEngineBlock *b : selected_blocks_) {
			if (OakEngineBlock *clip = block_as_clip(b)) {
				oakengine_clip_discard_cache(
					reinterpret_cast<OakEngineClip *>(clip));
			}
		}
	}
}

void TimelineWidget::multicam_enabled_triggered(bool e)
{
	void *command = oakengine_undo_command_create_multi();

	for (OakEngineBlock *b : std::as_const(selected_blocks_)) {
		if (OakEngineBlock *c = block_as_clip(b)) {
			OakEngineNode *viewer = oakengine_clip_get_connected_viewer(c);
			OakEngineSequence *s = oakengine_node_is_sequence(viewer) ?
							  reinterpret_cast<OakEngineSequence *>(viewer) :
							  nullptr;
			if (s) {
				if (e) {
					// Adding multicams
					// Create multicam node and add it to the graph
					OakEngineNode *n = oakengine_project_add_node(
						oakengine_node_parent(
							reinterpret_cast<OakEngineNode *>(s)),
						"org.olivevideoeditor.Olive.multicam");
					{
						oak_node_value v;
						v.type = OAK_NODE_VALUE_INT;
						v.num = track_type_of(block_track(c));
						oakengine_node_set_input(
							n,
							oakengine_multicam_input_sequence_type(), &v);
					}
					// Node was already added by oakengine_project_add_node

					// For each output the sequence has to this clip, disconnect it and
					// connect to the multicam instead
					QVector<oak::Input> inputs;
					find_ways_node_arrives_here(
						reinterpret_cast<OakEngineNode *>(s),
						reinterpret_cast<OakEngineNode *>(c), inputs);
					for (const oak::Input &i : inputs) {
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_disconnect_command(
								i.node_handle(),
								i.input_id().toUtf8().constData(),
								i.element()));
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_connect_command(
								n,
								i.node_handle(),
								i.input_id().toUtf8().constData(),
								i.element()));
					}

					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							reinterpret_cast<OakEngineNode *>(s),
							n,
							oakengine_multicam_input_sequence(),
							-1));

					// Move sequence node one unit back, and place multicam in sequence's spot
					double pos_x = 0, pos_y = 0;
					oakengine_node_get_context_position(
						reinterpret_cast<OakEngineNode *>(s),
						reinterpret_cast<OakEngineNode *>(c), &pos_x, &pos_y,
						nullptr);
					QPointF sequence_pos(pos_x, pos_y);
					QPointF shifted_pos = sequence_pos - QPointF(1, 0);
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(s), reinterpret_cast<void *>(c), shifted_pos.x(), shifted_pos.y(), 0));
					oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(n), reinterpret_cast<void *>(c), sequence_pos.x(), sequence_pos.y(), 0));

				} else {
					// Removing multicams
					// Locate first multicam that specifically ends up at this clip
					QVector<oak::Input> inputs;
					find_ways_node_arrives_here(
						reinterpret_cast<OakEngineNode *>(s),
						reinterpret_cast<OakEngineNode *>(c), inputs);
					for (const oak::Input &i : inputs) {
						OakEngineNode *mcn = i.node_handle();
						if (oakengine_node_is_multicam(mcn)) {
							oak::Node mcnode(mcn);
							const int out_count =
								mcnode.output_connection_count();
							for (int k = 0; k < out_count; k++) {
								const oak::NodeConnection oc =
									mcnode.output_connection_at_ex(k);
								oakengine_undo_command_multi_add_child(
									command,
									oakengine_node_disconnect_command(
										oc.node.handle(),
										oc.input_id.toUtf8().constData(),
										oc.element));
								oakengine_undo_command_multi_add_child(
									command,
									oakengine_node_connect_command(
										reinterpret_cast<OakEngineNode *>(s),
										oc.node.handle(),
										oc.input_id.toUtf8().constData(),
										oc.element));
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

	update_viewports(static_cast<TrackReference::Type>(ghost->get_track().type()));
}

void TimelineWidget::update_view_timebases()
{
	for (int i = 0; i < views_.size(); i++) {
		TimelineAndTrackView *view = views_.at(i);

		if (get_connected_node() && use_audio_time_units_ &&
			i == TrackReference::k_audio) {
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
		foreach (OakEngineBlock *b, selected_blocks_) {
			if (block_in(b) + amount < 0) {
				amount = -block_in(b);
			}
		}

		if (amount.isNull()) {
			return;
		}

		void *command = oakengine_undo_command_create_multi();

		foreach (OakEngineBlock *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(block_track(b)), reinterpret_cast<void *>(b), false ? 1 : 0));
		}

		foreach (OakEngineBlock *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), track_type_of(block_track(b)))), track_index_of(block_track(b)), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(block_in(b) + amount, timebase())));
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
		QHash<OakEngineTrack *, Rational> earliest_pts;
		foreach (OakEngineBlock *b, selected_blocks_) {
			oakengine_undo_command_multi_add_child(command, oakengine_track_replace_block_with_gap_command(reinterpret_cast<void *>(block_track(b)), reinterpret_cast<void *>(b), false ? 1 : 0));

			Rational r = earliest_pts.value(block_track(b),
											out ? RATIONAL_MIN : RATIONAL_MAX);
			Rational compare = out ? block_out(b) : block_in(b);
			if ((compare < r) == !out) {
				earliest_pts.insert(block_track(b), compare);
			}
		}

		foreach (OakEngineBlock *b, selected_blocks_) {
			Rational shift_amt = viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())) -
								 earliest_pts.value(block_track(b));
			Rational new_in = block_in(b) + shift_amt;
			bool can_shift = true;

			if (new_in < 0) {
				// Handle clips threatening to go below 0
				Rational new_out = new_in + block_length(b);
				if (new_out <= 0) {
					can_shift = false;
				} else {
					oakengine_undo_command_multi_add_child(command, oakengine_block_resize_with_media_in_command(reinterpret_cast<void *>(b), new_out.numerator(), new_out.denominator()));
					new_in = 0;
				}
			}

			if (can_shift) {
				oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), track_type_of(block_track(b)))), track_index_of(block_track(b)), reinterpret_cast<void *>(b), core::Timecode::time_to_timestamp(new_in, timebase())));
			}
		}

		// Shift selections
		TimelineWidgetSelections new_sel = get_selections();
		for (auto it = new_sel.begin(); it != new_sel.end(); it++) {
			Rational track_adj =
				viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())) -
				earliest_pts.value(get_track_from_reference(it.key()),
								   viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node())));
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

void TimelineWidget::set_view_transition_overlay(OakEngineClip *out, OakEngineClip *in)
{
	foreach (TimelineAndTrackView *tview, views_) {
		tview->view()->set_transition_overlay(
			reinterpret_cast<OakEngineBlock *>(out),
			reinterpret_cast<OakEngineBlock *>(in));
	}
}

void TimelineWidget::set_block_links_selected(OakEngineClip *block, bool selected)
{
	foreach (OakEngineBlock *link,
			 block_all_links(reinterpret_cast<OakEngineBlock *>(block))) {
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

Rational TimelineWidget::get_timebase_for_track_type(TrackReference::Type type)
{
	return views_.at(type)->view()->timebase();
}

const QRect &TimelineWidget::get_rubber_band_geometry() const
{
	return rubberband_.geometry();
}

void TimelineWidget::signal_selected_blocks(QVector<OakEngineBlock *> input, bool filter)
{
	if (input.isEmpty()) {
		return;
	}

	if (filter) {
		// If filtering, remove all the blocks that are already selected
		for (int i = 0; i < input.size(); i++) {
			OakEngineBlock *b = input.at(i);

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
	const QVector<OakEngineBlock *> &deselected_blocks)
{
	if (deselected_blocks.isEmpty()) {
		return;
	}

	foreach (OakEngineBlock *b, deselected_blocks) {
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

QVector<TimelineApp::EditToInfo>
TimelineWidget::get_edit_to_info(const Rational &playhead_time,
							  TimelineApp::MovementMode mode)
{
	// Get list of unlocked tracks
	QVector<OakEngineTrack *> tracks;
	{
		const QVector<OakEngineTrack *> all = sequence_all_tracks(sequence());
		for (OakEngineTrack *t : all) {
			if (!track_is_locked(t)) {
				tracks.append(t);
			}
		}
	}

	// Create list to cache nearest times and the blocks at this point
	QVector<TimelineApp::EditToInfo> info_list(tracks.size());

	const int64_t playhead_ts = core::Timecode::time_to_timestamp(
		playhead_time, sequence_timebase(sequence()));

	for (int i = 0; i < tracks.size(); i++) {
		TimelineApp::EditToInfo &info = info_list[i];

		OakEngineTrack *track = tracks.at(i);
		info.track = track;

		OakEngineBlock *b;

		// Determine what block is at this time (for "trim in", we want to catch blocks that start at
		// the time, for "trim out" we don't)
		if (mode == TimelineApp::k_trim_in) {
			b = oakengine_track_nearest_block_before_or_at(track, playhead_ts);
		} else {
			b = oakengine_track_nearest_block_before(track, playhead_ts);
		}

		// If we have a block here, cache how close it is to the track
		if (b) {
			Rational this_track_closest_point;

			if (mode == TimelineApp::k_trim_in) {
				this_track_closest_point = block_in(b);
			} else {
				this_track_closest_point = block_out(b);
			}

			info.nearest_time = this_track_closest_point;
		}

		info.nearest_block = b;
	}

	return info_list;
}

void TimelineWidget::ripple_to(TimelineApp::MovementMode mode)
{
	if (!get_connected_node()) {
		return;
	}

	Rational playhead_time = viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node()));

	QVector<TimelineApp::EditToInfo> tracks = get_edit_to_info(playhead_time, mode);

	if (tracks.isEmpty()) {
		return;
	}

	// Find each track's nearest point and determine the overall timeline's nearest point
	Rational closest_point_to_playhead =
		(mode == TimelineApp::k_trim_in) ? RATIONAL_MIN : RATIONAL_MAX;

	foreach (const TimelineApp::EditToInfo &info, tracks) {
		if (info.nearest_block) {
			if (mode == TimelineApp::k_trim_in) {
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
		if (mode == TimelineApp::k_trim_in) {
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
		sequence(),
		Timecode::time_to_timestamp(in_ripple, timebase(), Timecode::k_round),
		Timecode::time_to_timestamp(out_ripple, timebase(),
									Timecode::k_round));

	// If we rippled, ump to where new cut is if applicable
	if (mode == TimelineApp::k_trim_in) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			closest_point_to_playhead.numerator(),
			closest_point_to_playhead.denominator());
	} else if (mode == TimelineApp::k_trim_out &&
			   closest_point_to_playhead ==
				   viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node()))) {
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			playhead_time.numerator(), playhead_time.denominator());
	}
}

void TimelineWidget::edit_to(TimelineApp::MovementMode mode)
{
	const Rational playhead_time =
		viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node()));

	// Batch trim through the liboakengine C ABI facade (one undoable
	// command; the per-track nearest-block semantics of the old app-side
	// assembly live behind the facade now).
	oakengine_sequence_trim_clips_to(
		sequence(),
		(mode == TimelineApp::k_trim_in) ? 0 : 1,
		Timecode::time_to_timestamp(playhead_time, timebase(),
									Timecode::k_round));
}

void TimelineWidget::update_viewports(const TrackReference::Type &type)
{
	if (type == TrackReference::k_none) {
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
		oakengine_node_get_project(
			reinterpret_cast<OakEngineNode *>(get_connected_node())),
		nullptr);
	int result_code = OAKENGINE_SERIALIZER_NO_DATA;
	oakengine_clipboard_paste(cb, OAKENGINE_CLIPBOARD_CLIPS,
							oakengine_node_get_project(
								reinterpret_cast<OakEngineNode *>(
									get_connected_node())),
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

	OakEngineProject *project = oakengine_node_get_project(
		reinterpret_cast<OakEngineNode *>(get_connected_node()));
	for (int i = 0; i < node_count; i++) {
		OakEngineNode *n = oakengine_clipboard_get_loaded_node_at(cb, i);
		oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			project,
			n));
		auto *n_handle = n;
		if (oakengine_node_is_item(n_handle) &&
			!oakengine_node_folder(n_handle)) {
			oakengine_folder_add_child(
			oakengine_project_root(project),
			n);
		}
	}

	// Collect connections
	struct Conn {
		OakEngineNode *output;
		OakEngineNode *input;
		QString input_id;
		int element;
	};
	QVector<Conn> conns;
	oakengine_clipboard_foreach_connection(
		cb,
		[](OakEngineNode *out, OakEngineNode *in, const char *input_id,
		   int element, void *userdata) -> int {
			auto *v = static_cast<QVector<Conn> *>(userdata);
			v->append({out, in, QString::fromUtf8(input_id), element});
			return 0;
		},
		&conns);

	for (const Conn &c : conns) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_connect_command(
				c.output,
				c.input,
				c.input_id.toUtf8().constData(),
				c.element));
	}

	Rational paste_start = viewer_playhead(reinterpret_cast<OakEngineNode *>(get_connected_node()));

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
				OakEngineBlock *block = reinterpret_cast<OakEngineBlock *>(node);
				Rational length = block_length(block);
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
		OakEngineBlock *block = reinterpret_cast<OakEngineBlock *>(it.key());
		if (it.value().contains(QStringLiteral("in"))) {
			Rational in = Rational::from_string(
				it.value()[QStringLiteral("in")].toStdString());
			TrackReference track = TrackReference::from_string(
				it.value()[QStringLiteral("track")]);
			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(sequence(), static_cast<int>(track.type()))), track.index(), reinterpret_cast<void *>(block), core::Timecode::time_to_timestamp(paste_start + in, timebase())));
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

QHash<OakEngineNode *, OakEngineNode *>
TimelineWidget::generate_existing_paste_map(void *clipboard)
{
	QHash<OakEngineNode *, OakEngineNode *> m;
	OakEngineClipboard *cb = static_cast<OakEngineClipboard *>(clipboard);
	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);

	for (int i = 0; i < node_count; i++) {
		OakEngineNode *n = oakengine_clipboard_get_loaded_node_at(cb, i);
		for (OakEngineBlock *b : std::as_const(this->selected_blocks_)) {
			// WRAPPER-GAP: no C ABI for Node::get_context_positions(); the
			// block's track-owning sequence is the context that matters here
			// (blocks on a timeline always live in their sequence's context).
			OakEngineNode *context =
				oakengine_track_get_sequence(
					reinterpret_cast<OakEngineNode *>(block_track(b)));
			if (context && !m.contains(context) &&
				oak::Node(context).id() == oak::Node(n).id()) {
				m.insert(context, n);
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
	QVector<OakEngineBlock *> items_in_rubberband;

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

	foreach (OakEngineBlock *b, items_in_rubberband) {
		if (oakengine_block_is_gap(b)) {
			continue;
		}

		OakEngineTrack *t = block_track(b);
		if (track_is_locked(t)) {
			continue;
		}

		if (!rubberband_now_selected_.contains(b)) {
			add_selection(b);
			rubberband_now_selected_.append(b);
		}

		if (block_as_clip(b) && select_links) {
			foreach (OakEngineBlock *link, block_all_links(b)) {
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
								  const TrackReference &track)
{
	selections_[track].insert(time);

	update_viewports(track.type());
}

void TimelineWidget::add_selection(OakEngineBlock *item)
{
	if (block_track(item)) {
		add_selection(block_range(item), track_app_ref(block_track(item)));
	}
}

void TimelineWidget::remove_selection(const TimeRange &time,
									 const TrackReference &track)
{
	selections_[track].remove(time);

	update_viewports(track.type());
}

void TimelineWidget::remove_selection(OakEngineBlock *item)
{
	if (block_track(item)) {
		remove_selection(block_range(item), track_app_ref(block_track(item)));
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
		QVector<OakEngineBlock *> deselected;
		QVector<OakEngineBlock *> selected;

		foreach (OakEngineBlock *b, selected_blocks_) {
			if (!s[track_app_ref(block_track(b))].contains(block_range(b))) {
				deselected.append(b);
			}
		}

		// NOTE: This loop could do with some optimization
		for (auto it = s.cbegin(); it != s.cend(); it++) {
			OakEngineTrack *track = get_track_from_reference(it.key());
			if (track) {
				const TimeRangeList &ranges = it.value();

				foreach (OakEngineBlock *b, track_all_blocks(track)) {
					if (!selected_blocks_.contains(b) &&
						ranges.contains(block_range(b))) {
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

OakEngineBlock *TimelineWidget::get_item_at_scene_pos(const TimelineCoordinate &coord)
{
	return views_.at(coord.get_track().type())
		->view()
		->get_item_at_scene_pos(coord.get_frame(), coord.get_track().index());
}


}
