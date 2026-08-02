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

#include "node.h"

#ifndef _WIN32
#include <execinfo.h>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QDebug>
#include <QFile>

#include "common/lerp.h"
#include "config/config.h"
#include "node/group/group.h"
#include "node/project/serializer/typeserializer.h"
#include "nodeundo.h"
#include "project.h"
#include "serializeddata.h"
#include "ui/colorcoding.h"

namespace olive
{

#define super QObject

const QString Node::k_enabled_input = QStringLiteral("enabled_in");

Node::Node()
	: override_color_(-1)
	, folder_(nullptr)
	, flags_(k_none)
	, caches_enabled_(true)
{
	add_input(k_enabled_input, NodeValue::k_boolean, true);

	video_cache_ = new FrameHashCache(this);
	thumbnail_cache_ = new ThumbnailCache(this);
	audio_cache_ = new AudioPlaybackCache(this);
	waveform_cache_ = new AudioWaveformCache(this);

	waveform_cache_->set_saving_enabled(false);
}

Node::~Node()
{
	// Disconnect all edges
	disconnect_all();

	// Remove self from anything while we're still a node rather than a base QObject
	setParent(nullptr);

	// Remove all immediates
	foreach (NodeInputImmediate *i, standard_immediates_) {
		delete i;
	}
	for (auto it = array_immediates_.cbegin(); it != array_immediates_.cend();
		 it++) {
		foreach (NodeInputImmediate *i, it.value()) {
			delete i;
		}
	}
}

Project *Node::parent() const
{
	return static_cast<Project *>(QObject::parent());
}

Project *Node::project() const
{
	return Project::get_project_from_object(this);
}

QString Node::short_name() const
{
	return name();
}

QString Node::description() const
{
	// Return an empty string by default
	return QString();
}

void Node::retranslate()
{
	set_input_name(k_enabled_input, tr("Enabled"));
}

QVariant Node::data(const DataType &d) const
{
	if (d == icon) {
		// Just a meaningless default icon to be used where necessary
		return QStringLiteral("new");
	}

	return QVariant();
}

bool Node::set_node_position_in_context(Node *node, const QPointF &pos)
{
	Position p = context_positions_.value(node);

	p.position = pos;

	return set_node_position_in_context(node, p);
}

bool Node::set_node_position_in_context(Node *node, const Position &pos)
{
	bool added = !context_contains_node(node);
	context_positions_.insert(node, pos);

	if (added) {
		emit node_added_to_context(node);
	}

	emit node_position_in_context_changed(node, pos.position);

	return added;
}

bool Node::remove_node_from_context(Node *node)
{
	if (context_contains_node(node)) {
		context_positions_.remove(node);
		emit node_removed_from_context(node);
		return true;
	} else {
		return false;
	}
}

Color Node::color() const
{
	int c;

	if (override_color_ >= 0) {
		c = override_color_;
	} else {
		c = OAK_CONFIG_STR(
				QStringLiteral("CatColor%1").arg(this->category().first()))
				.toInt();
	}

	return ColorCoding::get_color(c);
}

QLinearGradient Node::gradient_color(qreal top, qreal bottom) const
{
	QLinearGradient grad;

	grad.setStart(0, top);
	grad.setFinalStop(0, bottom);

	QColor c = QtUtils::to_q_color(color());

	grad.setColorAt(0.0, c.lighter());
	grad.setColorAt(1.0, c);

	return grad;
}

QBrush Node::brush(qreal top, qreal bottom) const
{
	if (OAK_CONFIG("UseGradients").toBool()) {
		return gradient_color(top, bottom);
	} else {
		return QtUtils::to_q_color(color());
	}
}

void Node::connect_edge(Node *output, const NodeInput &input)
{
	// Ensure graph is the same
	Q_ASSERT(input.node()->parent() == output->parent());

	// Ensure a connection isn't getting overwritten
	Q_ASSERT(input.node()->input_connections().find(input) ==
			 input.node()->input_connections().end());

	// Insert connection on both sides
	input.node()->input_connections_[input] = output;
	output->output_connections_.push_back(
		std::pair<Node *, NodeInput>({ output, input }));

	// Call internal events
	input.node()->InputConnectedEvent(input.input(), input.element(), output);
	output->OutputConnectedEvent(input);

	// Emit signals
	emit input.node()->input_connected(output, input);
	emit output->output_connected(output, input);

	// Invalidate all if this node isn't ignoring this input
	if (!(input.node()->get_input_flags(input.input()) &
		  k_input_flag_ignore_invalidations)) {
		input.node()->invalidate_all(input.input(), input.element());
	}
}

void Node::disconnect_edge(Node *output, const NodeInput &input, bool silent)
{
	// Ensure graph is the same
	Q_ASSERT(input.node()->parent() == output->parent());

	// Ensure connection exists
	Q_ASSERT(input.node()->input_connections().at(input) == output);

	// Remove connection from both sides
	InputConnections &inputs = input.node()->input_connections_;
	inputs.erase(inputs.find(input));

	OutputConnections &outputs = output->output_connections_;
	outputs.erase(std::find(outputs.begin(), outputs.end(),
							std::pair<Node *, NodeInput>({ output, input })));

	if (silent) {
		// Teardown-only mode: just drop the edge from both maps. No events,
		// no signals, no invalidation — the whole graph is being destroyed
		// and handlers would touch half-destroyed nodes.
		return;
	}

	if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
		qWarning("EDGE-DEBUG: disconnect_edge %p -> %p (%s)", (void *)output,
				 (void *)input.node(), qPrintable(input.input()));
	}

	// Call internal events
	input.node()->InputDisconnectedEvent(input.input(), input.element(),
										 output);
	output->OutputDisconnectedEvent(input);

	emit input.node()->input_disconnected(output, input);
	emit output->output_disconnected(output, input);

	if (!(input.node()->get_input_flags(input.input()) &
		  k_input_flag_ignore_invalidations)) {
		input.node()->invalidate_all(input.input(), input.element());
	}
}

void Node::copy_cache_uuids_from(Node *n)
{
	video_cache_->set_uuid(n->video_cache_->get_uuid());
	audio_cache_->set_uuid(n->audio_cache_->get_uuid());
	thumbnail_cache_->set_uuid(n->thumbnail_cache_->get_uuid());
	waveform_cache_->set_uuid(n->waveform_cache_->get_uuid());
}

QString Node::get_input_name(const QString &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->human_name;
	} else {
		report_invalid_input("get name of", id, -1);
		return QString();
	}
}

bool Node::is_input_hidden(const QString &input) const
{
	return (get_input_flags(input) & k_input_flag_hidden);
}

bool Node::is_input_connectable(const QString &input) const
{
	return !(get_input_flags(input) & k_input_flag_not_connectable);
}

bool Node::is_input_keyframable(const QString &input) const
{
	return !(get_input_flags(input) & k_input_flag_not_keyframable);
}

bool Node::is_input_keyframing(const QString &input, int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->is_keyframing();
	} else {
		report_invalid_input("get keyframing state of", input, element);
		return false;
	}
}

void Node::set_input_is_keyframing(const QString &input, bool e, int element)
{
	if (!is_input_keyframable(input)) {
		qDebug() << "Ignored set keyframing of" << input
				 << "because this input is not keyframable";
		return;
	}

	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		imm->set_is_keyframing(e);

		emit keyframe_enable_changed(NodeInput(this, input, element), e);
	} else {
		report_invalid_input("set keyframing state of", input, element);
	}
}

bool Node::is_input_connected(const QString &input, int element) const
{
	return get_connected_output(input, element);
}

Node *Node::get_connected_output(const QString &input, int element) const
{
	for (auto it = input_connections_.cbegin(); it != input_connections_.cend();
		 it++) {
		if (it->first.input() == input && it->first.element() == element) {
			return it->second;
		}
	}

	return nullptr;
}

bool Node::is_using_standard_value(const QString &input, int track,
								int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->is_using_standard_value(track);
	} else {
		report_invalid_input("determine whether using standard value in", input,
						   element);
		return true;
	}
}

NodeValue::Type Node::get_input_data_type(const QString &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->type;
	} else {
		report_invalid_input("get data type of", id, -1);
		return NodeValue::k_none;
	}
}

void Node::set_input_data_type(const QString &id, const NodeValue::Type &type)
{
	Input *input_meta = get_internal_input_data(id);

	if (input_meta) {
		input_meta->type = type;

		int array_sz = input_array_size(id);
		for (int i = -1; i < array_sz; i++) {
			get_immediate(id, i)->set_data_type(type);
		}

		emit input_data_type_changed(id, type);
	} else {
		report_invalid_input("set data type of", id, -1);
	}
}

bool Node::has_input_property(const QString &id, const QString &name) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->properties.contains(name);
	} else {
		report_invalid_input("get property of", id, -1);
		return false;
	}
}

