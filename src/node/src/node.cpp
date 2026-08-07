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

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "lerp.h"
#include "configaccessor.h"
#include "../c_api/nodehandle.h"
#include "group/group.h"
#include "project/serializer/typeserializer.h"
#include "nodeundo.h"
#include "project.h"
#include "serializeddata.h"
#include "ui/colorcoding.h"

namespace olive
{

/**
 * @brief QString::number() equivalent for doubles ('g', 6 significant digits)
 */
static std::string number_to_string(double d)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%g", d);
	return buf;
}

const std::string Node::k_enabled_input = "enabled_in";

namespace
{

/**
 * @brief Invalidate a node-owned cache over a rational time range
 *        through the oakrender C ABI.
 */
void invalidate_handle_cache(const OakRenderCache &cache,
							 const olive::TimeRange &range)
{
	oakrender_cache_invalidate_range(cache, range.in().numerator(),
									 range.in().denominator(),
									 range.out().numerator(),
									 range.out().denominator());
}

/**
 * @brief Fetch a cache UUID through the two-stage string API.
 */
std::string cache_uuid_string(const OakRenderCache &cache)
{
	int needed = oakrender_cache_get_uuid(cache, nullptr, 0);
	if (needed <= 0) {
		return std::string();
	}
	std::string uuid(size_t(needed), '\0');
	oakrender_cache_get_uuid(cache, uuid.data(), needed);
	uuid.resize(size_t(needed - 1));
	return uuid;
}

} // namespace

Node::Node()
	: override_color_(-1)
	, folder_(nullptr)
	, parent_(nullptr)
	, flags_(k_none)
	, caches_enabled_(true)
{
	add_input(k_enabled_input, NodeValue::k_boolean, true);

	// Borrowed self-handle: the caches keep a native back-pointer
	// inside oakrender, the box goes away right here
	OakNodeNode self = oaknode_c_api::make_handle<OakNodeNode>(
		this, false, nullptr);
	video_cache_ = oakrender_cache_create_for_node(
		self, OAKRENDER_CACHE_VIDEO_FRAME);
	thumbnail_cache_ = oakrender_cache_create_for_node(
		self, OAKRENDER_CACHE_THUMBNAIL);
	audio_cache_ = oakrender_cache_create_for_node(
		self, OAKRENDER_CACHE_AUDIO_PLAYBACK);
	waveform_cache_ = oakrender_cache_create_for_node(
		self, OAKRENDER_CACHE_AUDIO_WAVEFORM);
	self.release(self.ctx);

	oakrender_cache_set_saving_enabled(waveform_cache_, 0);
}

Node::~Node()
{
	// Disconnect all edges
	disconnect_all();

	// Remove self from the graph while we're still a fully formed Node
	set_parent(nullptr);

	// Remove all immediates
	for (const auto &pair : standard_immediates_) {
		delete pair.second;
	}
	for (auto it = array_immediates_.cbegin(); it != array_immediates_.cend();
		 it++) {
		for (NodeInputImmediate *i : it->second) {
			delete i;
		}
	}

	oakrender_cache_free(&video_cache_);
	oakrender_cache_free(&thumbnail_cache_);
	oakrender_cache_free(&audio_cache_);
	oakrender_cache_free(&waveform_cache_);
}

std::string Node::short_name() const
{
	return name();
}

std::string Node::description() const
{
	// Return an empty string by default
	return std::string();
}

void Node::retranslate()
{
	set_input_name(k_enabled_input, "Enabled");
}

Variant Node::data(const DataType &d) const
{
	if (d == icon) {
		// Just a meaningless default icon to be used where necessary
		return std::string("new");
	}

	return Variant();
}

bool Node::set_node_position_in_context(Node *node, const PointF &pos)
{
	auto it = context_positions_.find(node);
	Position p = it != context_positions_.end() ? it->second : Position();

	p.position = pos;

	return set_node_position_in_context(node, p);
}

bool Node::set_node_position_in_context(Node *node, const Position &pos)
{
	bool added = !context_contains_node(node);
	context_positions_[node] = pos;

	return added;
}

bool Node::remove_node_from_context(Node *node)
{
	if (context_contains_node(node)) {
		context_positions_.erase(node);
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
		c = OAK_CONFIG_STR("CatColor" + std::to_string(int(this->category().front())))
				.to_int();
	}

	return ColorCoding::get_color(c);
}

void Node::connect_edge(Node *output, const NodeInput &input)
{
	// Ensure graph is the same
	assert(input.node()->parent() == output->parent());

	// Ensure a connection isn't getting overwritten
	assert(input.node()->input_connections().find(input) ==
		   input.node()->input_connections().end());

	// Insert connection on both sides
	input.node()->input_connections_[input] = output;
	output->output_connections_.push_back(
		std::pair<Node *, NodeInput>({ output, input }));

	// Call internal events
	input.node()->InputConnectedEvent(input.input(), input.element(), output);
	output->OutputConnectedEvent(input);

	// Invalidate all if this node isn't ignoring this input
	if (!(input.node()->get_input_flags(input.input()) &
		  k_input_flag_ignore_invalidations)) {
		input.node()->invalidate_all(input.input(), input.element());
	}
}

void Node::disconnect_edge(Node *output, const NodeInput &input, bool silent)
{
	// Ensure graph is the same
	assert(input.node()->parent() == output->parent());

	// Ensure connection exists
	assert(input.node()->input_connections().at(input) == output);

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

	if (std::getenv("OAK_DEBUG_EDGES")) {
		fprintf(stderr, "EDGE-DEBUG: disconnect_edge %p -> %p (%s)\n",
				(void *)output, (void *)input.node(), input.input().c_str());
	}

	// Call internal events
	input.node()->InputDisconnectedEvent(input.input(), input.element(),
										 output);
	output->OutputDisconnectedEvent(input);

	if (!(input.node()->get_input_flags(input.input()) &
		  k_input_flag_ignore_invalidations)) {
		input.node()->invalidate_all(input.input(), input.element());
	}
}

void Node::copy_cache_uuids_from(Node *n)
{
	auto copy_cache_uuid = [](const OakRenderCache &from, OakRenderCache *to) {
		oakrender_cache_set_uuid(*to, cache_uuid_string(from).c_str());
	};

	copy_cache_uuid(n->video_cache_, &video_cache_);
	copy_cache_uuid(n->audio_cache_, &audio_cache_);
	copy_cache_uuid(n->thumbnail_cache_, &thumbnail_cache_);
	copy_cache_uuid(n->waveform_cache_, &waveform_cache_);
}

std::string Node::get_input_name(const std::string &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->human_name;
	} else {
		report_invalid_input("get name of", id, -1);
		return std::string();
	}
}

