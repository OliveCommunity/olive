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

#ifndef OAK_TIMELINESCALEDOBJECT_H
#define OAK_TIMELINESCALEDOBJECT_H

#include <olive/core/core.h>
#include <QWidget>

#include "node/block/block.h"

namespace olive
{

/**
 * @brief Provides base functionality for any object that uses time and scale
 */
class TimeScaledObject {
public:
	TimeScaledObject();
	virtual ~TimeScaledObject() = default;

	void set_timebase(const Rational &timebase);

	const Rational &timebase() const;
	const double &timebase_dbl() const;

	static Rational scene_to_time(const double &x, const double &x_scale,
								const Rational &timebase, bool round = false);
	static Rational scene_to_time_no_grid(const double &x, const double &x_scale);

	const double &get_scale() const;
	const double &get_maximum_scale() const
	{
		return max_scale_;
	}

	void set_scale(const double &scale);

	void set_scale_from_dimensions(double viewport_width, double content_width);
	static double calculate_scale_from_dimensions(double viewport_sz,
											   double content_sz);
	static double calculate_padding_from_dimension_scale(double viewport_sz);

	double time_to_scene(const Rational &time) const;
	Rational scene_to_time(const double &x, bool round = false) const;
	Rational scene_to_time_no_grid(const double &x) const;

protected:
	virtual void TimebaseChangedEvent(const Rational &)
	{
	}

	virtual void ScaleChangedEvent(const double &)
	{
	}

	void set_maximum_scale(const double &max);

	void set_minimum_scale(const double &min);

private:
	Rational timebase_;

	double timebase_dbl_;

	double scale_;

	double min_scale_;

	double max_scale_;

	static const int k_calculate_dimensions_padding;
};

class TimelineScaledWidget : public QWidget, public TimeScaledObject {
	Q_OBJECT
public:
	TimelineScaledWidget(QWidget *parent = nullptr);
};

}

#endif // OAK_TIMELINESCALEDOBJECT_H
