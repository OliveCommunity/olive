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

#ifndef OAK_LIBOLIVECORE_TIMERANGE_H
#define OAK_LIBOLIVECORE_TIMERANGE_H

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <list>
#include <vector>

#include "olive/core/oakcore/timerange.h"
#include "rational.h"
#include "timecodefunctions.h"

namespace olive::core
{

/**
 * @brief A range of time, normalized so that in() <= out()
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakTimeRange handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation, except that
 * the getters return Rational by value instead of by const reference.
 */
class TimeRange {
public:
	TimeRange()
		: handle_(oakcore_timerange_create())
	{
	}

	TimeRange(const Rational &in, const Rational &out)
		: handle_(oakcore_timerange_create_io(in.handle(), out.handle()))
	{
	}

	TimeRange(const TimeRange &r)
		: handle_(oakcore_timerange_copy(r.handle_))
	{
	}

	TimeRange(TimeRange &&r) noexcept
		: handle_(r.handle_)
	{
		r.handle_ = nullptr;
	}

	~TimeRange()
	{
		oakcore_timerange_free(handle_);
	}

	TimeRange &operator=(const TimeRange &r)
	{
		if (this != &r) {
			oakcore_timerange_free(handle_);
			handle_ = oakcore_timerange_copy(r.handle_);
		}
		return *this;
	}

	TimeRange &operator=(TimeRange &&r) noexcept
	{
		if (this != &r) {
			oakcore_timerange_free(handle_);
			handle_ = r.handle_;
			r.handle_ = nullptr;
		}
		return *this;
	}

	Rational in() const
	{
		return Rational::from_handle(oakcore_timerange_in(handle_));
	}

	Rational out() const
	{
		return Rational::from_handle(oakcore_timerange_out(handle_));
	}

	Rational length() const
	{
		return Rational::from_handle(oakcore_timerange_length(handle_));
	}

	void set_in(const Rational &in)
	{
		oakcore_timerange_set_in(handle_, in.handle());
	}

	void set_out(const Rational &out)
	{
		oakcore_timerange_set_out(handle_, out.handle());
	}

	void set_range(const Rational &in, const Rational &out)
	{
		oakcore_timerange_set_range(handle_, in.handle(), out.handle());
	}

	bool operator==(const TimeRange &r) const
	{
		return oakcore_timerange_equal(handle_, r.handle_) != 0;
	}

	bool operator!=(const TimeRange &r) const
	{
		return !(*this == r);
	}

	bool overlaps_with(const TimeRange &a, bool in_inclusive = true,
					  bool out_inclusive = true) const
	{
		return oakcore_timerange_overlaps_with(handle_, a.handle_,
											   in_inclusive ? 1 : 0,
											   out_inclusive ? 1 : 0) != 0;
	}

	bool contains(const TimeRange &a, bool in_inclusive = true,
				  bool out_inclusive = true) const
	{
		return oakcore_timerange_contains_range(handle_, a.handle_,
												in_inclusive ? 1 : 0,
												out_inclusive ? 1 : 0) != 0;
	}

	bool contains(const Rational &r) const
	{
		return oakcore_timerange_contains_time(handle_, r.handle()) != 0;
	}

	TimeRange combined(const TimeRange &a) const
	{
		return from_handle(oakcore_timerange_combined(handle_, a.handle_));
	}

	static TimeRange combine(const TimeRange &a, const TimeRange &b)
	{
		return from_handle(oakcore_timerange_combine(a.handle_, b.handle_));
	}

	TimeRange intersected(const TimeRange &a) const
	{
		return from_handle(oakcore_timerange_intersected(handle_, a.handle_));
	}

	static TimeRange intersect(const TimeRange &a, const TimeRange &b)
	{
		return from_handle(oakcore_timerange_intersect(a.handle_, b.handle_));
	}

	TimeRange operator+(const Rational &rhs) const
	{
		return from_handle(oakcore_timerange_add(handle_, rhs.handle()));
	}

	TimeRange operator-(const Rational &rhs) const
	{
		return from_handle(oakcore_timerange_sub(handle_, rhs.handle()));
	}

	const TimeRange &operator+=(const Rational &rhs)
	{
		oakcore_timerange_add_assign(handle_, rhs.handle());
		return *this;
	}

	const TimeRange &operator-=(const Rational &rhs)
	{
		oakcore_timerange_sub_assign(handle_, rhs.handle());
		return *this;
	}

