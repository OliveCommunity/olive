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

#include "block.h"

#include <QDebug>

#include "node/inputdragger.h"
#include "node/sliderdisplaytype.h"

namespace olive
{

#define super Node

const QString Block::k_length_input = QStringLiteral("length_in");

Block::Block()
	: previous_(nullptr)
	, next_(nullptr)
	, track_(nullptr)
{
	add_input(k_length_input, NodeValue::k_rational,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable |
						k_input_flag_hidden));
	set_input_property(k_length_input, QStringLiteral("min"),
					 QVariant::fromValue(Rational(0, 1)));
	set_input_property(k_length_input, QStringLiteral("view"),
					 slider::k_time);
	set_input_property(k_length_input, QStringLiteral("viewlock"), true);

	set_input_flag(k_enabled_input, k_input_flag_not_connectable);
	set_input_flag(k_enabled_input, k_input_flag_not_keyframable);

	set_flag(k_dont_show_in_param_view);
}

QVector<Node::CategoryID> Block::category() const
{
	return { k_category_timeline };
}

Rational Block::length() const
{
	return get_standard_value(k_length_input).value<Rational>();
}

void Block::set_length_and_media_out(const Rational &length)
{
	if (length == this->length()) {
		return;
	}

	set_length_internal(length);
}

void Block::set_length_and_media_in(const Rational &length)
{
	if (length == this->length()) {
		return;
	}

	// Set the length without setting media out
	set_length_internal(length);
}

bool Block::is_enabled() const
{
	return get_standard_value(k_enabled_input).toBool();
}

void Block::set_enabled(bool e)
{
	set_standard_value(k_enabled_input, e);

	emit enabled_changed();
}

void Block::InputValueChangedEvent(const QString &input, int element)
{
	super::InputValueChangedEvent(input, element);

	if (input == k_length_input) {
		emit length_changed();
	} else if (input == k_enabled_input) {
		emit enabled_changed();
	}
}

void Block::set_length_internal(const Rational &length)
{
	set_standard_value(k_length_input, QVariant::fromValue(length));
}

void Block::retranslate()
{
	super::retranslate();

	set_input_name(k_length_input, tr("Length"));
	set_input_name(k_enabled_input, tr("Enabled"));
}

void Block::invalidate_cache(const TimeRange &range, const QString &from,
							int element, InvalidateCacheOptions options)
{
	TimeRange r;

	if (from == k_length_input) {
		// We must intercept the signal here
		r = TimeRange(qMin(length(), last_length_), RATIONAL_MAX);

		if (!NodeInputDragger::is_input_being_dragged()) {
			last_length_ = length();
		}

		options.insert(QStringLiteral("lengthevent"), true);
	} else {
		r = range;
	}

	super::invalidate_cache(r, from, element, options);
}

void Block::set_previous_next(Block *previous, Block *next)
{
	if (previous) {
		previous->set_next(next);
	}
	if (next) {
		next->set_previous(previous);
	}
}

}
