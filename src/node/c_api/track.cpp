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

#include "node/track.h"

#include <algorithm>

#include "node/block.h"
#include "node/node.h"
#include "node/sequence.h"

#include "valueconvert.h"

#include "block/block.h"
#include "output/track/track.h"
#include "output/track/tracklist.h"
#include "project/sequence/sequence.h"

#include "nodehandle.h"

using oaknode_c_api::delete_as;
using oaknode_c_api::free_handle;
using oaknode_c_api::make_handle;
using oaknode_c_api::mark_container_owned;
using oaknode_c_api::to_native;

namespace
{

bool valid_type(int type)
{
	return type >= OAKNODE_TRACK_TYPE_VIDEO && type < OAKNODE_TRACK_TYPE_COUNT;
}

/**
 * @brief Refresh the cached lengths after a block mutation
 *
 * De-Qt wave: TrackList::update_total_length / Sequence::verify_length
 * were signal-driven; the C API performs the refresh synchronously so
 * that state read back right after a mutation is consistent.
 */
void refresh_lengths(olive::Track *t)
{
	olive::Sequence *s = t->sequence();
	if (s && valid_type(int(t->type()))) {
		olive::TrackList *l = s->track_list(t->type());
		if (l) {
			l->update_total_length();
		}
		s->verify_length();
	}
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

} // namespace

/* ---------------------------------------------------------------- Track */

OakNodeTrack oaknode_track_create(int type)
{
	if (!valid_type(type)) {
		return OakNodeTrack{};
	}
	try {
		olive::Track *t = new olive::Track();
		t->set_type(static_cast<olive::Track::Type>(type));
		return make_handle<OakNodeTrack>(t, true, &delete_as<olive::Track>);
	} catch (...) {
		return OakNodeTrack{};
	}
}

void oaknode_track_free(OakNodeTrack *track)
{
	free_handle(track);
}

int oaknode_track_get_type(OakNodeTrack track, int *type)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !type) {
		return OAKNODE_E_INVALID;
	}
	*type = int(t->type());
	return OAKNODE_OK;
}

int oaknode_track_set_type(OakNodeTrack track, int type)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !valid_type(type)) {
		return OAKNODE_E_INVALID;
	}
	t->set_type(static_cast<olive::Track::Type>(type));
	return OAKNODE_OK;
}

int oaknode_track_get_height(OakNodeTrack track, double *height)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !height) {
		return OAKNODE_E_INVALID;
	}
	*height = t->get_track_height();
	return OAKNODE_OK;
}

int oaknode_track_set_height(OakNodeTrack track, double height)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_track_height(height);
	return OAKNODE_OK;
}

int oaknode_track_get_height_in_pixels(OakNodeTrack track, int *height)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !height) {
		return OAKNODE_E_INVALID;
	}
	*height = t->get_track_height_in_pixels();
	return OAKNODE_OK;
}

int oaknode_track_set_height_in_pixels(OakNodeTrack track, int height)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_track_height_in_pixels(height);
	return OAKNODE_OK;
}

int oaknode_track_get_default_height_in_pixels(void)
{
	return olive::Track::get_default_track_height_in_pixels();
}

int oaknode_track_get_minimum_height_in_pixels(void)
{
	return olive::Track::get_minimum_track_height_in_pixels();
}

int oaknode_track_get_index(OakNodeTrack track, int *index)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !index) {
		return OAKNODE_E_INVALID;
	}
	*index = t->index();
	return OAKNODE_OK;
}

int oaknode_track_set_index(OakNodeTrack track, int index)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_index(index);
	return OAKNODE_OK;
}

int oaknode_track_get_muted(OakNodeTrack track, int *muted)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !muted) {
		return OAKNODE_E_INVALID;
	}
	*muted = t->is_muted() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_track_set_muted(OakNodeTrack track, int muted)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_muted(muted != 0);
	return OAKNODE_OK;
}

int oaknode_track_get_locked(OakNodeTrack track, int *locked)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !locked) {
		return OAKNODE_E_INVALID;
	}
	*locked = t->is_locked() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_track_set_locked(OakNodeTrack track, int locked)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_locked(locked != 0);
	return OAKNODE_OK;
}

int oaknode_track_get_reference(OakNodeTrack track, int *type, int *index)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !type || !index) {
		return OAKNODE_E_INVALID;
	}
	olive::Track::Reference ref = t->to_reference();
	*type = int(ref.type());
	*index = ref.index();
	return OAKNODE_OK;
}

int oaknode_track_get_length(OakNodeTrack track, int *numerator,
							 int *denominator)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(t->track_length(), numerator, denominator);
}

