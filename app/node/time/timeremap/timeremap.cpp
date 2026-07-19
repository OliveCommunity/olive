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

#include "timeremap.h"

#include "node/sliderdisplaytype.h"

namespace olive
{

const QString TimeRemapNode::k_time_input = QStringLiteral("time_in");
const QString TimeRemapNode::k_input_input = QStringLiteral("input_in");

#define super Node

TimeRemapNode::TimeRemapNode()
{
	add_input(k_time_input, NodeValue::k_rational, QVariant::fromValue(Rational(0)),
			 InputFlags(k_input_flag_not_connectable));
	set_input_property(k_time_input, QStringLiteral("view"), slider::k_time);
	set_input_property(k_time_input, QStringLiteral("viewlock"), true);

	add_input(k_input_input, NodeValue::k_none,
			 InputFlags(k_input_flag_not_keyframable));
}

QString TimeRemapNode::name() const
{
	return tr("Time Remap");
}

QString TimeRemapNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.timeremap");
}

QVector<Node::CategoryID> TimeRemapNode::category() const
{
	return { k_category_time };
}

QString TimeRemapNode::description() const
{
	return tr("Arbitrarily remap time through the nodes.");
}

TimeRange TimeRemapNode::input_time_adjustment(const QString &input, int element,
											 const TimeRange &input_time,
											 bool clamp) const
{
	if (input == k_input_input) {
		return TimeRange(get_remapped_time(input_time.in()),
						 get_remapped_time(input_time.out()));
	} else {
		return super::input_time_adjustment(input, element, input_time, clamp);
	}
}

TimeRange TimeRemapNode::output_time_adjustment(const QString &input, int element,
											  const TimeRange &input_time) const
{
	/*if (input == kInputInput) {
    Rational target_time = GetValueAtTime(kTimeInput, input_time.in()).value<Rational>();

    return TimeRange(target_time, target_time + input_time.length());
  } else {
    return super::output_time_adjustment(input, element, input_time);
  }*/
	return super::output_time_adjustment(input, element, input_time);
}

void TimeRemapNode::retranslate()
{
	super::retranslate();

	set_input_name(k_time_input, QStringLiteral("Time"));
	set_input_name(k_input_input, QStringLiteral("Input"));
}

void TimeRemapNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						  NodeValueTable *table) const
{
	table->push(value[k_input_input]);
}

Rational TimeRemapNode::get_remapped_time(const Rational &input) const
{
	return get_value_at_time(k_time_input, input).value<Rational>();
}

}
