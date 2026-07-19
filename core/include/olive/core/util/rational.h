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

#ifndef OAK_LIBOLIVECORE_RATIONAL_H
#define OAK_LIBOLIVECORE_RATIONAL_H

#include <climits>
#include <iostream>
#include <string>

#ifdef USE_OTIO
#include <opentime/rationalTime.h>
#endif

#include "olive/core/oakcore/rational.h"

namespace olive::core
{

/**
 * @brief Rational number value type
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakRational handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation.
 */
class Rational {
public:
	Rational(const int &numerator = 0)
		: handle_(oakcore_rational_create(numerator))
	{
	}

	Rational(const int &numerator, const int &denominator)
		: handle_(oakcore_rational_create_nd(numerator, denominator))
	{
	}

	Rational(const Rational &rhs)
		: handle_(oakcore_rational_copy(rhs.handle_))
	{
	}

	Rational(Rational &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~Rational()
	{
		oakcore_rational_free(handle_);
	}

	static Rational from_double(const double &flt, bool *ok = nullptr)
	{
		int c_ok = 0;
		Rational r(from_handle(oakcore_rational_from_double(flt, &c_ok)));
		if (ok) {
			*ok = (c_ok != 0);
		}
		return r;
	}

	static Rational from_string(const std::string &str, bool *ok = nullptr)
	{
		int c_ok = 0;
		Rational r(
			from_handle(oakcore_rational_from_string(str.c_str(), &c_ok)));
		if (ok) {
			*ok = (c_ok != 0);
		}
		return r;
	}

	static const Rational na_n;

	//Assignment Operators
	Rational &operator=(const Rational &rhs)
	{
		if (this != &rhs) {
			oakcore_rational_free(handle_);
			handle_ = oakcore_rational_copy(rhs.handle_);
		}
		return *this;
	}

	Rational &operator=(Rational &&rhs) noexcept
	{
		if (this != &rhs) {
			oakcore_rational_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	Rational &operator+=(const Rational &rhs)
	{
		oakcore_rational_add_assign(handle_, rhs.handle_);
		return *this;
	}

	Rational &operator-=(const Rational &rhs)
	{
		oakcore_rational_sub_assign(handle_, rhs.handle_);
		return *this;
	}

	Rational &operator/=(const Rational &rhs)
	{
		oakcore_rational_div_assign(handle_, rhs.handle_);
		return *this;
	}

	Rational &operator*=(const Rational &rhs)
	{
		oakcore_rational_mul_assign(handle_, rhs.handle_);
		return *this;
	}

	//Binary math operators
	Rational operator+(const Rational &rhs) const
	{
		Rational answer(*this);
		answer += rhs;
		return answer;
	}

	Rational operator-(const Rational &rhs) const
	{
		Rational answer(*this);
		answer -= rhs;
		return answer;
	}

	Rational operator/(const Rational &rhs) const
	{
		Rational answer(*this);
		answer /= rhs;
		return answer;
	}

	Rational operator*(const Rational &rhs) const
	{
		Rational answer(*this);
		answer *= rhs;
		return answer;
	}

	//Relational and equality operators
	bool operator<(const Rational &rhs) const
	{
		return oakcore_rational_compare(handle_, rhs.handle_) < 0;
	}

	bool operator<=(const Rational &rhs) const
	{
		return oakcore_rational_compare(handle_, rhs.handle_) <= 0;
	}

	bool operator>(const Rational &rhs) const
	{
		return oakcore_rational_compare(handle_, rhs.handle_) > 0;
	}

	bool operator>=(const Rational &rhs) const
	{
		return oakcore_rational_compare(handle_, rhs.handle_) >= 0;
	}

	bool operator==(const Rational &rhs) const
	{
		return oakcore_rational_compare(handle_, rhs.handle_) == 0;
	}

	bool operator!=(const Rational &rhs) const
	{
		return !(*this == rhs);
	}

	//Unary operators
	Rational operator+() const
	{
		return *this;
	}

	Rational operator-() const
	{
		return Rational(numerator(), -denominator());
	}

	bool operator!() const
	{
		return numerator() == 0;
	}

	//Function: convert to double
	double to_double() const
	{
		return oakcore_rational_to_double(handle_);
	}

#ifdef USE_OTIO
	static Rational fromRationalTime(const opentime::RationalTime &t)
	{
		return from_double(t.to_seconds());
	}

	opentime::RationalTime toRationalTime(double framerate = 24) const
	{
		// Olive can store rationals as 0/0 which causes errors in OTIO
		const int den = denominator();
		opentime::RationalTime time(numerator(), den == 0 ? 1 : den);
		return time.rescaled_to(framerate);
	}
#endif

	// Produce "flipped" version
	Rational flipped() const
	{
		return from_handle(oakcore_rational_flipped(handle_));
	}

	void flip()
	{
		oakcore_rational_flip(handle_);
	}

	// Returns whether the Rational is valid but equal to zero or not
	//
	// A NaN is always a null, but a null is not always a NaN
	bool isNull() const
	{
		return oakcore_rational_is_null(handle_) != 0;
	}

	// Returns whether this Rational is not a valid number (denominator == 0)
	bool isNaN() const
	{
		return oakcore_rational_is_nan(handle_) != 0;
	}

	int numerator() const
	{
		return oakcore_rational_numerator(handle_);
	}

	int denominator() const
	{
		return oakcore_rational_denominator(handle_);
	}

	std::string to_string() const
	{
		const int size = oakcore_rational_to_string(handle_, nullptr, 0);
		std::string s(size_t(size) + 1, '\0');
		oakcore_rational_to_string(handle_, s.data(), size + 1);
		s.resize(size_t(size));
		return s;
	}

	friend std::ostream &operator<<(std::ostream &out, const Rational &value)
	{
		out << value.numerator() << '/' << value.denominator();

		return out;
	}

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakRational *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static Rational from_handle(OakRational *handle)
	{
		return Rational(handle);
	}

private:
	explicit Rational(OakRational *handle)
		: handle_(handle)
	{
	}

	OakRational *handle_;
};

inline const Rational Rational::na_n =
	Rational::from_handle(oakcore_rational_create_nan());

#define RATIONAL_MIN Rational(INT_MIN)
#define RATIONAL_MAX Rational(INT_MAX)

}

#endif // OAK_LIBOLIVECORE_RATIONAL_H
