/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "oakcore/bezier.h"

#include "util/bezier.h"

namespace
{

olive::core::internal::Bezier *impl(OakBezier *h)
{
	return reinterpret_cast<olive::core::internal::Bezier *>(h);
}

const olive::core::internal::Bezier *impl(const OakBezier *h)
{
	return reinterpret_cast<const olive::core::internal::Bezier *>(h);
}

OakBezier *wrap(olive::core::internal::Bezier *b)
{
	return reinterpret_cast<OakBezier *>(b);
}

} // namespace

extern "C"
{

OakBezier *oakcore_bezier_create(void)
{
	return wrap(new olive::core::internal::Bezier());
}

OakBezier *oakcore_bezier_create_xy(double x, double y)
{
	return wrap(new olive::core::internal::Bezier(x, y));
}

OakBezier *oakcore_bezier_create_full(double x, double y, double cp1_x,
									  double cp1_y, double cp2_x, double cp2_y)
{
	return wrap(new olive::core::internal::Bezier(x, y, cp1_x, cp1_y, cp2_x,
												  cp2_y));
}

OakBezier *oakcore_bezier_copy(const OakBezier *self)
{
	return wrap(new olive::core::internal::Bezier(*impl(self)));
}

void oakcore_bezier_free(OakBezier *self)
{
	delete impl(self);
}

double oakcore_bezier_x(const OakBezier *self)
{
	return impl(self)->x();
}

double oakcore_bezier_y(const OakBezier *self)
{
	return impl(self)->y();
}

double oakcore_bezier_cp1_x(const OakBezier *self)
{
	return impl(self)->cp1_x();
}

double oakcore_bezier_cp1_y(const OakBezier *self)
{
	return impl(self)->cp1_y();
}

double oakcore_bezier_cp2_x(const OakBezier *self)
{
	return impl(self)->cp2_x();
}

double oakcore_bezier_cp2_y(const OakBezier *self)
{
	return impl(self)->cp2_y();
}

void oakcore_bezier_set_x(OakBezier *self, double x)
{
	impl(self)->set_x(x);
}

void oakcore_bezier_set_y(OakBezier *self, double y)
{
	impl(self)->set_y(y);
}

void oakcore_bezier_set_cp1_x(OakBezier *self, double cp1_x)
{
	impl(self)->set_cp1_x(cp1_x);
}

void oakcore_bezier_set_cp1_y(OakBezier *self, double cp1_y)
{
	impl(self)->set_cp1_y(cp1_y);
}

void oakcore_bezier_set_cp2_x(OakBezier *self, double cp2_x)
{
	impl(self)->set_cp2_x(cp2_x);
}

void oakcore_bezier_set_cp2_y(OakBezier *self, double cp2_y)
{
	impl(self)->set_cp2_y(cp2_y);
}

double oakcore_bezier_quadratic_xto_t(double x, double a, double b, double c)
{
	return olive::core::internal::Bezier::quadratic_xto_t(x, a, b, c);
}

double oakcore_bezier_quadratic_tto_y(double a, double b, double c, double t)
{
	return olive::core::internal::Bezier::quadratic_tto_y(a, b, c, t);
}

double oakcore_bezier_cubic_xto_t(double x, double a, double b, double c,
								  double d)
{
	return olive::core::internal::Bezier::cubic_xto_t(x, a, b, c, d);
}

double oakcore_bezier_cubic_tto_y(double a, double b, double c, double d,
								  double t)
{
	return olive::core::internal::Bezier::cubic_tto_y(a, b, c, d, t);
}

} // extern "C"
