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

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "olive/core/oakcore/bezier.h"

static int double_eq(double a, double b)
{
	return fabs(a - b) < 1e-9;
}

int main(void)
{
	// Default constructor: everything zeroed
	OakBezier *b = oakcore_bezier_create();
	assert(b != NULL);
	assert(double_eq(oakcore_bezier_x(b), 0.0));
	assert(double_eq(oakcore_bezier_y(b), 0.0));
	assert(double_eq(oakcore_bezier_cp1_x(b), 0.0));
	assert(double_eq(oakcore_bezier_cp1_y(b), 0.0));
	assert(double_eq(oakcore_bezier_cp2_x(b), 0.0));
	assert(double_eq(oakcore_bezier_cp2_y(b), 0.0));

	// Setters write through to the getters
	oakcore_bezier_set_x(b, 1.5);
	oakcore_bezier_set_y(b, -2.25);
	oakcore_bezier_set_cp1_x(b, 0.1);
	oakcore_bezier_set_cp1_y(b, 0.2);
	oakcore_bezier_set_cp2_x(b, 0.3);
	oakcore_bezier_set_cp2_y(b, 0.4);
	assert(double_eq(oakcore_bezier_x(b), 1.5));
	assert(double_eq(oakcore_bezier_y(b), -2.25));
	assert(double_eq(oakcore_bezier_cp1_x(b), 0.1));
	assert(double_eq(oakcore_bezier_cp1_y(b), 0.2));
	assert(double_eq(oakcore_bezier_cp2_x(b), 0.3));
	assert(double_eq(oakcore_bezier_cp2_y(b), 0.4));

	// x/y constructor: control points stay zeroed
	OakBezier *xy = oakcore_bezier_create_xy(3.0, 4.0);
	assert(double_eq(oakcore_bezier_x(xy), 3.0));
	assert(double_eq(oakcore_bezier_y(xy), 4.0));
	assert(double_eq(oakcore_bezier_cp1_x(xy), 0.0));
	assert(double_eq(oakcore_bezier_cp1_y(xy), 0.0));
	assert(double_eq(oakcore_bezier_cp2_x(xy), 0.0));
	assert(double_eq(oakcore_bezier_cp2_y(xy), 0.0));

	// Full constructor
	OakBezier *full =
		oakcore_bezier_create_full(1.0, 2.0, 0.25, 0.5, 0.75, 1.0);
	assert(double_eq(oakcore_bezier_x(full), 1.0));
	assert(double_eq(oakcore_bezier_y(full), 2.0));
	assert(double_eq(oakcore_bezier_cp1_x(full), 0.25));
	assert(double_eq(oakcore_bezier_cp1_y(full), 0.5));
	assert(double_eq(oakcore_bezier_cp2_x(full), 0.75));
	assert(double_eq(oakcore_bezier_cp2_y(full), 1.0));

	// Copy: same values, independent storage
	OakBezier *dup = oakcore_bezier_copy(full);
	assert(double_eq(oakcore_bezier_x(dup), 1.0));
	assert(double_eq(oakcore_bezier_cp2_y(dup), 1.0));
	oakcore_bezier_set_x(dup, 9.0);
	oakcore_bezier_set_cp1_y(dup, 8.0);
	assert(double_eq(oakcore_bezier_x(full), 1.0));
	assert(double_eq(oakcore_bezier_cp1_y(full), 0.5));

	// Quadratic evaluation: endpoints and x->t->y round trip
	assert(double_eq(oakcore_bezier_quadratic_tto_y(0.0, 0.5, 1.0, 0.0), 0.0));
	assert(double_eq(oakcore_bezier_quadratic_tto_y(0.0, 0.5, 1.0, 1.0), 1.0));
	{
		const double x = 0.3;
		const double t =
			oakcore_bezier_quadratic_xto_t(x, 0.0, 0.25, 1.0);
		const double y =
			oakcore_bezier_quadratic_tto_y(0.0, 0.75, 1.0, t);
		assert(t >= 0.0 && t <= 1.0);
		// xto_t solves on the x curve; feeding the same x back must hold
		assert(fabs(oakcore_bezier_quadratic_tto_y(0.0, 0.25, 1.0, t) - x) <
			   1e-5);
		(void) y;
	}
	// xto_t clamps x into [a, c] instead of diverging
	{
		const double t_lo = oakcore_bezier_quadratic_xto_t(-5.0, 0.0, 0.5, 1.0);
		const double t_hi = oakcore_bezier_quadratic_xto_t(5.0, 0.0, 0.5, 1.0);
		assert(t_lo >= 0.0 && t_lo <= 1.0);
		assert(t_hi >= 0.0 && t_hi <= 1.0);
	}

	// Cubic evaluation: endpoints and x->t->y round trip
	assert(double_eq(oakcore_bezier_cubic_tto_y(0.0, 0.25, 0.75, 1.0, 0.0),
					 0.0));
	assert(double_eq(oakcore_bezier_cubic_tto_y(0.0, 0.25, 0.75, 1.0, 1.0),
					 1.0));
	{
		const double x = 0.6;
		const double t =
			oakcore_bezier_cubic_xto_t(x, 0.0, 0.1, 0.9, 1.0);
		const double y =
			oakcore_bezier_cubic_tto_y(0.0, 0.8, 0.2, 1.0, t);
		assert(t >= 0.0 && t <= 1.0);
		assert(fabs(oakcore_bezier_cubic_tto_y(0.0, 0.1, 0.9, 1.0, t) - x) <
			   1e-5);
		(void) y;
	}
	// xto_t clamps x into [a, d] instead of diverging
	{
		const double t_lo =
			oakcore_bezier_cubic_xto_t(-5.0, 0.0, 0.25, 0.75, 1.0);
		const double t_hi =
			oakcore_bezier_cubic_xto_t(5.0, 0.0, 0.25, 0.75, 1.0);
		assert(t_lo >= 0.0 && t_lo <= 1.0);
		assert(t_hi >= 0.0 && t_hi <= 1.0);
	}

	// Ownership: every handle released exactly once
	oakcore_bezier_free(b);
	oakcore_bezier_free(xy);
	oakcore_bezier_free(full);
	oakcore_bezier_free(dup);
	oakcore_bezier_free(NULL);

	printf("oakcore_bezier_test: all assertions passed\n");
	return 0;
}