QHash<QString, QVariant> Node::get_input_properties(const QString &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->properties;
	} else {
		report_invalid_input("get property table of", id, -1);
		return QHash<QString, QVariant>();
	}
}

QVariant Node::get_input_property(const QString &id, const QString &name) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->properties.value(name);
	} else {
		report_invalid_input("get property of", id, -1);
		return QVariant();
	}
}

void Node::set_input_property(const QString &id, const QString &name,
							const QVariant &value)
{
	Input *i = get_internal_input_data(id);

	if (i) {
		i->properties.insert(name, value);

		emit input_property_changed(id, name, value);
	} else {
		report_invalid_input("set property of", id, -1);
	}
}

SplitValue Node::get_split_value_at_time(const QString &input, const Rational &time,
									 int element) const
{
	SplitValue vals;

	int nb_tracks = get_number_of_keyframe_tracks(input);

	for (int i = 0; i < nb_tracks; i++) {
		vals.append(get_split_value_at_time_on_track(input, time, i, element));
	}

	return vals;
}

QVariant Node::get_split_value_at_time_on_track(const QString &input,
										  const Rational &time, int track,
										  int element) const
{
	if (!is_using_standard_value(input, track, element)) {
		const NodeKeyframeTrack &key_track =
			get_keyframe_tracks(input, element).at(track);

		if (key_track.first()->time() >= time) {
			// This time precedes any keyframe, so we just return the first value
			return key_track.first()->value();
		}

		if (key_track.last()->time() <= time) {
			// This time is after any keyframes so we return the last value
			return key_track.last()->value();
		}

		NodeValue::Type type = get_input_data_type(input);

		// If we're here, the time must be somewhere in between the keyframes
		NodeKeyframe *before = nullptr, *after = nullptr;

		int low = 0;
		int high = key_track.size() - 1;
		while (low <= high) {
			int mid = low + (high - low) / 2;
			NodeKeyframe *mid_key = key_track.at(mid);
			NodeKeyframe *next_key = key_track.at(mid + 1);

			if (mid_key->time() <= time && next_key->time() > time) {
				before = mid_key;
				after = next_key;
				break;
			} else if (mid_key->time() < time) {
				low = mid + 1;
			} else {
				high = mid - 1;
			}
		}

		if (before) {
			if (before->time() == time ||
				((!NodeValue::type_can_be_interpolated(type) ||
				  before->type() == NodeKeyframe::k_hold) &&
				 after->time() > time)) {
				// Time == keyframe time, so value is precise
				return before->value();

			} else if (after->time() == time) {
				// Time == keyframe time, so value is precise
				return after->value();

			} else if (before->time() < time && after->time() > time) {
				// We must interpolate between these keyframes

				double before_val, after_val, interpolated;
				if (type == NodeValue::k_rational) {
					// Keys for Rational inputs usually hold rationals, but may
					// hold plain doubles, in which case we convert to Rational
					// first to preserve the value
					before_val = (before->value().canConvert<Rational>() ?
									  before->value().value<Rational>() :
									  Rational::from_double(
										  before->value().toDouble()))
									 .to_double();
					after_val = (after->value().canConvert<Rational>() ?
									 after->value().value<Rational>() :
									 Rational::from_double(
										 after->value().toDouble()))
									.to_double();
				} else {
					before_val = before->value().toDouble();
					after_val = after->value().toDouble();
				}

				if (before->type() == NodeKeyframe::k_bezier &&
					after->type() == NodeKeyframe::k_bezier) {
					// Perform a cubic bezier with two control points
					interpolated = Bezier::cubic_xto_y(
						time.to_double(),
						Imath::V2d(before->time().to_double(), before_val),
						Imath::V2d(before->time().to_double() +
									   before->valid_bezier_control_out().x(),
								   before_val +
									   before->valid_bezier_control_out().y()),
						Imath::V2d(after->time().to_double() +
									   after->valid_bezier_control_in().x(),
								   after_val +
									   after->valid_bezier_control_in().y()),
						Imath::V2d(after->time().to_double(), after_val));

				} else if (before->type() == NodeKeyframe::k_bezier ||
						   after->type() == NodeKeyframe::k_bezier) {
					// Perform a quadratic bezier with only one control point

					Imath::V2d control_point;

					if (before->type() == NodeKeyframe::k_bezier) {
						control_point.x =
							(before->valid_bezier_control_out().x() +
							 before->time().to_double());
						control_point.y =
							(before->valid_bezier_control_out().y() +
							 before_val);
					} else {
						control_point.x =
							(after->valid_bezier_control_in().x() +
							 after->time().to_double());
						control_point.y =
							(after->valid_bezier_control_in().y() + after_val);
					}

					// Interpolate value using quadratic beziers
					interpolated = Bezier::quadratic_xto_y(
						time.to_double(),
						Imath::V2d(before->time().to_double(), before_val),
						control_point,
						Imath::V2d(after->time().to_double(), after_val));

				} else {
					// To have arrived here, the keyframes must both be linear
					qreal period_progress =
						(time.to_double() - before->time().to_double()) /
						(after->time().to_double() - before->time().to_double());

					interpolated = lerp(before_val, after_val, period_progress);
				}

				if (type == NodeValue::k_rational) {
					return QVariant::fromValue(
						Rational::from_double(interpolated));
				} else {
					return interpolated;
				}
			}
		} else {
			qWarning() << "Binary search for keyframes failed";
		}
	}

	return get_split_standard_value_on_track(input, track, element);
}

QVariant Node::get_default_value(const QString &input) const
{
	NodeValue::Type type = get_input_data_type(input);

	return NodeValue::combine_track_values_into_normal_value(
		type, get_split_default_value(input));
}

SplitValue Node::get_split_default_value(const QString &input) const
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return i->default_value;
	} else {
		report_invalid_input("retrieve default value of", input, -1);
		return SplitValue();
	}
}

QVariant Node::get_split_default_value_on_track(const QString &input,
										   int track) const
{
	SplitValue val = get_split_default_value(input);
	if (track < val.size()) {
		return val.at(track);
	} else {
		return QVariant();
	}
}

void Node::set_default_value(const QString &input, const QVariant &val)
{
	NodeValue::Type type = get_input_data_type(input);

	set_split_default_value(
		input, NodeValue::split_normal_value_into_track_values(type, val));
}

void Node::set_split_default_value(const QString &input, const SplitValue &val)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		i->default_value = val;
	} else {
		report_invalid_input("set default value of", input, -1);
	}
}

void Node::set_split_default_value_on_track(const QString &input,
									   const QVariant &val, int track)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		if (track < i->default_value.size()) {
			i->default_value[track] = val;
		}
	} else {
		report_invalid_input("set default value on track of", input, -1);
	}
}

const QVector<NodeKeyframeTrack> &Node::get_keyframe_tracks(const QString &input,
														  int element) const
{
	return get_immediate(input, element)->keyframe_tracks();
}

QVector<NodeKeyframe *> Node::get_keyframes_at_time(const QString &input,
												 const Rational &time,
												 int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_keyframe_at_time(time);
	} else {
		report_invalid_input("get keyframes at time from", input, element);
		return QVector<NodeKeyframe *>();
	}
}

NodeKeyframe *Node::get_keyframe_at_time_on_track(const QString &input,
											 const Rational &time, int track,
											 int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_keyframe_at_time_on_track(time, track);
	} else {
		report_invalid_input("get keyframe at time on track from", input,
						   element);
		return nullptr;
	}
}

NodeKeyframe::Type Node::get_best_keyframe_type_for_time_on_track(const QString &input,
														   const Rational &time,
														   int track,
														   int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_best_keyframe_type_for_time(time, track);
	} else {
		report_invalid_input("get closest keyframe before a time from", input,
						   element);
		return NodeKeyframe::k_default_type;
	}
}

int Node::get_number_of_keyframe_tracks(const QString &id) const
{
	return NodeValue::get_number_of_keyframe_tracks(get_input_data_type(id));
}

NodeKeyframe *Node::get_earliest_keyframe(const QString &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_earliest_keyframe();
	} else {
		report_invalid_input("get earliest keyframe from", id, element);
		return nullptr;
	}
}

NodeKeyframe *Node::get_latest_keyframe(const QString &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_latest_keyframe();
	} else {
		report_invalid_input("get latest keyframe from", id, element);
		return nullptr;
	}
}

NodeKeyframe *Node::get_closest_keyframe_before_time(const QString &id,
												 const Rational &time,
												 int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_closest_keyframe_before_time(time);
	} else {
		report_invalid_input("get closest keyframe before a time from", id,
						   element);
		return nullptr;
	}
}

NodeKeyframe *Node::get_closest_keyframe_after_time(const QString &id,
												const Rational &time,
												int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_closest_keyframe_after_time(time);
	} else {
		report_invalid_input("get closest keyframe after a time from", id,
						   element);
		return nullptr;
	}
}

