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

#include "stringslider.h"

namespace olive
{

#define super SliderBase

StringSlider::StringSlider(QWidget *parent)
	: super(parent)
{
	set_value(QString());

	connect(label(), &SliderLabel::label_released, this,
			&SliderBase::show_editor);
}

QString StringSlider::get_value() const
{
	return get_value_internal().toString();
}

void StringSlider::set_value(const QString &v)
{
	set_value_internal(v);
}

void StringSlider::SetDefaultValue(const QString &v)
{
	super::set_default_value(v);
}

QString StringSlider::value_to_string(const QVariant &v) const
{
	QString vstr = v.toString();
	return (vstr.isEmpty()) ? tr("(none)") : vstr;
}

QVariant StringSlider::string_to_value(const QString &s, bool *ok) const
{
	*ok = true;
	return s;
}

void StringSlider::value_signal_event(const QVariant &value)
{
	emit value_changed(value.toString());
}

}
