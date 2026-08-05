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

#include "keyframe.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "node.h"
#include "xmlutils.h"

namespace olive
{

const NodeKeyframe::Type NodeKeyframe::k_default_type = k_linear;

static std::string number_to_string(double d)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%g", d);
	return std::string(buf);
}

NodeKeyframe::NodeKeyframe(const Rational &time, const Variant &value,
						   Type type, int track, int element,
						   const std::string &input, Node *parent)
	: time_(time)
	, value_(value)
	, type_(type)
	, bezier_control_in_(PointF(0.0, 0.0))
	, bezier_control_out_(PointF(0.0, 0.0))
	, input_(input)
	, track_(track)
	, element_(element)
	, parent_(parent)
	, previous_(nullptr)
	, next_(nullptr)
{
}

NodeKeyframe::NodeKeyframe()
	: type_(NodeKeyframe::k_linear)
	, bezier_control_in_(PointF(0.0, 0.0))
	, bezier_control_out_(PointF(0.0, 0.0))
	, track_(-1)
	, element_(-1)
	, parent_(nullptr)
	, previous_(nullptr)
	, next_(nullptr)
{
}

NodeKeyframe::~NodeKeyframe()
{
}

NodeKeyframe *NodeKeyframe::copy(int element, Node *parent) const
{
	NodeKeyframe *copy =
		new NodeKeyframe(time_, value_, type_, track_, element, input_, parent);
	copy->bezier_control_in_ = bezier_control_in_;
	copy->bezier_control_out_ = bezier_control_out_;
	return copy;
}

NodeKeyframe *NodeKeyframe::copy(Node *parent) const
{
	return copy(element_, parent);
}

Node *NodeKeyframe::parent() const
{
	return parent_;
}

const Rational &NodeKeyframe::time() const
{
	return time_;
}

void NodeKeyframe::set_time(const Rational &time)
{
	time_ = time;
}

const Variant &NodeKeyframe::value() const
{
	return value_;
}

void NodeKeyframe::set_value(const Variant &value)
{
	value_ = value;
}

const NodeKeyframe::Type &NodeKeyframe::type() const
{
	return type_;
}

void NodeKeyframe::set_type(const NodeKeyframe::Type &type)
{
	if (type_ != type) {
		set_type_no_bezier_adj(type);

		if (type_ == k_bezier) {
			// Set some sane defaults if this keyframe existed in the track and was just changed
			if (bezier_control_in_.is_null()) {
				if (previous_) {
					// Set the in point to be half way between
					set_bezier_control_in(
						PointF((previous_->time().to_double() -
								this->time().to_double()) *
								   0.5,
							   0.0));
				} else {
					set_bezier_control_in(PointF(-1.0, 0.0));
				}
			}

			if (bezier_control_out_.is_null()) {
				if (next_) {
					set_bezier_control_out(PointF(
						(next_->time().to_double() - this->time().to_double()) *
							0.5,
						0.0));
				} else {
					set_bezier_control_out(PointF(1.0, 0.0));
				}
			}
		}
	}
}

void NodeKeyframe::set_type_no_bezier_adj(const Type &type)
{
	type_ = type;
}

const PointF &NodeKeyframe::bezier_control_in() const
{
	return bezier_control_in_;
}

void NodeKeyframe::set_bezier_control_in(const PointF &control)
{
	bezier_control_in_ = control;
}

const PointF &NodeKeyframe::bezier_control_out() const
{
	return bezier_control_out_;
}

void NodeKeyframe::set_bezier_control_out(const PointF &control)
{
	bezier_control_out_ = control;
}

PointF NodeKeyframe::valid_bezier_control_in() const
{
	double t = time().to_double();
	double adjusted_x = t + bezier_control_in_.x();

	if (previous_) {
		// Limit to the point of that keyframe
		adjusted_x = std::max(adjusted_x, previous_->time().to_double());
	}

	return PointF(adjusted_x - t, bezier_control_in_.y());
}

PointF NodeKeyframe::valid_bezier_control_out() const
{
	double t = time().to_double();
	double adjusted_x = t + bezier_control_out_.x();

	if (next_) {
		// Limit to the point of that keyframe
		adjusted_x = std::min(adjusted_x, next_->time().to_double());
	}

	return PointF(adjusted_x - t, bezier_control_out_.y());
}

const PointF &NodeKeyframe::bezier_control(NodeKeyframe::BezierType type) const
{
	if (type == k_in_handle) {
		return bezier_control_in();
	} else {
		return bezier_control_out();
	}
}

void NodeKeyframe::set_bezier_control(NodeKeyframe::BezierType type,
									  const PointF &control)
{
	if (type == k_in_handle) {
		set_bezier_control_in(control);
	} else {
		set_bezier_control_out(control);
	}
}

NodeKeyframe::BezierType
NodeKeyframe::get_opposing_bezier_type(NodeKeyframe::BezierType type)
{
	if (type == k_in_handle) {
		return k_out_handle;
	} else {
		return k_in_handle;
	}
}

bool NodeKeyframe::has_sibling_at_time(const Rational &t) const
{
	NodeKeyframe *k =
		parent()->get_keyframe_at_time_on_track(input(), t, track(), element());
	return k && k != this;
}

bool NodeKeyframe::load(XmlStreamReader *reader, NodeValue::Type data_type)
{
	std::string key_input;
	PointF key_in_handle;
	PointF key_out_handle;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "input") {
			key_input = attr.value;
		} else if (attr.name == "time") {
			this->set_time(Rational::from_string(attr.value));
		} else if (attr.name == "type") {
			this->set_type_no_bezier_adj(static_cast<NodeKeyframe::Type>(
				strtol(attr.value.c_str(), nullptr, 10)));
		} else if (attr.name == "inhandlex") {
			key_in_handle.set_x(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "inhandley") {
			key_in_handle.set_y(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "outhandlex") {
			key_out_handle.set_x(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "outhandley") {
			key_out_handle.set_y(strtod(attr.value.c_str(), nullptr));
		}
	}

	this->set_value(
		NodeValue::string_to_value(data_type, reader->read_element_text(), true));

	if (!key_input.empty()) {
		this->set_input(key_input);
	}

	this->set_bezier_control_in(key_in_handle);
	this->set_bezier_control_out(key_out_handle);

	return true;
}

void NodeKeyframe::save(XmlStreamWriter *writer,
						NodeValue::Type data_type) const
{
	writer->write_attribute("input", this->input());
	writer->write_attribute("time", this->time().to_string());
	writer->write_attribute("type", std::to_string(this->type()));
	writer->write_attribute("inhandlex",
							number_to_string(this->bezier_control_in().x()));
	writer->write_attribute("inhandley",
							number_to_string(this->bezier_control_in().y()));
	writer->write_attribute("outhandlex",
							number_to_string(this->bezier_control_out().x()));
	writer->write_attribute("outhandley",
							number_to_string(this->bezier_control_out().y()));

	writer->write_characters(
		NodeValue::value_to_string(data_type, this->value(), true));
}

}
