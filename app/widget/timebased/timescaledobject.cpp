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

#include "timescaledobject.h"

#include <cfloat>
#include <QtMath>

#include "oakengine/node.h"
namespace olive
{

const int TimeScaledObject::k_calculate_dimensions_padding = 10;

TimeScaledObject::TimeScaledObject()
	: scale_(1.0)
	, min_scale_(0)
	, max_scale_(oakengine_audio_waveform_max_sample_rate())
{
}

void TimeScaledObject::set_timebase(const Rational &timebase)
{
	timebase_ = timebase;
	timebase_dbl_ = timebase_.to_double();

	TimebaseChangedEvent(timebase);
}

const Rational &TimeScaledObject::timebase() const
{
	return timebase_;
}

const double &TimeScaledObject::timebase_dbl() const
{
	return timebase_dbl_;
}

Rational TimeScaledObject::scene_to_time(const double &x, const double &x_scale,
									   const Rational &timebase, bool round)
{
	if (timebase.isNull()) {
		return Rational();
	}
	double unscaled_time = x / x_scale / timebase.to_double();

	// Adjust screen point by scale and timebase
	qint64 rounded_x_mvmt;

	if (round) {
		rounded_x_mvmt = qRound64(unscaled_time);
	} else if (unscaled_time < 0) {
		// "floor" to zero
		rounded_x_mvmt = qCeil(unscaled_time);
	} else {
		rounded_x_mvmt = qFloor(unscaled_time);
	}

	// Return a time in the timebase
	return Rational(rounded_x_mvmt * timebase.numerator(),
					timebase.denominator());
}

Rational TimeScaledObject::scene_to_time_no_grid(const double &x,
											 const double &x_scale)
{
	double unscaled_time = x / x_scale;

	return Rational::from_double(unscaled_time);
}

double TimeScaledObject::time_to_scene(const Rational &time) const
{
	if (timebase_.isNull()) {
		return 0.0;
	}
	return time.to_double() * scale_;
}

Rational TimeScaledObject::scene_to_time(const double &x, bool round) const
{
	if (timebase_.isNull()) {
		return Rational();
	}
	return scene_to_time(x, scale_, timebase_, round);
}

Rational TimeScaledObject::scene_to_time_no_grid(const double &x) const
{
	if (timebase_.isNull()) {
		return Rational::from_double(x / scale_);
	}
	return scene_to_time_no_grid(x, scale_);
}

void TimeScaledObject::set_maximum_scale(const double &max)
{
	max_scale_ = max;

	if (get_scale() > max_scale_) {
		set_scale(max_scale_);
	}
}

void TimeScaledObject::set_minimum_scale(const double &min)
{
	min_scale_ = min;

	if (get_scale() < min_scale_) {
		set_scale(min_scale_);
	}
}

const double &TimeScaledObject::get_scale() const
{
	return scale_;
}

void TimeScaledObject::set_scale(const double &scale)
{
	Q_ASSERT(scale > 0);

	scale_ = std::clamp(scale, min_scale_, max_scale_);

	ScaleChangedEvent(scale_);
}

void TimeScaledObject::set_scale_from_dimensions(double viewport_width,
											  double content_width)
{
	set_scale(calculate_scale_from_dimensions(viewport_width, content_width));
}

double TimeScaledObject::calculate_scale_from_dimensions(double viewport_sz,
													  double content_sz)
{
	return static_cast<double>(viewport_sz / k_calculate_dimensions_padding *
							   (k_calculate_dimensions_padding - 1)) /
		   static_cast<double>(content_sz);
}

double TimeScaledObject::calculate_padding_from_dimension_scale(double viewport_sz)
{
	return (viewport_sz / (k_calculate_dimensions_padding * 2));
}

TimelineScaledWidget::TimelineScaledWidget(QWidget *parent)
	: QWidget(parent)
{
}

}
