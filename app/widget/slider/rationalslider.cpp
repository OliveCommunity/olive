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

#include "rationalslider.h"

#include "core.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"

namespace olive
{

#define super DecimalSliderBase

RationalSlider::RationalSlider(QWidget *parent)
	: super(parent)
	, lock_display_type_(false)
{
	connect(Core::instance(), &Core::timecode_display_changed, this,
			&RationalSlider::update_label);
	connect(SliderBase::label(), &SliderLabel::customContextMenuRequested, this,
			&RationalSlider::show_display_type_menu);

	set_display_type(slider::k_float);

	set_value(Rational(0, 0));
}

Rational RationalSlider::get_value()
{
	return get_value_internal().value<Rational>();
}

void RationalSlider::set_value(const Rational &d)
{
	set_value_internal(QVariant::fromValue(d));
}

void RationalSlider::SetDefaultValue(const Rational &r)
{
	super::set_default_value(QVariant::fromValue(r));
}

void RationalSlider::set_minimum(const Rational &d)
{
	set_minimum_internal(QVariant::fromValue(d));
}

void RationalSlider::set_maximum(const Rational &d)
{
	set_maximum_internal(QVariant::fromValue(d));
}

void RationalSlider::set_timebase(const Rational &timebase)
{
	timebase_ = timebase;

	// Refresh label since we have a new timebase to generate a timecode with
	update_label();
}

void RationalSlider::set_display_type(const RationalSlider::DisplayType &type)
{
	display_type_ = type;

	update_label();
}

void RationalSlider::set_lock_display_type(bool e)
{
	lock_display_type_ = e;
}

bool RationalSlider::get_lock_display_type()
{
	return lock_display_type_;
}

void RationalSlider::disable_display_type(RationalSlider::DisplayType type)
{
	disabled_.append(type);
}

QString RationalSlider::value_to_string(const QVariant &v) const
{
	Rational r = v.value<Rational>();

	if (r.isNaN()) {
		return tr("NaN");
	} else {
		double val = r.to_double() + get_offset().value<Rational>().to_double();

		switch (display_type_) {
		case slider::k_time:
			return QString::fromStdString(Timecode::time_to_timecode(
				r, timebase_, Core::instance()->get_timecode_display()));
		case slider::k_float:
			return float_to_string(val, get_decimal_places(),
								 get_auto_trim_decimal_places());
		case slider::k_rational:
			return QString::fromStdString(v.value<Rational>().to_string());
		}

		return v.toString();
	}
}

QVariant RationalSlider::string_to_value(const QString &s, bool *ok) const
{
	Rational r;
	*ok = false;

	switch (display_type_) {
	case slider::k_time: {
		r = Timecode::timecode_to_time(s.toStdString(), timebase_,
									   Core::instance()->get_timecode_display(),
									   ok);
		break;
	}
	case slider::k_float: {
		// First, convert to a double
		double d = s.toDouble(ok);
		if (!(*ok)) {
			break;
		}

		// If double conversion succeeded, convert to a Rational
		r = Rational::from_double(d, ok);
		break;
	}
	case slider::k_rational:
		r = Rational::from_string(s.toStdString(), ok);
		break;
	}

	//return QVariant::fromValue(r - GetOffset().value<Rational>());
	return QVariant::fromValue(r);
}

QVariant RationalSlider::adjust_drag_distance_internal(const QVariant &start,
													const double &drag) const
{
	// Assume we want smallest increment to be timebase or 1 frame
	return QVariant::fromValue(start.value<Rational>() +
							   Rational::from_double(drag) * timebase_);
}

void RationalSlider::value_signal_event(const QVariant &v)
{
	emit value_changed(v.value<Rational>());
}

bool RationalSlider::value_greater_than(const QVariant &lhs,
									  const QVariant &rhs) const
{
	return lhs.value<Rational>() > rhs.value<Rational>();
}

bool RationalSlider::value_less_than(const QVariant &lhs,
								   const QVariant &rhs) const
{
	return lhs.value<Rational>() < rhs.value<Rational>();
}

void RationalSlider::show_display_type_menu()
{
	Menu m(this);

	if (!get_lock_display_type()) {
		if (!disabled_.contains(slider::k_float)) {
			QAction *float_action = m.addAction(tr("Float"));
			float_action->setData(slider::k_float);
			connect(float_action, &QAction::triggered, this,
					&RationalSlider::set_display_type_from_menu);
		}

		if (!disabled_.contains(slider::k_rational)) {
			QAction *rational_action = m.addAction(tr("Rational"));
			rational_action->setData(slider::k_rational);
			connect(rational_action, &QAction::triggered, this,
					&RationalSlider::set_display_type_from_menu);
		}

		if (!disabled_.contains(slider::k_time)) {
			QAction *time_action = m.addAction(tr("Time"));
			time_action->setData(slider::k_time);
			connect(time_action, &QAction::triggered, this,
					&RationalSlider::set_display_type_from_menu);
		}
	}

	if (display_type_ == slider::k_time) {
		if (!m.actions().isEmpty()) {
			m.addSeparator();
		}
		MenuShared::instance()->add_items_for_time_ruler_menu(&m);
		MenuShared::instance()->about_to_show_time_ruler_actions(timebase_);
	}

	if (!m.actions().isEmpty()) {
		m.exec(QCursor::pos());
		update_label();
	}
}

void RationalSlider::set_display_type_from_menu()
{
	QAction *action = static_cast<QAction *>(sender());

	DisplayType type = static_cast<DisplayType>(action->data().toInt());

	set_display_type(type);
}

}