bool Node::is_input_hidden(const std::string &input) const
{
	return (get_input_flags(input) & k_input_flag_hidden);
}

bool Node::is_input_connectable(const std::string &input) const
{
	return !(get_input_flags(input) & k_input_flag_not_connectable);
}

bool Node::is_input_keyframable(const std::string &input) const
{
	return !(get_input_flags(input) & k_input_flag_not_keyframable);
}

bool Node::is_input_keyframing(const std::string &input, int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->is_keyframing();
	} else {
		report_invalid_input("get keyframing state of", input, element);
		return false;
	}
}

void Node::set_input_is_keyframing(const std::string &input, bool e, int element)
{
	if (!is_input_keyframable(input)) {
		fprintf(stderr,
				"Ignored set keyframing of %s because this input is not keyframable\n",
				input.c_str());
		return;
	}

	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		imm->set_is_keyframing(e);
	} else {
		report_invalid_input("set keyframing state of", input, element);
	}
}

bool Node::is_input_connected(const std::string &input, int element) const
{
	return get_connected_output(input, element);
}

Node *Node::get_connected_output(const std::string &input, int element) const
{
	for (auto it = input_connections_.cbegin(); it != input_connections_.cend();
		 it++) {
		if (it->first.input() == input && it->first.element() == element) {
			return it->second;
		}
	}

	return nullptr;
}

bool Node::is_using_standard_value(const std::string &input, int track,
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

NodeValue::Type Node::get_input_data_type(const std::string &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->type;
	} else {
		report_invalid_input("get data type of", id, -1);
		return NodeValue::k_none;
	}
}

void Node::set_input_data_type(const std::string &id, const NodeValue::Type &type)
{
	Input *input_meta = get_internal_input_data(id);

	if (input_meta) {
		input_meta->type = type;

		int array_sz = input_array_size(id);
		for (int i = -1; i < array_sz; i++) {
			get_immediate(id, i)->set_data_type(type);
		}
	} else {
		report_invalid_input("set data type of", id, -1);
	}
}

bool Node::has_input_property(const std::string &id, const std::string &name) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->properties.count(name);
	} else {
		report_invalid_input("get property of", id, -1);
		return false;
	}
}

std::map<std::string, Variant> Node::get_input_properties(const std::string &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->properties;
	} else {
		report_invalid_input("get property table of", id, -1);
		return std::map<std::string, Variant>();
	}
}

Variant Node::get_input_property(const std::string &id, const std::string &name) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		auto it = i->properties.find(name);
		return it != i->properties.end() ? it->second : Variant();
	} else {
		report_invalid_input("get property of", id, -1);
		return Variant();
	}
}

void Node::set_input_property(const std::string &id, const std::string &name,
							const Variant &value)
{
	Input *i = get_internal_input_data(id);

	if (i) {
		i->properties[name] = value;
	} else {
		report_invalid_input("set property of", id, -1);
	}
}

SplitValue Node::get_split_value_at_time(const std::string &input, const Rational &time,
								 int element) const
{
	SplitValue vals;

	int nb_tracks = get_number_of_keyframe_tracks(input);

	for (int i = 0; i < nb_tracks; i++) {
		vals.push_back(get_split_value_at_time_on_track(input, time, i, element));
	}

	return vals;
}

Variant Node::get_split_value_at_time_on_track(const std::string &input,
										  const Rational &time, int track,
										  int element) const
{
	if (!is_using_standard_value(input, track, element)) {
		const NodeKeyframeTrack &key_track =
			get_keyframe_tracks(input, element).at(track);

		if (key_track.front()->time() >= time) {
			// This time precedes any keyframe, so we just return the first value
			return key_track.front()->value();
		}

		if (key_track.back()->time() <= time) {
			// This time is after any keyframes so we return the last value
			return key_track.back()->value();
		}

		NodeValue::Type type = get_input_data_type(input);

		// If we're here, the time must be somewhere in between the keyframes
		NodeKeyframe *before = nullptr, *after = nullptr;

		int low = 0;
		int high = int(key_track.size()) - 1;
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
					before_val = (before->value().can_convert<Rational>() ?
									  before->value().value<Rational>() :
									  Rational::from_double(
										  before->value().to_double()))
									 .to_double();
					after_val = (after->value().can_convert<Rational>() ?
									 after->value().value<Rational>() :
									 Rational::from_double(
										 after->value().to_double()))
									.to_double();
				} else {
					before_val = before->value().to_double();
					after_val = after->value().to_double();
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
					double period_progress =
						(time.to_double() - before->time().to_double()) /
						(after->time().to_double() - before->time().to_double());

					interpolated = lerp(before_val, after_val, period_progress);
				}

				if (type == NodeValue::k_rational) {
					return Variant::from_value(
						Rational::from_double(interpolated));
				} else {
					return interpolated;
				}
			}
		} else {
			fprintf(stderr, "Binary search for keyframes failed\n");
		}
	}

	return get_split_standard_value_on_track(input, track, element);
}

Variant Node::get_default_value(const std::string &input) const
{
	NodeValue::Type type = get_input_data_type(input);

	return NodeValue::combine_track_values_into_normal_value(
		type, get_split_default_value(input));
}

SplitValue Node::get_split_default_value(const std::string &input) const
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return i->default_value;
	} else {
		report_invalid_input("retrieve default value of", input, -1);
		return SplitValue();
	}
}

Variant Node::get_split_default_value_on_track(const std::string &input,
										   int track) const
{
	SplitValue val = get_split_default_value(input);
	if (track < int(val.size())) {
		return val.at(track);
	} else {
		return Variant();
	}
}

void Node::set_default_value(const std::string &input, const Variant &val)
{
	NodeValue::Type type = get_input_data_type(input);

	set_split_default_value(
		input, NodeValue::split_normal_value_into_track_values(type, val));
}

void Node::set_split_default_value(const std::string &input, const SplitValue &val)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		i->default_value = val;
	} else {
		report_invalid_input("set default value of", input, -1);
	}
}

void Node::set_split_default_value_on_track(const std::string &input,
									   const Variant &val, int track)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		if (track < int(i->default_value.size())) {
			i->default_value[track] = val;
		}
	} else {
		report_invalid_input("set default value on track of", input, -1);
	}
}

const std::vector<NodeKeyframeTrack> &Node::get_keyframe_tracks(const std::string &input,
														  int element) const
{
	return get_immediate(input, element)->keyframe_tracks();
}

std::vector<NodeKeyframe *> Node::get_keyframes_at_time(const std::string &input,
												 const Rational &time,
												 int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_keyframe_at_time(time);
	} else {
		report_invalid_input("get keyframes at time from", input, element);
		return std::vector<NodeKeyframe *>();
	}
}

