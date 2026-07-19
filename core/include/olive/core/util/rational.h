/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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

#ifndef OAK_LIBOLIVECORE_RATIONAL_H
#define OAK_LIBOLIVECORE_RATIONAL_H

#include <climits>
#include <iostream>

#ifdef USE_OTIO
#include <opentime/rationalTime.h>
#endif

namespace olive::core
{

class Rational {
public:
	Rational(const int &numerator = 0)
	{
		num_ = numerator;
		den_ = 1;
	}

	Rational(const int &numerator, const int &denominator)
	{
		num_ = numerator;
		den_ = denominator;

		fix_signs();
		reduce();
	}

	Rational(const Rational &rhs) = default;

	static Rational from_double(const double &flt, bool *ok = nullptr);
	static Rational from_string(const std::string &str, bool *ok = nullptr);

	static const Rational na_n;

	//Assignment Operators
	const Rational &operator=(const Rational &rhs);
	const Rational &operator+=(const Rational &rhs);
	const Rational &operator-=(const Rational &rhs);
	const Rational &operator/=(const Rational &rhs);
	const Rational &operator*=(const Rational &rhs);

	//Binary math operators
	Rational operator+(const Rational &rhs) const;
	Rational operator-(const Rational &rhs) const;
	Rational operator/(const Rational &rhs) const;
	Rational operator*(const Rational &rhs) const;

	//Relational and equality operators
	bool operator<(const Rational &rhs) const;
	bool operator<=(const Rational &rhs) const;
	bool operator>(const Rational &rhs) const;
	bool operator>=(const Rational &rhs) const;
	bool operator==(const Rational &rhs) const;
	bool operator!=(const Rational &rhs) const;

	//Unary operators
	const Rational &operator+() const
	{
		return *this;
	}
	Rational operator-() const
	{
		return Rational(num_, -den_);
	}
	bool operator!() const
	{
		return !num_;
	}

	//Function: convert to double
	double to_double() const;

#ifdef USE_OTIO
	static Rational fromRationalTime(const opentime::RationalTime &t)
	{
		// Is this the best way to do this?
		return fromDouble(t.to_seconds());
	}

	// Convert Olive rationals to opentime rationals with the given framerate (defaults to 24)
	opentime::RationalTime toRationalTime(double framerate = 24) const;
#endif

	// Produce "flipped" version
	Rational flipped() const;
	void flip();

	// Returns whether the Rational is valid but equal to zero or not
	//
	// A NaN is always a null, but a null is not always a NaN
	bool isNull() const
	{
		return num_ == 0;
	}

	// Returns whether this Rational is not a valid number (denominator == 0)
	bool isNaN() const
	{
		return den_ == 0;
	}

	const int &numerator() const
	{
		return num_;
	}
	const int &denominator() const
	{
		return den_;
	}

	std::string to_string() const;

	friend std::ostream &operator<<(std::ostream &out, const Rational &value)
	{
		out << value.num_ << '/' << value.den_;

		return out;
	}

private:
	void fix_signs();
	void reduce();

	int num_;
	int den_;
};

#define RATIONAL_MIN Rational(INT_MIN)
#define RATIONAL_MAX Rational(INT_MAX)

}

#endif // OAK_LIBOLIVECORE_RATIONAL_H
