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

#ifndef OAK_TIMELINEWORKAREA_H
#define OAK_TIMELINEWORKAREA_H

#include <olive/core/core.h>
#include <QObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace olive
{

using namespace core;

class TimelineWorkArea : public QObject {
	Q_OBJECT
public:
	TimelineWorkArea(QObject *parent = nullptr);

	bool enabled() const;
	void set_enabled(bool e);

	const Rational &in() const;
	const Rational &out() const;
	const Rational &length() const;
	const TimeRange &range() const;
	void set_range(const TimeRange &range);

	bool load(QXmlStreamReader *reader);
	void save(QXmlStreamWriter *writer) const;

	static const Rational k_reset_in;
	static const Rational k_reset_out;

signals:
	void enabled_changed(bool e);

	void range_changed(const TimeRange &r);

private:
	bool workarea_enabled_;

	TimeRange workarea_range_;
};

}

#endif // OAK_TIMELINEWORKAREA_H
