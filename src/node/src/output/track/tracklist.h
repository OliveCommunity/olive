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

#ifndef OAK_TRACKLIST_H
#define OAK_TRACKLIST_H

#include <algorithm>
#include <string>
#include <vector>

#include "output/track/track.h"

namespace olive
{

class Sequence;

class TrackList {
public:
	TrackList(Sequence *parent, const Track::Type &type,
			  const std::string &track_input);

	const Track::Type &type() const
	{
		return type_;
	}

	const std::vector<Track *> &get_tracks() const
	{
		return track_cache_;
	}

	Track *get_track_at(int index) const;

	const Rational &get_total_length() const
	{
		return total_length_;
	}

	int get_track_count() const
	{
		return track_cache_.size();
	}

	Project *get_parent_graph() const;

	const std::string &track_input() const;
	NodeInput track_input(int element) const;

	Sequence *parent() const
	{
		return parent_;
	}

	void set_parent(Sequence *parent)
	{
		parent_ = parent;
	}

	int array_size() const;

	void array_append();
	void array_remove_last();

	int get_array_index_from_cache_index(int index) const
	{
		return track_array_indexes_.at(index);
	}

	int get_cache_index_from_array_index(int index) const
	{
		auto it = std::find(track_array_indexes_.begin(),
							track_array_indexes_.end(), index);
		return (it == track_array_indexes_.end()) ?
				   -1 :
				   int(it - track_array_indexes_.begin());
	}

	/**
   * @brief Handler for when the track connection is added
   *
   * Formerly a slot connected to the Sequence's input_connected signal;
   * now invoked directly by the caller (facade / event layer).
   */
	void track_connected(Node *node, int element);

	/**
   * @brief Handler for when the track connection is removed
   *
   * Formerly a slot connected to the Sequence's input_disconnected signal;
   * now invoked directly by the caller (facade / event layer).
   */
	void track_disconnected(Node *node, int element);

	/**
   * @brief Handler for when any of the track's length changes so we can update the length of the tracklist
   *
   * Formerly a slot connected to Track::track_length_changed; now invoked
   * directly by the caller (facade / event layer).
   */
	void update_total_length();

private:
	void update_track_indexes_from(int index);

	/**
   * @brief A cache of connected Tracks
   */
	std::vector<Track *> track_cache_;
	std::vector<int> track_array_indexes_;

	Sequence *parent_;

	std::string track_input_;

	Rational total_length_;

	enum Track::Type type_;
};

}

#endif // OAK_TRACKLIST_H