NodeKeyframe *Node::get_keyframe_at_time_on_track(const std::string &input,
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

NodeKeyframe::Type Node::get_best_keyframe_type_for_time_on_track(const std::string &input,
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

int Node::get_number_of_keyframe_tracks(const std::string &id) const
{
	return NodeValue::get_number_of_keyframe_tracks(get_input_data_type(id));
}

NodeKeyframe *Node::get_earliest_keyframe(const std::string &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_earliest_keyframe();
	} else {
		report_invalid_input("get earliest keyframe from", id, element);
		return nullptr;
	}
}

NodeKeyframe *Node::get_latest_keyframe(const std::string &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_latest_keyframe();
	} else {
		report_invalid_input("get latest keyframe from", id, element);
		return nullptr;
	}
}

NodeKeyframe *Node::get_closest_keyframe_before_time(const std::string &id,
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

NodeKeyframe *Node::get_closest_keyframe_after_time(const std::string &id,
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

bool Node::has_keyframe_at_time(const std::string &id, const Rational &time,
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

StringList Node::get_combo_box_strings(const std::string &id) const
{
	return get_input_property(id, "combo_str").to_string_list();
}

Variant Node::get_standard_value(const std::string &id, int element) const
{
	NodeValue::Type type = get_input_data_type(id);

	return NodeValue::combine_track_values_into_normal_value(
		type, get_split_standard_value(id, element));
}

SplitValue Node::get_split_standard_value(const std::string &id, int element) const
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		return imm->get_split_standard_value();
	} else {
		report_invalid_input("get standard value of", id, element);
		return SplitValue();
	}
}

Variant Node::get_split_standard_value_on_track(const std::string &input, int track,
											int element) const
{
	NodeInputImmediate *imm = get_immediate(input, element);

	if (imm) {
		return imm->get_split_standard_value_on_track(track);
	} else {
		report_invalid_input("get standard value of", input, element);
		return Variant();
	}
}

void Node::set_standard_value(const std::string &id, const Variant &value,
							int element)
{
	NodeValue::Type type = get_input_data_type(id);

	set_split_standard_value(
		id, NodeValue::split_normal_value_into_track_values(type, value),
		element);
}

void Node::set_split_standard_value(const std::string &id, const SplitValue &value,
								 int element)
{
	NodeInputImmediate *imm = get_immediate(id, element);

	if (imm) {
		imm->set_split_standard_value(value);

		for (int i = 0; i < int(value.size()); i++) {
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

void Node::set_split_standard_value_on_track(const std::string &id, int track,
										const Variant &value, int element)
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

bool Node::input_is_array(const std::string &id) const
{
	return get_input_flags(id) & k_input_flag_array;
}

void Node::input_array_insert(const std::string &id, int index)
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

void Node::input_array_resize(const std::string &id, int size)
{
	if (input_array_size(id) == size) {
		return;
	}

	NodeArrayResizeCommand *c = new NodeArrayResizeCommand(this, id, size);
	c->redo_now();
	delete c;
}

void Node::input_array_remove(const std::string &id, int index)
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

int Node::input_array_size(const std::string &id) const
{
	const Input *i = get_internal_input_data(id);

	if (i) {
		return i->array_size;
	} else {
		report_invalid_input("retrieve array size of", id, -1);
		return 0;
	}
}

void Node::set_value_hint_for_input(const std::string &input, const ValueHint &hint,
								int element)
{
	value_hints_[{ input, element }] = hint;

	invalidate_all(input, element);
}

const NodeKeyframeTrack &Node::get_track_from_keyframe(NodeKeyframe *key) const
{
	return get_immediate(key->input(), key->element())
		->keyframe_tracks()
		.at(key->track());
}

NodeInputImmediate *Node::get_immediate(const std::string &input, int element) const
{
	if (element == -1) {
		auto it = standard_immediates_.find(input);
		return it != standard_immediates_.end() ? it->second : nullptr;
	} else {
		auto it = array_immediates_.find(input);
		if (it != array_immediates_.end()) {
			const std::vector<NodeInputImmediate *> &imm_arr = it->second;

			if (element >= 0 && element < int(imm_arr.size())) {
				return imm_arr.at(element);
			}
		}
	}

	return nullptr;
}

InputFlags Node::get_input_flags(const std::string &input) const
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return i->flags;
	} else {
		report_invalid_input("retrieve flags of", input, -1);
		return InputFlags(k_input_flag_normal);
	}
}

void Node::set_input_flag(const std::string &input, InputFlag f, bool on)
{
	Input *i = get_internal_input_data(input);

	if (i) {
		if (on) {
			i->flags |= f;
		} else {
			i->flags &= ~f;
		}
	} else {
		report_invalid_input("set flags of", input, -1);
	}
}

void Node::value(const NodeValueRow &value, const NodeGlobals &globals,
				 NodeValueTable *table) const
{
	// Do nothing
	(void)value;
	(void)globals;
	(void)table;
}

void Node::invalidate_cache(const TimeRange &range, const std::string &from,
						   int element, InvalidateCacheOptions options)
{
	(void)from;
	(void)element;

	if (are_caches_enabled()) {
		if (range.in() != range.out()) {
			TimeRange vr = range.intersected(get_video_cache_range());
			if (vr.length() != 0) {
				invalidate_handle_cache(video_cache_, vr);
				invalidate_handle_cache(thumbnail_cache_, vr);
			}
			TimeRange ar = range.intersected(get_audio_cache_range());
			if (ar.length() != 0) {
				invalidate_handle_cache(audio_cache_, ar);
				invalidate_handle_cache(waveform_cache_, ar);
			}
		}
	}

	send_invalidate_cache(range, options);
}

TimeRange Node::input_time_adjustment(const std::string &, int,
									const TimeRange &input_time,
									bool clamp) const
{
	// Default behavior is no time adjustment at all
	return input_time;
}

TimeRange Node::output_time_adjustment(const std::string &, int,
									 const TimeRange &input_time) const
{
	// Default behavior is no time adjustment at all
	return input_time;
}

std::vector<Node *> Node::copy_dependency_graph(const std::vector<Node *> &nodes,
										  MultiUndoCommand *command)
{
	int nb_nodes = int(nodes.size());

	std::vector<Node *> copies(nb_nodes);

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
			c->set_parent(graph);
		}

		// Store in array at the same index as source
		copies[i] = c;
	}

	copy_dependency_graph(nodes, copies, command);

	return copies;
}

