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

#include "numericsliderbase.h"

#include "common/qtutils.h"
#include "config/config.h"
#include "core.h"

namespace olive
{

bool NumericSliderBase::effects_slider_is_being_dragged = false;

NumericSliderBase::NumericSliderBase(QWidget *parent)
	: SliderBase(parent)
	, drag_ladder_(nullptr)
	, ladder_element_count_(0)
	, dragged_(false)
	, has_min_(false)
	, has_max_(false)
	, dragged_diff_(0)
	, drag_multiplier_(1.0)
	, setting_drag_value_(false)
{
	// Numeric sliders are draggable, so we have a cursor that indicates that
	setCursor(Qt::SizeHorCursor);

	connect(label(), &SliderLabel::label_pressed, this,
			&NumericSliderBase::label_pressed);
}

void NumericSliderBase::set_drag_multiplier(const double &d)
{
	drag_multiplier_ = d;
}

void NumericSliderBase::label_pressed()
{
	drag_ladder_ = new SliderLadder(drag_multiplier_, ladder_element_count_,
									get_formatted_value_to_string(99999999));
	connect(drag_ladder_, &SliderLadder::dragged_by_value, this,
			&NumericSliderBase::ladder_dragged);
	connect(drag_ladder_, &SliderLadder::released, this,
			&NumericSliderBase::ladder_released);

	drag_ladder_->set_value(get_formatted_value_to_string());
	drag_ladder_->resize(drag_ladder_->sizeHint());
	reposition_ladder();
	drag_ladder_->show();

	drag_start_value_ = get_value_internal();
}

void NumericSliderBase::ladder_dragged(int value, double multiplier)
{
	dragged_ = true;

	dragged_diff_ += value * multiplier;

	// Store current value to try and prevent any unnecessary signalling if the value doesn't change
	QVariant pre_set_value = get_value_internal();

	setting_drag_value_ = true;
	set_value_internal(
		adjust_drag_distance_internal(drag_start_value_, dragged_diff_));
	setting_drag_value_ = false;

	if (get_value_internal() != pre_set_value) {
		// We retrieve the value instead of storing it ourselves because SetValueInternal may do extra
		// processing (such as clamping).
		drag_ladder_->set_value(get_formatted_value_to_string());

		if (!using_ladders()) {
			reposition_ladder();
		}

		value_signal_event(get_value_internal());
	}
}

void NumericSliderBase::ladder_released()
{
	drag_ladder_->deleteLater();
	drag_ladder_ = nullptr;
	dragged_diff_ = 0;

	if (dragged_) {
		// This was a drag, send another value changed event
		value_signal_event(get_value_internal());

		dragged_ = false;
	} else {
		show_editor();
	}
}

void NumericSliderBase::reposition_ladder()
{
	if (drag_ladder_) {
		if (using_ladders()) {
			drag_ladder_->move(
				QCursor::pos() -
				QPoint(drag_ladder_->width() / 2, drag_ladder_->height() / 2));
		} else {
			QPoint label_global_pos = label()->mapToGlobal(label()->pos());
			int text_width = QtUtils::q_font_metrics_width(label()->fontMetrics(),
														label()->text());

			if (label()->alignment() & Qt::AlignRight) {
				label_global_pos.setX(label_global_pos.x() + label()->width() -
									  text_width);
			} else if (label()->alignment() & Qt::AlignHCenter) {
				label_global_pos.setX(label_global_pos.x() +
									  label()->width() / 2 - text_width / 2);
			}

			int ladder_x = label_global_pos.x() + text_width / 2 -
						   drag_ladder_->width() / 2;
			int ladder_y = label_global_pos.y() + label()->height() / 2 -
						   drag_ladder_->height() / 2;

			drag_ladder_->move(ladder_x, ladder_y);
		}

		drag_ladder_->start_listening_to_mouse_input();
	}
}

bool NumericSliderBase::is_dragging() const
{
	return drag_ladder_;
}

bool NumericSliderBase::using_ladders() const
{
	return ladder_element_count_ > 0 &&
		   OAK_CONFIG("UseSliderLadders").toBool();
}

QVariant NumericSliderBase::adjust_value(const QVariant &value) const
{
	// Clamps between min/max
	if (has_min_ && value_less_than(value, min_value_)) {
		return min_value_;
	} else if (has_max_ && value_greater_than(value, max_value_)) {
		return max_value_;
	}

	return value;
}

void NumericSliderBase::set_offset(const QVariant &v)
{
	offset_ = v;

	update_label();
}

QVariant NumericSliderBase::adjust_drag_distance_internal(const QVariant &start,
													   const double &drag) const
{
	return start.toDouble() + drag;
}

void NumericSliderBase::set_minimum_internal(const QVariant &v)
{
	min_value_ = v;
	has_min_ = true;

	// Limit value by this new minimum value
	if (value_less_than(get_value_internal(), min_value_)) {
		set_value_internal(min_value_);
	}
}

void NumericSliderBase::set_maximum_internal(const QVariant &v)
{
	max_value_ = v;
	has_max_ = true;

	// Limit value by this new maximum value
	if (value_greater_than(get_value_internal(), max_value_)) {
		set_value_internal(max_value_);
	}
}

bool NumericSliderBase::value_greater_than(const QVariant &lhs,
										 const QVariant &rhs) const
{
	return lhs.toDouble() > rhs.toDouble();
}

bool NumericSliderBase::value_less_than(const QVariant &lhs,
									  const QVariant &rhs) const
{
	return lhs.toDouble() < rhs.toDouble();
}

bool NumericSliderBase::can_set_value() const
{
	return !is_dragging() || setting_drag_value_;
}

}
