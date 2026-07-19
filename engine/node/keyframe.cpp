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

#include "node.h"

namespace olive
{

const NodeKeyframe::Type NodeKeyframe::k_default_type = k_linear;

NodeKeyframe::NodeKeyframe(const Rational &time, const QVariant &value,
						   Type type, int track, int element,
						   const QString &input, QObject *parent)
	: time_(time)
	, value_(value)
	, type_(type)
	, bezier_control_in_(QPointF(0.0, 0.0))
	, bezier_control_out_(QPointF(0.0, 0.0))
	, input_(input)
	, track_(track)
	, element_(element)
	, previous_(nullptr)
	, next_(nullptr)
{
	setParent(parent);
}

NodeKeyframe::NodeKeyframe()
	: type_(NodeKeyframe::k_linear)
	, bezier_control_in_(QPointF(0.0, 0.0))
	, bezier_control_out_(QPointF(0.0, 0.0))
	, track_(-1)
	, element_(-1)
	, previous_(nullptr)
	, next_(nullptr)
{
}

NodeKeyframe::~NodeKeyframe()
{
	setParent(nullptr);
}

NodeKeyframe *NodeKeyframe::copy(int element, QObject *parent) const
{
	NodeKeyframe *copy =
		new NodeKeyframe(time_, value_, type_, track_, element, input_, parent);
	copy->bezier_control_in_ = bezier_control_in_;
	copy->bezier_control_out_ = bezier_control_out_;
	return copy;
}

NodeKeyframe *NodeKeyframe::copy(QObject *parent) const
{
	return copy(element_, parent);
}

Node *NodeKeyframe::parent() const
{
	return static_cast<Node *>(QObject::parent());
}

const Rational &NodeKeyframe::time() const
{
	return time_;
}

void NodeKeyframe::set_time(const Rational &time)
{
	time_ = time;
	emit time_changed(time_);
}

const QVariant &NodeKeyframe::value() const
{
	return value_;
}

void NodeKeyframe::set_value(const QVariant &value)
{
	value_ = value;
	emit value_changed(value_);
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
			if (bezier_control_in_.isNull()) {
				if (previous_) {
					// Set the in point to be half way between
					set_bezier_control_in(
						QPointF((previous_->time().to_double() -
								 this->time().to_double()) *
									0.5,
								0.0));
				} else {
					set_bezier_control_in(QPointF(-1.0, 0.0));
				}
			}

			if (bezier_control_out_.isNull()) {
				if (next_) {
					set_bezier_control_out(QPointF(
						(next_->time().to_double() - this->time().to_double()) *
							0.5,
						0.0));
				} else {
					set_bezier_control_out(QPointF(1.0, 0.0));
				}
			}
		}
	}
}

void NodeKeyframe::set_type_no_bezier_adj(const Type &type)
{
	type_ = type;
	emit type_changed(type_);
}

const QPointF &NodeKeyframe::bezier_control_in() const
{
	return bezier_control_in_;
}

void NodeKeyframe::set_bezier_control_in(const QPointF &control)
{
	bezier_control_in_ = control;
	emit bezier_control_in_changed(bezier_control_in_);
}

const QPointF &NodeKeyframe::bezier_control_out() const
{
	return bezier_control_out_;
}

void NodeKeyframe::set_bezier_control_out(const QPointF &control)
{
	bezier_control_out_ = control;
	emit bezier_control_out_changed(bezier_control_out_);
}

QPointF NodeKeyframe::valid_bezier_control_in() const
{
	double t = time().to_double();
	qreal adjusted_x = t + bezier_control_in_.x();

	if (previous_) {
		// Limit to the point of that keyframe
		adjusted_x = qMax(adjusted_x, previous_->time().to_double());
	}

	return QPointF(adjusted_x - t, bezier_control_in_.y());
}

QPointF NodeKeyframe::valid_bezier_control_out() const
{
	double t = time().to_double();
	qreal adjusted_x = t + bezier_control_out_.x();

	if (next_) {
		// Limit to the point of that keyframe
		adjusted_x = qMin(adjusted_x, next_->time().to_double());
	}

	return QPointF(adjusted_x - t, bezier_control_out_.y());
}

const QPointF &NodeKeyframe::bezier_control(NodeKeyframe::BezierType type) const
{
	if (type == k_in_handle) {
		return bezier_control_in();
	} else {
		return bezier_control_out();
	}
}

void NodeKeyframe::set_bezier_control(NodeKeyframe::BezierType type,
									  const QPointF &control)
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

bool NodeKeyframe::load(QXmlStreamReader *reader, NodeValue::Type data_type)
{
	QString key_input;
	QPointF key_in_handle;
	QPointF key_out_handle;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("input")) {
			key_input = attr.value().toString();
		} else if (attr.name() == QStringLiteral("time")) {
			this->set_time(
				Rational::from_string(attr.value().toString().toStdString()));
		} else if (attr.name() == QStringLiteral("type")) {
			this->set_type_no_bezier_adj(
				static_cast<NodeKeyframe::Type>(attr.value().toInt()));
		} else if (attr.name() == QStringLiteral("inhandlex")) {
			key_in_handle.setX(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("inhandley")) {
			key_in_handle.setY(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("outhandlex")) {
			key_out_handle.setX(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("outhandley")) {
			key_out_handle.setY(attr.value().toDouble());
		}
	}

	this->set_value(
		NodeValue::string_to_value(data_type, reader->readElementText(), true));

	if (!key_input.isEmpty()) {
		this->set_input(key_input);
	}

	this->set_bezier_control_in(key_in_handle);
	this->set_bezier_control_out(key_out_handle);

	return true;
}

void NodeKeyframe::save(QXmlStreamWriter *writer,
						NodeValue::Type data_type) const
{
	writer->writeAttribute(QStringLiteral("input"), this->input());
	writer->writeAttribute(QStringLiteral("time"),
						   QString::fromStdString(this->time().to_string()));
	writer->writeAttribute(QStringLiteral("type"),
						   QString::number(this->type()));
	writer->writeAttribute(QStringLiteral("inhandlex"),
						   QString::number(this->bezier_control_in().x()));
	writer->writeAttribute(QStringLiteral("inhandley"),
						   QString::number(this->bezier_control_in().y()));
	writer->writeAttribute(QStringLiteral("outhandlex"),
						   QString::number(this->bezier_control_out().x()));
	writer->writeAttribute(QStringLiteral("outhandley"),
						   QString::number(this->bezier_control_out().y()));

	writer->writeCharacters(
		NodeValue::value_to_string(data_type, this->value(), true));
}

}
