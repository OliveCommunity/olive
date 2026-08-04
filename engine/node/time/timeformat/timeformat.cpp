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

#include "timeformat.h"

#include <QDateTime>
#include <QTimeZone>

namespace olive
{

#define super Node

const QString TimeFormatNode::k_time_input = QStringLiteral("time_in");
const QString TimeFormatNode::k_format_input = QStringLiteral("format_in");
const QString TimeFormatNode::k_local_time_input = QStringLiteral("localtime_in");

TimeFormatNode::TimeFormatNode()
{
	add_input(k_time_input, NodeValue::k_float);
	add_input(k_format_input, NodeValue::k_text, QStringLiteral("hh:mm:ss"));
	add_input(k_local_time_input, NodeValue::k_boolean);
}

QString TimeFormatNode::name() const
{
	return tr("Time Format");
}

QString TimeFormatNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.timeformat");
}

QVector<Node::CategoryID> TimeFormatNode::category() const
{
	return { k_category_generator };
}

QString TimeFormatNode::description() const
{
	return tr("Format time (in Unix epoch seconds) into a string.");
}

void TimeFormatNode::retranslate()
{
	super::retranslate();

	set_input_name(k_time_input, tr("Time"));
	set_input_name(k_format_input, tr("Format"));
	set_input_name(k_local_time_input, tr("Interpret time as local time"));
}

void TimeFormatNode::value(const NodeValueRow &value,
						   const NodeGlobals &globals,
						   NodeValueTable *table) const
{
	qint64 ms_since_epoch = value[k_time_input].to_double() * 1000;
	bool time_is_local = value[k_local_time_input].to_bool();
	QDateTime dt = QDateTime::fromMSecsSinceEpoch(
		ms_since_epoch,
		time_is_local ? QTimeZone::systemTimeZone() : QTimeZone::utc());
	QString format = value[k_format_input].to_string();
	QString output = dt.toString(format);
	table->push(NodeValue(NodeValue::k_text, output, this));
}

}