void Node::copy_dependency_graph(const std::vector<Node *> &src,
							   const std::vector<Node *> &dst,
							   MultiUndoCommand *command)
{
	for (size_t i = 0; i < src.size(); i++) {
		Node *src_node = src.at(i);
		Node *dst_node = dst.at(i);

		for (auto it = src_node->input_connections_.cbegin();
			 it != src_node->input_connections_.cend(); it++) {
			// Determine if the connected node is in our src list
			auto src_it = std::find(src.begin(), src.end(), it->second);
			int connection_index =
				src_it != src.end() ? int(src_it - src.begin()) : -1;

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
	std::map<Node *, Node *> &created, Node *node, MultiUndoCommand *command)
{
	// Make a new node of the same type
	Node *copy = node->copy();

	// Add to map
	created[node] = copy;

	// Add it to the same graph
	command->add_child(new NodeAddCommand(node->parent(), copy));

	// Copy context children
	const PositionMap &map = node->get_context_positions();
	for (auto it = map.cbegin(); it != map.cend(); it++) {
		// Add either the copy (if it exists) or the original node to the context
		Node *child;

		if (it->first->is_item()) {
			child = it->first;
		} else {
			auto created_it = created.find(it->first);
			child = created_it != created.end() ? created_it->second : nullptr;
			if (!child) {
				child = copy_node_and_dependency_graph_minus_items_internal(
					created, it->first, command);
			}
		}

		command->add_child(new NodeSetPositionCommand(child, copy, it->second));
	}

	// If this is a group, copy input and output passthroughs
	if (NodeGroup *src_group = dynamic_cast<NodeGroup *>(node)) {
		NodeGroup *dst_group = static_cast<NodeGroup *>(copy);

		for (auto it = src_group->get_input_passthroughs().cbegin();
			 it != src_group->get_input_passthroughs().cend(); it++) {
			// This node should have been created by the context loop above
			NodeInput input = it->second;
			auto created_it = created.find(input.node());
			input.set_node(created_it != created.end() ? created_it->second :
														 nullptr);
			command->add_child(
				new NodeGroupAddInputPassthrough(dst_group, input, it->first));
		}

		auto output_it = created.find(src_group->get_output_passthrough());
		command->add_child(new NodeGroupSetOutputPassthrough(
			dst_group, output_it != created.end() ? output_it->second : nullptr));
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
			auto created_it = created.find(connected);
			connected_copy =
				created_it != created.end() ? created_it->second : nullptr;
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
	std::map<Node *, Node *> created;

	return copy_node_and_dependency_graph_minus_items_internal(created, node, command);
}

Node *Node::copy_node_in_graph(Node *node, MultiUndoCommand *command)
{
	Node *copy;

	if (OAK_CONFIG("SplitClipsCopyNodes").to_bool()) {
		copy = Node::copy_node_and_dependency_graph_minus_items(node, command);
	} else {
		copy = node->copy();

		command->add_child(new NodeAddCommand(node->parent(), copy));

		copy_inputs(node, copy, true, command);

		const PositionMap &map = node->get_context_positions();
		for (auto it = map.cbegin(); it != map.cend(); it++) {
			// Add to the context
			command->add_child(
				new NodeSetPositionCommand(it->first, copy, it->second));
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

void Node::invalidate_all(const std::string &input, int element)
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

	a->links_.push_back(b);
	b->links_.push_back(a);

	a->LinkChangeEvent();
	b->LinkChangeEvent();

	return true;
}

bool Node::unlink(Node *a, Node *b)
{
	if (!are_linked(a, b)) {
		return false;
	}

	a->links_.erase(std::find(a->links_.begin(), a->links_.end(), b));
	b->links_.erase(std::find(b->links_.begin(), b->links_.end(), a));

	a->LinkChangeEvent();
	b->LinkChangeEvent();

	return true;
}

bool Node::are_linked(Node *a, Node *b)
{
	return std::find(a->links_.begin(), a->links_.end(), b) != a->links_.end();
}

bool Node::load(XmlStreamReader *reader, SerializedData *data)
{
	unsigned int version = 0;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "ptr") {
			uintptr_t ptr = strtoull(attr.value.c_str(), nullptr, 10);
			data->node_ptrs[ptr] = this;
		} else if (attr.name == "version") {
			version = unsigned(strtoul(attr.value.c_str(), nullptr, 10));
		}
	}

	(void)version;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "input") {
			load_input(reader, data);
		} else if (reader->name() == "label") {
			this->set_label(reader->read_element_text());
		} else if (reader->name() == "color") {
			this->set_override_color(
				int(strtol(reader->read_element_text().c_str(), nullptr, 10)));
		} else if (reader->name() == "links") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "link") {
					data->block_links.push_back(
						{ this,
						  strtoull(reader->read_element_text().c_str(), nullptr,
								   10) });
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "custom") {
			if (!load_custom(reader, data)) {
				return false;
			}
		} else if (reader->name() == "connections") {
			// Load connections
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "connection") {
					std::string param_id;
					int ele = -1;

					for (const XmlStreamAttribute &attr : reader->attributes()) {
						if (attr.name == "element") {
							ele = int(strtol(attr.value.c_str(), nullptr, 10));
						} else if (attr.name == "input") {
							param_id = attr.value;
						}
					}

					// Translate IDs renamed after older project files were
					// written
					param_id = get_input_id_for_legacy_id(param_id);

					std::string output_node_id;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == "output") {
							output_node_id = reader->read_element_text();
						} else {
							reader->skip_current_element();
						}
					}

					data->desired_connections.push_back(
						{ NodeInput(this, param_id, ele),
						  strtoull(output_node_id.c_str(), nullptr, 10) });
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "hints") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "hint") {
					std::string input;
					int element = -1;

					for (const XmlStreamAttribute &attr : reader->attributes()) {
						if (attr.name == "input") {
							input = attr.value;
						} else if (attr.name == "element") {
							element = int(strtol(attr.value.c_str(), nullptr, 10));
						}
					}

					Node::ValueHint vh;
					if (!vh.load(reader)) {
						return false;
					}
					this->set_value_hint_for_input(input, vh, element);
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "context") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "node") {
					uintptr_t node_ptr = 0;

					for (const XmlStreamAttribute &attr : reader->attributes()) {
						if (attr.name == "ptr") {
							node_ptr = strtoull(attr.value.c_str(), nullptr, 10);
						}
					}

					if (node_ptr) {
						Node::Position node_pos;
						if (!node_pos.load(reader)) {
							return false;
						}
						data->positions[this][node_ptr] = node_pos;
					} else {
						return false;
					}
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "caches") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "audio") {
					oakrender_cache_set_uuid(
						audio_cache_, reader->read_element_text().c_str());
				} else if (reader->name() == "video") {
					oakrender_cache_set_uuid(
						video_cache_, reader->read_element_text().c_str());
				} else if (reader->name() == "thumb") {
					oakrender_cache_set_uuid(
						thumbnail_cache_, reader->read_element_text().c_str());
				} else if (reader->name() == "waveform") {
					oakrender_cache_set_uuid(
						waveform_cache_, reader->read_element_text().c_str());
				} else {
					reader->skip_current_element();
				}
			}
		} else {
			reader->skip_current_element();
		}
	}

	this->LoadFinishedEvent();

	return true;
}

