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

#include "util/timerange.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "util/timecodefunctions.h"

namespace olive::core::internal
{

TimeRange::TimeRange(const Rational &in, const Rational &out)
	: in_(in)
	, out_(out)
{
	normalize();
}

const Rational &TimeRange::in() const
{
	return in_;
}

const Rational &TimeRange::out() const
{
	return out_;
}

const Rational &TimeRange::length() const
{
	return length_;
}

void TimeRange::set_in(const Rational &in)
{
	in_ = in;
	normalize();
}

void TimeRange::set_out(const Rational &out)
{
	out_ = out;
	normalize();
}

void TimeRange::set_range(const Rational &in, const Rational &out)
{
	in_ = in;
	out_ = out;
	normalize();
}

bool TimeRange::operator==(const TimeRange &r) const
{
	return in() == r.in() && out() == r.out();
}

bool TimeRange::operator!=(const TimeRange &r) const
{
	return in() != r.in() || out() != r.out();
}

bool TimeRange::overlaps_with(const TimeRange &a, bool in_inclusive,
							 bool out_inclusive) const
{
	bool doesnt_overlap_in = (in_inclusive) ? (a.out() < in()) :
											  (a.out() <= in());

	bool doesnt_overlap_out = (out_inclusive) ? (a.in() > out()) :
												(a.in() >= out());

	return !doesnt_overlap_in && !doesnt_overlap_out;
}

TimeRange TimeRange::combined(const TimeRange &a) const
{
	return combine(a, *this);
}

bool TimeRange::contains(const TimeRange &compare, bool in_inclusive,
						 bool out_inclusive) const
{
	bool contains_in = (in_inclusive) ? (compare.in() >= in()) :
										(compare.in() > in());

	bool contains_out = (out_inclusive) ? (compare.out() <= out()) :
										  (compare.out() < out());

	return contains_in && contains_out;
}

bool TimeRange::contains(const Rational &r) const
{
	return r >= in_ && r < out_;
}

TimeRange TimeRange::combine(const TimeRange &a, const TimeRange &b)
{
	return TimeRange(std::min(a.in(), b.in()), std::max(a.out(), b.out()));
}

TimeRange TimeRange::intersected(const TimeRange &a) const
{
	return intersect(a, *this);
}

TimeRange TimeRange::intersect(const TimeRange &a, const TimeRange &b)
{
	return TimeRange(std::max(a.in(), b.in()), std::min(a.out(), b.out()));
}

TimeRange TimeRange::operator+(const Rational &rhs) const
{
	TimeRange answer(*this);
	answer += rhs;
	return answer;
}

TimeRange TimeRange::operator-(const Rational &rhs) const
{
	TimeRange answer(*this);
	answer -= rhs;
	return answer;
}

const TimeRange &TimeRange::operator+=(const Rational &rhs)
{
	set_range(in_ + rhs, out_ + rhs);

	return *this;
}

const TimeRange &TimeRange::operator-=(const Rational &rhs)
{
	set_range(in_ - rhs, out_ - rhs);

	return *this;
}

std::list<TimeRange> TimeRange::split(const int &chunk_size) const
{
	std::list<TimeRange> split_ranges;

	int start_time =
		std::floor(this->in().to_double() / static_cast<double>(chunk_size)) *
		chunk_size;
	int end_time =
		std::ceil(this->out().to_double() / static_cast<double>(chunk_size)) *
		chunk_size;

	for (int i = start_time; i < end_time; i += chunk_size) {
		split_ranges.push_back(
			TimeRange(std::max(this->in(), Rational(i)),
					  std::min(this->out(), Rational(i + chunk_size))));
	}

	return split_ranges;
}

void TimeRange::normalize()
{
	// If `out` is earlier than `in`, swap them
	if (out_ < in_) {
		std::swap(out_, in_);
	}

	// Calculate length
	if (out_ == RATIONAL_MIN || out_ == RATIONAL_MAX || in_ == RATIONAL_MIN ||
		in_ == RATIONAL_MAX) {
		length_ = Rational::na_n;
	} else {
		length_ = out_ - in_;
	}
}

void TimeRangeList::insert(const TimeRangeList &list_to_add)
{
	for (auto it = list_to_add.cbegin(); it != list_to_add.cend(); it++) {
		insert(*it);
	}
}

void TimeRangeList::insert(TimeRange range_to_add)
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

void TimeRangeList::remove(const TimeRange &remove)
{
	util_remove(&array_, remove);
}

void TimeRangeList::remove(const TimeRangeList &list)
{
	for (const TimeRange &r : list) {
		remove(r);
	}
}

bool TimeRangeList::contains(const TimeRange &range, bool in_inclusive,
							 bool out_inclusive) const
{
	for (int i = 0; i < size(); i++) {
		if (array_.at(i).contains(range, in_inclusive, out_inclusive)) {
			return true;
		}
	}

	return false;
}

void TimeRangeList::shift(const Rational &diff)
{
	for (int i = 0; i < array_.size(); i++) {
		array_[i] += diff;
	}
}

void TimeRangeList::trim_in(const Rational &diff)
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

void TimeRangeList::trim_out(const Rational &diff)
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

TimeRangeList TimeRangeList::intersects(const TimeRange &range) const
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

TimeRangeListFrameIterator::TimeRangeListFrameIterator()
	: TimeRangeListFrameIterator(TimeRangeList(), Rational::na_n)
{
}

TimeRangeListFrameIterator::TimeRangeListFrameIterator(
	const TimeRangeList &list, const Rational &timebase)
	: list_(list)
	, timebase_(timebase)
	, range_index_(-1)
	, size_(-1)
	, frame_index_(0)
	, custom_range_(false)
{
	if (!list_.isEmpty() && timebase_.isNull()) {
		std::cerr
			<< "TimeRangeListFrameIterator created with null timebase but non-empty list, this will likely lead to infinite loops"
			<< std::endl;
	}

	update_index_if_necessary();
}

Rational TimeRangeListFrameIterator::snap(const Rational &r) const
{
	return Timecode::snap_time_to_timebase(r, timebase_, Timecode::k_floor);
}

bool TimeRangeListFrameIterator::get_next(Rational *out)
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

bool TimeRangeListFrameIterator::has_next() const
{
	return range_index_ < list_.size();
}

int TimeRangeListFrameIterator::size()
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

			int64_t start_ts = Timecode::time_to_timestamp(start, timebase_);
			int64_t end_ts = Timecode::time_to_timestamp(end, timebase_);

			size_ += 1 + (end_ts - start_ts);
		}
	}

	return size_;
}

void TimeRangeListFrameIterator::update_index_if_necessary()
{
	while (range_index_ < list_.size() &&
		   (range_index_ == -1 || current_ >= list_.at(range_index_).out())) {
		range_index_++;

		if (range_index_ < list_.size()) {
			current_ = snap(list_.at(range_index_).in());
		}
	}
}

}