bool Node::has_keyframe_at_time(const QString &id, const Rational &time,
							 int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->has_keyframe_at_time(time);
	} else {
		report_invalid_input("determine if it has a keyframe at a time from", id,
						   element);
		return false;
	}
}

QStringList Node::get_combo_box_strings(const QString &id) const
{
	return get_input_property(id, QStringLiteral("combo_str")).toStringList();
}

QVariant Node::get_standard_value(const QString &id, int element) const
{
	NodeValue::Type type = get_input_data_type(id);

	return NodeValue::combine_track_values_into_normal_value(
		type, get_split_standard_value(id, element));
}

SplitValue Node::get_split_standard_value(const QString &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_split_standard_value();
	} else {
		report_invalid_input("get standard value of", id, element);
		return SplitValue();
	}
}

QVariant Node::get_split_standard_value_on_track(const QString &input, int track,
											int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_split_standard_value_on_track(track);
	} else {
		report_invalid_input("get standard value of", input, element);
		return QVariant();
	}
}

void Node::set_standard_value(const QString &id, const QVariant &value,
							int element)
{
	NodeValue::Type type = get_input_data_type(id);

	set_split_standard_value(
		id, NodeValue::split_normal_value_into_track_values(type, value),
		element);
}

void Node::set_split_standard_value(const QString &id, const SplitValue &value,
								 int element)
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		imm->set_split_standard_value(value);

		for (int i = 0; i < value.size(); i++) {
			if (is_using_standard_value(id, i, element)) {
				// If this standard value is being used, we need to send a value changed signal
				parameter_value_changed(id, element,
									  TimeRange(RATIONAL_MIN, RATIONAL_MAX));
				break;
			}
		}
	} else {
		report_invalid_input("set standard value of", id, element);
	}
}

void Node::set_split_standard_value_on_track(const QString &id, int track,
										const QVariant &value, int element)
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		imm->set_standard_value_on_track(value, track);

		if (is_using_standard_value(id, track, element)) {
			// If this standard value is being used, we need to send a value changed signal
			parameter_value_changed(id, element,
								  TimeRange(RATIONAL_MIN, RATIONAL_MAX));
		}
	} else {
		report_invalid_input("set standard value of", id, element);
	}
}

bool Node::input_is_array(const QString &id) const
{
	return get_input_flags(id) & k_input_flag_array;
}

void Node::input_array_insert(const QString &id, int index)
{
	// Add new input
	array_resize_internal(id, input_array_size(id) + 1);

	// Move connections down
	InputConnections copied_edges = input_connections();
	for (auto it = copied_edges.crbegin(); it != copied_edges.crend(); it++) {
		if (it->first.input() == id && it->first.element() >= index) {
			// Disconnect this and reconnect it one element down
			NodeInput new_edge = it->first;
			new_edge.set_element(new_edge.element() + 1);

			disconnect_edge(it->second, it->first);
			connect_edge(it->second, new_edge);
		}
	}

	// Shift values and keyframes up one element
	for (int i = input_array_size(id) - 1; i > index; i--) {
		copy_values_of_element(this, this, id, i - 1, i);
	}

	// Reset value of element we just "inserted"
	clear_element(id, index);
}

void Node::input_array_resize(const QString &id, int size)
{
	if (input_array_size(id) == size) {
		return;
	}

	NodeArrayResizeCommand *c = new NodeArrayResizeCommand(this, id, size);
	c->redo_now();
	delete c;
}

void Node::input_array_remove(const QString &id, int index)
{
	// Remove input
	array_resize_internal(id, input_array_size(id) - 1);

	// Move connections up
	InputConnections copied_edges = input_connections();
	for (auto it = copied_edges.cbegin(); it != copied_edges.cend(); it++) {
		if (it->first.input() == id && it->first.element() >= index) {
			// Disconnect this and reconnect it one element up if it's not the element being removed
			disconnect_edge(it->second, it->first);

			if (it->first.element() > index) {
				NodeInput new_edge = it->first;
				new_edge.set_element(new_edge.element() - 1);

				connect_edge(it->second, new_edge);
			}
		}
	}

	// Shift values and keyframes down one element
	int arr_sz = input_array_size(id);
	for (int i = index; i < arr_sz; i++) {
		// Copying ArraySize()+1 is actually legal because immediates are never deleted
		copy_values_of_element(this, this, id, i + 1, i);
	}

	// Reset value of last element
	clear_element(id, arr_sz);
}

int Node::input_array_size(const QString &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->array_size;
	} else {
		report_invalid_input("retrieve array size of", id, -1);
		return 0;
	}
}

void Node::set_value_hint_for_input(const QString &input, const ValueHint &hint,
								int element)
{
	value_hints_.insert({ input, element }, hint);

	emit input_value_hint_changed(NodeInput(this, input, element));

	invalidate_all(input, element);
}

const NodeKeyframeTrack &Node::get_track_from_keyframe(NodeKeyframe *key) const
{
	return get_immediate(key->input(), key->element())
		->keyframe_tracks()
		.at(key->track());
}

NodeInputImmediate *Node::get_immediate(const QString &input, int element) const
{
	if (element == -1) {
		return standard_immediates_.value(input, nullptr);
	} else if (array_immediates_.contains(input)) {
		const QVector<NodeInputImmediate *> &imm_arr =
			array_immediates_.value(input);

		if (element >= 0 && element < imm_arr.size()) {
			return imm_arr.at(element);
		}
	}

	return nullptr;
}

InputFlags Node::get_input_flags(const QString &input) const
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return i->flags;
	} else {
		report_invalid_input("retrieve flags of", input, -1);
		return InputFlags(k_input_flag_normal);
	}
}

void Node::set_input_flag(const QString &input, InputFlag f, bool on)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		if (on) {
			i->flags |= f;
		} else {
			i->flags &= ~f;
		}
		emit input_flags_changed(input, i->flags);
	} else {
		report_invalid_input("set flags of", input, -1);
	}
}

void Node::value(const NodeValueRow &value, const NodeGlobals &globals,
				 NodeValueTable *table) const
{
	// Do nothing
	Q_UNUSED(value)
	Q_UNUSED(globals)
	Q_UNUSED(table)
}

void Node::invalidate_cache(const TimeRange &range, const QString &from,
						   int element, InvalidateCacheOptions options)
{
	Q_UNUSED(from)
	Q_UNUSED(element)

	if (are_caches_enabled()) {
		if (range.in() != range.out()) {
			TimeRange vr = range.intersected(get_video_cache_range());
			if (vr.length() != 0) {
				video_frame_cache()->invalidate(vr);
				thumbnail_cache()->invalidate(vr);
			}
			TimeRange ar = range.intersected(get_audio_cache_range());
			if (ar.length() != 0) {
				audio_playback_cache()->invalidate(ar);
				waveform_cache()->invalidate(ar);
			}
		}
	}

	send_invalidate_cache(range, options);
}

TimeRange Node::input_time_adjustment(const QString &, int,
									const TimeRange &input_time,
									bool clamp) const
{
	// Default behavior is no time adjustment at all
	return input_time;
}

TimeRange Node::output_time_adjustment(const QString &, int,
									 const TimeRange &input_time) const
{
	// Default behavior is no time adjustment at all
	return input_time;
}

QVector<Node *> Node::copy_dependency_graph(const QVector<Node *> &nodes,
										  MultiUndoCommand *command)
{
	int nb_nodes = nodes.size();

	QVector<Node *> copies(nb_nodes);

	for (int i = 0; i < nb_nodes; i++) {
		// Create another of the same node
		Node *c = nodes.at(i)->copy();

		// Copy the values, but NOT the connections, since we'll be connecting to our own clones later
		Node::copy_inputs(nodes.at(i), c, false);

		// Add to graph
		Project *graph = nodes.at(i)->parent();
		if (command) {
			command->add_child(new NodeAddCommand(graph, c));
		} else {
			c->setParent(graph);
		}

		// Store in array at the same index as source
		copies[i] = c;
	}

	copy_dependency_graph(nodes, copies, command);

	return copies;
}

void Node::copy_dependency_graph(const QVector<Node *> &src,
							   const QVector<Node *> &dst,
							   MultiUndoCommand *command)
{
	for (int i = 0; i < src.size(); i++) {
		Node *src_node = src.at(i);
		Node *dst_node = dst.at(i);

		for (auto it = src_node->input_connections_.cbegin();
			 it != src_node->input_connections_.cend(); it++) {
			// Determine if the connected node is in our src list
			int connection_index = src.indexOf(it->second);

			if (connection_index > -1) {
				// Find the equivalent node in the dst list
				Node *copied_output = dst.at(connection_index);
				NodeInput copied_input =
					NodeInput(dst_node, it->first.input(), it->first.element());

				if (command) {
					command->add_child(
						new NodeEdgeAddCommand(copied_output, copied_input));
					command->add_child(new NodeSetValueHintCommand(
						copied_input,
						src_node->get_value_hint_for_input(
							copied_input.input(), copied_input.element())));
				} else {
					connect_edge(copied_output, copied_input);
					copied_input.node()->set_value_hint_for_input(
						copied_input.input(),
						src_node->get_value_hint_for_input(copied_input.input(),
													   copied_input.element()),
						copied_input.element());
				}
			}
		}
	}
}

