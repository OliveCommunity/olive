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

#include "timeline/timelineundogeneral.h"

namespace olive
{

const std::string Sequence::k_track_input_format = "track_in_%1";

#define super ViewerOutput

// Replaces k_track_input_format.arg(i)
static std::string track_input_id_for(int i)
{
	std::string s = Sequence::k_track_input_format;
	std::string::size_type pos = s.find("%1");
	if (pos != std::string::npos) {
		s.replace(pos, 2, std::to_string(i));
	}
	return s;
}

Sequence::Sequence()
{
	set_flag(k_is_item);

	// Create TrackList instances
	track_lists_.resize(Track::k_count);

	for (int i = 0; i < Track::k_count; i++) {
		// Create track input
		std::string track_input_id = track_input_id_for(i);

		add_input(track_input_id, NodeValue::k_none,
				 InputFlags(k_input_flag_not_keyframable | k_input_flag_array |
							k_input_flag_hidden | k_input_flag_ignore_invalidations));

		TrackList *list =
			new TrackList(this, static_cast<Track::Type>(i), track_input_id);
		track_lists_[i] = list;

		// NOTE(de-Qt): the TrackList signal connections were removed with
		// QObject. Whoever de-Qt's TrackList must call, from the former
		// signal emission points:
		//   track_list_changed -> sequence->update_track_cache()
		//   length_changed     -> sequence->verify_length()
		// (The track_added/track_removed relay signals were deleted.)
	}
}

Sequence::~Sequence()
{
	// Match NODE_DEFAULT_DESTRUCTOR: disconnect while still a Sequence
	disconnect_all();

	// Owns the TrackLists created in the constructor (formerly QObject
	// children of this Sequence)
	for (TrackList *list : track_lists_) {
		delete list;
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

Variant Sequence::data(const DataType &d) const
{
	if (d == icon) {
		return "sequence";
	}

	return super::data(d);
}

std::vector<Track *> Sequence::get_unlocked_tracks() const
{
	std::vector<Track *> tracks = get_tracks();

	for (int i = 0; i < int(tracks.size()); i++) {
		if (tracks.at(i)->is_locked()) {
			tracks.erase(tracks.begin() + i);
			i--;
		}
	}

	return tracks;
}

void Sequence::retranslate()
{
	super::retranslate();

	for (int i = 0; i < Track::k_count; i++) {
		std::string input_name;

		switch (static_cast<Track::Type>(i)) {
		case Track::k_video:
			input_name = "Video Tracks";
			break;
		case Track::k_audio:
			input_name = "Audio Tracks";
			break;
		case Track::k_subtitle:
			input_name = "Subtitle Tracks";
			break;
		case Track::k_none:
		case Track::k_count:
			break;
		}

		if (!input_name.empty()) {
			set_input_name(track_input_id_for(i), input_name);
		}
	}
}

void Sequence::invalidate_cache(const TimeRange &range, const std::string &from,
							   int element, InvalidateCacheOptions options)
{
	// (The subtitles_changed() signal for the subtitle track input was
	// removed with QObject; observers are notified through the facade layer.)

	super::invalidate_cache(range, from, element, options);
}

Rational Sequence::verify_length_internal(Track::Type type) const
{
	if (!track_lists_.empty()) {
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

void Sequence::InputConnectedEvent(const std::string &input, int element,
								   Node *output)
{
	for (TrackList *list : track_lists_) {
		if (list->track_input() == input) {
			// Return because we found our input
			list->track_connected(output, element);
			return;
		}
	}

	super::InputConnectedEvent(input, element, output);
}

void Sequence::InputDisconnectedEvent(const std::string &input, int element,
									  Node *output)
{
	for (TrackList *list : track_lists_) {
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

	for (TrackList *list : track_lists_) {
		for (Track *track : list->get_tracks()) {
			track_cache_.push_back(track);
		}
	}
}

}
