#include <gtest/gtest.h>
#include <math.h>

#include "olive/core/oakcore/bezier.h"

TEST(OakcoreBezier, DefaultConstruction)
{
	OakBezier *b = oakcore_bezier_create();
	ASSERT_NE(b, nullptr);
	EXPECT_NEAR(oakcore_bezier_x(b), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_y(b), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_x(b), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_y(b), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_x(b), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_y(b), 0.0, 1e-9);
	oakcore_bezier_free(b);
}

TEST(OakcoreBezier, SettersAndGetters)
{
	OakBezier *b = oakcore_bezier_create();
	oakcore_bezier_set_x(b, 1.5);
	oakcore_bezier_set_y(b, -2.25);
	oakcore_bezier_set_cp1_x(b, 0.1);
	oakcore_bezier_set_cp1_y(b, 0.2);
	oakcore_bezier_set_cp2_x(b, 0.3);
	oakcore_bezier_set_cp2_y(b, 0.4);
	EXPECT_NEAR(oakcore_bezier_x(b), 1.5, 1e-9);
	EXPECT_NEAR(oakcore_bezier_y(b), -2.25, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_x(b), 0.1, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_y(b), 0.2, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_x(b), 0.3, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_y(b), 0.4, 1e-9);
	oakcore_bezier_free(b);
}

TEST(OakcoreBezier, XYConstructor)
{
	OakBezier *xy = oakcore_bezier_create_xy(3.0, 4.0);
	EXPECT_NEAR(oakcore_bezier_x(xy), 3.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_y(xy), 4.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_x(xy), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_y(xy), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_x(xy), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_y(xy), 0.0, 1e-9);
	oakcore_bezier_free(xy);
}

TEST(OakcoreBezier, FullConstructor)
{
	OakBezier *full = oakcore_bezier_create_full(1.0, 2.0, 0.25, 0.5, 0.75, 1.0);
	EXPECT_NEAR(oakcore_bezier_x(full), 1.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_y(full), 2.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_x(full), 0.25, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_y(full), 0.5, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_x(full), 0.75, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_y(full), 1.0, 1e-9);
	oakcore_bezier_free(full);
}

TEST(OakcoreBezier, CopyIsIndependent)
{
	OakBezier *full = oakcore_bezier_create_full(1.0, 2.0, 0.25, 0.5, 0.75, 1.0);
	OakBezier *dup = oakcore_bezier_copy(full);
	EXPECT_NEAR(oakcore_bezier_x(dup), 1.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp2_y(dup), 1.0, 1e-9);

	oakcore_bezier_set_x(dup, 9.0);
	oakcore_bezier_set_cp1_y(dup, 8.0);
	EXPECT_NEAR(oakcore_bezier_x(full), 1.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cp1_y(full), 0.5, 1e-9);

	oakcore_bezier_free(dup);
	oakcore_bezier_free(full);
}

TEST(OakcoreBezier, QuadraticEvaluation)
{
	EXPECT_NEAR(oakcore_bezier_quadratic_tto_y(0.0, 0.5, 1.0, 0.0), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_quadratic_tto_y(0.0, 0.5, 1.0, 1.0), 1.0, 1e-9);

	const double x = 0.3;
	const double t = oakcore_bezier_quadratic_xto_t(x, 0.0, 0.25, 1.0);
	EXPECT_GE(t, 0.0);
	EXPECT_LE(t, 1.0);
	EXPECT_NEAR(oakcore_bezier_quadratic_tto_y(0.0, 0.25, 1.0, t), x, 1e-5);
}

TEST(OakcoreBezier, QuadraticClamps)
{
	const double t_lo = oakcore_bezier_quadratic_xto_t(-5.0, 0.0, 0.5, 1.0);
	const double t_hi = oakcore_bezier_quadratic_xto_t(5.0, 0.0, 0.5, 1.0);
	EXPECT_GE(t_lo, 0.0);
	EXPECT_LE(t_lo, 1.0);
	EXPECT_GE(t_hi, 0.0);
	EXPECT_LE(t_hi, 1.0);
}

TEST(OakcoreBezier, CubicEvaluation)
{
	EXPECT_NEAR(oakcore_bezier_cubic_tto_y(0.0, 0.25, 0.75, 1.0, 0.0), 0.0, 1e-9);
	EXPECT_NEAR(oakcore_bezier_cubic_tto_y(0.0, 0.25, 0.75, 1.0, 1.0), 1.0, 1e-9);

	const double x = 0.6;
	const double t = oakcore_bezier_cubic_xto_t(x, 0.0, 0.1, 0.9, 1.0);
	EXPECT_GE(t, 0.0);
	EXPECT_LE(t, 1.0);
	EXPECT_NEAR(oakcore_bezier_cubic_tto_y(0.0, 0.1, 0.9, 1.0, t), x, 1e-5);
}

TEST(OakcoreBezier, CubicClamps)
{
	const double t_lo = oakcore_bezier_cubic_xto_t(-5.0, 0.0, 0.25, 0.75, 1.0);
	const double t_hi = oakcore_bezier_cubic_xto_t(5.0, 0.0, 0.25, 0.75, 1.0);
	EXPECT_GE(t_lo, 0.0);
	EXPECT_LE(t_lo, 1.0);
	EXPECT_GE(t_hi, 0.0);
	EXPECT_LE(t_hi, 1.0);
}

TEST(OakcoreBezier, FreeNull)
{
	oakcore_bezier_free(NULL);
}
