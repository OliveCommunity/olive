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

#include "param.h"

#include "node.h"

namespace olive
{

QString NodeInput::name() const
{
	if (is_valid()) {
		return node_->get_input_name(input_);
	} else {
		return QString();
	}
}

bool NodeInput::is_hidden() const
{
	if (is_valid()) {
		return node_->is_input_hidden(input_);
	} else {
		return false;
	}
}

bool NodeInput::is_connected() const
{
	if (is_valid()) {
		return node_->is_input_connected(*this);
	} else {
		return false;
	}
}

bool NodeInput::is_keyframing() const
{
	if (is_valid()) {
		return node_->is_input_keyframing(*this);
	} else {
		return false;
	}
}

bool NodeInput::is_array() const
{
	if (is_valid()) {
		return node_->input_is_array(input_);
	} else {
		return false;
	}
}

InputFlags NodeInput::get_flags() const
{
	if (is_valid()) {
		return node_->get_input_flags(input_);
	} else {
		return InputFlags(k_input_flag_normal);
	}
}

QString NodeInput::get_input_name() const
{
	if (is_valid()) {
		return node_->get_input_name(input_);
	} else {
		return QString();
	}
}

Node *NodeInput::get_connected_output() const
{
	if (is_valid()) {
		return node_->get_connected_output(*this);
	} else {
		return nullptr;
	}
}

NodeValue::Type NodeInput::get_data_type() const
{
	if (is_valid()) {
		return node_->get_input_data_type(input_);
	} else {
		return NodeValue::k_none;
	}
}

QVariant NodeInput::get_default_value() const
{
	if (is_valid()) {
		return node_->get_default_value(input_);
	} else {
		return QVariant();
	}
}

QStringList NodeInput::get_combo_box_strings() const
{
	if (is_valid()) {
		return node_->get_combo_box_strings(input_);
	} else {
		return QStringList();
	}
}

QVariant NodeInput::get_property(const QString &key) const
{
	if (is_valid()) {
		return node_->get_input_property(input_, key);
	} else {
		return QVariant();
	}
}

QHash<QString, QVariant> NodeInput::get_properties() const
{
	if (is_valid()) {
		return node_->get_input_properties(input_);
	} else {
		return QHash<QString, QVariant>();
	}
}

QVariant NodeInput::get_value_at_time(const Rational &time) const
{
	if (is_valid()) {
		return node_->get_value_at_time(*this, time);
	} else {
		return QVariant();
	}
}

NodeKeyframe *NodeInput::get_keyframe_at_time_on_track(const Rational &time,
												  int track) const
{
	if (is_valid()) {
		return node_->get_keyframe_at_time_on_track(*this, time, track);
	} else {
		return nullptr;
	}
}

QVariant NodeInput::get_split_default_value_for_track(int track) const
{
	if (is_valid()) {
		return node_->get_split_default_value_on_track(input_, track);
	} else {
		return QVariant();
	}
}

int NodeInput::get_array_size() const
{
	if (is_valid() && element_ == -1) {
		return node_->input_array_size(input_);
	} else {
		return 0;
	}
}

uint qHash(const NodeInput &i)
{
	return qHash(i.node()) ^ qHash(i.input()) ^ ::qHash(i.element());
}

uint qHash(const NodeKeyframeTrackReference &i)
{
	return qHash(i.input()) & ::qHash(i.track());
}

uint qHash(const NodeInputPair &i)
{
	return qHash(i.node) & qHash(i.input);
}

}
