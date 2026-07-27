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

#include "seekablewidget.h"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include "oakutil/qtutils.h"
#include "oakutil/range.h"
#include "core.h"
#include "dialog/markerproperties/markerpropertiesdialog.h"
#include "markerpainting.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "oakengine/serializer.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "oakengine/undo.h"
#include "timeline/timelineundoworkarea.h"
#include "widget/colorlabelmenu/colorlabelmenu.h"
#include "widget/menu/menushared.h"
#include "widget/timebased/timebasedwidget.h"

namespace olive
{

#define super TimeBasedView

SeekableWidget::SeekableWidget(QWidget *parent)
	: super(parent)
	, markers_(nullptr)
	, workarea_(nullptr)
	, dragging_(false)
	, ignore_next_focus_out_(false)
	, selection_manager_(this)
	, resize_item_(nullptr)
	, marker_top_(0)
	, marker_bottom_(0)
	, marker_editing_enabled_(true)
	, bridge_(new EngineEventBridge(this))
{
	QFontMetrics fm = fontMetrics();

	text_height_ = fm.height();

	// Set width of playhead marker
	playhead_width_ = QtUtils::q_font_metrics_width(fm, "H");

	setContextMenuPolicy(Qt::CustomContextMenu);
	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true);

	selection_manager_.set_snap_mask(TimeBasedWidget::k_snap_all);

	set_is_timeline_axes(true);
}

void SeekableWidget::set_markers(TimelineMarkerList *markers)
{
	// Unsubscribe old marker list events via bridge
	for (int64_t id : marker_list_subs_) {
		bridge_->unsubscribe(id);
	}
	marker_list_subs_.clear();

	if (markers_) {
		selection_manager_.clear_selection();
	}

	markers_ = markers;

	if (markers_) {
		// Subscribe to marker list events via bridge instead of direct TimelineMarkerList signals
		marker_list_subs_.append(bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED));
		marker_list_subs_.append(bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED));
		marker_list_subs_.append(bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED));

		// Wire the bridge signals once — bridge_ outlives individual marker
		// list subscriptions, re-connecting on every set_markers() would
		// stack duplicate viewport updates.
		if (!marker_connects_done_) {
			marker_connects_done_ = true;
			connect(bridge_, &EngineEventBridge::marker_list_marker_added, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
			connect(bridge_, &EngineEventBridge::marker_list_marker_removed, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
			connect(bridge_, &EngineEventBridge::marker_list_marker_modified, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
		}
	}

	viewport()->update();
}

void SeekableWidget::set_work_area(TimelineWorkArea *workarea)
{
	if (workarea_) {
		selection_manager_.clear_selection();

		if (workarea_range_sub_) {
			bridge_->unsubscribe(workarea_range_sub_);
			workarea_range_sub_ = 0;
		}
		if (workarea_enabled_sub_) {
			bridge_->unsubscribe(workarea_enabled_sub_);
			workarea_enabled_sub_ = 0;
		}
	}

	workarea_ = workarea;

	if (workarea_) {
		workarea_range_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED);
		workarea_enabled_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED);
		connect(bridge_, &EngineEventBridge::workarea_range_changed, viewport(),
				static_cast<void (QWidget::*)()>(&QWidget::update));
		connect(bridge_, &EngineEventBridge::workarea_enabled_changed, viewport(),
				static_cast<void (QWidget::*)()>(&QWidget::update));
	}

	viewport()->update();
}

void SeekableWidget::delete_selected()
{
	if (!selection_manager_.is_dragging()) {
		const auto &selected = selection_manager_.get_selected_objects();
		if (selected.empty()) {
			return;
		}

		if (Sequence *sequence =
				dynamic_cast<Sequence *>(get_viewer_node())) {
			// Batch removal through the liboakengine C ABI facade (one
			// undoable command). The facade family only wraps sequences;
			// markers of other viewer nodes (e.g. footage viewers) keep
			// the old per-marker command path below.
			QVector<int64_t> times;
			times.reserve(int(selected.size()));
			for (TimelineMarker *marker : selected) {
				times.append(Timecode::time_to_timestamp(
					marker->time().in(), timebase(), Timecode::k_round));
			}
			oakengine_sequence_marker_remove_many(
				reinterpret_cast<OakEngineSequence *>(sequence),
				times.constData(), times.size());
			return;
		}

		QVector<OakEngineMarker *> oak_markers;
		foreach (TimelineMarker *marker, selected) {
			oak_markers.append(
				reinterpret_cast<OakEngineMarker *>(marker));
		}
		// Remove each marker (undoable individually)
		for (auto *m : oak_markers) {
			oakengine_marker_remove(m);
		}
	}
}

