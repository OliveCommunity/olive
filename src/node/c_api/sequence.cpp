/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "node/sequence.h"

#include "alivecount.h"
#include "node/track.h"

#include "globals.h"
#include "output/track/tracklist.h"
#include "project/sequence/sequence.h"
#include "videoparams.h"

namespace
{

olive::Sequence *impl(OakNodeSequence *h)
{
	return reinterpret_cast<olive::Sequence *>(h);
}

int get_rational(const olive::core::Rational &r, int *numerator,
				 int *denominator)
{
	if (!numerator || !denominator) {
		return OAKNODE_E_INVALID;
	}
	*numerator = r.numerator();
	*denominator = r.denominator();
	return OAKNODE_OK;
}

bool valid_track_type(int type)
{
	return type >= OAKNODE_TRACK_TYPE_VIDEO && type < OAKNODE_TRACK_TYPE_COUNT;
}

} // namespace

OakNodeSequence *oaknode_sequence_create(void)
{
	try {
		olive::Sequence *s = new olive::Sequence();
		oaknode_c_api::alive_inc();
		return reinterpret_cast<OakNodeSequence *>(s);
	} catch (...) {
		return nullptr;
	}
}

void oaknode_sequence_free(OakNodeSequence *sequence)
{
	if (!sequence) {
		return;
	}
	olive::Sequence *s = impl(sequence);
	// ~Sequence() deletes the owned TrackLists
	delete s;
	oaknode_c_api::alive_dec();
}

int oaknode_sequence_get_track_list(OakNodeSequence *sequence, int type,
									OakNodeTrackList **out)
{
	if (!sequence || !out) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = reinterpret_cast<OakNodeTrackList *>(
		impl(sequence)->track_list(static_cast<olive::Track::Type>(type)));
	return OAKNODE_OK;
}

int oaknode_sequence_get_track_count(OakNodeSequence *sequence, int type,
									 int *count)
{
	if (!sequence || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	*count = impl(sequence)
				 ->track_list(static_cast<olive::Track::Type>(type))
				 ->get_track_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_track_at(OakNodeSequence *sequence, int type,
								  int index, OakNodeTrack **out)
{
	if (!sequence || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	olive::TrackList *list =
		impl(sequence)->track_list(static_cast<olive::Track::Type>(type));
	if (index >= list->get_track_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = reinterpret_cast<OakNodeTrack *>(list->get_track_at(index));
	return OAKNODE_OK;
}

int oaknode_sequence_get_all_track_count(OakNodeSequence *sequence, int *count)
{
	if (!sequence || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = int(impl(sequence)->get_tracks().size());
	return OAKNODE_OK;
}

int oaknode_sequence_get_all_track_at(OakNodeSequence *sequence, int index,
									  OakNodeTrack **out)
{
	if (!sequence || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const auto &tracks = impl(sequence)->get_tracks();
	if (index >= int(tracks.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = reinterpret_cast<OakNodeTrack *>(tracks.at(index));
	return OAKNODE_OK;
}

int oaknode_sequence_get_playhead(OakNodeSequence *sequence, int *numerator,
								  int *denominator)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(sequence)->get_playhead(), numerator, denominator);
}

int oaknode_sequence_set_playhead(OakNodeSequence *sequence, int numerator,
								  int denominator)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(sequence)->set_playhead(olive::core::Rational(numerator,
														   denominator));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_get_length(OakNodeSequence *sequence, int *numerator,
								int *denominator)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(sequence)->get_length(), numerator, denominator);
}

int oaknode_sequence_get_video_length(OakNodeSequence *sequence, int *numerator,
									  int *denominator)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(sequence)->get_video_length(), numerator,
						denominator);
}

int oaknode_sequence_get_audio_length(OakNodeSequence *sequence, int *numerator,
									  int *denominator)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(sequence)->get_audio_length(), numerator,
						denominator);
}

int oaknode_sequence_verify_length(OakNodeSequence *sequence)
{
	if (!sequence) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(sequence)->verify_length();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

/* --------------------------------------------------- Video/audio params */

int oaknode_sequence_get_video_stream_count(OakNodeSequence *sequence,
											int *count)
{
	if (!sequence || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = impl(sequence)->get_video_stream_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_audio_stream_count(OakNodeSequence *sequence,
											int *count)
{
	if (!sequence || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = impl(sequence)->get_audio_stream_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_video_params(OakNodeSequence *sequence, int index,
									  OakVideoParams *out)
{
	if (!sequence || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= impl(sequence)->get_video_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	try {
		const olive::VideoParams params =
			impl(sequence)->get_video_params(index);
		*out = oakcommon_videoparams_init_from_native(&params);
	} catch (...) {
		return OAKNODE_E_NOMEM;
	}
	if (!out->ctx) {
		return OAKNODE_E_NOMEM;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_set_video_params(OakNodeSequence *sequence, int index,
									  OakVideoParams params)
{
	if (!sequence || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const olive::VideoParams *native =
		oakcommon_videoparams_get_native(params);
	if (!native) {
		return OAKNODE_E_INVALID;
	}
	if (index >= impl(sequence)->get_video_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	try {
		impl(sequence)->set_video_params(*native, index);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_get_audio_params(OakNodeSequence *sequence, int index,
									  OakAudioParams **out)
{
	if (!sequence || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= impl(sequence)->get_audio_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	OakAudioParams *copy =
		oakcore_audioparams_copy(impl(sequence)->get_audio_params(index).handle());
	if (!copy) {
		return OAKNODE_E_NOMEM;
	}
	*out = copy;
	return OAKNODE_OK;
}

int oaknode_sequence_set_audio_params(OakNodeSequence *sequence, int index,
									  const OakAudioParams *params)
{
	if (!sequence || !params || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= impl(sequence)->get_audio_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	OakAudioParams *copy = oakcore_audioparams_copy(params);
	if (!copy) {
		return OAKNODE_E_NOMEM;
	}
	try {
		impl(sequence)->set_audio_params(
			olive::core::AudioParams::from_handle(copy), index);
	} catch (...) {
		oakcore_audioparams_free(copy);
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

OakNodeNode *oaknode_sequence_as_node(OakNodeSequence *sequence)
{
	return reinterpret_cast<OakNodeNode *>(sequence);
}
