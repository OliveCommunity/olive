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

#include "oakcore/fractionutils.h"

#include "util/fractionutils.h"

namespace
{

olive::core::internal::FractionRounding impl(OakFractionRounding rnd)
{
	return static_cast<olive::core::internal::FractionRounding>(rnd);
}

} // namespace

extern "C"
{

void oakcore_fractionutils_reduce_fraction(int64_t *num, int64_t *den, int64_t max)
{
	olive::core::internal::reduce_fraction(*num, *den, max);
}

int oakcore_fractionutils_compare_fractions(int an, int ad, int bn, int bd)
{
	return olive::core::internal::compare_fractions(an, ad, bn, bd);
}

int64_t oakcore_fractionutils_rescale_rnd(int64_t a, int64_t b, int64_t c, OakFractionRounding rnd)
{
	return olive::core::internal::rescale_rnd(a, b, c, impl(rnd));
}

} // extern "C"
