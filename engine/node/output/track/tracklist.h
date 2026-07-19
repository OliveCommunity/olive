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

#include <QHash>
#include <QObject>

#include "node/output/track/track.h"
#include "timeline/timelinecommon.h"

namespace olive
{

class Sequence;

class TrackList : public QObject {
	Q_OBJECT
public:
	TrackList(Sequence *parent, const Track::Type &type,
			  const QString &track_input);

	const Track::Type &type() const
	{
		return type_;
	}

	const QVector<Track *> &get_tracks() const
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

	const QString &track_input() const;
	NodeInput track_input(int element) const;

	Sequence *parent() const;

	int array_size() const;

	void array_append();
	void array_remove_last();

	int get_array_index_from_cache_index(int index) const
	{
		return track_array_indexes_.at(index);
	}

	int get_cache_index_from_array_index(int index) const
	{
		return track_array_indexes_.indexOf(index);
	}

public slots:
	/**
   * @brief Slot for when the track connection is added
   */
	void track_connected(Node *node, int element);

	/**
   * @brief Slot for when the track connection is removed
   */
	void track_disconnected(Node *node, int element);

signals:
	void track_list_changed();

	void length_changed(const Rational &length);

	void track_added(Track *track);

	void track_removed(Track *track);

	void track_height_changed(Track *track, int height);

private:
	void update_track_indexes_from(int index);

	/**
   * @brief A cache of connected Tracks
   */
	QVector<Track *> track_cache_;
	QVector<int> track_array_indexes_;

	/**
   * @brief Stored TrackHeightChanged connections so they can be disconnected again
   */
	QHash<Track *, QMetaObject::Connection> track_height_connections_;

	QString track_input_;

	Rational total_length_;

	enum Track::Type type_;

private slots:
	/**
   * @brief Slot for when any of the track's length changes so we can update the length of the tracklist
   */
	void update_total_length();
};

}

#endif // OAK_TRACKLIST_H
