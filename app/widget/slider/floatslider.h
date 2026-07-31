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

#ifndef OAK_FLOATSLIDER_H
#define OAK_FLOATSLIDER_H

#include "base/decimalsliderbase.h"
#include "sliderdisplaytypeapp.h"

namespace olive
{

class FloatSlider : public DecimalSliderBase {
	Q_OBJECT
public:
	FloatSlider(QWidget *parent = nullptr);

	// The canonical definition lives in the app-side mirror
	// (widget/slider/sliderdisplaytypeapp.h, ordinals synced with engine
	// node/sliderdisplaytype.h); this alias keeps existing call sites
	// source-compatible. Use slider::k_normal etc. for the enumerators.
	using DisplayType = slider::FloatDisplayType;

	double get_value() const;

	void set_value(const double &d);

	void SetDefaultValue(const double &d);

	void set_minimum(const double &d);

	void set_maximum(const double &d);

	void set_display_type(const DisplayType &type);

	static double transform_value_to_display(double val, DisplayType display);

	static double transform_display_to_value(double val, DisplayType display);

	static QString value_to_string(double val, DisplayType display,
								 int decimal_places,
								 bool autotrim_decimal_places);

protected:
	virtual QString value_to_string(const QVariant &v) const override;

	virtual QVariant string_to_value(const QString &s, bool *ok) const override;

	virtual QVariant
	adjust_drag_distance_internal(const QVariant &start,
							   const double &drag) const override;

	virtual void value_signal_event(const QVariant &value) override;

signals:
	void value_changed(double);

private:
	DisplayType display_type_;
};

}

#endif // OAK_FLOATSLIDER_H