bool SeekableWidget::copy_selected(bool cut)
{
	if (!selection_manager_.get_selected_objects().empty()) {
		const auto &selected = selection_manager_.get_selected_objects();
		std::vector<const OakEngineMarker *> markers;
		markers.reserve(selected.size());
		for (auto *m : selected) {
			markers.push_back(reinterpret_cast<OakEngineMarker *>(m));
		}

		OakEngineClipboard *cb = oakengine_clipboard_create(
			OAKENGINE_CLIPBOARD_MARKERS, nullptr, nullptr);
		oakengine_clipboard_set_markers(
			cb, markers.data(), static_cast<int>(markers.size()));
		oakengine_clipboard_copy(cb);
		oakengine_clipboard_free(cb);

		if (cut) {
			delete_selected();
		}

		return true;
	} else {
		return false;
	}
}

bool SeekableWidget::paste_markers()
{
	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_MARKERS,
		reinterpret_cast<OakEngineProject *>(get_viewer_node()->project()),
		nullptr);
	int result_code;
	oakengine_clipboard_paste(
		cb, OAKENGINE_CLIPBOARD_MARKERS,
		reinterpret_cast<OakEngineProject *>(get_viewer_node()->project()),
		&result_code, nullptr, 0);
	if (result_code == OAKENGINE_OK) {
		int count = oakengine_clipboard_get_loaded_marker_count(cb);
		if (count > 0) {
			// Collect the pasted markers
			std::vector<TimelineMarker *> markers;
			markers.reserve(count);
			for (int i = 0; i < count; i++) {
				markers.push_back(reinterpret_cast<TimelineMarker *>(
					oakengine_clipboard_get_loaded_marker_at(cb, i)));
			}

			// Normalize markers to start at playhead
			Rational min = RATIONAL_MAX;
			for (auto it = markers.cbegin(); it != markers.cend(); it++) {
				min = std::min(min, (*it)->time().in());
			}
			min -= get_viewer_node()->get_playhead();

			for (auto it = markers.cbegin(); it != markers.cend(); it++) {
				TimelineMarker *m = *it;

				Rational new_in = m->time().in() - min;
				oakengine_marker_set_time_live(
					reinterpret_cast<OakEngineMarker *>(m),
					new_in.numerator(), new_in.denominator(),
					new_in.numerator(), new_in.denominator());

				if (TimelineMarker *existing =
						markers_->get_marker_at_time(m->time().in())) {
					oakengine_marker_remove(
						reinterpret_cast<OakEngineMarker *>(existing));
				}

				// Re-add the clipboard marker to the list (undoable)
				oakengine_marker_list_add_existing(
					reinterpret_cast<OakEngineMarkerList *>(markers_),
					reinterpret_cast<OakEngineMarker *>(m));
			}

			oakengine_clipboard_free(cb);
			return true;
		}
	}

	oakengine_clipboard_free(cb);
	return false;
}

void SeekableWidget::mousePressEvent(QMouseEvent *event)
{
	TimelineMarker *initial;

	if (hand_press(event)) {
		return;
	} else if (event->modifiers() & Qt::ControlModifier) {
		selection_manager_.rubber_band_start(event);
	} else if (marker_editing_enabled_ &&
			   (initial = selection_manager_.mouse_press(event))) {
		selection_manager_.drag_start(initial, event);
	} else if (resize_item_) {
		// Handle selection, even though we won't be using it for dragging
		if (!(event->modifiers() & Qt::ShiftModifier)) {
			selection_manager_.clear_selection();
		}
		if (TimelineMarker *m = dynamic_cast<TimelineMarker *>(resize_item_)) {
			selection_manager_.select(m);
		}
		dragging_ = true;
		resize_start_ = mapToScene(event->pos());
	} else if (!selection_manager_.get_object_at_point(event->pos()) &&
			   event->button() == Qt::LeftButton) {
		seek_to_scene_point(mapToScene(event->pos()).x());
		dragging_ = true;

		deselect_all_markers();
	}
}

void SeekableWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (hand_move(event)) {
		return;
	} else if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_move(event->pos());
		viewport()->update();
	} else if (selection_manager_.is_dragging()) {
		selection_manager_.drag_move(event->pos());
	} else if (dragging_) {
		QPointF scene = mapToScene(event->pos());
		if (resize_item_) {
			drag_resize_handle(scene);
		} else {
			seek_to_scene_point(scene.x());
		}
	} else {
		// Look for resize points
		if (!last_playhead_shape_.containsPoint(event->pos(),
												Qt::OddEvenFill) &&
			!selection_manager_.get_object_at_point(event->pos()) &&
			find_resize_handle(event)) {
			setCursor(Qt::SizeHorCursor);
		} else {
			unsetCursor();
			clear_resize_handle();
		}
	}

	if (event->buttons()) {
		// Signal cursor pos in case we should scroll to catch up to it
		emit drag_moved(event->pos().x(), event->pos().y());
	}
}

void SeekableWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (hand_release(event)) {
		return;
	}

	if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_stop();
		return;
	}

	if (selection_manager_.is_dragging()) {
		void *command = oakengine_undo_command_create_multi();
		selection_manager_.drag_stop(command);
		oakengine_undo_push(
			command, tr("Moved %1 Marker(s)").arg(selection_manager_.get_selected_objects().size()).toUtf8().constData());
	}

	if (get_snap_service()) {
		get_snap_service()->hide_snaps();
	}

	if (resize_item_) {
		commit_resize_handle();
		resize_item_ = nullptr;
	}

	dragging_ = false;
	emit drag_released();
}

void SeekableWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	super::mouseDoubleClickEvent(event);

	if (selection_manager_.get_object_at_point(event->pos()) &&
		!selection_manager_.get_selected_objects().empty()) {
		show_marker_properties();
	}
}

void SeekableWidget::focusOutEvent(QFocusEvent *event)
{
	super::focusOutEvent(event);

	if (ignore_next_focus_out_) {
		ignore_next_focus_out_ = false;
	} else {
		// Deselect everything when we lose focus
		deselect_all_markers();
	}
}

void SeekableWidget::draw_markers(QPainter *p, int marker_bottom)
{
	selection_manager_.clear_drawn_objects();

	// Draw markers
	if (markers_ && !markers_->empty() && marker_bottom > 0) {
		int lim_left = get_left_limit();
		int lim_right = get_right_limit();

		for (auto it = markers_->cbegin(); it != markers_->cend(); it++) {
			TimelineMarker *marker = *it;

			int marker_right = time_to_scene(marker->time().out());
			if (marker_right < lim_left) {
				continue;
			}

			int marker_left = time_to_scene(marker->time().in());
			if (marker_left >= lim_right) {
				break;
			}

			int max_marker_right = lim_right;
			{
				// Check if there's a marker next
				auto next = it;
				next++;
				if (next != markers_->cend()) {
					max_marker_right =
						std::min(max_marker_right,
								 int(time_to_scene((*next)->time().in())));
				}
			}

			QRect marker_rect = MarkerPainting::draw(
				p, QPoint(marker_left, marker_bottom), max_marker_right,
				get_scale(), selection_manager_.is_selected(marker),
				marker->name(), marker->color(),
				marker->time().in(), marker->time().out());
			marker_top_ = marker_rect.top();
			selection_manager_.declare_drawn_object(marker, marker_rect);
		}
	}

	marker_bottom_ = marker_bottom;
}

void SeekableWidget::draw_work_area(QPainter *p)
{
	// Draw in/out workarea
	if (workarea_ && workarea_->enabled()) {
		int lim_left = get_left_limit();
		int lim_right = get_right_limit();

		int workarea_left = qMax(qreal(lim_left), time_to_scene(workarea_->in()));
		int workarea_right;

		if (workarea_->out() == RATIONAL_MAX) {
			workarea_right = lim_right;
		} else {
			workarea_right =
				qMin(qreal(lim_right), time_to_scene(workarea_->out()));
		}

		QColor translucent_highlight = palette().highlight().color();
		translucent_highlight.setAlpha(96);
		p->fillRect(workarea_left, 0, workarea_right - workarea_left, height(),
					translucent_highlight);
	}
}

void SeekableWidget::deselect_all_markers()
{
	selection_manager_.clear_selection();

	viewport()->update();
}

void SeekableWidget::set_marker_color(int c)
{
	QVector<OakEngineMarker *> oak_markers;
	foreach (TimelineMarker *marker,
			 selection_manager_.get_selected_objects()) {
		oak_markers.append(reinterpret_cast<OakEngineMarker *>(marker));
	}
	oakengine_marker_set_properties(
		oak_markers.data(), oak_markers.size(), c, nullptr, 0, 0, 0, 0, 0,
		nullptr);
}

