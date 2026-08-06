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

#include <algorithm>

#include "inputdragger.h"

#include "output/track/track.h"
#include "sliderdisplaytype.h"

namespace olive
{

#define super Node

const std::string Block::k_length_input = "length_in";

Block::Block()
	: previous_(nullptr)
	, next_(nullptr)
	, track_(nullptr)
{
	add_input(k_length_input, NodeValue::k_rational,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable |
						k_input_flag_hidden));
	set_input_property(k_length_input, "min",
					 Variant::from_value(Rational(0, 1)));
	set_input_property(k_length_input, "view",
					 slider::k_time);
	set_input_property(k_length_input, "viewlock", true);

	set_input_flag(k_enabled_input, k_input_flag_not_connectable);
	set_input_flag(k_enabled_input, k_input_flag_not_keyframable);

	set_flag(k_dont_show_in_param_view);
}

std::vector<Node::CategoryID> Block::category() const
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
	return get_standard_value(k_enabled_input).to_bool();
}

void Block::set_enabled(bool e)
{
	set_standard_value(k_enabled_input, e);
}

void Block::InputValueChangedEvent(const std::string &input, int element)
{
	super::InputValueChangedEvent(input, element);

	if (input == k_length_input && track_) {
		// Formerly the length_changed signal connected by Track::insert;
		// direct call after de-Qt (see DEQT.md §7)
		track_->block_length_changed(this);
	}
}

void Block::set_length_internal(const Rational &length)
{
	set_standard_value(k_length_input, Variant::from_value(length));
}

void Block::retranslate()
{
	super::retranslate();

	set_input_name(k_length_input, "Length");
	set_input_name(k_enabled_input, "Enabled");
}

void Block::invalidate_cache(const TimeRange &range, const std::string &from,
							int element, InvalidateCacheOptions options)
{
	TimeRange r;

	if (from == k_length_input) {
		// We must intercept the signal here
		r = TimeRange(std::min(length(), last_length_), RATIONAL_MAX);

		if (!NodeInputDragger::is_input_being_dragged()) {
			last_length_ = length();
		}

		options["lengthevent"] = true;
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
