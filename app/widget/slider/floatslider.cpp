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

#include "floatslider.h"

#include <cmath>
#include <QDebug>

#include "common/decibel.h"

namespace olive
{

#define super DecimalSliderBase

FloatSlider::FloatSlider(QWidget *parent)
	: super(parent)
	, display_type_(k_normal)
{
	set_value(0.0);
}

double FloatSlider::get_value() const
{
	return get_value_internal().toDouble();
}

void FloatSlider::set_value(const double &d)
{
	set_value_internal(d);
}

void FloatSlider::SetDefaultValue(const double &d)
{
	super::set_default_value(d);
}

void FloatSlider::set_minimum(const double &d)
{
	set_minimum_internal(d);
}

void FloatSlider::set_maximum(const double &d)
{
	set_maximum_internal(d);
}

void FloatSlider::set_display_type(const FloatSlider::DisplayType &type)
{
	display_type_ = type;

	switch (display_type_) {
	case k_normal:
		clear_format();
		break;
	case k_decibel:
		set_format(tr("%1 dB"));
		break;
	case k_percentage:
		set_format(tr("%1%"));
		break;
	}
}

double FloatSlider::transform_value_to_display(double val, DisplayType display)
{
	switch (display) {
	case k_normal:
		break;
	case k_decibel:
		val = Decibel::from_linear(val);
		break;
	case k_percentage:
		val *= 100.0;
		break;
	}

	return val;
}

double FloatSlider::transform_display_to_value(double val, DisplayType display)
{
	switch (display) {
	case k_normal:
		break;
	case k_decibel:
		val = Decibel::to_linear(val);
		break;
	case k_percentage:
		val *= 0.01;
		break;
	}

	return val;
}

QString FloatSlider::value_to_string(double val, FloatSlider::DisplayType display,
								   int decimal_places,
								   bool autotrim_decimal_places)
{
	// Return negative infinity for zero volume
	if (display == k_decibel && qIsNull(val)) {
		return tr("\xE2\x88\x9E");
	}

	return float_to_string(transform_value_to_display(val, display), decimal_places,
						 autotrim_decimal_places);
}

QString FloatSlider::value_to_string(const QVariant &v) const
{
	return value_to_string(v.toDouble() + get_offset().toDouble(), display_type_,
						 get_decimal_places(), get_auto_trim_decimal_places());
}

QVariant FloatSlider::string_to_value(const QString &s, bool *ok) const
{
	bool valid;
	double val = s.toDouble(&valid);

	// If we were given an `ok` pointer, set it to `valid`
	if (ok) {
		*ok = valid;
	}

	// If valid, transform it from display
	if (valid) {
		val = transform_display_to_value(val, display_type_);
	}

	// Return un-offset value
	return val - get_offset().toDouble();
}

QVariant FloatSlider::adjust_drag_distance_internal(const QVariant &start,
												 const double &drag) const
{
	switch (display_type_) {
	case k_normal:
		// No change here
		break;
	case k_decibel: {
		double current_db = Decibel::from_linear(start.toDouble());
		current_db += drag;
		double adjusted_linear = Decibel::to_linear(current_db);

		return adjusted_linear;
	}
	case k_percentage:
		return super::adjust_drag_distance_internal(start, drag * 0.01);
	}

	return super::adjust_drag_distance_internal(start, drag);
}

void FloatSlider::value_signal_event(const QVariant &value)
{
	emit value_changed(value.toDouble());
}

}
