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

#include "node/node.h"
#include "node/track.h"

#include "globals.h"
#include "output/track/tracklist.h"
#include "project/sequence/sequence.h"
#include "videoparams.h"

#include "nodehandle.h"

using oaknode_c_api::delete_as;
using oaknode_c_api::free_handle;
using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

namespace
{

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

OakNodeSequence oaknode_sequence_create(void)
{
	try {
		return make_handle<OakNodeSequence>(new olive::Sequence(), true,
											&delete_as<olive::Sequence>);
	} catch (...) {
		return OakNodeSequence{};
	}
}

void oaknode_sequence_free(OakNodeSequence *sequence)
{
	// The final release runs ~Sequence(), which deletes the owned TrackLists
	free_handle(sequence);
}

int oaknode_sequence_get_track_list(OakNodeSequence sequence, int type,
									OakNodeTrackList *out)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !out) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	// Borrowed; the track list stays owned by the sequence
	*out = make_handle<OakNodeTrackList>(
		s->track_list(static_cast<olive::Track::Type>(type)), false,
		&delete_as<olive::TrackList>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_sequence_get_track_count(OakNodeSequence sequence, int type,
									 int *count)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	*count = s->track_list(static_cast<olive::Track::Type>(type))
				 ->get_track_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_track_at(OakNodeSequence sequence, int type,
								  int index, OakNodeTrack *out)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (!valid_track_type(type)) {
		return OAKNODE_E_NOT_FOUND;
	}
	olive::TrackList *list =
		s->track_list(static_cast<olive::Track::Type>(type));
	if (index >= list->get_track_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	// Borrowed; the track stays owned by the list
	*out = make_handle<OakNodeTrack>(list->get_track_at(index), false,
									 &delete_as<olive::Track>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_sequence_get_all_track_count(OakNodeSequence sequence, int *count)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = int(s->get_tracks().size());
	return OAKNODE_OK;
}

int oaknode_sequence_get_all_track_at(OakNodeSequence sequence, int index,
									  OakNodeTrack *out)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const auto &tracks = s->get_tracks();
	if (index >= int(tracks.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	// Borrowed; the track stays owned by its list
	*out = make_handle<OakNodeTrack>(tracks.at(index), false,
									 &delete_as<olive::Track>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_sequence_get_playhead(OakNodeSequence sequence, int *numerator,
								  int *denominator)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(s->get_playhead(), numerator, denominator);
}

int oaknode_sequence_set_playhead(OakNodeSequence sequence, int numerator,
								  int denominator)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	try {
		s->set_playhead(olive::core::Rational(numerator, denominator));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_get_length(OakNodeSequence sequence, int *numerator,
								int *denominator)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(s->get_length(), numerator, denominator);
}

int oaknode_sequence_get_video_length(OakNodeSequence sequence,
									  int *numerator, int *denominator)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(s->get_video_length(), numerator, denominator);
}

int oaknode_sequence_get_audio_length(OakNodeSequence sequence,
									  int *numerator, int *denominator)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(s->get_audio_length(), numerator, denominator);
}

int oaknode_sequence_verify_length(OakNodeSequence sequence)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}
	try {
		s->verify_length();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

/* --------------------------------------------------- Video/audio params */

int oaknode_sequence_get_video_stream_count(OakNodeSequence sequence,
											int *count)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = s->get_video_stream_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_audio_stream_count(OakNodeSequence sequence,
											int *count)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = s->get_audio_stream_count();
	return OAKNODE_OK;
}

int oaknode_sequence_get_video_params(OakNodeSequence sequence, int index,
									  OakVideoParams *out)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= s->get_video_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	try {
		const olive::VideoParams params = s->get_video_params(index);
		*out = oakcommon_videoparams_init_from_native(&params);
	} catch (...) {
		return OAKNODE_E_NOMEM;
	}
	if (!out->ctx) {
		return OAKNODE_E_NOMEM;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_set_video_params(OakNodeSequence sequence, int index,
									  OakVideoParams params)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const olive::VideoParams *native =
		oakcommon_videoparams_get_native(params);
	if (!native) {
		return OAKNODE_E_INVALID;
	}
	if (index >= s->get_video_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	try {
		s->set_video_params(*native, index);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_sequence_get_audio_params(OakNodeSequence sequence, int index,
									  OakAudioParams **out)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= s->get_audio_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	OakAudioParams *copy =
		oakcore_audioparams_copy(s->get_audio_params(index).handle());
	if (!copy) {
		return OAKNODE_E_NOMEM;
	}
	*out = copy;
	return OAKNODE_OK;
}

int oaknode_sequence_set_audio_params(OakNodeSequence sequence, int index,
									  const OakAudioParams *params)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s || !params || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= s->get_audio_stream_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	OakAudioParams *copy = oakcore_audioparams_copy(params);
	if (!copy) {
		return OAKNODE_E_NOMEM;
	}
	try {
		s->set_audio_params(olive::core::AudioParams::from_handle(copy), index);
	} catch (...) {
		oakcore_audioparams_free(copy);
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

OakNodeNode oaknode_sequence_as_node(OakNodeSequence sequence)
{
	// Borrowed; releasing the result never destroys the sequence
	return make_handle<OakNodeNode>(to_native<olive::Sequence>(sequence),
									false, &delete_as<olive::Node>);
}

int oaknode_sequence_set_default_parameters(OakNodeSequence sequence)
{
	olive::Sequence *s = to_native<olive::Sequence>(sequence);
	if (!s) {
		return OAKNODE_E_INVALID;
	}

	try {
		s->set_default_parameters();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeSequence oaknode_sequence_from_node(OakNodeNode node)
{
	OakNodeSequence empty = {};
	if (!node.ctx) {
		return empty;
	}
	olive::Node *n = oaknode_c_api::to_native<olive::Node>(node);
	if (!n || !dynamic_cast<olive::Sequence *>(n)) {
		return empty;
	}
	return make_handle<OakNodeSequence>(n, false,
										&oaknode_c_api::delete_noop);
}