Node *Node::copy_node_and_dependency_graph_minus_items_internal(
	QMap<Node *, Node *> &created, Node *node, MultiUndoCommand *command)
{
	// Make a new node of the same type
	Node *copy = node->copy();

	// Add to map
	created.insert(node, copy);

	// Add it to the same graph
	command->add_child(new NodeAddCommand(node->parent(), copy));

	// Copy context children
	const PositionMap &map = node->get_context_positions();
	for (auto it = map.cbegin(); it != map.cend(); it++) {
		// Add either the copy (if it exists) or the original node to the context
		Node *child;

		if (it.key()->is_item()) {
			child = it.key();
		} else {
			child = created.value(it.key());
			if (!child) {
				child = copy_node_and_dependency_graph_minus_items_internal(
					created, it.key(), command);
			}
		}

		command->add_child(new NodeSetPositionCommand(child, copy, it.value()));
	}

	// If this is a group, copy input and output passthroughs
	if (NodeGroup *src_group = dynamic_cast<NodeGroup *>(node)) {
		NodeGroup *dst_group = static_cast<NodeGroup *>(copy);

		for (auto it = src_group->get_input_passthroughs().cbegin();
			 it != src_group->get_input_passthroughs().cend(); it++) {
			// This node should have been created by the context loop above
			NodeInput input = it->second;
			input.set_node(created.value(input.node()));
			command->add_child(
				new NodeGroupAddInputPassthrough(dst_group, input, it->first));
		}

		command->add_child(new NodeGroupSetOutputPassthrough(
			dst_group, created.value(src_group->get_output_passthrough())));
	}

	// Copy values to the clone
	copy_inputs(node, copy, false, command);

	// Go through input connections and copy if non-item and connect if item
	for (auto it = node->input_connections_.cbegin();
		 it != node->input_connections_.cend(); it++) {
		NodeInput input = it->first;
		Node *connected = it->second;
		Node *connected_copy;

		if (connected->is_item()) {
			// This is an item and we avoid copying those and just connect to them directly
			connected_copy = connected;
		} else {
			// Non-item, we want to clone this too
			connected_copy = created.value(connected, nullptr);
			if (!connected_copy) {
				connected_copy = copy_node_and_dependency_graph_minus_items_internal(
					created, connected, command);
			}
		}

		NodeInput copied_input = input;
		copied_input.set_node(copy);
		command->add_child(
			new NodeEdgeAddCommand(connected_copy, copied_input));
		command->add_child(new NodeSetValueHintCommand(
			copied_input,
			node->get_value_hint_for_input(input.input(), input.element())));
	}

	return copy;
}

Node *Node::copy_node_and_dependency_graph_minus_items(Node *node,
												 MultiUndoCommand *command)
{
	QMap<Node *, Node *> created;

	return copy_node_and_dependency_graph_minus_items_internal(created, node, command);
}

Node *Node::copy_node_in_graph(Node *node, MultiUndoCommand *command)
{
	Node *copy;

	if (OAK_CONFIG("SplitClipsCopyNodes").toBool()) {
		copy = Node::copy_node_and_dependency_graph_minus_items(node, command);
	} else {
		copy = node->copy();

		command->add_child(new NodeAddCommand(node->parent(), copy));

		copy_inputs(node, copy, true, command);

		const PositionMap &map = node->get_context_positions();
		for (auto it = map.cbegin(); it != map.cend(); it++) {
			// Add to the context
			command->add_child(
				new NodeSetPositionCommand(it.key(), copy, it.value()));
		}
	}

	return copy;
}

void Node::send_invalidate_cache(const TimeRange &range,
							   const InvalidateCacheOptions &options)
{
	// During project teardown, don't propagate invalidation across edges:
	// the nodes on the other side may already be half-destroyed.
	if (Project *p = project()) {
		if (p->is_being_cleared()) {
			return;
		}
	}

	for (const OutputConnection &conn : output_connections_) {
		// Send clear cache signal to the Node
		const NodeInput &in = conn.second;

		in.node()->invalidate_cache(range, in.input(), in.element(), options);
	}
}

void Node::invalidate_all(const QString &input, int element)
{
	if (Project *p = project()) {
		if (p->is_being_cleared()) {
			return;
		}
	}

	invalidate_cache(TimeRange(RATIONAL_MIN, RATIONAL_MAX), input, element);
}

bool Node::link(Node *a, Node *b)
{
	if (a == b || !a || !b) {
		return false;
	}

	if (are_linked(a, b)) {
		return false;
	}

	a->links_.append(b);
	b->links_.append(a);

	a->LinkChangeEvent();
	b->LinkChangeEvent();

	emit a->links_changed();
	emit b->links_changed();

	return true;
}

bool Node::unlink(Node *a, Node *b)
{
	if (!are_linked(a, b)) {
		return false;
	}

	a->links_.removeOne(b);
	b->links_.removeOne(a);

	a->LinkChangeEvent();
	b->LinkChangeEvent();

	emit a->links_changed();
	emit b->links_changed();

	return true;
}

bool Node::are_linked(Node *a, Node *b)
{
	return a->links_.contains(b);
}

bool Node::load(QXmlStreamReader *reader, SerializedData *data)
{
	uint version = 0;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("ptr")) {
			quintptr ptr = attr.value().toULongLong();
			data->node_ptrs.insert(ptr, this);
		} else if (attr.name() == QStringLiteral("version")) {
			version = attr.value().toUInt();
		}
	}

	Q_UNUSED(version)

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("input")) {
			load_input(reader, data);
		} else if (reader->name() == QStringLiteral("label")) {
			this->set_label(reader->readElementText());
		} else if (reader->name() == QStringLiteral("color")) {
			this->set_override_color(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("links")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("link")) {
					data->block_links.append(
						{ this, reader->readElementText().toULongLong() });
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("custom")) {
			if (!load_custom(reader, data)) {
				return false;
			}
		} else if (reader->name() == QStringLiteral("connections")) {
			// Load connections
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("connection")) {
					QString param_id;
					int ele = -1;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("element")) {
							ele = attr.value().toInt();
						} else if (attr.name() == QStringLiteral("input")) {
							param_id = attr.value().toString();
						}
					}

					// Translate IDs renamed after older project files were
					// written
					param_id = get_input_id_for_legacy_id(param_id);

					QString output_node_id;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("output")) {
							output_node_id = reader->readElementText();
						} else {
							reader->skipCurrentElement();
						}
					}

					data->desired_connections.append(
						{ NodeInput(this, param_id, ele),
						  output_node_id.toULongLong() });
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("hints")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("hint")) {
					QString input;
					int element = -1;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("input")) {
							input = attr.value().toString();
						} else if (attr.name() == QStringLiteral("element")) {
							element = attr.value().toInt();
						}
					}

					Node::ValueHint vh;
					if (!vh.load(reader)) {
						return false;
					}
					this->set_value_hint_for_input(input, vh, element);
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("context")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("node")) {
					quintptr node_ptr = 0;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("ptr")) {
							node_ptr = attr.value().toULongLong();
						}
					}

					if (node_ptr) {
						Node::Position node_pos;
						if (!node_pos.load(reader)) {
							return false;
						}
						data->positions[this].insert(node_ptr, node_pos);
					} else {
						return false;
					}
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("caches")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("audio")) {
					this->audio_playback_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("video")) {
					this->video_frame_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("thumb")) {
					this->thumbnail_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("waveform")) {
					this->waveform_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	this->LoadFinishedEvent();

	return true;
}

