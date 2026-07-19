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

#include "sequence.h"

#include <QThread>

#include "panel/timeline/timeline.h"
#include "ui/icons/icons.h"
#include "timeline/timelineundogeneral.h"

namespace olive
{

const QString Sequence::k_track_input_format = QStringLiteral("track_in_%1");

#define super ViewerOutput

Sequence::Sequence()
{
	set_flag(k_is_item);

	// Create TrackList instances
	track_lists_.resize(Track::k_count);

	for (int i = 0; i < Track::k_count; i++) {
		// Create track input
		QString track_input_id = k_track_input_format.arg(i);

		add_input(track_input_id, NodeValue::k_none,
				 InputFlags(k_input_flag_not_keyframable | k_input_flag_array |
							k_input_flag_hidden | k_input_flag_ignore_invalidations));

		TrackList *list =
			new TrackList(this, static_cast<Track::Type>(i), track_input_id);
		track_lists_.replace(i, list);
		connect(list, &TrackList::track_list_changed, this,
				&Sequence::update_track_cache);
		connect(list, &TrackList::length_changed, this, &Sequence::verify_length);
		connect(list, &TrackList::track_added, this, &Sequence::track_added);
		connect(list, &TrackList::track_removed, this, &Sequence::track_removed);
	}
}

void Sequence::add_default_nodes(MultiUndoCommand *command)
{
	// Create tracks and connect them to the viewer
	UndoCommand *video_track_command =
		new TimelineAddTrackCommand(track_list(Track::k_video));
	UndoCommand *audio_track_command =
		new TimelineAddTrackCommand(track_list(Track::k_audio));

	if (command) {
		command->add_child(video_track_command);
		command->add_child(audio_track_command);
	} else {
		video_track_command->redo_now();
		audio_track_command->redo_now();
		delete video_track_command;
		delete audio_track_command;
	}
}

QVariant Sequence::data(const DataType &d) const
{
	if (d == icon) {
		return icon::sequence;
	}

	return super::data(d);
}

QVector<Track *> Sequence::get_unlocked_tracks() const
{
	QVector<Track *> tracks = get_tracks();

	for (int i = 0; i < tracks.size(); i++) {
		if (tracks.at(i)->is_locked()) {
			tracks.removeAt(i);
			i--;
		}
	}

	return tracks;
}

void Sequence::retranslate()
{
	super::retranslate();

	for (int i = 0; i < Track::k_count; i++) {
		QString input_name;

		switch (static_cast<Track::Type>(i)) {
		case Track::k_video:
			input_name = tr("Video Tracks");
			break;
		case Track::k_audio:
			input_name = tr("Audio Tracks");
			break;
		case Track::k_subtitle:
			input_name = tr("Subtitle Tracks");
			break;
		case Track::k_none:
		case Track::k_count:
			break;
		}

		if (!input_name.isEmpty()) {
			set_input_name(k_track_input_format.arg(i), input_name);
		}
	}
}

void Sequence::invalidate_cache(const TimeRange &range, const QString &from,
							   int element, InvalidateCacheOptions options)
{
	if (from == k_track_input_format.arg(Track::k_subtitle)) {
		emit subtitles_changed(range);
	}

	super::invalidate_cache(range, from, element, options);
}

Rational Sequence::verify_length_internal(Track::Type type) const
{
	if (!track_lists_.isEmpty()) {
		switch (type) {
		case Track::k_video:
			return track_lists_.at(Track::k_video)->get_total_length();
		case Track::k_audio:
			return track_lists_.at(Track::k_audio)->get_total_length();
		case Track::k_subtitle:
			return track_lists_.at(Track::k_subtitle)->get_total_length();
		case Track::k_none:
		case Track::k_count:
			break;
		}
	}

	return 0;
}

void Sequence::InputConnectedEvent(const QString &input, int element,
								   Node *output)
{
	foreach (TrackList *list, track_lists_) {
		if (list->track_input() == input) {
			// Return because we found our input
			list->track_connected(output, element);
			return;
		}
	}

	super::InputConnectedEvent(input, element, output);
}

void Sequence::InputDisconnectedEvent(const QString &input, int element,
									  Node *output)
{
	foreach (TrackList *list, track_lists_) {
		if (list->track_input() == input) {
			// Return because we found our input
			list->track_disconnected(output, element);
			return;
		}
	}

	super::InputDisconnectedEvent(input, element, output);
}

void Sequence::update_track_cache()
{
	track_cache_.clear();

	foreach (TrackList *list, track_lists_) {
		foreach (Track *track, list->get_tracks()) {
			track_cache_.append(track);
		}
	}
}

}
