/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
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

#include "timelinemarker.h"

#include <cstdlib>

#include "common/config.h"

namespace olive
{

TimelineMarker::TimelineMarker()
	: color_(oakcommon_config_get_int(NULL, "MarkerColor", 0))
	, parent_(nullptr)
{
}

TimelineMarker::TimelineMarker(int color, const TimeRange &time,
							   const std::string &name)
	: time_(time)
	, name_(name)
	, color_(color)
	, parent_(nullptr)
{
}

void TimelineMarker::set_time(const TimeRange &time)
{
	time_ = time;

	// Formerly the time_changed signal; the list must re-sort
	if (parent_) {
		parent_->resort(this);
	}
}

void TimelineMarker::set_time(const Rational &time)
{
	set_time(TimeRange(time, time + time_.length()));
}

bool TimelineMarker::has_sibling_at_time(const Rational &t) const
{
	if (!parent_) {
		return false;
	}

	TimelineMarker *m = parent_->get_marker_at_time(t);
	return m && m != this;
}

void TimelineMarker::set_name(const std::string &name)
{
	name_ = name;
}

void TimelineMarker::set_color(int c)
{
	color_ = c;
}

bool TimelineMarker::load(XmlStreamReader *reader)
{
	Rational in, out;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "name") {
			this->set_name(attr.value);
		} else if (attr.name == "in") {
			in = Rational::from_string(attr.value);
		} else if (attr.name == "out") {
			out = Rational::from_string(attr.value);
		} else if (attr.name == "color") {
			this->set_color(atoi(attr.value.c_str()));
		}
	}

	this->set_time(TimeRange(in, out));

	// This element has no inner text, so just skip it
	reader->skip_current_element();

	return true;
}

void TimelineMarker::save(XmlStreamWriter *writer) const
{
	writer->write_attribute("name", this->name());
	writer->write_attribute("in", this->time().in().to_string());
	writer->write_attribute("out", this->time().out().to_string());
	writer->write_attribute("color", std::to_string(this->color()));
}

bool TimelineMarkerList::load(XmlStreamReader *reader)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "marker") {
			auto marker = std::make_unique<TimelineMarker>();
			if (!marker->load(reader)) {
				return false;
			}
			this->add_marker(std::move(marker));
		} else {
			reader->skip_current_element();
		}
	}

	return true;
}

void TimelineMarkerList::save(XmlStreamWriter *writer) const
{
	for (const auto &marker : markers_) {
		writer->write_start_element("marker");

		marker->save(writer);

		writer->write_end_element(); // marker
	}
}

void TimelineMarkerList::add_marker(std::unique_ptr<TimelineMarker> marker)
{
	marker->parent_ = this;
	insert_into_list(std::move(marker));
}

std::unique_ptr<TimelineMarker>
TimelineMarkerList::remove_marker(TimelineMarker *m)
{
	for (auto it = markers_.begin(); it != markers_.end(); it++) {
		if (it->get() == m) {
			std::unique_ptr<TimelineMarker> removed = std::move(*it);
			markers_.erase(it);
			removed->parent_ = nullptr;
			return removed;
		}
	}

	return nullptr;
}

void TimelineMarkerList::resort(TimelineMarker *m)
{
	std::unique_ptr<TimelineMarker> holder = remove_marker(m);
	if (holder) {
		insert_into_list(std::move(holder));
	}
}

void TimelineMarkerList::insert_into_list(
	std::unique_ptr<TimelineMarker> marker)
{
	// Insertion sort by time to allow some loop optimizations
	for (auto it = markers_.begin(); it != markers_.end(); it++) {
		if ((*it)->time().in() > marker->time().in()) {
			markers_.insert(it, std::move(marker));
			return;
		}
	}

	markers_.push_back(std::move(marker));
}

MarkerAddCommand::MarkerAddCommand(TimelineMarkerList *marker_list,
								   const TimeRange &range,
								   const std::string &name, int color)
	: marker_list_(marker_list)
	, added_marker_(nullptr)
{
	memory_manager_ =
		std::make_unique<TimelineMarker>(color, range, name);
	added_marker_ = memory_manager_.get();
}

MarkerAddCommand::MarkerAddCommand(
	TimelineMarkerList *marker_list, std::unique_ptr<TimelineMarker> marker)
	: marker_list_(marker_list)
	, added_marker_(marker.get())
{
	memory_manager_ = std::move(marker);
}

void MarkerAddCommand::redo()
{
	if (!memory_manager_) {
		// Marker is already in the list (redo after redo)
		return;
	}
	marker_list_->add_marker(std::move(memory_manager_));
}

void MarkerAddCommand::undo()
{
	memory_manager_ = marker_list_->remove_marker(added_marker_);
}

MarkerRemoveCommand::MarkerRemoveCommand(TimelineMarker *marker,
										 TimelineMarkerList *marker_list)
	: marker_(marker)
	, marker_list_(marker_list)
{
}

void MarkerRemoveCommand::redo()
{
	memory_manager_ = marker_list_->remove_marker(marker_);
}

void MarkerRemoveCommand::undo()
{
	if (memory_manager_) {
		marker_list_->add_marker(std::move(memory_manager_));
	}
}

MarkerChangeColorCommand::MarkerChangeColorCommand(TimelineMarker *marker,
												   int new_color)
	: marker_(marker)
	, old_color_(marker->color())
	, new_color_(new_color)
{
}

void MarkerChangeColorCommand::redo()
{
	marker_->set_color(new_color_);
}

void MarkerChangeColorCommand::undo()
{
	marker_->set_color(old_color_);
}

MarkerChangeNameCommand::MarkerChangeNameCommand(TimelineMarker *marker,
												 std::string new_name)
	: marker_(marker)
	, old_name_(marker->name())
	, new_name_(std::move(new_name))
{
}

void MarkerChangeNameCommand::redo()
{
	marker_->set_name(new_name_);
}

void MarkerChangeNameCommand::undo()
{
	marker_->set_name(old_name_);
}

MarkerChangeTimeCommand::MarkerChangeTimeCommand(TimelineMarker *marker,
												 const TimeRange &time,
												 const TimeRange &old_time)
	: marker_(marker)
	, old_time_(old_time)
	, new_time_(time)
{
}

void MarkerChangeTimeCommand::redo()
{
	marker_->set_time(new_time_);
}

void MarkerChangeTimeCommand::undo()
{
	marker_->set_time(old_time_);
}

}
