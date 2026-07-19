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

#ifndef OAK_LIBOLIVECORE_TIMERANGE_H
#define OAK_LIBOLIVECORE_TIMERANGE_H

#include <list>
#include <vector>

#include "rational.h"

namespace olive::core::internal
{

class TimeRange {
public:
	TimeRange() = default;
	TimeRange(const Rational &in, const Rational &out);
	TimeRange(const TimeRange &r)
		: TimeRange(r.in(), r.out())
	{
	}

	TimeRange &operator=(const TimeRange &r)
	{
		set_range(r.in(), r.out());
		return *this;
	}

	const Rational &in() const;
	const Rational &out() const;
	const Rational &length() const;

	void set_in(const Rational &in);
	void set_out(const Rational &out);
	void set_range(const Rational &in, const Rational &out);

	bool operator==(const TimeRange &r) const;
	bool operator!=(const TimeRange &r) const;

	bool overlaps_with(const TimeRange &a, bool in_inclusive = true,
					  bool out_inclusive = true) const;
	bool contains(const TimeRange &a, bool in_inclusive = true,
				  bool out_inclusive = true) const;
	bool contains(const Rational &r) const;

	TimeRange combined(const TimeRange &a) const;
	static TimeRange combine(const TimeRange &a, const TimeRange &b);
	TimeRange intersected(const TimeRange &a) const;
	static TimeRange intersect(const TimeRange &a, const TimeRange &b);

	TimeRange operator+(const Rational &rhs) const;
	TimeRange operator-(const Rational &rhs) const;

	const TimeRange &operator+=(const Rational &rhs);
	const TimeRange &operator-=(const Rational &rhs);

	std::list<TimeRange> split(const int &chunk_size) const;

private:
	void normalize();

	Rational in_;
	Rational out_;
	Rational length_;
};

class TimeRangeList {
public:
	TimeRangeList() = default;

	TimeRangeList(std::initializer_list<TimeRange> r)
		: array_(r)
	{
	}

	void insert(const TimeRangeList &list_to_add);
	void insert(TimeRange range_to_add);

	void remove(const TimeRange &remove);
	void remove(const TimeRangeList &list);

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
				  bool out_inclusive = true) const;

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

	void shift(const Rational &diff);

	void trim_in(const Rational &diff);

	void trim_out(const Rational &diff);

	TimeRangeList intersects(const TimeRange &range) const;

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

class TimeRangeListFrameIterator {
public:
	TimeRangeListFrameIterator();
	TimeRangeListFrameIterator(const TimeRangeList &list,
							   const Rational &timebase);

	Rational snap(const Rational &r) const;

	bool get_next(Rational *out);

	bool has_next() const;

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

	int size();

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
	void update_index_if_necessary();

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