	std::list<TimeRange> split(const int &chunk_size) const
	{
		const int count = oakcore_timerange_split_count(handle_, chunk_size);
		std::vector<OakTimeRange *> handles{size_t(count)};
		oakcore_timerange_split(handle_, chunk_size, handles.data(), count);

		std::list<TimeRange> ranges;
		for (OakTimeRange *h : handles) {
			ranges.push_back(from_handle(h));
		}
		return ranges;
	}

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakTimeRange *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static TimeRange from_handle(OakTimeRange *handle)
	{
		return TimeRange(handle);
	}

private:
	explicit TimeRange(OakTimeRange *handle)
		: handle_(handle)
	{
	}

	OakTimeRange *handle_;
};

/**
 * @brief A list of TimeRanges, kept merged and non-overlapping on insert()
 *
 * Consumer-side inline reimplementation over the wrapped TimeRange: every
 * operation is expressed through the public API of olive::core::TimeRange
 * and olive::core::Rational, so a container like this needs no C ABI surface
 * of its own. The public API is unchanged from the original implementation.
 */
class TimeRangeList {
public:
	TimeRangeList() = default;

	TimeRangeList(std::initializer_list<TimeRange> r)
		: array_(r)
	{
	}

	void insert(const TimeRangeList &list_to_add)
	{
		for (auto it = list_to_add.cbegin(); it != list_to_add.cend(); it++) {
			insert(*it);
		}
	}

	void insert(TimeRange range_to_add)
	{
		// See if list contains this range
		if (contains(range_to_add)) {
			return;
		}

		// Does not contain range, so we'll almost certainly be adding it in some way
		for (auto it = array_.begin(); it != array_.end();) {
			const TimeRange &compare = *it;

			if (compare.overlaps_with(range_to_add)) {
				range_to_add = TimeRange::combine(range_to_add, compare);
				it = array_.erase(it);
			} else {
				it++;
			}
		}

		array_.push_back(range_to_add);
	}

	void remove(const TimeRange &remove)
	{
		util_remove(&array_, remove);
	}

	void remove(const TimeRangeList &list)
	{
		for (const TimeRange &r : list) {
			remove(r);
		}
	}

	template <typename T>
	static void util_remove(std::vector<T> *list, const TimeRange &remove)
	{
		std::vector<T> additions;

		for (auto it = list->begin(); it != list->end();) {
			T &compare = *it;

			if (remove.contains(compare)) {
				// This element is entirely encompassed in this range, remove it
				it = list->erase(it);
			} else {
				if (compare.contains(remove, false, false)) {
					// The remove range is within this element, only choice is to split the element into two
					T new_range = compare;
					new_range.set_in(remove.out());
					compare.set_out(remove.in());

					additions.push_back(new_range);
					break;
				} else {
					if (compare.in() < remove.in() &&
						compare.out() > remove.in()) {
						// This element's out point overlaps the range's in, we'll trim it
						compare.set_out(remove.in());
					} else if (compare.in() < remove.out() &&
							   compare.out() > remove.out()) {
						// This element's in point overlaps the range's out, we'll trim it
						compare.set_in(remove.out());
					}

					it++;
				}
			}
		}

		list->insert(list->end(), additions.begin(), additions.end());
	}

	bool contains(const TimeRange &range, bool in_inclusive = true,
				  bool out_inclusive = true) const
	{
		for (int i = 0; i < size(); i++) {
			if (array_.at(i).contains(range, in_inclusive, out_inclusive)) {
				return true;
			}
		}

		return false;
	}

	bool contains(const Rational &r) const
	{
		for (const TimeRange &range : array_) {
			if (range.contains(r)) {
				return true;
			}
		}

		return false;
	}

	bool overlaps_with(const TimeRange &r, bool in_inclusive = true,
					  bool out_inclusive = true) const
	{
		for (const TimeRange &range : array_) {
			if (range.overlaps_with(r, in_inclusive, out_inclusive)) {
				return true;
			}
		}

		return false;
	}

	bool isEmpty() const
	{
		return array_.empty();
	}

	void clear()
	{
		array_.clear();
	}

	int size() const
	{
		return array_.size();
	}

	void shift(const Rational &diff)
	{
		for (size_t i = 0; i < array_.size(); i++) {
			array_[i] += diff;
		}
	}

	void trim_in(const Rational &diff)
	{
		// Re-do list since we want to handle overlaps
		TimeRangeList temp = *this;

		clear();

		for (auto it = temp.array_.begin(); it != temp.array_.end(); it++) {
			TimeRange &r = *it;
			r.set_in(r.in() + diff);
			insert(r);
		}
	}

	void trim_out(const Rational &diff)
	{
		// Re-do list since we want to handle overlaps
		TimeRangeList temp = *this;

		clear();

		for (auto it = temp.array_.begin(); it != temp.array_.end(); it++) {
			TimeRange &r = *it;
			r.set_out(r.out() + diff);
			insert(r);
		}
	}

