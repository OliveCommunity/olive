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

#ifndef OAK_INTEGERSLIDER_H
#define OAK_INTEGERSLIDER_H

#include "base/numericsliderbase.h"

namespace olive
{

class IntegerSlider : public NumericSliderBase {
	Q_OBJECT
public:
	IntegerSlider(QWidget *parent = nullptr);

	int64_t get_value();

	void set_value(const int64_t &v);

	void set_minimum(const int64_t &d);

	void set_maximum(const int64_t &d);

	void SetDefaultValue(const int64_t &d);

protected:
	virtual QString value_to_string(const QVariant &v) const override;

	virtual QVariant string_to_value(const QString &s, bool *ok) const override;

	virtual void value_signal_event(const QVariant &value) override;

	virtual QVariant
	adjust_drag_distance_internal(const QVariant &start,
							   const double &drag) const override;

signals:
	void value_changed(int64_t);
};

}

#endif // OAK_INTEGERSLIDER_H
