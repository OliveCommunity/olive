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

#ifndef OAK_TIMEBASEDVIEWSELECTIONMANAGER_H
#define OAK_TIMEBASEDVIEWSELECTIONMANAGER_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QRubberBand>
#include <QToolTip>

#include "oakutil/qtutils.h"
#include "olive/core/util/timecodefunctions.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "timebasedview.h"
#include "timebasedwidget.h"
#include "widget/keyframeview/keyframehandle.h"
#include "widget/timeruler/markerhandle.h"
#include "widget/timetarget/timetarget.h"

namespace olive
{

template <typename T> class TimeBasedViewSelectionManager {
public:
	TimeBasedViewSelectionManager(TimeBasedView *view)
		: view_(view)
		, rubberband_(nullptr)
		, snap_mask_(TimeBasedWidget::k_snap_all)
	{
	}

	void set_snap_mask(TimeBasedWidget::SnapMask e)
	{
		snap_mask_ = e;
	}

	void clear_drawn_objects()
	{
		drawn_objects_.clear();
	}

	void declare_drawn_object(T *object, const QRectF &rect)
	{
		QRectF r(view_->unscale_point(rect.topLeft()),
				 view_->unscale_point(rect.bottomRight()));
		drawn_objects_.push_back({ object, r });
	}

	bool select(T *key)
	{
		Q_ASSERT(key);

		if (!is_selected(key)) {
			selected_.push_back(key);
			return true;
		}

		return false;
	}

	bool deselect(T *key)
	{
		Q_ASSERT(key);

		auto it = std::find(selected_.cbegin(), selected_.cend(), key);
		if (it == selected_.cend()) {
			return false;
		} else {
			selected_.erase(it);
			return true;
		}
	}

	void clear_selection()
	{
		selected_.clear();
	}

	bool is_selected(T *key) const
	{
		return std::find(selected_.cbegin(), selected_.cend(), key) !=
			   selected_.cend();
	}

	const std::vector<T *> &get_selected_objects() const
	{
		return selected_;
	}

	void set_timebase(const Rational &tb)
	{
		timebase_ = tb;
	}

	T *get_object_at_point(const QPointF &scene_pt)
	{
		// Iterate in reverse order because the objects drawn later will appear on top to the user
		QPointF unscaled = view_->unscale_point(scene_pt);
		for (auto it = drawn_objects_.crbegin(); it != drawn_objects_.crend();
			 it++) {
			const DrawnObject &kp = *it;
			if (kp.second.contains(unscaled)) {
				return kp.first;
			}
		}

		return nullptr;
	}

	T *get_object_at_point(const QPoint &pt)
	{
		return get_object_at_point(view_->mapToScene(pt));
	}

	T *mouse_press(QMouseEvent *event)
	{
		T *key_under_cursor = nullptr;

		if (event->button() == Qt::LeftButton ||
			event->button() == Qt::RightButton) {
			// See if there's a keyframe in this position
			key_under_cursor = get_object_at_point(event->pos());

			bool holding_shift = event->modifiers() & Qt::ShiftModifier;

			if (!key_under_cursor || !is_selected(key_under_cursor)) {
				if (!holding_shift) {
					// If not already selecting and not holding shift, clear the current selection
					clear_selection();
				}

				// Add item to selection, either nothing if shift wasn't held, or the existing selection
				if (key_under_cursor) {
					select(key_under_cursor);
					view_->SelectionManagerSelectEvent(key_under_cursor);
				}
			} else if (holding_shift) {
				// If selected and holding shift, de-select this item but do nothing else
				deselect(key_under_cursor);
				view_->SelectionManagerDeselectEvent(key_under_cursor);
				key_under_cursor = nullptr;
			}
		}

		return key_under_cursor;
	}

	bool is_dragging() const
	{
		return !dragging_.empty();
	}

	void drag_start(T *initial_item, QMouseEvent *event,
				   TimeTargetObject *target = nullptr)
	{
		if (event->button() != Qt::LeftButton) {
			return;
		}

		time_target_ = target;

		initial_drag_item_ = initial_item;

		dragging_.resize(selected_.size());

		if constexpr (std::is_same_v<T, OakEngineMarker>) {
			snap_points_.resize(selected_.size() * 2);
		} else {
			snap_points_.resize(selected_.size());
		}

		if (target) {
			time_targets_.resize(snap_points_.size());
			memset(time_targets_.data(), 0,
				   time_targets_.size() * sizeof(OakEngineNode *));
		} else {
			time_targets_.clear();
		}

		for (size_t i = 0; i < selected_.size(); i++) {
			T *obj = selected_.at(i);

			if constexpr (std::is_same_v<T, OakEngineMarker>) {
				dragging_[i] = selection_time(obj);
				snap_points_[i] = selection_time(obj);
				snap_points_[i + selected_.size()] = selection_time_end(obj);

				if (target) {
					time_targets_[i] = time_targets_[i + selected_.size()] =
						selection_time_target_parent(obj);
				}
			} else {
				dragging_[i] = selection_time(obj);
				snap_points_[i] = selection_time(obj);

				if (target) {
					time_targets_[i] = selection_time_target_parent(obj);
				}
			}
		}

		drag_mouse_start_ =
			view_->unscale_point(view_->mapToScene(event->pos()));
	}

	void snap_points(Rational *movement)
	{
		std::vector<Rational> copy = snap_points_;

		if (time_target_) {
			for (size_t i = 0; i < copy.size(); i++) {
				if (OakEngineNode *parent = time_targets_[i]) {
					copy[i] = time_target_->get_adjusted_time(
						parent,
						time_target_->get_time_target(), copy[i],
						k_transform_towards_output);
				}
			}
		}

		if (Core::instance()->snapping() && view_->get_snap_service()) {
			view_->get_snap_service()->snap_point(copy, movement, snap_mask_);
		}
	}

	void unsnap()
	{
		if (view_->get_snap_service()) {
			view_->get_snap_service()->hide_snaps();
		}
	}

	void drag_move(const QPoint &local_pos,
				  const QString &tip_format = QString())
	{
		Rational time_diff =
			view_->scene_to_time_no_grid(view_->mapToScene(local_pos).x() -
									 view_->scale_point(drag_mouse_start_).x());

		// Snap points
		Rational presnap_time_diff = time_diff;
		snap_points(&time_diff);

		// Validate snapping
		if (Core::instance()->snapping() && view_->get_snap_service()) {
			for (size_t i = 0; i < selected_.size(); i++) {
				Rational proposed_time = dragging_.at(i) + time_diff;
				T *sel = selected_.at(i);

				if (selection_has_sibling_at_time(sel, proposed_time)) {
					// Unsnap
					time_diff = presnap_time_diff;
					if (view_->get_snap_service()) {
						view_->get_snap_service()->hide_snaps();
					}
					break;
				}
			}
		}

		// Validate movement
		for (size_t i = 0; i < selected_.size(); i++) {
			Rational proposed_time = dragging_.at(i) + time_diff;
			T *sel = selected_.at(i);

			// Magic number: use interval of 1ms to avoid collisions
			Rational adj(1, 1000);
			if (dragging_.at(i) < proposed_time) {
				// Negate adjustment value if origin is less than proposed time
				adj = -adj;
			}

			bool loop;
			do {
				loop = false;
				while (selection_has_sibling_at_time(sel, proposed_time)) {
					proposed_time += adj;
					unsnap();
				}

				if (proposed_time < 0) {
					// Prevent any object from going below zero
					proposed_time = 0;
					unsnap();

					// Setting our proposed time to zero may (re)introduce a conflict that we just avoided
					// with the sibling check above, so we request it to happen again. To avoid a negative
					// adj bringing us back below zero, we force adj to positive so it'll only nudge higher
					adj = qAbs(adj);

					loop = true;
				}
			} while (loop);

			time_diff = proposed_time - dragging_.at(i);
		}

		// Apply movement
		for (size_t i = 0; i < selected_.size(); i++) {
			selection_set_time(selected_.at(i),
							   dragging_.at(i) + time_diff);
		}

		// Show information about this keyframe
		Rational display_time;

		if constexpr (std::is_same_v<T, OakEngineMarker>) {
			display_time = selection_time(initial_drag_item_);
		} else {
			display_time = selection_time(initial_drag_item_);
		}

		QString tip = QString::fromStdString(Timecode::time_to_timecode(
			display_time, timebase_, Core::instance()->get_timecode_display(),
			false));

		last_used_tip_format_ = tip_format;
		if (!tip_format.isEmpty()) {
			tip = tip_format.arg(tip);
		}

		QToolTip::hideText();
		QToolTip::showText(QCursor::pos(), tip);
	}

	void drag_stop(void *command)
	{
		QToolTip::hideText();

		for (size_t i = 0; i < selected_.size(); i++) {
			if constexpr (std::is_same_v<T, OakEngineKeyframe>) {
				int tbn = 0, tbd = 0;
				oakengine_node_frame_time_base(
					oakengine_keyframe_get_node(selected_.at(i)),
					&tbn, &tbd);
				const int64_t new_ts = olive::core::Timecode::time_to_timestamp(
					dragging_.at(i), olive::Rational(tbn, tbd),
					olive::core::Timecode::k_round);
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_keyframe_set_time_command(
						selected_.at(i),
						new_ts));
			} else if constexpr (std::is_same_v<T, OakEngineMarker>) {
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_marker_set_time_command(
						selected_.at(i),
						dragging_.at(i).numerator(),
						dragging_.at(i).denominator()));
			}
		}

