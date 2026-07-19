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

#ifndef OAK_NUMERICSLIDERBASE_H
#define OAK_NUMERICSLIDERBASE_H

#include "sliderbase.h"

namespace olive
{

class NumericSliderBase : public SliderBase {
	Q_OBJECT
public:
	NumericSliderBase(QWidget *parent = nullptr);

	void set_ladder_element_count(int b)
	{
		ladder_element_count_ = b;
	}

	void set_drag_multiplier(const double &d);

	void set_offset(const QVariant &v);

	bool is_dragging() const;

protected:
	const QVariant &get_offset() const
	{
		return offset_;
	}

	virtual QVariant adjust_drag_distance_internal(const QVariant &start,
												const double &drag) const;

	void set_minimum_internal(const QVariant &v);

	void set_maximum_internal(const QVariant &v);

	virtual bool value_greater_than(const QVariant &lhs,
								  const QVariant &rhs) const;

	virtual bool value_less_than(const QVariant &lhs, const QVariant &rhs) const;

	virtual bool can_set_value() const override;

private:
	bool using_ladders() const;

	virtual QVariant adjust_value(const QVariant &value) const override;

	SliderLadder *drag_ladder_;

	int ladder_element_count_;

	bool dragged_;

	bool has_min_;
	QVariant min_value_;

	bool has_max_;
	QVariant max_value_;

	double dragged_diff_;

	QVariant drag_start_value_;

	QVariant offset_;

	double drag_multiplier_;

	bool setting_drag_value_;

	/**
   * @brief An effects slider somewhere is being dragged
   */
	static bool effects_slider_is_being_dragged;

private slots:
	void label_pressed();

	void reposition_ladder();

	void ladder_dragged(int value, double multiplier);

	void ladder_released();
};

}

#endif // OAK_NUMERICSLIDERBASE_H
