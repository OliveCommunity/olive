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

#include "timelineworkarea.h"

#include <cstdlib>

namespace olive
{

const Rational TimelineWorkArea::k_reset_in = 0;
const Rational TimelineWorkArea::k_reset_out = RATIONAL_MAX;

TimelineWorkArea::TimelineWorkArea()
	: workarea_enabled_(false)
{
}

bool TimelineWorkArea::enabled() const
{
	return workarea_enabled_;
}

void TimelineWorkArea::set_enabled(bool e)
{
	workarea_enabled_ = e;
}

const TimeRange &TimelineWorkArea::range() const
{
	return workarea_range_;
}

void TimelineWorkArea::set_range(const TimeRange &range)
{
	workarea_range_ = range;
}

bool TimelineWorkArea::load(XmlStreamReader *reader)
{
	Rational range_in = this->in();
	Rational range_out = this->out();

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		// version is currently unused
		(void)attr;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "enabled") {
			this->set_enabled(reader->read_element_text() != "0");
		} else if (reader->name() == "in") {
			range_in = Rational::from_string(reader->read_element_text());
		} else if (reader->name() == "out") {
			range_out = Rational::from_string(reader->read_element_text());
		} else {
			reader->skip_current_element();
		}
	}

	TimeRange loaded_workarea(range_in, range_out);

	if (loaded_workarea != this->range()) {
		this->set_range(loaded_workarea);
	}

	return true;
}

void TimelineWorkArea::save(XmlStreamWriter *writer) const
{
	writer->write_attribute("version", "1");

	writer->write_text_element("enabled", std::to_string(this->enabled()));
	writer->write_text_element("in", this->in().to_string());
	writer->write_text_element("out", this->out().to_string());
}

Rational TimelineWorkArea::in() const
{
	return workarea_range_.in();
}

Rational TimelineWorkArea::out() const
{
	return workarea_range_.out();
}

Rational TimelineWorkArea::length() const
{
	return workarea_range_.length();
}

}