void SeekableWidget::show_marker_properties()
{
	MarkerPropertiesDialog mpd(selection_manager_.get_selected_objects(),
							   timebase(), this);
	ignore_next_focus_out_ = true;
	mpd.exec();
}

void SeekableWidget::TimebaseChangedEvent(const Rational &t)
{
	super::TimebaseChangedEvent(t);

	selection_manager_.set_timebase(t);
}

void SeekableWidget::seek_to_scene_point(qreal scene)
{
	if (timebase().isNull()) {
		return;
	}

	Rational playhead_time = qMax(Rational(0), scene_to_time(scene));

	if (Core::instance()->snapping() && get_snap_service()) {
		Rational movement;

		get_snap_service()->snap_point({ playhead_time }, &movement,
									TimeBasedWidget::k_snap_all &
										~TimeBasedWidget::k_snap_to_playhead);

		playhead_time += movement;
	}

	ViewerOutput *viewer = get_viewer_node();
	if (viewer && playhead_time != viewer->get_playhead()) {
		oakengine_viewer_set_playhead(
		reinterpret_cast<OakEngineNode *>(viewer),
		playhead_time.numerator(), playhead_time.denominator());
	}
}

void SeekableWidget::SelectionManagerSelectEvent(void *obj)
{
	super::SelectionManagerSelectEvent(obj);

	viewport()->update();
}

void SeekableWidget::SelectionManagerDeselectEvent(void *obj)
{
	super::SelectionManagerDeselectEvent(obj);

	viewport()->update();
}

void SeekableWidget::CatchUpScrollEvent()
{
	super::CatchUpScrollEvent();

	this->selection_manager_.force_drag_update();
}

void SeekableWidget::draw_playhead(QPainter *p, int x, int y)
{
	int half_width = playhead_width_ / 2;

	{
		int test = x - this->get_scroll();
		if (test + half_width < 0 || test - half_width > width()) {
			return;
		}
	}

	p->setRenderHint(QPainter::Antialiasing);

	int half_text_height = text_height() / 3;

	last_playhead_shape_ = QPolygon({
		QPoint(x, y),
		QPoint(x - half_width, y - half_text_height),
		QPoint(x - half_width, y - text_height()),
		QPoint(x + 1 + half_width, y - text_height()),
		QPoint(x + 1 + half_width, y - half_text_height),
		QPoint(x + 1, y),
	});

	p->drawPolygon(last_playhead_shape_);

	p->setRenderHint(QPainter::Antialiasing, false);
}

int SeekableWidget::get_left_limit() const
{
	return get_scroll();
}

int SeekableWidget::get_right_limit() const
{
	return get_left_limit() + width();
}

bool SeekableWidget::show_context_menu(const QPoint &p)
{
	if (marker_editing_enabled_ && selection_manager_.get_object_at_point(p) &&
		!selection_manager_.get_selected_objects().empty()) {
		// Show marker-specific menu
		Menu m;

		ColorLabelMenu color_coding_menu;
		connect(&color_coding_menu, &ColorLabelMenu::color_selected, this,
				&SeekableWidget::set_marker_color);
		m.addMenu(&color_coding_menu);

		m.addSeparator();

		MenuShared::instance()->add_items_for_edit_menu(&m, false);

		m.addSeparator();

		QAction *properties_action = m.addAction(tr("Properties"));
		connect(properties_action, &QAction::triggered, this,
				&SeekableWidget::show_marker_properties);

		ignore_next_focus_out_ = true;
		m.exec(mapToGlobal(p));
		return true;
	} else {
		return false;
	}
}

