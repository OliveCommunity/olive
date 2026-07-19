#include <gtest/gtest.h>

#include "olive/core/util/bezier.h"

using namespace olive::core;

TEST(CoreBezier, DefaultConstruction)
{
	Bezier b;
	EXPECT_DOUBLE_EQ(b.x(), 0.0);
	EXPECT_DOUBLE_EQ(b.y(), 0.0);
	EXPECT_DOUBLE_EQ(b.cp1_x(), 0.0);
	EXPECT_DOUBLE_EQ(b.cp1_y(), 0.0);
	EXPECT_DOUBLE_EQ(b.cp2_x(), 0.0);
	EXPECT_DOUBLE_EQ(b.cp2_y(), 0.0);
}

TEST(CoreBezier, ValueConstruction)
{
	Bezier b(1.0, 2.0);
	EXPECT_DOUBLE_EQ(b.x(), 1.0);
	EXPECT_DOUBLE_EQ(b.y(), 2.0);
}

TEST(CoreBezier, FullConstruction)
{
	Bezier b(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	EXPECT_DOUBLE_EQ(b.x(), 1.0);
	EXPECT_DOUBLE_EQ(b.y(), 2.0);
	EXPECT_DOUBLE_EQ(b.cp1_x(), 3.0);
	EXPECT_DOUBLE_EQ(b.cp1_y(), 4.0);
	EXPECT_DOUBLE_EQ(b.cp2_x(), 5.0);
	EXPECT_DOUBLE_EQ(b.cp2_y(), 6.0);
}

TEST(CoreBezier, Setters)
{
	Bezier b;
	b.set_x(10.0);
	b.set_y(20.0);
	b.set_cp1_x(30.0);
	b.set_cp1_y(40.0);
	b.set_cp2_x(50.0);
	b.set_cp2_y(60.0);

	EXPECT_DOUBLE_EQ(b.x(), 10.0);
	EXPECT_DOUBLE_EQ(b.y(), 20.0);
	EXPECT_DOUBLE_EQ(b.cp1_x(), 30.0);
	EXPECT_DOUBLE_EQ(b.cp1_y(), 40.0);
	EXPECT_DOUBLE_EQ(b.cp2_x(), 50.0);
	EXPECT_DOUBLE_EQ(b.cp2_y(), 60.0);
}

TEST(CoreBezier, QuadraticXtoT)
{
	double t = Bezier::quadratic_xto_t(0.5, 0.0, 0.5, 1.0);
	EXPECT_NEAR(t, 0.5, 0.00001);

	t = Bezier::quadratic_xto_t(0.0, 0.0, 0.5, 1.0);
	EXPECT_NEAR(t, 0.0, 0.00001);

	t = Bezier::quadratic_xto_t(1.0, 0.0, 0.5, 1.0);
	EXPECT_NEAR(t, 1.0, 0.00001);
}

TEST(CoreBezier, QuadraticTtoY)
{
	EXPECT_NEAR(Bezier::quadratic_tto_y(0.0, 0.5, 1.0, 0.0), 0.0, 0.00001);
	EXPECT_NEAR(Bezier::quadratic_tto_y(0.0, 0.5, 1.0, 0.5), 0.5, 0.00001);
	EXPECT_NEAR(Bezier::quadratic_tto_y(0.0, 0.5, 1.0, 1.0), 1.0, 0.00001);
}

TEST(CoreBezier, QuadraticXtoY)
{
	Imath::V2d a(0.0, 0.0);
	Imath::V2d b(0.5, 0.5);
	Imath::V2d c(1.0, 1.0);

	EXPECT_NEAR(Bezier::quadratic_xto_y(0.5, a, b, c), 0.5, 0.00001);
}

TEST(CoreBezier, CubicXtoT)
{
	// Independent expectation from the Bernstein basis: with x control
	// values 0, 0.33, 0.66, 1.0 the curve expands to x(t) = 0.99t + 0.01t^3,
	// and x(t) = 0.5 is solved by t = 0.5037592 (Newton-Raphson). The
	// implementation bisects until |x(t) - x| < 1e-6 and dx/dt >= 0.99 on
	// [0,1], so the returned t is well within 1e-5 of the true root.
	double t = Bezier::cubic_xto_t(0.5, 0.0, 0.33, 0.66, 1.0);
	EXPECT_NEAR(t, 0.5037592, 1e-5);
}

TEST(CoreBezier, CubicTtoY)
{
	EXPECT_NEAR(Bezier::cubic_tto_y(0.0, 0.33, 0.66, 1.0, 0.0), 0.0, 0.00001);
	EXPECT_NEAR(Bezier::cubic_tto_y(0.0, 0.33, 0.66, 1.0, 1.0), 1.0, 0.00001);
}

TEST(CoreBezier, CubicXtoY)
{
	Imath::V2d a(0.0, 0.0);
	Imath::V2d b(0.33, 0.0);
	Imath::V2d c(0.66, 1.0);
	Imath::V2d d(1.0, 1.0);

	// Independent expectation from the Bernstein basis: the x curve is
	// x(t) = 0.99t + 0.01t^3, so x = 0.5 gives t = 0.5037592 (Newton-Raphson);
	// the y curve is y(t) = 3(1-t)t^2 + t^3 = 3t^2 - 2t^3, which then yields
	// y = 0.5056392. The implementation's 1e-6 bisection tolerance in x is
	// amplified by dy/dt < 1.5, keeping the y error well under 1e-5.
	double y = Bezier::cubic_xto_y(0.5, a, b, c, d);
	EXPECT_NEAR(y, 0.5056392, 1e-5);
}

TEST(CoreBezier, VectorConverters)
{
	Bezier b(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	Imath::V2d v = b.to_vec();
	EXPECT_DOUBLE_EQ(v.x, 1.0);
	EXPECT_DOUBLE_EQ(v.y, 2.0);

	Imath::V2d cp1 = b.control_point_1_to_vec();
	EXPECT_DOUBLE_EQ(cp1.x, 3.0);
	EXPECT_DOUBLE_EQ(cp1.y, 4.0);

	Imath::V2d cp2 = b.control_point_2_to_vec();
	EXPECT_DOUBLE_EQ(cp2.x, 5.0);
	EXPECT_DOUBLE_EQ(cp2.y, 6.0);
}