		dragging_.clear();
		unsnap();
	}

	void rubber_band_start(QMouseEvent *event)
	{
		if (event->button() == Qt::LeftButton ||
			event->button() == Qt::RightButton) {
			rubberband_scene_start_ =
				view_->unscale_point(view_->mapToScene(event->pos()));

			rubberband_ = new QRubberBand(QRubberBand::Rectangle, view_);
			rubberband_->setGeometry(
				QRect(event->pos().x(), event->pos().y(), 0, 0));
			rubberband_->show();

			rubberband_preselected_ = selected_;
		}
	}

	void rubber_band_move(const QPoint &pos)
	{
		if (is_rubber_banding()) {
			QRectF band_rect = QRectF(view_->mapFromScene(view_->scale_point(
										  rubberband_scene_start_)),
									  pos)
								   .normalized();
			rubberband_->setGeometry(band_rect.toRect());

			QPointF current = view_->unscale_point(view_->mapToScene(pos));
			QRectF scene_rect =
				QRectF(rubberband_scene_start_, current).normalized();

			selected_ = rubberband_preselected_;
			foreach (const DrawnObject &kp, drawn_objects_) {
				if (scene_rect.intersects(kp.second)) {
					select(kp.first);
				}
			}
		}
	}

	void rubber_band_stop()
	{
		if (is_rubber_banding()) {
			delete rubberband_;
			rubberband_ = nullptr;
		}
	}

	bool is_rubber_banding() const
	{
		return rubberband_;
	}

	void force_drag_update()
	{
		if (is_rubber_banding() || is_dragging()) {
			QPoint local_pos = view_->viewport()->mapFromGlobal(QCursor::pos());
			if (is_rubber_banding()) {
				rubber_band_move(local_pos);
			} else {
				drag_move(local_pos, last_used_tip_format_);
			}
		}
	}

private:
TimeBasedView *view_;

	using DrawnObject = QPair<T *, QRectF>;
	std::vector<DrawnObject> drawn_objects_;

	std::vector<T *> selected_;

	std::vector<Rational> dragging_;
	std::vector<Rational> snap_points_;
	std::vector<OakEngineNode *> time_targets_;

	T *initial_drag_item_;

	QPointF drag_mouse_start_;

	Rational timebase_;

	QRubberBand *rubberband_;
	QPointF rubberband_scene_start_;
	std::vector<T *> rubberband_preselected_;

	TimeBasedWidget::SnapMask snap_mask_;

	TimeTargetObject *time_target_;

	QString last_used_tip_format_;
};

}

#endif // OAK_TIMEBASEDVIEWSELECTIONMANAGER_H