void Node::save(XmlStreamWriter *writer) const
{
	writer->write_attribute("version", std::to_string(1));
	writer->write_attribute("id", this->id());
	writer->write_attribute("ptr",
						   std::to_string(reinterpret_cast<uintptr_t>(this)));

	if (!this->get_label().empty()) {
		writer->write_text_element("label", this->get_label());
	}

	if (this->get_override_color() != -1) {
		writer->write_text_element("color",
								 std::to_string(this->get_override_color()));
	}

	for (const std::string &input : this->inputs()) {
		writer->write_start_element("input");

		save_input(writer, input);

		writer->write_end_element(); // input
	}

	if (!this->links().empty()) {
		writer->write_start_element("links");
		for (Node *link : this->links()) {
			writer->write_text_element(
				"link", std::to_string(reinterpret_cast<uintptr_t>(link)));
		}
		writer->write_end_element(); // links
	}

	if (!this->input_connections().empty()) {
		writer->write_start_element("connections");
		for (auto it = this->input_connections().cbegin();
			 it != this->input_connections().cend(); it++) {
			writer->write_start_element("connection");

			writer->write_attribute("input", it->first.input());
			writer->write_attribute("element",
								   std::to_string(it->first.element()));

			writer->write_text_element(
				"output",
				std::to_string(reinterpret_cast<uintptr_t>(it->second)));

			writer->write_end_element(); // connection
		}
		writer->write_end_element(); // connections
	}

	if (!this->get_value_hints().empty()) {
		writer->write_start_element("hints");
		for (auto it = this->get_value_hints().cbegin();
			 it != this->get_value_hints().cend(); it++) {
			writer->write_start_element("hint");

			writer->write_attribute("input", it->first.input);
			writer->write_attribute("element",
								   std::to_string(it->first.element));

			it->second.save(writer);

			writer->write_end_element(); // hint
		}
		writer->write_end_element(); // hints
	}

	const Node::PositionMap &map = this->get_context_positions();

	if (!map.empty()) {
		writer->write_start_element("context");

		for (auto jt = map.cbegin(); jt != map.cend(); jt++) {
			writer->write_start_element("node");
			writer->write_attribute(
				"ptr", std::to_string(reinterpret_cast<uintptr_t>(jt->first)));
			jt->second.save(writer);
			writer->write_end_element(); // node
		}

		writer->write_end_element(); // context
	}

	writer->write_start_element("caches");

	writer->write_text_element("audio", cache_uuid_string(audio_cache_));
	writer->write_text_element("video", cache_uuid_string(video_cache_));
	writer->write_text_element("thumb", cache_uuid_string(thumbnail_cache_));
	writer->write_text_element("waveform",
							   cache_uuid_string(waveform_cache_));

	writer->write_end_element(); // caches

	writer->write_start_element("custom");

	save_custom(writer);

	writer->write_end_element(); // custom
}

bool Node::load_custom(XmlStreamReader *reader, SerializedData *data)
{
	reader->skip_current_element();
	return true;
}

void Node::PostLoadEvent(SerializedData *data)
{
	// Resolve positions
	auto pos_it = data->positions.find(this);
	if (pos_it == data->positions.end()) {
		return;
	}

	for (auto jt = pos_it->second.cbegin(); jt != pos_it->second.cend(); jt++) {
		auto node_it = data->node_ptrs.find(jt->first);
		if (node_it != data->node_ptrs.end() && node_it->second) {
			this->set_node_position_in_context(node_it->second, jt->second);
		}
	}
}

std::string Node::get_input_id_for_legacy_id(const std::string &id) const
{
	return id;
}

bool Node::load_input(XmlStreamReader *reader, SerializedData *data)
{
	if (dynamic_cast<NodeGroup *>(this)) {
		// Ignore input of group
		reader->skip_current_element();
		return true;
	}

	std::string param_id;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "id") {
			param_id = attr.value;

			break;
		}
	}

	if (param_id.empty()) {
		fprintf(stderr, "Failed to load parameter with missing ID\n");
		reader->skip_current_element();
		return false;
	}

	// Translate IDs renamed after older project files were written
	param_id = get_input_id_for_legacy_id(param_id);

	if (!this->has_input_with_id(param_id)) {
		fprintf(stderr, "Failed to load parameter that didn't exist: %s\n",
				param_id.c_str());
		reader->skip_current_element();
		return false;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "primary") {
			// Load primary immediate
			if (!load_immediate(reader, param_id, -1, data)) {
				return false;
			}
		} else if (reader->name() == "subelements") {
			// Load subelements
			for (const XmlStreamAttribute &attr : reader->attributes()) {
				if (attr.name == "count") {
					this->input_array_resize(
						param_id, int(strtol(attr.value.c_str(), nullptr, 10)));
				}
			}

			int element_counter = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "element") {
					if (!load_immediate(reader, param_id, element_counter,
									   data)) {
						return false;
					}

					element_counter++;
				} else {
					reader->skip_current_element();
				}
			}
		} else {
			reader->skip_current_element();
		}
	}

	return true;
}

void Node::save_input(XmlStreamWriter *writer, const std::string &id) const
{
	writer->write_attribute("id", id);

	writer->write_start_element("primary");

	save_immediate(writer, id, -1);

	writer->write_end_element(); // primary

	int arr_sz = this->input_array_size(id);

	if (arr_sz > 0) {
		writer->write_start_element("subelements");

		writer->write_attribute("count", std::to_string(arr_sz));

		for (int i = 0; i < arr_sz; i++) {
			writer->write_start_element("element");

			save_immediate(writer, id, i);

			writer->write_end_element(); // element
		}

		writer->write_end_element(); // subelements
	}
}

