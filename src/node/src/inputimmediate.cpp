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

#include "inputimmediate.h"

#include <algorithm>
#include <cassert>

#include "node.h"

namespace olive
{

NodeInputImmediate::NodeInputImmediate(NodeValue::Type type,
									   const SplitValue &default_val)
	: default_value_(default_val)
	, keyframing_(false)
{
	set_data_type(type);
}

void NodeInputImmediate::set_standard_value_on_track(const Variant &value,
													 int track)
{
	standard_value_[track] = value;
}

void NodeInputImmediate::set_split_standard_value(const SplitValue &value)
{
	for (int i = 0; i < int(value.size()) && i < int(standard_value_.size());
		 i++) {
		standard_value_[i] = value[i];
	}
}

std::vector<NodeKeyframe *>
NodeInputImmediate::get_keyframe_at_time(const Rational &time) const
{
	std::vector<NodeKeyframe *> keys;

	for (int i = 0; i < int(keyframe_tracks_.size()); i++) {
		NodeKeyframe *key_at_time = get_keyframe_at_time_on_track(time, i);

		if (key_at_time) {
			keys.push_back(key_at_time);
		}
	}

	return keys;
}

NodeKeyframe *
NodeInputImmediate::get_keyframe_at_time_on_track(const Rational &time,
												  int track) const
{
	if (!is_using_standard_value(track)) {
		for (NodeKeyframe *key : keyframe_tracks_.at(track)) {
			if (key->time() == time) {
				return key;
			}
		}
	}

	return nullptr;
}

NodeKeyframe *
NodeInputImmediate::get_closest_keyframe_to_time_on_track(const Rational &time,
														  int track) const
{
	if (is_using_standard_value(track)) {
		return nullptr;
	}

	const NodeKeyframeTrack &key_track = keyframe_tracks_.at(track);

	if (time <= key_track.front()->time()) {
		return key_track.front();
	}

	if (time >= key_track.back()->time()) {
		return key_track.back();
	}

	for (int i = 1; i < int(key_track.size()); i++) {
		NodeKeyframe *prev_key = key_track.at(i - 1);
		NodeKeyframe *next_key = key_track.at(i);

		if (prev_key->time() <= time && next_key->time() >= time) {
			// Return whichever is closer
			Rational prev_diff = time - prev_key->time();
			Rational next_diff = next_key->time() - time;

			if (next_diff < prev_diff) {
				return next_key;
			} else {
				return prev_key;
			}
		}
	}

	return nullptr;
}

NodeKeyframe *
NodeInputImmediate::get_closest_keyframe_before_time(const Rational &time) const
{
	NodeKeyframe *key = nullptr;

	for (const NodeKeyframeTrack &track : keyframe_tracks_) {
		for (NodeKeyframe *k : track) {
			if (k->time() >= time) {
				break;
			} else if (!key || k->time() > key->time()) {
				key = k;
			}
		}
	}

	return key;
}

NodeKeyframe *
NodeInputImmediate::get_closest_keyframe_after_time(const Rational &time) const
{
	NodeKeyframe *key = nullptr;

	for (const NodeKeyframeTrack &track : keyframe_tracks_) {
		for (int i = int(track.size()) - 1; i >= 0; i--) {
			NodeKeyframe *k = track.at(i);

			if (k->time() <= time) {
				break;
			} else if (!key || k->time() < key->time()) {
				key = k;
			}
		}
	}

	return key;
}

NodeKeyframe::Type
NodeInputImmediate::get_best_keyframe_type_for_time(const Rational &time,
													int track) const
{
	NodeKeyframe *closest_key =
		get_closest_keyframe_to_time_on_track(time, track);

	if (closest_key) {
		return closest_key->type();
	}

	return NodeKeyframe::k_default_type;
}

bool NodeInputImmediate::has_keyframe_at_time(const Rational &time) const
{
	if (!is_keyframing()) {
		return false;
	}

	// Loop through keyframes to see if any match
	for (const NodeKeyframeTrack &track : keyframe_tracks_) {
		for (NodeKeyframe *key : track) {
			if (key->time() == time) {
				return true;
			}
		}
	}

	// None match
	return false;
}

void NodeInputImmediate::set_data_type(NodeValue::Type type)
{
	int track_size = NodeValue::get_number_of_keyframe_tracks(type);

	keyframe_tracks_.resize(track_size);
	standard_value_.resize(track_size);

	set_split_standard_value(default_value_);
}

NodeKeyframe *NodeInputImmediate::get_earliest_keyframe() const
{
	NodeKeyframe *earliest = nullptr;

	for (const NodeKeyframeTrack &track : keyframe_tracks_) {
		if (!track.empty()) {
			NodeKeyframe *earliest_in_track = track.front();

			if (!earliest || earliest_in_track->time() < earliest->time()) {
				earliest = earliest_in_track;
			}
		}
	}

	return earliest;
}

NodeKeyframe *NodeInputImmediate::get_latest_keyframe() const
{
	NodeKeyframe *latest = nullptr;

	for (const NodeKeyframeTrack &track : keyframe_tracks_) {
		if (!track.empty()) {
			NodeKeyframe *latest_in_track = track.back();

			if (!latest || latest_in_track->time() > latest->time()) {
				latest = latest_in_track;
			}
		}
	}

	return latest;
}

void NodeInputImmediate::insert_keyframe(NodeKeyframe *key)
{
	NodeKeyframeTrack &key_track = keyframe_tracks_[key->track()];

	int insert_index = int(key_track.size());

	for (int i = 0; i < int(key_track.size()); i++) {
		NodeKeyframe *compare = key_track.at(i);

		// Ensure we aren't trying to insert two keyframes at the same time
		assert(compare->time() != key->time());

		if (compare->time() > key->time()) {
			insert_index = i;
			break;
		}
	}

	key_track.insert(key_track.begin() + insert_index, key);

	NodeKeyframe *previous = insert_index > 0 ? key_track.at(insert_index - 1) :
												nullptr;
	NodeKeyframe *next = insert_index < int(key_track.size()) - 1 ?
							 key_track.at(insert_index + 1) :
							 nullptr;

	key->set_previous(previous);
	key->set_next(next);

	if (previous) {
		previous->set_next(key);
	}

	if (next) {
		next->set_previous(key);
	}
}

void NodeInputImmediate::remove_keyframe(NodeKeyframe *key)
{
	if (key->previous()) {
		key->previous()->set_next(key->next());
	}

	if (key->next()) {
		key->next()->set_previous(key->previous());
	}

	key->set_previous(nullptr);
	key->set_next(nullptr);

	NodeKeyframeTrack &track = keyframe_tracks_[key->track()];
	track.erase(std::find(track.begin(), track.end(), key));
}

void NodeInputImmediate::delete_all_keyframes(
	std::vector<NodeKeyframe *> *reclaimed)
{
	for (NodeKeyframeTrack &track : keyframe_tracks_) {
		// Iterate over a copy of the track, since the keyframes are removed
		// from it as we go
		const NodeKeyframeTrack copy = track;

		for (NodeKeyframe *key : copy) {
			remove_keyframe(key);

			if (reclaimed) {
				reclaimed->push_back(key);
			} else {
				delete key;
			}
		}
	}
}

}
