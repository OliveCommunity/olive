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

#include "oakcore/rational.h"

#include <stdio.h>

#include "util/rational.h"

namespace
{

olive::core::internal::Rational *impl(OakRational *h)
{
	return reinterpret_cast<olive::core::internal::Rational *>(h);
}

const olive::core::internal::Rational *impl(const OakRational *h)
{
	return reinterpret_cast<const olive::core::internal::Rational *>(h);
}

OakRational *wrap(olive::core::internal::Rational *r)
{
	return reinterpret_cast<OakRational *>(r);
}

} // namespace

extern "C"
{

OakRational *oakcore_rational_create(int numerator)
{
	return wrap(new olive::core::internal::Rational(numerator));
}

OakRational *oakcore_rational_create_nd(int numerator, int denominator)
{
	return wrap(new olive::core::internal::Rational(numerator, denominator));
}

OakRational *oakcore_rational_create_nan(void)
{
	return wrap(new olive::core::internal::Rational(olive::core::internal::Rational::na_n));
}

OakRational *oakcore_rational_copy(const OakRational *self)
{
	return wrap(new olive::core::internal::Rational(*impl(self)));
}

void oakcore_rational_free(OakRational *self)
{
	delete impl(self);
}

int oakcore_rational_numerator(const OakRational *self)
{
	return impl(self)->numerator();
}

int oakcore_rational_denominator(const OakRational *self)
{
	return impl(self)->denominator();
}

double oakcore_rational_to_double(const OakRational *self)
{
	return impl(self)->to_double();
}

int oakcore_rational_to_string(const OakRational *self, char *buf, int buf_size)
{
	const std::string s = impl(self)->to_string();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", s.c_str());
	}
	return int(s.size());
}

OakRational *oakcore_rational_from_double(double value, int *ok)
{
	bool b = false;
	olive::core::internal::Rational *r =
		new olive::core::internal::Rational(olive::core::internal::Rational::from_double(value, &b));
	if (ok) {
		*ok = b ? 1 : 0;
	}
	return wrap(r);
}

OakRational *oakcore_rational_from_string(const char *str, int *ok)
{
	bool b = false;
	olive::core::internal::Rational *r = new olive::core::internal::Rational(
		olive::core::internal::Rational::from_string(str ? str : "", &b));
	if (ok) {
		*ok = b ? 1 : 0;
	}
	return wrap(r);
}

int oakcore_rational_is_null(const OakRational *self)
{
	return impl(self)->isNull() ? 1 : 0;
}

int oakcore_rational_is_nan(const OakRational *self)
{
	return impl(self)->isNaN() ? 1 : 0;
}

OakRational *oakcore_rational_flipped(const OakRational *self)
{
	return wrap(new olive::core::internal::Rational(impl(self)->flipped()));
}

void oakcore_rational_flip(OakRational *self)
{
	impl(self)->flip();
}

void oakcore_rational_add_assign(OakRational *self, const OakRational *other)
{
	*impl(self) += *impl(other);
}

void oakcore_rational_sub_assign(OakRational *self, const OakRational *other)
{
	*impl(self) -= *impl(other);
}

void oakcore_rational_mul_assign(OakRational *self, const OakRational *other)
{
	*impl(self) *= *impl(other);
}

void oakcore_rational_div_assign(OakRational *self, const OakRational *other)
{
	*impl(self) /= *impl(other);
}

int oakcore_rational_compare(const OakRational *self, const OakRational *other)
{
	if (*impl(self) < *impl(other)) {
		return -1;
	}
	if (*impl(other) < *impl(self)) {
		return 1;
	}
	return 0;
}

} // extern "C"