bool SeekableWidget::find_resize_handle(QMouseEvent *event)
{
	if (!marker_editing_enabled_) {
		return false;
	}

	clear_resize_handle();

	QPointF scene = mapToScene(event->pos());
	const int border = 10;
	Rational min = scene_to_time_no_grid(scene.x() - border);
	Rational max = scene_to_time_no_grid(scene.x() + border);

	// Test for workarea
	if (workarea_ && workarea_->enabled()) {
		if (workarea_->in() >= min && workarea_->in() < max) {
			resize_mode_ = k_resize_in;
		} else if (workarea_->out() >= min && workarea_->out() < max) {
			resize_mode_ = k_resize_out;
		}
	}

	if (resize_mode_ != k_resize_none) {
		if (workarea_) {
			resize_item_ = workarea_;
			resize_item_range_ = workarea_->range();
			resize_snap_mask_ = TimeBasedWidget::k_snap_all &
								~TimeBasedWidget::k_snap_to_workarea;
		}
	} else if (event->pos().y() >= marker_top_ &&
			   event->pos().y() < marker_bottom_) {
		if (markers_) {
			// Check for markers
			for (auto it = markers_->cbegin(); it != markers_->cend(); it++) {
				TimelineMarker *m = *it;
				if (m->time().in() != m->time().out()) {
					if (m->time().in() >= min && m->time().in() < max) {
						resize_mode_ = k_resize_in;
					} else if (m->time().out() >= min &&
							   m->time().out() < max) {
						resize_mode_ = k_resize_out;
					}

					if (resize_mode_ != k_resize_none) {
						resize_item_ = m;
						resize_item_range_ = m->time();
						resize_snap_mask_ = TimeBasedWidget::k_snap_all;
						break;
					}
				}
			}
		}
	}

	return resize_item_;
}

void SeekableWidget::clear_resize_handle()
{
	resize_item_ = nullptr;
	resize_mode_ = k_resize_none;
}

void SeekableWidget::drag_resize_handle(const QPointF &scene)
{
	qreal diff = scene.x() - resize_start_.x();

	Rational proposed_time;

	if (resize_mode_ == k_resize_in) {
		proposed_time = qMax(Rational(0), qMin(resize_item_range_.out(),
											   resize_item_range_.in() +
												   scene_to_time_no_grid(diff)));
	} else {
		proposed_time =
			qMax(resize_item_range_.in(),
				 resize_item_range_.out() + scene_to_time_no_grid(diff));
	}

	Rational presnap_time = proposed_time;

	if (Core::instance()->snapping() && get_snap_service()) {
		Rational movement;

		get_snap_service()->snap_point({ proposed_time }, &movement,
									resize_snap_mask_);

		proposed_time += movement;
	}

	TimeRange new_range = resize_item_range_;
	if (resize_mode_ == k_resize_in) {
		// Markers should not have the same time as anything else
		// NOTE: This code is largely duplicated from TimeBasedViewSelectionManager::DragMove. Not ideal,
		//       but I'm not sure if there's a good way to re-use that code
		if (TimelineMarker *marker =
				dynamic_cast<TimelineMarker *>(resize_item_)) {
			if (markers_ &&
				markers_->get_marker_at_time(proposed_time) != marker) {
				proposed_time = presnap_time;

				if (get_snap_service()) {
					get_snap_service()->hide_snaps();
				}
			}

			while (markers_ &&
				   markers_->get_marker_at_time(proposed_time) != marker &&
				   markers_->get_marker_at_time(proposed_time)) {
				proposed_time += Rational(1, 1000);
			}
		}

		new_range.set_in(proposed_time);
	} else {
		new_range.set_out(proposed_time);
	}

	if (TimelineMarker *marker = dynamic_cast<TimelineMarker *>(resize_item_)) {
		oakengine_marker_set_time_live(
			reinterpret_cast<OakEngineMarker *>(marker),
			new_range.in().numerator(), new_range.in().denominator(),
			new_range.out().numerator(), new_range.out().denominator());
	} else if (TimelineWorkArea *workarea =
				   dynamic_cast<TimelineWorkArea *>(resize_item_)) {
		oakengine_workarea_set_range(
			reinterpret_cast<OakEngineWorkarea *>(workarea),
			new_range.in().numerator(), new_range.in().denominator(),
			new_range.out().numerator(), new_range.out().denominator());
	}
}

void SeekableWidget::commit_resize_handle()
{
	if (TimelineMarker *marker = dynamic_cast<TimelineMarker *>(resize_item_)) {
		oakengine_marker_set_properties(
			reinterpret_cast<OakEngineMarker **>(&marker), 1, -1, nullptr, 1,
			resize_item_range_.in().numerator(),
			resize_item_range_.in().denominator(),
			resize_item_range_.out().numerator(),
			resize_item_range_.out().denominator(),
			nullptr);
	} else if (TimelineWorkArea *workarea =
				   dynamic_cast<TimelineWorkArea *>(resize_item_)) {
		void *wa_cmd = oakengine_undo_command_create(
			tr("Changed Workarea Length").toUtf8().constData(),
			nullptr, nullptr, nullptr, nullptr);
		oakengine_undo_push(wa_cmd,
			tr("Changed Workarea Length").toUtf8().constData());
	}
}

}