	TimeRangeList intersects(const TimeRange &range) const
	{
		TimeRangeList intersect_list;

		for (int i = 0; i < size(); i++) {
			const TimeRange &compare = array_.at(i);

			if (compare.out() <= range.in() || compare.in() >= range.out()) {
				// No intersect
				continue;
			} else {
				// Crop the time range to the range and add it to the list
				TimeRange cropped(std::max(range.in(), compare.in()),
								  std::min(range.out(), compare.out()));

				intersect_list.insert(cropped);
			}
		}

		return intersect_list;
	}

	using const_iterator = std::vector<TimeRange>::const_iterator;

	const_iterator begin() const
	{
		return array_.cbegin();
	}

	const_iterator end() const
	{
		return array_.cend();
	}

	const_iterator cbegin() const
	{
		return begin();
	}

	const_iterator cend() const
	{
		return end();
	}

	const TimeRange &first() const
	{
		return array_.front();
	}

	const TimeRange &last() const
	{
		return array_.back();
	}

	const TimeRange &at(int index) const
	{
		return array_.at(index);
	}

	const std::vector<TimeRange> &internal_array() const
	{
		return array_;
	}

	bool operator==(const TimeRangeList &rhs) const
	{
		return array_ == rhs.array_;
	}

private:
	std::vector<TimeRange> array_;
};

/**
 * @brief Steps through a TimeRangeList frame by frame at a given timebase
 *
 * Consumer-side inline reimplementation: frame snapping and timestamp
 * conversion go through the wrapped olive::core::Timecode functions, the
 * range storage through the wrapped olive::core::TimeRangeList above. The
 * public API is unchanged from the original implementation.
 */
class TimeRangeListFrameIterator {
public:
	TimeRangeListFrameIterator()
		: TimeRangeListFrameIterator(TimeRangeList(), Rational::na_n)
	{
	}

	TimeRangeListFrameIterator(const TimeRangeList &list,
							   const Rational &timebase)
		: list_(list)
		, timebase_(timebase)
		, range_index_(-1)
		, size_(-1)
		, frame_index_(0)
		, custom_range_(false)
	{
		if (!list_.isEmpty() && timebase_.isNull()) {
			std::cerr
				<< "TimeRangeListFrameIterator created with null timebase but "
				   "non-empty list, this will likely lead to infinite loops"
				<< std::endl;
		}

		update_index_if_necessary();
	}

	Rational snap(const Rational &r) const
	{
		return Timecode::snap_time_to_timebase(r, timebase_, Timecode::k_floor);
	}

	bool get_next(Rational *out)
	{
		if (!has_next()) {
			return false;
		}

		// Output current value
		*out = current_;

		// Determine next value by adding timebase
		current_ += timebase_;

		// If this time is outside the current range, jump to the next one
		update_index_if_necessary();

		// Increment frame index
		frame_index_++;

		return true;
	}

	bool has_next() const
	{
		return range_index_ < list_.size();
	}

	std::vector<Rational> to_vector() const
	{
		TimeRangeListFrameIterator copy(list_, timebase_);
		std::vector<Rational> times;
		Rational r;
		while (copy.get_next(&r)) {
			times.push_back(r);
		}
		return times;
	}

	int size()
	{
		if (size_ == -1) {
			// Size isn't calculated automatically for optimization, so we'll calculate it now
			size_ = 0;

			for (const TimeRange &range : list_) {
				Rational start = snap(range.in());
				Rational end = Timecode::snap_time_to_timebase(
					range.out(), timebase_, Timecode::k_floor);

				if (end == range.out()) {
					end -= timebase_;
				}

				int64_t start_ts =
					Timecode::time_to_timestamp(start, timebase_);
				int64_t end_ts = Timecode::time_to_timestamp(end, timebase_);

				size_ += 1 + (end_ts - start_ts);
			}
		}

		return size_;
	}

	void reset()
	{
		*this = TimeRangeListFrameIterator();
	}

	void insert(const TimeRange &range)
	{
		list_.insert(range);
	}

	void insert(const TimeRangeList &list)
	{
		list_.insert(list);
	}

	bool is_custom_range() const
	{
		return custom_range_;
	}

	void set_custom_range(bool e)
	{
		custom_range_ = e;
	}

	int frame_index() const
	{
		return frame_index_;
	}

private:
	void update_index_if_necessary()
	{
		while (range_index_ < list_.size() &&
			   (range_index_ == -1 ||
				current_ >= list_.at(range_index_).out())) {
			range_index_++;

			if (range_index_ < list_.size()) {
				current_ = snap(list_.at(range_index_).in());
			}
		}
	}

	TimeRangeList list_;

	Rational timebase_;

	Rational current_;

	int range_index_;

	int size_;

	int frame_index_;

	bool custom_range_;
};

}

#endif // OAK_LIBOLIVECORE_TIMERANGE_H
