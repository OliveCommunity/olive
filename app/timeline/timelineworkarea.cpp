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

#include "common/xmlutils.h"

namespace olive
{

const Rational TimelineWorkArea::k_reset_in = 0;
const Rational TimelineWorkArea::k_reset_out = RATIONAL_MAX;

TimelineWorkArea::TimelineWorkArea(QObject *parent)
	: QObject(parent)
	, workarea_enabled_(false)
{
}

bool TimelineWorkArea::enabled() const
{
	return workarea_enabled_;
}

void TimelineWorkArea::set_enabled(bool e)
{
	workarea_enabled_ = e;
	emit enabled_changed(workarea_enabled_);
}

const TimeRange &TimelineWorkArea::range() const
{
	return workarea_range_;
}

void TimelineWorkArea::set_range(const TimeRange &range)
{
	workarea_range_ = range;
	emit range_changed(workarea_range_);
}

bool TimelineWorkArea::load(QXmlStreamReader *reader)
{
	Rational range_in = this->in();
	Rational range_out = this->out();

	uint version = 0;
	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("version")) {
			version = attr.value().toUInt();
		}
	}
	Q_UNUSED(version)

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("enabled")) {
			this->set_enabled(reader->readElementText() != QStringLiteral("0"));
		} else if (reader->name() == QStringLiteral("in")) {
			range_in =
				Rational::from_string(reader->readElementText().toStdString());
		} else if (reader->name() == QStringLiteral("out")) {
			range_out =
				Rational::from_string(reader->readElementText().toStdString());
		} else {
			reader->skipCurrentElement();
		}
	}

	TimeRange loaded_workarea(range_in, range_out);

	if (loaded_workarea != this->range()) {
		this->set_range(loaded_workarea);
	}

	return true;
}

void TimelineWorkArea::save(QXmlStreamWriter *writer) const
{
	writer->writeAttribute(QStringLiteral("version"), QString::number(1));

	writer->writeTextElement(QStringLiteral("enabled"),
							 QString::number(this->enabled()));
	writer->writeTextElement(QStringLiteral("in"),
							 QString::fromStdString(this->in().to_string()));
	writer->writeTextElement(QStringLiteral("out"),
							 QString::fromStdString(this->out().to_string()));
}

const Rational &TimelineWorkArea::in() const
{
	return workarea_range_.in();
}

const Rational &TimelineWorkArea::out() const
{
	return workarea_range_.out();
}

const Rational &TimelineWorkArea::length() const
{
	return workarea_range_.length();
}

}
