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

#include "tracklist.h"

#include <algorithm>

#include "factory.h"
#include "math/math/math.h"
#include "math/merge/merge.h"
#include "output/viewer/viewer.h"
#include "project/sequence/sequence.h"

namespace olive
{

TrackList::TrackList(Sequence *parent, const Track::Type &type,
					 const std::string &track_input)
	: parent_(parent)
	, track_input_(track_input)
	, total_length_(0)
	, type_(type)
{
}

Track *TrackList::get_track_at(int index) const
{
	if (index >= 0 && index < int(track_cache_.size())) {
		return track_cache_.at(index);
	} else {
		return nullptr;
	}
}

void TrackList::track_connected(Node *node, int element)
{
	if (element == -1) {
		parent()->invalidate_all(track_input(), element);
		return;
	}

	Track *track = dynamic_cast<Track *>(node);

	if (!track) {
		return;
	}

	// Determine where in the cache this block will be
	int cache_index = -1;
	for (int i = element + 1; i < array_size(); i++) {
		// Find next track because this will be the index we insert at
		cache_index = get_cache_index_from_array_index(i);

		if (cache_index >= 0) {
			break;
		}
	}

	// If there was no next, this will be inserted at the end
	if (cache_index == -1) {
		cache_index = int(track_cache_.size());
	}

	track_cache_.insert(track_cache_.begin() + cache_index, track);
	track_array_indexes_.insert(track_array_indexes_.begin() + cache_index,
								element);

	// Update track indexes in the list (including this track)
	update_track_indexes_from(cache_index);

	track->set_type(type_);
	track->set_sequence(parent());

	update_total_length();
}

void TrackList::track_disconnected(Node *node, int element)
{
	if (element == -1) {
		// User has replaced the entire array, we will invalidate everything
		parent()->invalidate_all(track_input(), element);
		return;
	}

	Track *track = dynamic_cast<Track *>(node);

	if (!track) {
		return;
	}

	int cache_index = get_cache_index_from_array_index(element);

	// Remove track here
	track_cache_.erase(track_cache_.begin() + cache_index);
	track_array_indexes_.erase(track_array_indexes_.begin() + cache_index);

	// Update indices for all subsequent tracks
	update_track_indexes_from(cache_index);

	track->set_index(-1);
	track->set_type(Track::k_none);
	track->set_sequence(nullptr);

	update_total_length();
}

void TrackList::update_track_indexes_from(int index)
{
	for (int i = index; i < int(track_cache_.size()); i++) {
		track_cache_.at(i)->set_index(i);
	}
}

Project *TrackList::get_parent_graph() const
{
	return parent()->parent();
}

const std::string &TrackList::track_input() const
{
	return track_input_;
}

NodeInput TrackList::track_input(int element) const
{
	return NodeInput(parent(), track_input(), element);
}

int TrackList::array_size() const
{
	return parent()->input_array_size(track_input());
}

void TrackList::array_append()
{
	parent()->input_array_append(track_input());
}

void TrackList::array_remove_last()
{
	parent()->input_array_remove_last(track_input());
}

void TrackList::update_total_length()
{
	total_length_ = 0;

	for (Track *track : track_cache_) {
		if (track) {
			total_length_ = std::max(total_length_, track->track_length());
		}
	}
}

}