bool Node::load_immediate(XmlStreamReader *reader, const std::string &input,
						 int element, SerializedData *data)
{
	NodeValue::Type data_type = this->get_input_data_type(input);

	// HACK: SubtitleParams contain the actual subtitle data, so loading/replacing it will overwrite
	//       the valid subtitles. We hack around it by simply skipping loading subtitles, we'll see
	//       if this ends up being an issue in the future.
	if (data_type == NodeValue::k_subtitle_params) {
		reader->skip_current_element();
		return true;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "standard") {
			// Load standard value
			int val_index = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "track") {
					Variant value_on_track;

					if (data_type == NodeValue::k_video_params) {
						VideoParams vp;
						vp.load(reader);
						value_on_track = Variant::from_value(vp);
					} else if (data_type == NodeValue::k_audio_params) {
						AudioParams ap =
							TypeSerializer::load_audio_params(reader);
						value_on_track = Variant::from_value(ap);
					} else {
						std::string value_text = reader->read_element_text();

						if (!value_text.empty()) {
							value_on_track = NodeValue::string_to_value(
								data_type, value_text, true);
						}
					}

					this->set_split_standard_value_on_track(input, val_index,
													   value_on_track, element);

					val_index++;
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "keyframing") {
			bool k = strtol(reader->read_element_text().c_str(), nullptr, 10);
			if (this->is_input_keyframable(input)) {
				this->set_input_is_keyframing(input, k, element);
			}
		} else if (reader->name() == "keyframes") {
			int track = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "track") {
					while (xml_read_next_start_element(reader)) {
						if (reader->name() == "key") {
							// Constructed with this node as parent (replaces
							// the former setParent() call); load() overwrites
							// the placeholder time/type/value below
							NodeKeyframe *key = new NodeKeyframe(
								Rational(), Variant(), NodeKeyframe::k_linear,
								track, element, input, this);

							if (!key->load(reader, data_type)) {
								delete key;
								return false;
							}
							add_keyframe(key);
						} else {
							reader->skip_current_element();
						}
					}

					track++;
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "csinput") {
			this->set_input_property(input, "col_input",
								   reader->read_element_text());
		} else if (reader->name() == "csdisplay") {
			this->set_input_property(input, "col_display",
								   reader->read_element_text());
		} else if (reader->name() == "csview") {
			this->set_input_property(input, "col_view",
								   reader->read_element_text());
		} else if (reader->name() == "cslook") {
			this->set_input_property(input, "col_look",
								   reader->read_element_text());
		} else {
			reader->skip_current_element();
		}
	}

	return true;
}

void Node::save_immediate(XmlStreamWriter *writer, const std::string &input,
						 int element) const
{
	bool is_keyframing = this->is_input_keyframing(input, element);

	if (this->is_input_keyframable(input)) {
		writer->write_text_element("keyframing",
								 std::to_string(int(is_keyframing)));
	}

	NodeValue::Type data_type = this->get_input_data_type(input);

	// Write standard value
	writer->write_start_element("standard");

	for (const Variant &v : this->get_split_standard_value(input, element)) {
		writer->write_start_element("track");

		if (data_type == NodeValue::k_video_params) {
			v.value<VideoParams>().save(writer);
		} else if (data_type == NodeValue::k_audio_params) {
			TypeSerializer::save_audio_params(writer, v.value<AudioParams>());
		} else {
			writer->write_characters(
				NodeValue::value_to_string(data_type, v, true));
		}

		writer->write_end_element(); // track
	}

	writer->write_end_element(); // standard

	// Write keyframes
	if (is_keyframing) {
		writer->write_start_element("keyframes");

		for (const NodeKeyframeTrack &track :
			 this->get_keyframe_tracks(input, element)) {
			writer->write_start_element("track");

			for (NodeKeyframe *key : track) {
				writer->write_start_element("key");

				key->save(writer, data_type);

				writer->write_end_element(); // key
			}

			writer->write_end_element(); // track
		}

		writer->write_end_element(); // keyframes
	}

	if (data_type == NodeValue::k_color) {
		// Save color management information
		writer->write_text_element(
			"csinput",
			this->get_input_property(input, "col_input").to_string());
		writer->write_text_element(
			"csdisplay",
			this->get_input_property(input, "col_display").to_string());
		writer->write_text_element(
			"csview",
			this->get_input_property(input, "col_view").to_string());
		writer->write_text_element(
			"cslook",
			this->get_input_property(input, "col_look").to_string());
	}
}

void Node::insert_input(const std::string &id, NodeValue::Type type,
					   const Variant &default_value, InputFlags flags,
					   int index)
{
	if (id.empty()) {
		fprintf(stderr, "Rejected adding input with an empty ID on node %s\n",
				this->id().c_str());
		return;
	}

	if (has_param_with_id(id)) {
		fprintf(stderr,
				"Failed to add input to node %s - param with ID %s already exists\n",
				this->id().c_str(), id.c_str());
		return;
	}

	Node::Input i;

	i.type = type;
	i.default_value =
		NodeValue::split_normal_value_into_track_values(type, default_value);
	i.flags = flags;
	i.array_size = 0;

	input_ids_.insert(input_ids_.begin() + index, id);
	input_data_.insert(input_data_.begin() + index, i);

	if (standard_immediates_.find(id) == standard_immediates_.end()) {
		standard_immediates_[id] = create_immediate(id);
	}
}

void Node::remove_input(const std::string &id)
{
	int index = get_internal_input_index(id);

	if (index == -1) {
		report_invalid_input("remove", id, -1);
		return;
	}

	input_ids_.erase(input_ids_.begin() + index);
	input_data_.erase(input_data_.begin() + index);
}

void Node::report_invalid_input(const char *attempted_action, const std::string &id,
							  int element) const
{
	fprintf(stderr,
			"Failed to %s parameter %s element %d in node %s - input doesn't exist\n",
			attempted_action, id.c_str(), element, this->id().c_str());

#ifndef _WIN32
	if (std::getenv("OAK_DEBUG_INVALID_INPUT")) {
		void *frames[32];
		const int n = backtrace(frames, 32);
		char **symbols = backtrace_symbols(frames, n);
		if (symbols) {
			for (int i = 0; i < n; i++) {
				fprintf(stderr, "INVALID-INPUT-BT: %s\n", symbols[i]);
			}
			free(symbols);
		}
	}
#endif
}

NodeInputImmediate *Node::create_immediate(const std::string &input)
{
	const Input *i = get_internal_input_data(input);

	if (i) {
		return new NodeInputImmediate(i->type, i->default_value);
	} else {
		report_invalid_input("create immediate", input, -1);
		return nullptr;
	}
}

void Node::array_resize_internal(const std::string &id, int size)
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
			std::vector<NodeInputImmediate *> &subinputs = array_immediates_[id];
			for (int i = int(subinputs.size()); i < size; i++) {
				subinputs.push_back(create_immediate(id));
			}

			// Note that we do not delete any immediates when decreasing size since the user might still
			// want that data. Therefore it's important to note that array_size_ does NOT necessarily
			// equal subinputs_.size()
		}

		imm->array_size = size;
		parameter_value_changed(id, -1, TimeRange(RATIONAL_MIN, RATIONAL_MAX));
	}
}