void Node::save(QXmlStreamWriter *writer) const
{
	writer->writeAttribute(QStringLiteral("version"), QString::number(1));
	writer->writeAttribute(QStringLiteral("id"), this->id());
	writer->writeAttribute(QStringLiteral("ptr"),
						   QString::number(reinterpret_cast<quintptr>(this)));

	if (!this->get_label().isEmpty()) {
		writer->writeTextElement(QStringLiteral("label"), this->get_label());
	}

	if (this->get_override_color() != -1) {
		writer->writeTextElement(QStringLiteral("color"),
								 QString::number(this->get_override_color()));
	}

	foreach (const QString &input, this->inputs()) {
		writer->writeStartElement(QStringLiteral("input"));

		save_input(writer, input);

		writer->writeEndElement(); // input
	}

	if (!this->links().empty()) {
		writer->writeStartElement(QStringLiteral("links"));
		foreach (Node *link, this->links()) {
			writer->writeTextElement(
				QStringLiteral("link"),
				QString::number(reinterpret_cast<quintptr>(link)));
		}
		writer->writeEndElement(); // links
	}

	if (!this->input_connections().empty()) {
		writer->writeStartElement(QStringLiteral("connections"));
		for (auto it = this->input_connections().cbegin();
			 it != this->input_connections().cend(); it++) {
			writer->writeStartElement(QStringLiteral("connection"));

			writer->writeAttribute(QStringLiteral("input"), it->first.input());
			writer->writeAttribute(QStringLiteral("element"),
								   QString::number(it->first.element()));

			writer->writeTextElement(
				QStringLiteral("output"),
				QString::number(reinterpret_cast<quintptr>(it->second)));

			writer->writeEndElement(); // connection
		}
		writer->writeEndElement(); // connections
	}

	if (!this->get_value_hints().empty()) {
		writer->writeStartElement(QStringLiteral("hints"));
		for (auto it = this->get_value_hints().cbegin();
			 it != this->get_value_hints().cend(); it++) {
			writer->writeStartElement(QStringLiteral("hint"));

			writer->writeAttribute(QStringLiteral("input"), it.key().input);
			writer->writeAttribute(QStringLiteral("element"),
								   QString::number(it.key().element));

			it.value().save(writer);

			writer->writeEndElement(); // hint
		}
		writer->writeEndElement(); // hints
	}

	const Node::PositionMap &map = this->get_context_positions();

	if (!map.isEmpty()) {
		writer->writeStartElement(QStringLiteral("context"));

		for (auto jt = map.cbegin(); jt != map.cend(); jt++) {
			writer->writeStartElement(QStringLiteral("node"));
			writer->writeAttribute(
				QStringLiteral("ptr"),
				QString::number(reinterpret_cast<quintptr>(jt.key())));
			jt.value().save(writer);
			writer->writeEndElement(); // node
		}

		writer->writeEndElement(); // context
	}

	writer->writeStartElement(QStringLiteral("caches"));

	writer->writeTextElement(
		QStringLiteral("audio"),
		this->audio_playback_cache()->get_uuid().toString());
	writer->writeTextElement(QStringLiteral("video"),
							 this->video_frame_cache()->get_uuid().toString());
	writer->writeTextElement(QStringLiteral("thumb"),
							 this->thumbnail_cache()->get_uuid().toString());
	writer->writeTextElement(QStringLiteral("waveform"),
							 this->waveform_cache()->get_uuid().toString());

	writer->writeEndElement(); // caches

	writer->writeStartElement(QStringLiteral("custom"));

	save_custom(writer);

	writer->writeEndElement(); // custom
}

bool Node::load_custom(QXmlStreamReader *reader, SerializedData *data)
{
	reader->skipCurrentElement();
	return true;
}

void Node::PostLoadEvent(SerializedData *data)
{
	// Resolve positions
	const QMap<quintptr, Node::Position> &positions =
		data->positions.value(this);

	for (auto jt = positions.cbegin(); jt != positions.cend(); jt++) {
		Node *n = data->node_ptrs.value(jt.key());
		if (n) {
			this->set_node_position_in_context(n, jt.value());
		}
	}
}

QString Node::get_input_id_for_legacy_id(const QString &id) const
{
	return id;
}