int oaknode_track_get_sequence(OakNodeTrack track, OakNodeSequence *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	// Borrowed (empty when the track is not in a sequence)
	*out = make_handle<OakNodeSequence>(t->sequence(), false,
										&delete_as<olive::Sequence>);
	return OAKNODE_OK;
}

/* ------------------------------------------------------- Track blocks */

int oaknode_track_get_block_count(OakNodeTrack track, int *count)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = int(t->blocks().size());
	return OAKNODE_OK;
}

int oaknode_track_get_block_at(OakNodeTrack track, int index,
							   OakNodeBlock *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const auto &blocks = t->blocks();
	if (index >= int(blocks.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	// Borrowed; the block stays owned by the track
	*out = make_handle<OakNodeBlock>(blocks.at(index), false,
									 &delete_as<olive::Block>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_track_append_block(OakNodeTrack track, OakNodeBlock block)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	if (!t || !b) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->append_block(b);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	// The track now owns the block; the caller's handle becomes a
	// non-owning reference (its release no longer deletes)
	mark_container_owned(block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_prepend_block(OakNodeTrack track, OakNodeBlock block)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	if (!t || !b) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->prepend_block(b);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	mark_container_owned(block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_insert_block_at_index(OakNodeTrack track,
										OakNodeBlock block, int index)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	if (!t || !b) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->insert_block_at_index(b, index);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	mark_container_owned(block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_insert_block_after(OakNodeTrack track, OakNodeBlock block,
									 OakNodeBlock before)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	olive::Block *bf = to_native<olive::Block>(before);
	if (!t || !b || !bf) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->insert_block_after(b, bf);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	mark_container_owned(block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_insert_block_before(OakNodeTrack track, OakNodeBlock block,
									  OakNodeBlock after)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	olive::Block *af = to_native<olive::Block>(after);
	if (!t || !b || !af) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->insert_block_before(b, af);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	mark_container_owned(block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_ripple_remove_block(OakNodeTrack track, OakNodeBlock block)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	if (!t || !b) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->ripple_remove_block(b);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_replace_block(OakNodeTrack track, OakNodeBlock old_block,
								OakNodeBlock new_block)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *old_b = to_native<olive::Block>(old_block);
	olive::Block *new_b = to_native<olive::Block>(new_block);
	if (!t || !old_b || !new_b) {
		return OAKNODE_E_INVALID;
	}
	try {
		t->replace_block(old_b, new_b);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	// The track takes over the replacement; the caller's handle to it
	// becomes non-owning
	mark_container_owned(new_block);
	refresh_lengths(t);
	return OAKNODE_OK;
}

int oaknode_track_get_block_index(OakNodeTrack track, OakNodeBlock block,
								  int *index)
{
	olive::Track *t = to_native<olive::Track>(track);
	olive::Block *b = to_native<olive::Block>(block);
	if (!t || !b || !index) {
		return OAKNODE_E_INVALID;
	}
	int i = t->get_array_index_from_block(b);
	if (i < 0) {
		return OAKNODE_E_NOT_FOUND;
	}
	*index = i;
	return OAKNODE_OK;
}

int oaknode_track_get_block_containing_time(OakNodeTrack track, int numerator,
											int denominator,
											OakNodeBlock *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	olive::Block *b = t->block_containing_time(
		olive::core::Rational(numerator, denominator));
	if (!b) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = make_handle<OakNodeBlock>(b, false, &delete_as<olive::Block>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_track_get_visible_block_at_time(OakNodeTrack track, int numerator,
											int denominator,
											OakNodeBlock *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	olive::Block *b = t->visible_block_at_time(
		olive::core::Rational(numerator, denominator));
	if (!b) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = make_handle<OakNodeBlock>(b, false, &delete_as<olive::Block>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_track_is_range_free(OakNodeTrack track, int in_num, int in_den,
								int out_num, int out_den, int *is_free)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !is_free) {
		return OAKNODE_E_INVALID;
	}
	*is_free = t->is_range_free(
				   olive::core::TimeRange(
					   olive::core::Rational(in_num, in_den),
					   olive::core::Rational(out_num, out_den))) ?
				   1 :
				   0;
	return OAKNODE_OK;
}

/* ------------------------------------------------------------ TrackList */

int oaknode_tracklist_get_type(OakNodeTrackList list, int *type)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !type) {
		return OAKNODE_E_INVALID;
	}
	*type = int(l->type());
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_count(OakNodeTrackList list, int *count)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = l->get_track_count();
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_at(OakNodeTrackList list, int index,
								   OakNodeTrack *out)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= l->get_track_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	// Borrowed; the track stays owned by the list
	*out = make_handle<OakNodeTrack>(l->get_track_at(index), false,
									 &delete_as<olive::Track>);
	return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
}

int oaknode_tracklist_get_total_length(OakNodeTrackList list, int *numerator,
									   int *denominator)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(l->get_total_length(), numerator, denominator);
}

int oaknode_tracklist_get_array_size(OakNodeTrackList list, int *size)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !size) {
		return OAKNODE_E_INVALID;
	}
	*size = l->array_size();
	return OAKNODE_OK;
}

int oaknode_tracklist_add_track(OakNodeTrackList list, OakNodeTrack track)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	olive::Track *t = to_native<olive::Track>(track);
	if (!l || !t) {
		return OAKNODE_E_INVALID;
	}
	olive::Sequence *sequence = l->parent();
	if (!sequence) {
		return OAKNODE_E_STATE;
	}
	try {
		// Graph steps of TimelineAddTrackCommand::redo() minus auto-merge
		t->set_parent(l->get_parent_graph());
		if (l->get_track_count() > 0) {
			t->set_track_height(
				l->get_track_at(l->get_track_count() - 1)->get_track_height());
		}
		l->array_append();
		olive::Node::connect_edge(t, l->track_input(l->array_size() - 1));

		// De-Qt wave: the former signal emissions are the caller's job
		sequence->update_track_cache();
		l->update_total_length();
		sequence->verify_length();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	// The graph now owns the track; the caller's handle becomes a
	// non-owning reference (its release no longer deletes)
	mark_container_owned(track);
	return OAKNODE_OK;
}

int oaknode_tracklist_remove_track(OakNodeTrackList list, OakNodeTrack track)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	olive::Track *t = to_native<olive::Track>(track);
	if (!l || !t) {
		return OAKNODE_E_INVALID;
	}
	olive::Sequence *sequence = l->parent();
	if (!sequence) {
		return OAKNODE_E_STATE;
	}

	const auto &tracks = l->get_tracks();
	auto it = std::find(tracks.begin(), tracks.end(), t);
	if (it == tracks.end()) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		int cache_index = int(it - tracks.begin());
		int array_index = l->get_array_index_from_cache_index(cache_index);

		olive::Node::disconnect_edge(t, l->track_input(array_index));
		sequence->input_array_remove(l->track_input(), array_index);

		// De-Qt wave: the former signal emissions are the caller's job
		sequence->update_track_cache();
		l->update_total_length();
		sequence->verify_length();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_track_get_nearest_block_before_or_at(OakNodeTrack track,
		int numerator, int denominator, OakNodeBlock *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	// Borrowed (empty when there is no such block)
	*out = make_handle<OakNodeBlock>(
		t->nearest_block_before_or_at(olive::core::Rational(numerator, denominator)),
		false, &delete_as<olive::Block>);
	return OAKNODE_OK;
}

int oaknode_track_get_nearest_block_after_or_at(OakNodeTrack track,
		int numerator, int denominator, OakNodeBlock *out)
{
	olive::Track *t = to_native<olive::Track>(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = make_handle<OakNodeBlock>(
		t->nearest_block_after_or_at(olive::core::Rational(numerator, denominator)),
		false, &delete_as<olive::Block>);
	return OAKNODE_OK;
}

int oaknode_tracklist_get_sequence(OakNodeTrackList list,
								   OakNodeSequence *out)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !out) {
		return OAKNODE_E_INVALID;
	}
	// Borrowed; the sequence owns the list
	*out = make_handle<OakNodeSequence>(l->parent(), false,
										&delete_as<olive::Sequence>);
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_input_id(OakNodeTrackList list, char *buf,
										 int buf_size)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l) {
		return OAKNODE_E_INVALID;
	}
	return oaknode_c_api::copy_string(l->track_input(), buf, buf_size);
}

int oaknode_tracklist_array_append(OakNodeTrackList list)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l) {
		return OAKNODE_E_INVALID;
	}
	try {
		l->array_append();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_tracklist_array_remove_last(OakNodeTrackList list)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l) {
		return OAKNODE_E_INVALID;
	}
	try {
		l->array_remove_last();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_tracklist_get_array_index_from_cache_index(
	OakNodeTrackList list, int cache_index, int *out_index)
{
	olive::TrackList *l = to_native<olive::TrackList>(list);
	if (!l || !out_index) {
		return OAKNODE_E_INVALID;
	}
	*out_index = l->get_array_index_from_cache_index(cache_index);
	return OAKNODE_OK;
}

OakNodeNode oaknode_track_as_node(OakNodeTrack track)
{
	// Borrowed; releasing the result never destroys the track
	return make_handle<OakNodeNode>(to_native<olive::Track>(track), false,
									&delete_as<olive::Node>);
}
