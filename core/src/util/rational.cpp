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

#include "util/rational.h"

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <climits>
#include <limits>

#include "util/fractionutils.h"
#include "util/stringutils.h"

namespace olive::core::internal
{

const Rational Rational::na_n = Rational(0, 0);

Rational Rational::from_double(const double &flt, bool *ok)
{
	if (isnan(flt)) {
		// Return NaN Rational
		if (ok)
			*ok = false;
		return na_n;
	}

	if (fabs(flt) > double(INT_MAX) + 3.0) {
		// Value is out of range for a Rational, return NaN
		if (ok) {
			*ok = false;
		}
		return na_n;
	}

	// Continued fraction conversion (ported from FFmpeg's av_d2q)
	int exponent;
	frexp(flt, &exponent);
	exponent = std::max(exponent - 1, 0);
	int64_t den = 1LL << (62 - exponent);
	int64_t num = int64_t(floor(flt * den + 0.5));

	int64_t rnum = num, rden = den;
	reduce_fraction(rnum, rden, INT_MAX);

	if ((!rnum || !rden) && flt) {
		// Value was too small to represent above, retry with maximum precision
		rnum = int64_t(flt * double(INT64_MAX));
		rden = INT64_MAX;
		reduce_fraction(rnum, rden, INT_MAX);
	}

	if (rden == 0) {
		// If den == 0, we were unable to convert to a Rational
		if (ok) {
			*ok = false;
		}
		return na_n;
	}

	// Otherwise, assume we received a real Rational
	if (ok) {
		*ok = true;
	}

	return Rational(int(rnum), int(rden));
}

Rational Rational::from_string(const std::string &str, bool *ok)
{
	std::vector<std::string> elements = StringUtils::split(str, '/');

	switch (elements.size()) {
	case 1:
		return Rational(StringUtils::to_int(elements.front(), ok));
	case 2:
		return Rational(StringUtils::to_int(elements.at(0), ok),
						StringUtils::to_int(elements.at(1), ok));
	default:
		// Returns NaN with ok set to false
		if (ok) {
			*ok = false;
		}
		return na_n;
	}
}

//Function: convert to double

double Rational::to_double() const
{
	if (den_ != 0) {
		return double(num_) / double(den_);
	} else {
		return std::numeric_limits<double>::quiet_NaN();
	}
}

#ifdef USE_OTIO
opentime::RationalTime Rational::toRationalTime(double framerate) const
{
	// Is this the best way of doing this?
	// Olive can store rationals as 0/0 which causes errors in OTIO
	opentime::RationalTime time =
		opentime::RationalTime(num_, den_ == 0 ? 1 : den_);
	return time.rescaled_to(framerate);
}
#endif

Rational Rational::flipped() const
{
	Rational r = *this;
	r.flip();
	return r;
}

void Rational::flip()
{
	if (!isNull()) {
		std::swap(den_, num_);
		fix_signs();
	}
}

std::string Rational::to_string() const
{
	return StringUtils::format("%d/%d", num_, den_);
}

void Rational::fix_signs()
{
	if (den_ < 0) {
		// Normalize so that denominator is always positive
		den_ = -den_;
		num_ = -num_;
	} else if (den_ == 0) {
		// Normalize to 0/0 (aka NaN) if denominator is zero
		num_ = 0;
	} else if (num_ == 0) {
		// Normalize to 0/1 if numerator is zero
		den_ = 1;
	}
}

void Rational::reduce()
{
	int64_t n = num_, d = den_;
	reduce_fraction(n, d, INT_MAX);
	num_ = int(n);
	den_ = int(d);
}

//Assignment Operators

const Rational &Rational::operator=(const Rational &rhs)
{
	num_ = rhs.num_;
	den_ = rhs.den_;
	return *this;
}

const Rational &Rational::operator+=(const Rational &rhs)
{
	if (*this == RATIONAL_MIN || *this == RATIONAL_MAX || rhs == RATIONAL_MIN ||
		rhs == RATIONAL_MAX) {
		*this = na_n;
	} else if (!isNaN()) {
		if (rhs.isNaN()) {
			*this = na_n;
		} else {
			int64_t n = num_ * int64_t(rhs.den_) + rhs.num_ * int64_t(den_);
			int64_t d = den_ * int64_t(rhs.den_);
			reduce_fraction(n, d, INT_MAX);
			num_ = int(n);
			den_ = int(d);
			fix_signs();
		}
	}

	return *this;
}

const Rational &Rational::operator-=(const Rational &rhs)
{
	if (*this == RATIONAL_MIN || *this == RATIONAL_MAX || rhs == RATIONAL_MIN ||
		rhs == RATIONAL_MAX) {
		*this = na_n;
	} else if (!isNaN()) {
		if (rhs.isNaN()) {
			*this = na_n;
		} else {
			int64_t n = num_ * int64_t(rhs.den_) - rhs.num_ * int64_t(den_);
			int64_t d = den_ * int64_t(rhs.den_);
			reduce_fraction(n, d, INT_MAX);
			num_ = int(n);
			den_ = int(d);
			fix_signs();
		}
	}

	return *this;
}

const Rational &Rational::operator*=(const Rational &rhs)
{
	if (*this == RATIONAL_MIN || *this == RATIONAL_MAX || rhs == RATIONAL_MIN ||
		rhs == RATIONAL_MAX) {
		*this = na_n;
	} else if (!isNaN()) {
		if (rhs.isNaN()) {
			*this = na_n;
		} else {
			int64_t n = num_ * int64_t(rhs.num_);
			int64_t d = den_ * int64_t(rhs.den_);
			reduce_fraction(n, d, INT_MAX);
			num_ = int(n);
			den_ = int(d);
			fix_signs();
		}
	}

	return *this;
}

const Rational &Rational::operator/=(const Rational &rhs)
{
	if (*this == RATIONAL_MIN || *this == RATIONAL_MAX || rhs == RATIONAL_MIN ||
		rhs == RATIONAL_MAX) {
		*this = na_n;
	} else if (!isNaN()) {
		if (rhs.isNaN()) {
			*this = na_n;
		} else {
			int64_t n = num_ * int64_t(rhs.den_);
			int64_t d = den_ * int64_t(rhs.num_);
			reduce_fraction(n, d, INT_MAX);
			num_ = int(n);
			den_ = int(d);
			fix_signs();
		}
	}

	return *this;
}

//Binary math operators

Rational Rational::operator+(const Rational &rhs) const
{
	Rational answer(*this);
	answer += rhs;
	return answer;
}

Rational Rational::operator-(const Rational &rhs) const
{
	Rational answer(*this);
	answer -= rhs;
	return answer;
}

Rational Rational::operator/(const Rational &rhs) const
{
	Rational answer(*this);
	answer /= rhs;
	return answer;
}

Rational Rational::operator*(const Rational &rhs) const
{
	Rational answer(*this);
	answer *= rhs;
	return answer;
}

//Relational and equality operators

bool Rational::operator<(const Rational &rhs) const
{
	return compare_fractions(num_, den_, rhs.num_, rhs.den_) == -1;
}

bool Rational::operator<=(const Rational &rhs) const
{
	int cmp = compare_fractions(num_, den_, rhs.num_, rhs.den_);
	return cmp == 0 || cmp == -1;
}

bool Rational::operator>(const Rational &rhs) const
{
	return compare_fractions(num_, den_, rhs.num_, rhs.den_) == 1;
}

bool Rational::operator>=(const Rational &rhs) const
{
	int cmp = compare_fractions(num_, den_, rhs.num_, rhs.den_);
	return cmp == 0 || cmp == 1;
}

bool Rational::operator==(const Rational &rhs) const
{
	return compare_fractions(num_, den_, rhs.num_, rhs.den_) == 0;
}

bool Rational::operator!=(const Rational &rhs) const
{
	return !(*this == rhs);
}

}