bool Node::load_input(QXmlStreamReader *reader, SerializedData *data)
{
	if (dynamic_cast<NodeGroup *>(this)) {
		// Ignore input of group
		reader->skipCurrentElement();
		return true;
	}

	QString param_id;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("id")) {
			param_id = attr.value().toString();

			break;
		}
	}

	if (param_id.isEmpty()) {
		qWarning() << "Failed to load parameter with missing ID";
		reader->skipCurrentElement();
		return false;
	}

	// Translate IDs renamed after older project files were written
	param_id = get_input_id_for_legacy_id(param_id);

	if (!this->has_input_with_id(param_id)) {
		qWarning() << "Failed to load parameter that didn't exist:" << param_id;
		reader->skipCurrentElement();
		return false;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("primary")) {
			// Load primary immediate
			if (!load_immediate(reader, param_id, -1, data)) {
				return false;
			}
		} else if (reader->name() == QStringLiteral("subelements")) {
			// Load subelements
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("count")) {
					this->input_array_resize(param_id, attr.value().toInt());
				}
			}

			int element_counter = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("element")) {
					if (!load_immediate(reader, param_id, element_counter,
									   data)) {
						return false;
					}

					element_counter++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void Node::save_input(QXmlStreamWriter *writer, const QString &id) const
{
	writer->writeAttribute(QStringLiteral("id"), id);

	writer->writeStartElement(QStringLiteral("primary"));

	save_immediate(writer, id, -1);

	writer->writeEndElement(); // primary

	int arr_sz = this->input_array_size(id);

	if (arr_sz > 0) {
		writer->writeStartElement(QStringLiteral("subelements"));

		writer->writeAttribute(QStringLiteral("count"),
							   QString::number(arr_sz));

		for (int i = 0; i < arr_sz; i++) {
			writer->writeStartElement(QStringLiteral("element"));

			save_immediate(writer, id, i);

			writer->writeEndElement(); // element
		}

		writer->writeEndElement(); // subelements
	}
}

bool Node::load_immediate(QXmlStreamReader *reader, const QString &input,
						 int element, SerializedData *data)
{
	NodeValue::Type data_type = this->get_input_data_type(input);

	// HACK: SubtitleParams contain the actual subtitle data, so loading/replacing it will overwrite
	//       the valid subtitles. We hack around it by simply skipping loading subtitles, we'll see
	//       if this ends up being an issue in the future.
	if (data_type == NodeValue::k_subtitle_params) {
		reader->skipCurrentElement();
		return true;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("standard")) {
			// Load standard value
			int val_index = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("track")) {
					QVariant value_on_track;

					if (data_type == NodeValue::k_video_params) {
						VideoParams vp;
						vp.load(reader);
						value_on_track = QVariant::fromValue(vp);
					} else if (data_type == NodeValue::k_audio_params) {
						AudioParams ap =
							TypeSerializer::load_audio_params(reader);
						value_on_track = QVariant::fromValue(ap);
					} else {
						QString value_text = reader->readElementText();

						if (!value_text.isEmpty()) {
							value_on_track = NodeValue::string_to_value(
								data_type, value_text, true);
						}
					}

					this->set_split_standard_value_on_track(input, val_index,
													   value_on_track, element);

					val_index++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("keyframing")) {
			bool k = reader->readElementText().toInt();
			if (this->is_input_keyframable(input)) {
				this->set_input_is_keyframing(input, k, element);
			}
		} else if (reader->name() == QStringLiteral("keyframes")) {
			int track = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("track")) {
					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("key")) {
							NodeKeyframe *key = new NodeKeyframe();
							key->set_input(input);
							key->set_element(element);
							key->set_track(track);

							if (!key->load(reader, data_type)) {
								delete key;
								return false;
							}
							key->setParent(this);
						} else {
							reader->skipCurrentElement();
						}
					}

					track++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("csinput")) {
			this->set_input_property(input, QStringLiteral("col_input"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("csdisplay")) {
			this->set_input_property(input, QStringLiteral("col_display"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("csview")) {
			this->set_input_property(input, QStringLiteral("col_view"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("cslook")) {
			this->set_input_property(input, QStringLiteral("col_look"),
								   reader->readElementText());
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void Node::save_immediate(QXmlStreamWriter *writer, const QString &input,
						 int element) const
{
	bool is_keyframing = this->is_input_keyframing(input, element);

	if (this->is_input_keyframable(input)) {
		writer->writeTextElement(QStringLiteral("keyframing"),
								 QString::number(is_keyframing));
	}

	NodeValue::Type data_type = this->get_input_data_type(input);

	// Write standard value
	writer->writeStartElement(QStringLiteral("standard"));

	foreach (const QVariant &v, this->get_split_standard_value(input, element)) {
		writer->writeStartElement(QStringLiteral("track"));

		if (data_type == NodeValue::k_video_params) {
			v.value<VideoParams>().save(writer);
		} else if (data_type == NodeValue::k_audio_params) {
			TypeSerializer::save_audio_params(writer, v.value<AudioParams>());
		} else {
			writer->writeCharacters(
				NodeValue::value_to_string(data_type, v, true));
		}

		writer->writeEndElement(); // track
	}

	writer->writeEndElement(); // standard

	// Write keyframes
	if (is_keyframing) {
		writer->writeStartElement(QStringLiteral("keyframes"));

		for (const NodeKeyframeTrack &track :
			 this->get_keyframe_tracks(input, element)) {
			writer->writeStartElement(QStringLiteral("track"));

			for (NodeKeyframe *key : track) {
				writer->writeStartElement(QStringLiteral("key"));

				key->save(writer, data_type);

				writer->writeEndElement(); // key
			}

			writer->writeEndElement(); // track
		}

		writer->writeEndElement(); // keyframes
	}

	if (data_type == NodeValue::k_color) {
		// Save color management information
		writer->writeTextElement(
			QStringLiteral("csinput"),
			this->get_input_property(input, QStringLiteral("col_input"))
				.toString());
		writer->writeTextElement(
			QStringLiteral("csdisplay"),
			this->get_input_property(input, QStringLiteral("col_display"))
				.toString());
		writer->writeTextElement(
			QStringLiteral("csview"),
			this->get_input_property(input, QStringLiteral("col_view"))
				.toString());
		writer->writeTextElement(
			QStringLiteral("cslook"),
			this->get_input_property(input, QStringLiteral("col_look"))
				.toString());
	}
}

void Node::insert_input(const QString &id, NodeValue::Type type,
					   const QVariant &default_value, InputFlags flags,
					   int index)
{
	if (id.isEmpty()) {
		qWarning()
			<< "Rejected adding input with an empty ID on node" << this->id();
		return;
	}

	if (has_param_with_id(id)) {
		qWarning() << "Failed to add input to node" << this->id()
				   << "- param with ID" << id << "already exists";
		return;
	}

	Node::Input i;

	i.type = type;
	i.default_value =
		NodeValue::split_normal_value_into_track_values(type, default_value);
	i.flags = flags;
	i.array_size = 0;

	input_ids_.insert(index, id);
	input_data_.insert(index, i);

	if (!standard_immediates_.value(id, nullptr)) {
		standard_immediates_.insert(id, create_immediate(id));
	}

	emit input_added(id);
}

void Node::remove_input(const QString &id)
{
	int index = input_ids_.indexOf(id);

	if (index == -1) {
		report_invalid_input("remove", id, -1);
		return;
	}

	input_ids_.removeAt(index);
	input_data_.removeAt(index);

	emit input_removed(id);
}

void Node::report_invalid_input(const char *attempted_action, const QString &id,
							  int element) const
{
	qWarning()
		<< "Failed to" << attempted_action << "parameter" << id << "element"
		<< element << "in node" << this->id() << "- input doesn't exist";

#ifndef _WIN32
	if (qEnvironmentVariableIsSet("OAK_DEBUG_INVALID_INPUT")) {
		void *frames[32];
		const int n = backtrace(frames, 32);
		char **symbols = backtrace_symbols(frames, n);
		if (symbols) {
			for (int i = 0; i < n; i++) {
				qWarning("INVALID-INPUT-BT: %s", symbols[i]);
			}
			free(symbols);
		}
	}
#endif
}

NodeInputImmediate *Node::create_immediate(const QString &input)
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return new NodeInputImmediate(i->type, i->default_value);
	} else {
		report_invalid_input("create immediate", input, -1);
		return nullptr;
	}
}

void Node::array_resize_internal(const QString &id, int size)
{
	Input *imm = get_internal_input_data(id);

	if (!imm) {
		report_invalid_input("set array size", id, -1);
		return;
	}

	if (imm->array_size != size) {
		// Update array size
		if (imm->array_size < size) {
			// Size is larger, create any immediates that don't exist
			QVector<NodeInputImmediate *> &subinputs = array_immediates_[id];
			for (int i = subinputs.size(); i < size; i++) {
				subinputs.append(create_immediate(id));
			}

			// Note that we do not delete any immediates when decreasing size since the user might still
			// want that data. Therefore it's important to note that array_size_ does NOT necessarily
			// equal subinputs_.size()
		}

		int old_sz = imm->array_size;
		imm->array_size = size;
		emit input_array_size_changed(id, old_sz, size);
		parameter_value_changed(id, -1, TimeRange(RATIONAL_MIN, RATIONAL_MAX));
	}
}

QString Node::get_connect_command_string(Node *output, const NodeInput &input)
{
	return tr("Connected %1 to %2 - %3")
		.arg(output->get_label_and_name(), input.node()->get_label_and_name(),
			 input.get_input_name());
}

QString Node::get_disconnect_command_string(Node *output, const NodeInput &input)
{
	return tr("Disconnected %1 from %2 - %3")
		.arg(output->get_label_and_name(), input.node()->get_label_and_name(),
			 input.get_input_name());
}

int Node::get_internal_input_array_size(const QString &input)
{
	return array_immediates_.value(input).size();
}

void find_ways_node_arrives_here_recursively(const Node *output, const Node *input,
										QVector<NodeInput> &v)
{
	for (auto it = input->input_connections().cbegin();
		 it != input->input_connections().cend(); it++) {
		if (it->second == output) {
			v.append(it->first);
		} else {
			find_ways_node_arrives_here_recursively(output, it->second, v);
		}
	}
}

QVector<NodeInput> Node::find_ways_node_arrives_here(const Node *output) const
{
	QVector<NodeInput> v;

	find_ways_node_arrives_here_recursively(output, this, v);

	return v;
}

void Node::set_input_name(const QString &id, const QString &name)
{
	Input *i = get_internal_input_data(id);

	if (i) {
		i->human_name = name;

		emit input_name_changed(id, name);
	} else {
		report_invalid_input("set name of", id, -1);
	}
}

const QString &Node::get_label() const
{
	return label_;
}

void Node::set_label(const QString &s)
{
	if (label_ != s) {
		label_ = s;

		emit label_changed(label_);
	}
}

QString Node::get_label_and_name() const
{
	if (get_label().isEmpty()) {
		return name();
	} else {
		return tr("%1 (%2)").arg(get_label(), name());
	}
}

QString Node::get_label_or_name() const
{
	if (get_label().isEmpty()) {
		return name();
	}
	return get_label();
}

void Node::copy_inputs(const Node *source, Node *destination,
					  bool include_connections, MultiUndoCommand *command)
{
	Q_ASSERT(source->id() == destination->id());

	foreach (const QString &input, source->inputs()) {
		// NOTE: This assert is to ensure that inputs in the source also exist in the destination, which
		//       they should. If they don't and you hit this assert, check if you're handling group
		//       passthroughs correctly.
		Q_ASSERT(destination->has_input_with_id(input));

		copy_input(source, destination, input, include_connections, true,
				  command);
	}

	if (command) {
		command->add_child(
			new NodeRenameCommand(destination, source->get_label()));
	} else {
		destination->set_label(source->get_label());
	}

	if (command) {
		command->add_child(new NodeOverrideColorCommand(
			destination, source->get_override_color()));
	} else {
		destination->set_override_color(source->get_override_color());
	}
}

void Node::copy_input(const Node *src, Node *dst, const QString &input,
					 bool include_connections, bool traverse_arrays,
					 MultiUndoCommand *command)
{
	Q_ASSERT(src->id() == dst->id());

	copy_values_of_element(src, dst, input, -1, command);

	// Copy array size
	if (src->input_is_array(input) && traverse_arrays) {
		int src_array_sz = src->input_array_size(input);

		for (int i = 0; i < src_array_sz; i++) {
			copy_values_of_element(src, dst, input, i, command);
		}
	}

	// Copy connections
	if (include_connections) {
		// Copy all connections
		for (auto it = src->input_connections().cbegin();
			 it != src->input_connections().cend(); it++) {
			if (!traverse_arrays && it->first.element() != -1) {
				continue;
			}

			auto conn_output = it->second;
			NodeInput conn_input(dst, input, it->first.element());

			if (command) {
				command->add_child(
					new NodeEdgeAddCommand(conn_output, conn_input));
			} else {
				connect_edge(conn_output, conn_input);
			}
		}
	}
}

void Node::copy_values_of_element(const Node *src, Node *dst, const QString &input,
							   int src_element, int dst_element,
							   MultiUndoCommand *command)
{
	if (dst_element >= dst->get_internal_input_array_size(input)) {
		qDebug() << "Ignored destination element that was out of array bounds";
		return;
	}

	NodeInput dst_input(dst, input, dst_element);

	// Copy standard value
	SplitValue standard = src->get_split_standard_value(input, src_element);
	if (command) {
		command->add_child(
			new NodeParamSetSplitStandardValueCommand(dst_input, standard));
	} else {
		dst->set_split_standard_value(input, standard, dst_element);
	}

	// Copy keyframes
	if (NodeInputImmediate *immediate = dst->get_immediate(input, dst_element)) {
		if (command) {
			command->add_child(
				new NodeImmediateRemoveAllKeyframesCommand(immediate));
		} else {
			immediate->delete_all_keyframes();
		}
	}

	for (const NodeKeyframeTrack &track :
		 src->get_immediate(input, src_element)->keyframe_tracks()) {
		for (NodeKeyframe *key : track) {
			NodeKeyframe *copy =
				key->copy(dst_element, command ? nullptr : dst);
			if (command) {
				command->add_child(
					new NodeParamInsertKeyframeCommand(dst, copy));
			}
		}
	}

	// Copy keyframing state
	if (src->is_input_keyframable(input)) {
		bool is_keying = src->is_input_keyframing(input, src_element);
		if (command) {
			command->add_child(
				new NodeParamSetKeyframingCommand(dst_input, is_keying));
		} else {
			dst->set_input_is_keyframing(input, is_keying, dst_element);
		}
	}

	// If this is the root of an array, copy the array size
	if (src_element == -1 && dst_element == -1) {
		int array_sz = src->input_array_size(input);
		if (command) {
			command->add_child(
				new NodeArrayResizeCommand(dst, input, array_sz));
		} else {
			dst->array_resize_internal(input, array_sz);
		}
	}

	// Copy value hint
	Node::ValueHint vh = src->get_value_hint_for_input(input, src_element);
	if (command) {
		command->add_child(new NodeSetValueHintCommand(dst_input, vh));
	} else {
		dst->set_value_hint_for_input(input, vh, dst_element);
	}
}

void get_dependencies_recursively(QVector<Node *> &list, const Node *node,
								bool traverse, bool exclusive_only)
{
	for (auto it = node->input_connections().cbegin();
		 it != node->input_connections().cend(); it++) {
		Node *connected_node = it->second;

		if (!exclusive_only || !connected_node->is_item()) {
			if (!list.contains(connected_node)) {
				list.append(connected_node);

				if (traverse) {
					get_dependencies_recursively(list, connected_node, traverse,
											   exclusive_only);
				}
			}
		}
	}
}

/**
 * @brief Recursively collects dependencies of Node `n` and appends them to QList `list`
 *
 * @param traverse
 *
 * TRUE to recursively traverse each node for a complete dependency graph. FALSE to return only the immediate
 * dependencies.
 */
QVector<Node *> Node::get_dependencies_internal(bool traverse,
											  bool exclusive_only) const
{
	QVector<Node *> list;

	get_dependencies_recursively(list, this, traverse, exclusive_only);

	return list;
}

QVector<Node *> Node::get_dependencies() const
{
	return get_dependencies_internal(true, false);
}

QVector<Node *> Node::get_exclusive_dependencies() const
{
	return get_dependencies_internal(true, true);
}

QVector<Node *> Node::get_immediate_dependencies() const
{
	return get_dependencies_internal(false, false);
}

ShaderCode Node::get_shader_code(const ShaderRequest &request) const
{
	return ShaderCode(QString(), QString());
}

void Node::process_samples(const NodeValueRow &, const SampleBuffer &,
						  SampleBuffer &, int) const
{
}

void Node::generate_frame(FramePtr frame, const GenerateJob &job) const
{
	Q_UNUSED(frame)
	Q_UNUSED(job)
}

bool Node::inputs_from(Node *n, bool recursively) const
{
	for (auto it = input_connections_.cbegin(); it != input_connections_.cend();
		 it++) {
		Node *connected = it->second;

		if (connected == n) {
			return true;
		} else if (recursively && connected->inputs_from(n, recursively)) {
			return true;
		}
	}

	return false;
}

bool Node::inputs_from(const QString &id, bool recursively) const
{
	for (auto it = input_connections_.cbegin(); it != input_connections_.cend();
		 it++) {
		Node *connected = it->second;

		if (connected->id() == id) {
			return true;
		} else if (recursively && connected->inputs_from(id, recursively)) {
			return true;
		}
	}

	return false;
}

void Node::disconnect_all()
{
	// During project teardown, skip the disconnect events/signals: the
	// other side's handlers touch this node's members which are already
	// destroyed at ~Node time (e.g. caches), which is a use-after-free.
	const bool silent = project() && project()->is_being_cleared();

	// Disconnect inputs (copy map since internal map will change as we disconnect)
	InputConnections copy = input_connections_;
	for (auto it = copy.cbegin(); it != copy.cend(); it++) {
		disconnect_edge(it->second, it->first, silent);
	}

	while (!output_connections_.empty()) {
		OutputConnection conn = output_connections_.back();
		disconnect_edge(conn.first, conn.second, silent);
	}
}

QString Node::get_category_name(const CategoryID &c)
{
	switch (c) {
	case k_category_output:
		return tr("Output");
	case k_category_distort:
		return tr("Distort");
	case k_category_math:
		return tr("Math");
	case k_category_keying:
		return tr("Keying");
	case k_category_color:
		return tr("Color");
	case k_category_filter:
		return tr("Filter");
	case k_category_timeline:
		return tr("Timeline");
	case k_category_generator:
		return tr("Generator");
	case k_category_transition:
		return tr("Transition");
	case k_category_project:
		return tr("Project");
	case k_category_open_fx:
		return tr("OpenFX");
	case k_category_time:
		return tr("Time");
	case k_category_unknown:
	case k_category_count:
		break;
	}

	return tr("Uncategorized");
}

TimeRange Node::transform_time_to(TimeRange time, Node *target,
								TransformTimeDirection dir, int path_index)
{
	Node *from = this;
	Node *to = target;

	if (dir == k_transform_towards_input) {
		std::swap(from, to);
	}

	std::list<NodeInput> path = find_path(from, to, path_index);

	if (!path.empty()) {
		if (dir == k_transform_towards_input) {
			for (auto it = path.crbegin(); it != path.crend(); it++) {
				const NodeInput &i = (*it);
				time = i.node()->input_time_adjustment(i.input(), i.element(),
													 time, false);
			}
		} else {
			// Traverse in output direction
			for (auto it = path.cbegin(); it != path.cend(); it++) {
				const NodeInput &i = (*it);
				time = i.node()->output_time_adjustment(i.input(), i.element(),
													  time);
			}
		}
	}

	return time;
}

void Node::parameter_value_changed(const QString &input, int element,
								 const TimeRange &range)
{
	InputValueChangedEvent(input, element);

	emit value_changed(NodeInput(this, input, element), range);

	if (get_input_flags(input) & k_input_flag_ignore_invalidations) {
		return;
	}

	invalidate_cache(range, input, element);
}

TimeRange Node::get_range_affected_by_keyframe(NodeKeyframe *key) const
{
	const NodeKeyframeTrack &key_track = get_track_from_keyframe(key);
	int keyframe_index = key_track.indexOf(key);

	TimeRange range = get_range_around_index(key->input(), keyframe_index,
										  key->track(), key->element());

	// If a previous key exists and it's a hold, we don't need to invalidate those frames
	if (key_track.size() > 1 && keyframe_index > 0 &&
		key_track.at(keyframe_index - 1)->type() == NodeKeyframe::k_hold) {
		range.set_in(key->time());
	}

	return range;
}

TimeRange Node::get_range_around_index(const QString &input, int index, int track,
									int element) const
{
	Rational range_begin = RATIONAL_MIN;
	Rational range_end = RATIONAL_MAX;

	const NodeKeyframeTrack &key_track =
		get_immediate(input, element)->keyframe_tracks().at(track);

	if (key_track.size() > 1) {
		if (index > 0) {
			// If this is not the first key, we'll need to limit it to the key just before
			range_begin = key_track.at(index - 1)->time();
		}
		if (index < key_track.size() - 1) {
			// If this is not the last key, we'll need to limit it to the key just after
			range_end = key_track.at(index + 1)->time();
		}
	}

	return TimeRange(range_begin, range_end);
}

void Node::clear_element(const QString &input, int index)
{
	get_immediate(input, index)->delete_all_keyframes();

	if (is_input_keyframable(input)) {
		set_input_is_keyframing(input, false, index);
	}

	set_split_standard_value(input, get_split_default_value(input), index);
}

void Node::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(input)
	Q_UNUSED(element)
}

void Node::InputConnectedEvent(const QString &input, int element, Node *output)
{
	Q_UNUSED(input)
	Q_UNUSED(element)
	Q_UNUSED(output)
}

void Node::InputDisconnectedEvent(const QString &input, int element,
								  Node *output)
{
	Q_UNUSED(input)
	Q_UNUSED(element)
	Q_UNUSED(output)
}

void Node::OutputConnectedEvent(const NodeInput &input)
{
	Q_UNUSED(input)
}

void Node::OutputDisconnectedEvent(const NodeInput &input)
{
	Q_UNUSED(input)
}

void Node::childEvent(QChildEvent *event)
{
	super::childEvent(event);

	if (NodeKeyframe *key = dynamic_cast<NodeKeyframe *>(event->child())) {
		NodeInput i(this, key->input(), key->element());

		if (event->type() == QEvent::ChildAdded) {
			get_immediate(key->input(), key->element())->insert_keyframe(key);

			connect(key, &NodeKeyframe::time_changed, this,
					&Node::invalidate_from_keyframe_time_change);
			connect(key, &NodeKeyframe::value_changed, this,
					&Node::invalidate_from_keyframe_value_change);
			connect(key, &NodeKeyframe::type_changed, this,
					&Node::invalidate_from_keyframe_type_changed);
			connect(key, &NodeKeyframe::bezier_control_in_changed, this,
					&Node::invalidate_from_keyframe_bezier_in_change);
			connect(key, &NodeKeyframe::bezier_control_out_changed, this,
					&Node::invalidate_from_keyframe_bezier_out_change);

			emit keyframe_added(reinterpret_cast<OakEngineKeyframe *>(key));
			parameter_value_changed(i, get_range_affected_by_keyframe(key));
		} else if (event->type() == QEvent::ChildRemoved) {
			TimeRange time_affected = get_range_affected_by_keyframe(key);

			disconnect(key, &NodeKeyframe::time_changed, this,
					   &Node::invalidate_from_keyframe_time_change);
			disconnect(key, &NodeKeyframe::value_changed, this,
					   &Node::invalidate_from_keyframe_value_change);
			disconnect(key, &NodeKeyframe::type_changed, this,
					   &Node::invalidate_from_keyframe_type_changed);
			disconnect(key, &NodeKeyframe::bezier_control_in_changed, this,
					   &Node::invalidate_from_keyframe_bezier_in_change);
			disconnect(key, &NodeKeyframe::bezier_control_out_changed, this,
					   &Node::invalidate_from_keyframe_bezier_out_change);

			emit keyframe_removed(reinterpret_cast<OakEngineKeyframe *>(key));

			get_immediate(key->input(), key->element())->remove_keyframe(key);
			parameter_value_changed(i, time_affected);
		}
	} else if (NodeGizmo *gizmo = dynamic_cast<NodeGizmo *>(event->child())) {
		if (event->type() == QEvent::ChildAdded) {
			gizmos_.append(gizmo);
		} else if (event->type() == QEvent::ChildRemoved) {
			gizmos_.removeOne(gizmo);
		}
	}
}

void Node::invalidate_from_keyframe_bezier_in_change()
{
	NodeKeyframe *key = static_cast<NodeKeyframe *>(sender());
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);
	int keyframe_index = track.indexOf(key);

	Rational start = RATIONAL_MIN;
	Rational end = key->time();

	if (keyframe_index > 0) {
		start = track.at(keyframe_index - 1)->time();
	}

	parameter_value_changed(key->key_track_ref().input(), TimeRange(start, end));
}

void Node::invalidate_from_keyframe_bezier_out_change()
{
	NodeKeyframe *key = static_cast<NodeKeyframe *>(sender());
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);
	int keyframe_index = track.indexOf(key);

	Rational start = key->time();
	Rational end = RATIONAL_MAX;

	if (keyframe_index < track.size() - 1) {
		end = track.at(keyframe_index + 1)->time();
	}

	parameter_value_changed(key->key_track_ref().input(), TimeRange(start, end));
}

void Node::invalidate_from_keyframe_time_change()
{
	NodeKeyframe *key = static_cast<NodeKeyframe *>(sender());
	NodeInputImmediate *immediate = get_immediate(key->input(), key->element());
	TimeRange original_range = get_range_affected_by_keyframe(key);

	TimeRangeList invalidate_range;
	invalidate_range.insert(original_range);

	if (!(original_range.in() < key->time() &&
		  original_range.out() > key->time())) {
		// This keyframe needs resorting, store it and remove it from the list
		immediate->remove_keyframe(key);

		// Automatically insertion sort
		immediate->insert_keyframe(key);

		// Invalidate new area that the keyframe has been moved to
		invalidate_range.insert(get_range_affected_by_keyframe(key));
	}

	// Invalidate entire area surrounding the keyframe (either where it currently is, or where it used to be before it
	// was resorted in the if block above)
	foreach (const TimeRange &r, invalidate_range) {
		parameter_value_changed(key->key_track_ref().input(), r);
	}

	emit keyframe_time_changed(reinterpret_cast<OakEngineKeyframe *>(key));
}

void Node::invalidate_from_keyframe_value_change()
{
	NodeKeyframe *key = static_cast<NodeKeyframe *>(sender());
	parameter_value_changed(key->key_track_ref().input(),
						  get_range_affected_by_keyframe(key));

	emit keyframe_value_changed(reinterpret_cast<OakEngineKeyframe *>(key));
}

void Node::invalidate_from_keyframe_type_changed()
{
	NodeKeyframe *key = static_cast<NodeKeyframe *>(sender());
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);

	if (track.size() == 1) {
		// If there are no other frames, the interpolation won't do anything
		return;
	}

	// Invalidate entire range
	parameter_value_changed(key->key_track_ref().input(),
						  get_range_around_index(key->input(), track.indexOf(key),
											  key->track(), key->element()));

	emit keyframe_type_changed(reinterpret_cast<OakEngineKeyframe *>(key));
}

void Node::set_value_at_time(const NodeInput &input, const Rational &time,
						  const QVariant &value, int track,
						  MultiUndoCommand *command,
						  bool insert_on_all_tracks_if_no_key)
{
	if (input.is_keyframing()) {
		Rational node_time = time;

		NodeKeyframe *existing_key =
			input.get_keyframe_at_time_on_track(node_time, track);

		if (existing_key) {
			command->add_child(
				new NodeParamSetKeyframeValueCommand(existing_key, value));
		} else {
			// No existing key, create a new one
			int nb_tracks = NodeValue::get_number_of_keyframe_tracks(
				input.node()->get_input_data_type(input.input()));
			for (int i = 0; i < nb_tracks; i++) {
				QVariant track_value;

				if (i == track) {
					track_value = value;
				} else if (!insert_on_all_tracks_if_no_key) {
					continue;
				} else {
					track_value = input.node()->get_split_value_at_time_on_track(
						input.input(), node_time, i, input.element());
				}

				NodeKeyframe *new_key = new NodeKeyframe(
					node_time, track_value,
					input.node()->get_best_keyframe_type_for_time_on_track(
						NodeKeyframeTrackReference(input, i), node_time),
					i, input.element(), input.input());

				command->add_child(
					new NodeParamInsertKeyframeCommand(input.node(), new_key));
			}
		}
	} else {
		command->add_child(new NodeParamSetStandardValueCommand(
			NodeKeyframeTrackReference(input, track), value));
	}
}

bool find_path_internal(std::list<NodeInput> &vec, Node *from, Node *to,
					  int &path_index)
{
	for (auto it = from->output_connections().cbegin();
		 it != from->output_connections().cend(); it++) {
		const NodeInput &next = it->second;

		vec.push_back(next);

		if (next.node() == to) {
			// Found a path! Determine if it's the index we want
			if (path_index == 0) {
				// It is!
				return true;
			} else {
				// It isn't, keep looking...
				path_index--;
			}
		}

		if (find_path_internal(vec, next.node(), to, path_index)) {
			return true;
		}

		vec.pop_back();
	}

	return false;
}

std::list<NodeInput> Node::find_path(Node *from, Node *to, int path_index)
{
	std::list<NodeInput> v;

	find_path_internal(v, from, to, path_index);

	return v;
}

bool Node::ValueHint::load(QXmlStreamReader *reader)
{
	uint version = 0;
	XMLAttributeLoop(reader, attr)
	{
		version = attr.value().toUInt();
	}

	Q_UNUSED(version)

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("types")) {
			QVector<NodeValue::Type> types;
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("type")) {
					types.append(static_cast<NodeValue::Type>(
						reader->readElementText().toInt()));
				} else {
					reader->skipCurrentElement();
				}
			}
			this->set_type(types);
		} else if (reader->name() == QStringLiteral("index")) {
			this->set_index(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("tag")) {
			this->set_tag(reader->readElementText());
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void Node::ValueHint::save(QXmlStreamWriter *writer) const
{
	writer->writeAttribute(QStringLiteral("version"), QString::number(1));

	writer->writeStartElement(QStringLiteral("types"));

	for (auto it = this->types().cbegin(); it != this->types().cend(); it++) {
		writer->writeTextElement(QStringLiteral("type"), QString::number(*it));
	}

	writer->writeEndElement(); // types

	writer->writeTextElement(QStringLiteral("index"),
							 QString::number(this->index()));

	writer->writeTextElement(QStringLiteral("tag"), this->tag());
}

bool Node::Position::load(QXmlStreamReader *reader)
{
	bool got_pos_x = false;
	bool got_pos_y = false;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("x")) {
			this->position.setX(reader->readElementText().toDouble());
			got_pos_x = true;
		} else if (reader->name() == QStringLiteral("y")) {
			this->position.setY(reader->readElementText().toDouble());
			got_pos_y = true;
		} else if (reader->name() == QStringLiteral("expanded")) {
			this->expanded = reader->readElementText().toInt();
		} else {
			reader->skipCurrentElement();
		}
	}

	return got_pos_x && got_pos_y;
}

void Node::Position::save(QXmlStreamWriter *writer) const
{
	writer->writeTextElement(QStringLiteral("x"),
							 QString::number(this->position.x()));
	writer->writeTextElement(QStringLiteral("y"),
							 QString::number(this->position.y()));
	writer->writeTextElement(QStringLiteral("expanded"),
							 QString::number(this->expanded));
}

}
