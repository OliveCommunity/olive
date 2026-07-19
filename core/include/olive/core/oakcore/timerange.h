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

#ifndef OAKCORE_TIMERANGE_H
#define OAKCORE_TIMERANGE_H

#include "export.h"
#include "rational.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file timerange.h
 * @brief C ABI for the time range value type
 *
 * Opaque handle + free functions. All returned OakTimeRange and OakRational
 * pointers are owned by the caller and must be released with
 * oakcore_timerange_free() and oakcore_rational_free() respectively.
 */
typedef struct OakTimeRange OakTimeRange;

OAKCORE_API OakTimeRange *oakcore_timerange_create(void);
OAKCORE_API OakTimeRange *oakcore_timerange_create_io(const OakRational *in,
													  const OakRational *out);
OAKCORE_API OakTimeRange *oakcore_timerange_copy(const OakTimeRange *self);
OAKCORE_API void oakcore_timerange_free(OakTimeRange *self);

OAKCORE_API OakRational *oakcore_timerange_in(const OakTimeRange *self);
OAKCORE_API OakRational *oakcore_timerange_out(const OakTimeRange *self);
OAKCORE_API OakRational *oakcore_timerange_length(const OakTimeRange *self);

OAKCORE_API void oakcore_timerange_set_in(OakTimeRange *self,
										  const OakRational *in);
OAKCORE_API void oakcore_timerange_set_out(OakTimeRange *self,
										   const OakRational *out);
OAKCORE_API void oakcore_timerange_set_range(OakTimeRange *self,
											 const OakRational *in,
											 const OakRational *out);

OAKCORE_API int oakcore_timerange_equal(const OakTimeRange *self,
										const OakTimeRange *other);

OAKCORE_API int oakcore_timerange_overlaps_with(const OakTimeRange *self,
												const OakTimeRange *other,
												int in_inclusive,
												int out_inclusive);
OAKCORE_API int oakcore_timerange_contains_range(const OakTimeRange *self,
												 const OakTimeRange *other,
												 int in_inclusive,
												 int out_inclusive);
OAKCORE_API int oakcore_timerange_contains_time(const OakTimeRange *self,
												const OakRational *time);

OAKCORE_API OakTimeRange *oakcore_timerange_combined(
	const OakTimeRange *self, const OakTimeRange *other);
OAKCORE_API OakTimeRange *oakcore_timerange_combine(const OakTimeRange *a,
													const OakTimeRange *b);
OAKCORE_API OakTimeRange *oakcore_timerange_intersected(
	const OakTimeRange *self, const OakTimeRange *other);
OAKCORE_API OakTimeRange *oakcore_timerange_intersect(const OakTimeRange *a,
													  const OakTimeRange *b);

OAKCORE_API OakTimeRange *oakcore_timerange_add(const OakTimeRange *self,
												const OakRational *rhs);
OAKCORE_API OakTimeRange *oakcore_timerange_sub(const OakTimeRange *self,
												const OakRational *rhs);
OAKCORE_API void oakcore_timerange_add_assign(OakTimeRange *self,
											  const OakRational *rhs);
OAKCORE_API void oakcore_timerange_sub_assign(OakTimeRange *self,
											  const OakRational *rhs);

/**
 * Splits the range into chunks of chunk_size (the first and last chunk are
 * clamped to the range bounds) and writes a newly allocated owned handle per
 * chunk into out_ranges. Returns the total number of chunks, so
 * out_ranges == NULL (or a too-small array) can be used to query the
 * required size; oakcore_timerange_split_count() is the direct equivalent
 * of that query.
 */
OAKCORE_API int oakcore_timerange_split_count(const OakTimeRange *self,
											  int chunk_size);
OAKCORE_API int oakcore_timerange_split(const OakTimeRange *self,
										int chunk_size,
										OakTimeRange **out_ranges,
										int out_size);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_TIMERANGE_H */