std::string Node::get_connect_command_string(Node *output, const NodeInput &input)
{
	return "Connected " + output->get_label_and_name() + " to " +
		   input.node()->get_label_and_name() + " - " + input.get_input_name();
}

std::string Node::get_disconnect_command_string(Node *output, const NodeInput &input)
{
	return "Disconnected " + output->get_label_and_name() + " from " +
		   input.node()->get_label_and_name() + " - " + input.get_input_name();
}

int Node::get_internal_input_array_size(const std::string &input)
{
	auto it = array_immediates_.find(input);
	return it != array_immediates_.end() ? int(it->second.size()) : 0;
}

void find_ways_node_arrives_here_recursively(const Node *output, const Node *input,
											std::vector<NodeInput> &v)
{
	for (auto it = input->input_connections().cbegin();
		 it != input->input_connections().cend(); it++) {
		if (it->second == output) {
			v.push_back(it->first);
		} else {
			find_ways_node_arrives_here_recursively(output, it->second, v);
		}
	}
}

std::vector<NodeInput> Node::find_ways_node_arrives_here(const Node *output) const
{
	std::vector<NodeInput> v;

	find_ways_node_arrives_here_recursively(output, this, v);

	return v;
}

void Node::set_input_name(const std::string &id, const std::string &name)
{
	Input *i = get_internal_input_data(id);

	if (i) {
		i->human_name = name;
	} else {
		report_invalid_input("set name of", id, -1);
	}
}

const std::string &Node::get_label() const
{
	return label_;
}

void Node::set_label(const std::string &s)
{
	if (label_ != s) {
		label_ = s;
	}
}

std::string Node::get_label_and_name() const
{
	if (get_label().empty()) {
		return name();
	} else {
		return get_label() + " (" + name() + ")";
	}
}

std::string Node::get_label_or_name() const
{
	if (get_label().empty()) {
		return name();
	}
	return get_label();
}

