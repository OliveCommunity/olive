/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_LIBOLIVECORE_BEZIER_H
#define OAK_LIBOLIVECORE_BEZIER_H

#include <Imath/ImathVec.h>

#include "olive/core/oakcore/bezier.h"

namespace olive::core
{

/**
 * @brief Keyframe easing bezier curve value type
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakBezier handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation.
 */
class Bezier {
public:
	Bezier()
		: handle_(oakcore_bezier_create())
	{
	}

	Bezier(double x, double y)
		: handle_(oakcore_bezier_create_xy(x, y))
	{
	}

	Bezier(double x, double y, double cp1_x, double cp1_y, double cp2_x,
		   double cp2_y)
		: handle_(
			oakcore_bezier_create_full(x, y, cp1_x, cp1_y, cp2_x, cp2_y))
	{
	}

	Bezier(const Bezier &rhs)
		: handle_(oakcore_bezier_copy(rhs.handle_))
	{
	}

	Bezier(Bezier &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~Bezier()
	{
		oakcore_bezier_free(handle_);
	}

	Bezier &operator=(const Bezier &rhs)
	{
		if (this != &rhs) {
			oakcore_bezier_free(handle_);
			handle_ = oakcore_bezier_copy(rhs.handle_);
		}
		return *this;
	}

	Bezier &operator=(Bezier &&rhs) noexcept
	{
		if (this != &rhs) {
			oakcore_bezier_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	double x() const
	{
		return oakcore_bezier_x(handle_);
	}
	double y() const
	{
		return oakcore_bezier_y(handle_);
	}
	double cp1_x() const
	{
		return oakcore_bezier_cp1_x(handle_);
	}
	double cp1_y() const
	{
		return oakcore_bezier_cp1_y(handle_);
	}
	double cp2_x() const
	{
		return oakcore_bezier_cp2_x(handle_);
	}
	double cp2_y() const
	{
		return oakcore_bezier_cp2_y(handle_);
	}

	Imath::V2d to_vec() const
	{
		return Imath::V2d(x(), y());
	}

	Imath::V2d control_point_1_to_vec() const
	{
		return Imath::V2d(cp1_x(), cp1_y());
	}

	Imath::V2d control_point_2_to_vec() const
	{
		return Imath::V2d(cp2_x(), cp2_y());
	}

	void set_x(const double &x)
	{
		oakcore_bezier_set_x(handle_, x);
	}
	void set_y(const double &y)
	{
		oakcore_bezier_set_y(handle_, y);
	}
	void set_cp1_x(const double &cp1_x)
	{
		oakcore_bezier_set_cp1_x(handle_, cp1_x);
	}
	void set_cp1_y(const double &cp1_y)
	{
		oakcore_bezier_set_cp1_y(handle_, cp1_y);
	}
	void set_cp2_x(const double &cp2_x)
	{
		oakcore_bezier_set_cp2_x(handle_, cp2_x);
	}
	void set_cp2_y(const double &cp2_y)
	{
		oakcore_bezier_set_cp2_y(handle_, cp2_y);
	}

	static double quadratic_xto_t(double x, double a, double b, double c)
	{
		return oakcore_bezier_quadratic_xto_t(x, a, b, c);
	}

	static double quadratic_tto_y(double a, double b, double c, double t)
	{
		return oakcore_bezier_quadratic_tto_y(a, b, c, t);
	}

	static double quadratic_xto_y(double x, const Imath::V2d &a,
								  const Imath::V2d &b, const Imath::V2d &c)
	{
		return quadratic_tto_y(a.y, b.y, c.y, quadratic_xto_t(x, a.x, b.x, c.x));
	}

	static double cubic_xto_t(double x, double a, double b, double c, double d)
	{
		return oakcore_bezier_cubic_xto_t(x, a, b, c, d);
	}

	static double cubic_tto_y(double a, double b, double c, double d, double t)
	{
		return oakcore_bezier_cubic_tto_y(a, b, c, d, t);
	}

	static double cubic_xto_y(double x, const Imath::V2d &a, const Imath::V2d &b,
							  const Imath::V2d &c, const Imath::V2d &d)
	{
		return cubic_tto_y(a.y, b.y, c.y, d.y,
						   cubic_xto_t(x, a.x, b.x, c.x, d.x));
	}

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakBezier *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static Bezier from_handle(OakBezier *handle)
	{
		return Bezier(handle);
	}

private:
	explicit Bezier(OakBezier *handle)
		: handle_(handle)
	{
	}

	OakBezier *handle_;
};

}

#endif // OAK_LIBOLIVECORE_BEZIER_H
