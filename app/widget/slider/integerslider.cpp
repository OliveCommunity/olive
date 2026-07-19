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

#include "integerslider.h"

namespace olive
{

#define super NumericSliderBase

IntegerSlider::IntegerSlider(QWidget *parent)
	: super(parent)
{
	set_value(0);
}

int64_t IntegerSlider::get_value()
{
	return get_value_internal().toLongLong();
}

void IntegerSlider::set_value(const int64_t &v)
{
	set_value_internal(QVariant::fromValue(v));
}

void IntegerSlider::set_minimum(const int64_t &d)
{
	set_minimum_internal(QVariant::fromValue(d));
}

void IntegerSlider::set_maximum(const int64_t &d)
{
	set_maximum_internal(QVariant::fromValue(d));
}

void IntegerSlider::SetDefaultValue(const int64_t &d)
{
	super::set_default_value(QVariant::fromValue(d));
}

QString IntegerSlider::value_to_string(const QVariant &v) const
{
	return QString::number(v.toLongLong() + get_offset().toLongLong());
}

QVariant IntegerSlider::string_to_value(const QString &s, bool *ok) const
{
	bool valid;

	// Allow both floats and integers for either modes
	double decimal_val = s.toDouble(&valid);

	if (ok) {
		*ok = valid;
	}

	decimal_val -= get_offset().toLongLong();

	if (valid) {
		// But for an integer, we round it
		return qRound(decimal_val);
	}

	return QVariant();
}

void IntegerSlider::value_signal_event(const QVariant &value)
{
	emit value_changed(value.toInt());
}

QVariant IntegerSlider::adjust_drag_distance_internal(const QVariant &start,
												   const double &drag) const
{
	return qRound64(super::adjust_drag_distance_internal(start, drag).toDouble());
}

}
