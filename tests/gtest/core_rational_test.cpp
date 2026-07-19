#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <sstream>

#include "olive/core/util/rational.h"

using namespace olive::core;

TEST(CoreRational, DefaultConstruction)
{
	Rational r;
	EXPECT_EQ(r.numerator(), 0);
	EXPECT_EQ(r.denominator(), 1);
}

TEST(CoreRational, IntegerConstruction)
{
	Rational r(5);
	EXPECT_EQ(r.numerator(), 5);
	EXPECT_EQ(r.denominator(), 1);
}

TEST(CoreRational, FractionConstructionReduces)
{
	Rational r(4, 8);
	EXPECT_EQ(r.numerator(), 1);
	EXPECT_EQ(r.denominator(), 2);
}

TEST(CoreRational, NegativeDenominatorNormalizes)
{
	Rational r(1, -2);
	EXPECT_EQ(r.numerator(), -1);
	EXPECT_EQ(r.denominator(), 2);
}

TEST(CoreRational, ZeroNormalizes)
{
	Rational r(0, 5);
	EXPECT_EQ(r.numerator(), 0);
	EXPECT_EQ(r.denominator(), 1);
}

TEST(CoreRational, FromDouble)
{
	bool ok = false;
	Rational r = Rational::from_double(0.5, &ok);
	EXPECT_TRUE(ok);
	EXPECT_EQ(r, Rational(1, 2));

	r = Rational::from_double(std::numeric_limits<double>::quiet_NaN(), &ok);
	EXPECT_FALSE(ok);
	EXPECT_TRUE(r.isNaN());
}

TEST(CoreRational, FromString)
{
	bool ok = false;
	Rational r = Rational::from_string("3/4", &ok);
	EXPECT_TRUE(ok);
	EXPECT_EQ(r, Rational(3, 4));

	r = Rational::from_string("42", &ok);
	EXPECT_TRUE(ok);
	EXPECT_EQ(r, Rational(42));

	r = Rational::from_string("1/2/3", &ok);
	EXPECT_FALSE(ok);
	EXPECT_TRUE(r.isNaN());
}

TEST(CoreRational, ToDouble)
{
	EXPECT_DOUBLE_EQ(Rational(1, 2).to_double(), 0.5);
	EXPECT_TRUE(std::isnan(Rational::na_n.to_double()));
}

TEST(CoreRational, ToString)
{
	EXPECT_EQ(Rational(1, 2).to_string(), "1/2");
}

TEST(CoreRational, Arithmetic)
{
	Rational a(1, 2);
	Rational b(1, 3);

	EXPECT_EQ(a + b, Rational(5, 6));
	EXPECT_EQ(a - b, Rational(1, 6));
	EXPECT_EQ(a * b, Rational(1, 6));
	EXPECT_EQ(a / b, Rational(3, 2));
}

TEST(CoreRational, CompoundAssignment)
{
	Rational a(1, 2);
	a += Rational(1, 4);
	EXPECT_EQ(a, Rational(3, 4));

	a *= Rational(2, 3);
	EXPECT_EQ(a, Rational(1, 2));
}

TEST(CoreRational, Comparisons)
{
	Rational a(1, 2);
	Rational b(2, 4);
	Rational c(1, 3);

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a != b);
	EXPECT_TRUE(a > c);
	EXPECT_TRUE(c < a);
	EXPECT_TRUE(a >= b);
	EXPECT_TRUE(c <= a);
}

TEST(CoreRational, UnaryOperators)
{
	Rational a(1, 2);
	EXPECT_EQ(+a, a);
	EXPECT_EQ(-a, Rational(-1, 2));
	EXPECT_FALSE(!a);

	Rational zero(0);
	EXPECT_TRUE(!zero);
}

TEST(CoreRational, Flip)
{
	Rational a(2, 3);
	a.flip();
	EXPECT_EQ(a, Rational(3, 2));

	Rational zero(0);
	zero.flip();
	EXPECT_EQ(zero, Rational(0));
}

TEST(CoreRational, Flipped)
{
	EXPECT_EQ(Rational(2, 3).flipped(), Rational(3, 2));
}

TEST(CoreRational, IsNullAndIsNaN)
{
	Rational zero(0);
	EXPECT_TRUE(zero.isNull());
	EXPECT_FALSE(zero.isNaN());

	Rational nan = Rational::na_n;
	EXPECT_TRUE(nan.isNaN());
	EXPECT_TRUE(nan.isNull());
}

TEST(CoreRational, NaNPropagation)
{
	Rational a(1, 2);
	Rational nan = Rational::na_n;

	a += nan;
	EXPECT_TRUE(a.isNaN());
}

TEST(CoreRational, StreamOutput)
{
	std::ostringstream oss;
	oss << Rational(3, 4);
	EXPECT_EQ(oss.str(), "3/4");
}

TEST(CoreRational, MinMaxConstants)
{
	EXPECT_TRUE(RATIONAL_MIN < Rational(0));
	EXPECT_TRUE(RATIONAL_MAX > Rational(0));
}
