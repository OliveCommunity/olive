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

#include "oakcore/timerange.h"

#include <list>

#include "util/rational.h"
#include "util/timerange.h"

namespace
{

olive::core::internal::TimeRange *impl(OakTimeRange *h)
{
	return reinterpret_cast<olive::core::internal::TimeRange *>(h);
}

const olive::core::internal::TimeRange *impl(const OakTimeRange *h)
{
	return reinterpret_cast<const olive::core::internal::TimeRange *>(h);
}

OakTimeRange *wrap(olive::core::internal::TimeRange *r)
{
	return reinterpret_cast<OakTimeRange *>(r);
}

const olive::core::internal::Rational *rimpl(const OakRational *h)
{
	return reinterpret_cast<const olive::core::internal::Rational *>(h);
}

OakRational *rwrap(olive::core::internal::Rational *r)
{
	return reinterpret_cast<OakRational *>(r);
}

} // namespace

extern "C"
{

OakTimeRange *oakcore_timerange_create(void)
{
	return wrap(new olive::core::internal::TimeRange());
}

OakTimeRange *oakcore_timerange_create_io(const OakRational *in,
										  const OakRational *out)
{
	return wrap(
		new olive::core::internal::TimeRange(*rimpl(in), *rimpl(out)));
}

OakTimeRange *oakcore_timerange_copy(const OakTimeRange *self)
{
	return wrap(new olive::core::internal::TimeRange(*impl(self)));
}

void oakcore_timerange_free(OakTimeRange *self)
{
	delete impl(self);
}

OakRational *oakcore_timerange_in(const OakTimeRange *self)
{
	return rwrap(new olive::core::internal::Rational(impl(self)->in()));
}

OakRational *oakcore_timerange_out(const OakTimeRange *self)
{
	return rwrap(new olive::core::internal::Rational(impl(self)->out()));
}

OakRational *oakcore_timerange_length(const OakTimeRange *self)
{
	return rwrap(new olive::core::internal::Rational(impl(self)->length()));
}

void oakcore_timerange_set_in(OakTimeRange *self, const OakRational *in)
{
	impl(self)->set_in(*rimpl(in));
}

void oakcore_timerange_set_out(OakTimeRange *self, const OakRational *out)
{
	impl(self)->set_out(*rimpl(out));
}

void oakcore_timerange_set_range(OakTimeRange *self, const OakRational *in,
								 const OakRational *out)
{
	impl(self)->set_range(*rimpl(in), *rimpl(out));
}

int oakcore_timerange_equal(const OakTimeRange *self,
							const OakTimeRange *other)
{
	return *impl(self) == *impl(other) ? 1 : 0;
}

int oakcore_timerange_overlaps_with(const OakTimeRange *self,
									const OakTimeRange *other,
									int in_inclusive, int out_inclusive)
{
	return impl(self)
				   ->overlaps_with(*impl(other), in_inclusive != 0,
								   out_inclusive != 0) ?
			   1 :
			   0;
}

int oakcore_timerange_contains_range(const OakTimeRange *self,
									 const OakTimeRange *other,
									 int in_inclusive, int out_inclusive)
{
	return impl(self)
				   ->contains(*impl(other), in_inclusive != 0,
							  out_inclusive != 0) ?
			   1 :
			   0;
}

int oakcore_timerange_contains_time(const OakTimeRange *self,
									const OakRational *time)
{
	return impl(self)->contains(*rimpl(time)) ? 1 : 0;
}

OakTimeRange *oakcore_timerange_combined(const OakTimeRange *self,
										 const OakTimeRange *other)
{
	return wrap(
		new olive::core::internal::TimeRange(impl(self)->combined(*impl(other))));
}

OakTimeRange *oakcore_timerange_combine(const OakTimeRange *a,
										const OakTimeRange *b)
{
	return wrap(new olive::core::internal::TimeRange(
		olive::core::internal::TimeRange::combine(*impl(a), *impl(b))));
}

OakTimeRange *oakcore_timerange_intersected(const OakTimeRange *self,
											const OakTimeRange *other)
{
	return wrap(new olive::core::internal::TimeRange(
		impl(self)->intersected(*impl(other))));
}

OakTimeRange *oakcore_timerange_intersect(const OakTimeRange *a,
										  const OakTimeRange *b)
{
	return wrap(new olive::core::internal::TimeRange(
		olive::core::internal::TimeRange::intersect(*impl(a), *impl(b))));
}

OakTimeRange *oakcore_timerange_add(const OakTimeRange *self,
									const OakRational *rhs)
{
	return wrap(
		new olive::core::internal::TimeRange(*impl(self) + *rimpl(rhs)));
}

OakTimeRange *oakcore_timerange_sub(const OakTimeRange *self,
									const OakRational *rhs)
{
	return wrap(
		new olive::core::internal::TimeRange(*impl(self) - *rimpl(rhs)));
}

void oakcore_timerange_add_assign(OakTimeRange *self, const OakRational *rhs)
{
	*impl(self) += *rimpl(rhs);
}

void oakcore_timerange_sub_assign(OakTimeRange *self, const OakRational *rhs)
{
	*impl(self) -= *rimpl(rhs);
}

int oakcore_timerange_split_count(const OakTimeRange *self, int chunk_size)
{
	return int(impl(self)->split(chunk_size).size());
}

int oakcore_timerange_split(const OakTimeRange *self, int chunk_size,
							OakTimeRange **out_ranges, int out_size)
{
	const std::list<olive::core::internal::TimeRange> ranges =
		impl(self)->split(chunk_size);
	int n = 0;
	for (const olive::core::internal::TimeRange &r : ranges) {
		if (out_ranges && n < out_size) {
			out_ranges[n] =
				wrap(new olive::core::internal::TimeRange(r));
		}
		n++;
	}
	return n;
}

} // extern "C"