void Node::copy_inputs(const Node *source, Node *destination,
					  bool include_connections, MultiUndoCommand *command)
{
	assert(source->id() == destination->id());

	for (const std::string &input : source->inputs()) {
		// NOTE: This assert is to ensure that inputs in the source also exist in the destination, which
		//       they should. If they don't and you hit this assert, check if you're handling group
		//       passthroughs correctly.
		assert(destination->has_input_with_id(input));

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

void Node::copy_input(const Node *src, Node *dst, const std::string &input,
					 bool include_connections, bool traverse_arrays,
					 MultiUndoCommand *command)
{
	assert(src->id() == dst->id());

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

void Node::copy_values_of_element(const Node *src, Node *dst, const std::string &input,
							   int src_element, int dst_element,
							   MultiUndoCommand *command)
{
	if (dst_element >= dst->get_internal_input_array_size(input)) {
		fprintf(stderr,
				"Ignored destination element that was out of array bounds\n");
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
			NodeKeyframe *copy = key->copy(dst_element, command ? nullptr : dst);
			if (command) {
				command->add_child(
					new NodeParamInsertKeyframeCommand(dst, copy));
			} else {
				dst->add_keyframe(copy);
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

void get_dependencies_recursively(std::vector<Node *> &list, const Node *node,
								bool traverse, bool exclusive_only)
{
	for (auto it = node->input_connections().cbegin();
		 it != node->input_connections().cend(); it++) {
		Node *connected_node = it->second;

		if (!exclusive_only || !connected_node->is_item()) {
			if (std::find(list.begin(), list.end(), connected_node) ==
				list.end()) {
				list.push_back(connected_node);

				if (traverse) {
					get_dependencies_recursively(list, connected_node, traverse,
											   exclusive_only);
				}
			}
		}
	}
}

/**
 * @brief Recursively collects dependencies of Node `n` and appends them to `list`
 *
 * @param traverse
 *
 * TRUE to recursively traverse each node for a complete dependency graph. FALSE to return only the immediate
 * dependencies.
 */
std::vector<Node *> Node::get_dependencies_internal(bool traverse,
											  bool exclusive_only) const
{
	std::vector<Node *> list;

	get_dependencies_recursively(list, this, traverse, exclusive_only);

	return list;
}

std::vector<Node *> Node::get_dependencies() const
{
	return get_dependencies_internal(true, false);
}

std::vector<Node *> Node::get_exclusive_dependencies() const
{
	return get_dependencies_internal(true, true);
}

std::vector<Node *> Node::get_immediate_dependencies() const
{
	return get_dependencies_internal(false, false);
}

ShaderCode Node::get_shader_code(const ShaderRequest &request) const
{
	return ShaderCode(std::string(), std::string());
}

void Node::process_samples(const NodeValueRow &, const SampleBuffer &,
						  SampleBuffer &, int) const
{
}

void Node::generate_frame(FramePtr frame, const GenerateJob &job) const
{
	(void)frame;
	(void)job;
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

bool Node::inputs_from(const std::string &id, bool recursively) const
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

std::string Node::get_category_name(const CategoryID &c)
{
	switch (c) {
	case k_category_output:
		return "Output";
	case k_category_distort:
		return "Distort";
	case k_category_math:
		return "Math";
	case k_category_keying:
		return "Keying";
	case k_category_color:
		return "Color";
	case k_category_filter:
		return "Filter";
	case k_category_timeline:
		return "Timeline";
	case k_category_generator:
		return "Generator";
	case k_category_transition:
		return "Transition";
	case k_category_project:
		return "Project";
	case k_category_open_fx:
		return "OpenFX";
	case k_category_time:
		return "Time";
	case k_category_unknown:
	case k_category_count:
		break;
	}

	return "Uncategorized";
}

TimeRange Node::transform_time_to(TimeRange time, Node *target,
								TransformTimeDirection dir, int path_index)
{
	Node *from = this;
	Node *to = target;

	if (dir == k_towards_input) {
		std::swap(from, to);
	}

	std::list<NodeInput> path = find_path(from, to, path_index);

	if (!path.empty()) {
		if (dir == k_towards_input) {
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

void Node::parameter_value_changed(const std::string &input, int element,
								 const TimeRange &range)
{
	InputValueChangedEvent(input, element);

	if (get_input_flags(input) & k_input_flag_ignore_invalidations) {
		return;
	}

	invalidate_cache(range, input, element);
}

TimeRange Node::get_range_affected_by_keyframe(NodeKeyframe *key) const
{
	const NodeKeyframeTrack &key_track = get_track_from_keyframe(key);
	int keyframe_index = int(
		std::find(key_track.begin(), key_track.end(), key) - key_track.begin());

	TimeRange range = get_range_around_index(key->input(), keyframe_index,
										  key->track(), key->element());

	// If a previous key exists and it's a hold, we don't need to invalidate those frames
	if (key_track.size() > 1 && keyframe_index > 0 &&
		key_track.at(keyframe_index - 1)->type() == NodeKeyframe::k_hold) {
		range.set_in(key->time());
	}

	return range;
}

TimeRange Node::get_range_around_index(const std::string &input, int index, int track,
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
		if (index < int(key_track.size()) - 1) {
			// If this is not the last key, we'll need to limit it to the key just after
			range_end = key_track.at(index + 1)->time();
		}
	}

	return TimeRange(range_begin, range_end);
}

void Node::clear_element(const std::string &input, int index)
{
	get_immediate(input, index)->delete_all_keyframes();

	if (is_input_keyframable(input)) {
		set_input_is_keyframing(input, false, index);
	}

	set_split_standard_value(input, get_split_default_value(input), index);
}

void Node::InputValueChangedEvent(const std::string &input, int element)
{
	(void)input;
	(void)element;
}

void Node::InputConnectedEvent(const std::string &input, int element, Node *output)
{
	(void)input;
	(void)element;
	(void)output;
}

void Node::InputDisconnectedEvent(const std::string &input, int element,
								  Node *output)
{
	(void)input;
	(void)element;
	(void)output;
}

void Node::OutputConnectedEvent(const NodeInput &input)
{
	(void)input;
}

void Node::OutputDisconnectedEvent(const NodeInput &input)
{
	(void)input;
}

void Node::add_keyframe(NodeKeyframe *key)
{
	// Formerly the ChildAdded branch of childEvent(): register with the
	// input immediate (insertion sorted) and invalidate the affected range.
	key->set_parent(this);

	get_immediate(key->input(), key->element())->insert_keyframe(key);

	parameter_value_changed(NodeInput(this, key->input(), key->element()),
						  get_range_affected_by_keyframe(key));
}

void Node::remove_keyframe(NodeKeyframe *key)
{
	// Formerly the ChildRemoved branch of childEvent(): invalidate the
	// affected range and unregister from the input immediate.
	TimeRange time_affected = get_range_affected_by_keyframe(key);

	get_immediate(key->input(), key->element())->remove_keyframe(key);

	key->set_parent(nullptr);

	parameter_value_changed(NodeInput(this, key->input(), key->element()),
						  time_affected);
}

void Node::invalidate_from_keyframe_bezier_in_change(NodeKeyframe *key)
{
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);
	int keyframe_index = int(
		std::find(track.begin(), track.end(), key) - track.begin());

	Rational start = RATIONAL_MIN;
	Rational end = key->time();

	if (keyframe_index > 0) {
		start = track.at(keyframe_index - 1)->time();
	}

	parameter_value_changed(key->key_track_ref().input(), TimeRange(start, end));
}

void Node::invalidate_from_keyframe_bezier_out_change(NodeKeyframe *key)
{
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);
	int keyframe_index = int(
		std::find(track.begin(), track.end(), key) - track.begin());

	Rational start = key->time();
	Rational end = RATIONAL_MAX;

	if (keyframe_index < int(track.size()) - 1) {
		end = track.at(keyframe_index + 1)->time();
	}

	parameter_value_changed(key->key_track_ref().input(), TimeRange(start, end));
}

void Node::invalidate_from_keyframe_time_change(NodeKeyframe *key)
{
	NodeInputImmediate *immediate = get_immediate(key->input(), key->element());
	TimeRange original_range = get_range_affected_by_keyframe(key);

	core::TimeRangeList invalidate_range;
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
	for (const TimeRange &r : invalidate_range) {
		parameter_value_changed(key->key_track_ref().input(), r);
	}
}

void Node::invalidate_from_keyframe_value_change(NodeKeyframe *key)
{
	parameter_value_changed(key->key_track_ref().input(),
						  get_range_affected_by_keyframe(key));
}

void Node::invalidate_from_keyframe_type_changed(NodeKeyframe *key)
{
	const NodeKeyframeTrack &track = get_track_from_keyframe(key);

	if (track.size() == 1) {
		// If there are no other frames, the interpolation won't do anything
		return;
	}

	// Invalidate entire range
	parameter_value_changed(
		key->key_track_ref().input(),
		get_range_around_index(key->input(),
							   int(std::find(track.begin(), track.end(), key) -
								   track.begin()),
							   key->track(), key->element()));
}

void Node::set_value_at_time(const NodeInput &input, const Rational &time,
						  const Variant &value, int track,
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
				Variant track_value;

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

bool Node::ValueHint::load(XmlStreamReader *reader)
{
	unsigned int version = 0;
	for (const XmlStreamAttribute &attr : reader->attributes()) {
		version = unsigned(strtoul(attr.value.c_str(), nullptr, 10));
	}

	(void)version;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "types") {
			std::vector<NodeValue::Type> types;
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "type") {
					types.push_back(static_cast<NodeValue::Type>(
						strtol(reader->read_element_text().c_str(), nullptr,
							   10)));
				} else {
					reader->skip_current_element();
				}
			}
			this->set_type(types);
		} else if (reader->name() == "index") {
			this->set_index(
				int(strtol(reader->read_element_text().c_str(), nullptr, 10)));
		} else if (reader->name() == "tag") {
			this->set_tag(reader->read_element_text());
		} else {
			reader->skip_current_element();
		}
	}

	return true;
}

void Node::ValueHint::save(XmlStreamWriter *writer) const
{
	writer->write_attribute("version", std::to_string(1));

	writer->write_start_element("types");

	for (auto it = this->types().cbegin(); it != this->types().cend(); it++) {
		writer->write_text_element("type", std::to_string(int(*it)));
	}

	writer->write_end_element(); // types

	writer->write_text_element("index", std::to_string(this->index()));

	writer->write_text_element("tag", this->tag());
}

bool Node::Position::load(XmlStreamReader *reader)
{
	bool got_pos_x = false;
	bool got_pos_y = false;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "x") {
			this->position.set_x(
				strtod(reader->read_element_text().c_str(), nullptr));
			got_pos_x = true;
		} else if (reader->name() == "y") {
			this->position.set_y(
				strtod(reader->read_element_text().c_str(), nullptr));
			got_pos_y = true;
		} else if (reader->name() == "expanded") {
			this->expanded =
				strtol(reader->read_element_text().c_str(), nullptr, 10);
		} else {
			reader->skip_current_element();
		}
	}

	return got_pos_x && got_pos_y;
}

void Node::Position::save(XmlStreamWriter *writer) const
{
	writer->write_text_element("x", number_to_string(this->position.x()));
	writer->write_text_element("y", number_to_string(this->position.y()));
	writer->write_text_element("expanded", std::to_string(int(this->expanded)));
}

}
