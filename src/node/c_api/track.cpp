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

#include "alivecount.h"

#include "valueconvert.h"

#include "block/block.h"
#include "output/track/track.h"
#include "output/track/tracklist.h"
#include "project/sequence/sequence.h"

namespace
{

olive::Track *impl(OakNodeTrack *h)
{
	return reinterpret_cast<olive::Track *>(h);
}

olive::TrackList *list_impl(OakNodeTrackList *h)
{
	return reinterpret_cast<olive::TrackList *>(h);
}

olive::Block *block_impl(OakNodeBlock *h)
{
	return reinterpret_cast<olive::Block *>(h);
}

OakNodeTrack *wrap(olive::Track *t)
{
	return reinterpret_cast<OakNodeTrack *>(t);
}

OakNodeBlock *wrap_block(olive::Block *b)
{
	return reinterpret_cast<OakNodeBlock *>(b);
}

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

OakNodeTrack *oaknode_track_create(int type)
{
	if (!valid_type(type)) {
		return nullptr;
	}
	try {
		olive::Track *t = new olive::Track();
		t->set_type(static_cast<olive::Track::Type>(type));
		oaknode_c_api::alive_inc();
		return wrap(t);
	} catch (...) {
		return nullptr;
	}
}

void oaknode_track_free(OakNodeTrack *track)
{
	if (!track) {
		return;
	}
	delete impl(track);
	oaknode_c_api::alive_dec();
}

int oaknode_track_get_type(OakNodeTrack *track, int *type)
{
	if (!track || !type) {
		return OAKNODE_E_INVALID;
	}
	*type = int(impl(track)->type());
	return OAKNODE_OK;
}

int oaknode_track_set_type(OakNodeTrack *track, int type)
{
	if (!track || !valid_type(type)) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_type(static_cast<olive::Track::Type>(type));
	return OAKNODE_OK;
}

int oaknode_track_get_height(OakNodeTrack *track, double *height)
{
	if (!track || !height) {
		return OAKNODE_E_INVALID;
	}
	*height = impl(track)->get_track_height();
	return OAKNODE_OK;
}

int oaknode_track_set_height(OakNodeTrack *track, double height)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_track_height(height);
	return OAKNODE_OK;
}

int oaknode_track_get_height_in_pixels(OakNodeTrack *track, int *height)
{
	if (!track || !height) {
		return OAKNODE_E_INVALID;
	}
	*height = impl(track)->get_track_height_in_pixels();
	return OAKNODE_OK;
}

int oaknode_track_set_height_in_pixels(OakNodeTrack *track, int height)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_track_height_in_pixels(height);
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

int oaknode_track_get_index(OakNodeTrack *track, int *index)
{
	if (!track || !index) {
		return OAKNODE_E_INVALID;
	}
	*index = impl(track)->index();
	return OAKNODE_OK;
}

int oaknode_track_set_index(OakNodeTrack *track, int index)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_index(index);
	return OAKNODE_OK;
}

int oaknode_track_get_muted(OakNodeTrack *track, int *muted)
{
	if (!track || !muted) {
		return OAKNODE_E_INVALID;
	}
	*muted = impl(track)->is_muted() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_track_set_muted(OakNodeTrack *track, int muted)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_muted(muted != 0);
	return OAKNODE_OK;
}

int oaknode_track_get_locked(OakNodeTrack *track, int *locked)
{
	if (!track || !locked) {
		return OAKNODE_E_INVALID;
	}
	*locked = impl(track)->is_locked() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_track_set_locked(OakNodeTrack *track, int locked)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	impl(track)->set_locked(locked != 0);
	return OAKNODE_OK;
}

int oaknode_track_get_reference(OakNodeTrack *track, int *type, int *index)
{
	if (!track || !type || !index) {
		return OAKNODE_E_INVALID;
	}
	olive::Track::Reference ref = impl(track)->to_reference();
	*type = int(ref.type());
	*index = ref.index();
	return OAKNODE_OK;
}

int oaknode_track_get_length(OakNodeTrack *track, int *numerator,
							 int *denominator)
{
	if (!track) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(track)->track_length(), numerator, denominator);
}

int oaknode_track_get_sequence(OakNodeTrack *track, OakNodeSequence **out)
{
	if (!track || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeSequence *>(impl(track)->sequence());
	return OAKNODE_OK;
}

/* ------------------------------------------------------- Track blocks */

int oaknode_track_get_block_count(OakNodeTrack *track, int *count)
{
	if (!track || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = int(impl(track)->blocks().size());
	return OAKNODE_OK;
}

int oaknode_track_get_block_at(OakNodeTrack *track, int index,
							   OakNodeBlock **out)
{
	if (!track || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const auto &blocks = impl(track)->blocks();
	if (index >= int(blocks.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = wrap_block(blocks.at(index));
	return OAKNODE_OK;
}

int oaknode_track_append_block(OakNodeTrack *track, OakNodeBlock *block)
{
	if (!track || !block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->append_block(block_impl(block));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_prepend_block(OakNodeTrack *track, OakNodeBlock *block)
{
	if (!track || !block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->prepend_block(block_impl(block));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_insert_block_at_index(OakNodeTrack *track,
										OakNodeBlock *block, int index)
{
	if (!track || !block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->insert_block_at_index(block_impl(block), index);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_insert_block_after(OakNodeTrack *track, OakNodeBlock *block,
									 OakNodeBlock *before)
{
	if (!track || !block || !before) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->insert_block_after(block_impl(block), block_impl(before));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_insert_block_before(OakNodeTrack *track, OakNodeBlock *block,
									  OakNodeBlock *after)
{
	if (!track || !block || !after) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->insert_block_before(block_impl(block), block_impl(after));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_ripple_remove_block(OakNodeTrack *track, OakNodeBlock *block)
{
	if (!track || !block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->ripple_remove_block(block_impl(block));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_replace_block(OakNodeTrack *track, OakNodeBlock *old_block,
								OakNodeBlock *new_block)
{
	if (!track || !old_block || !new_block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(track)->replace_block(block_impl(old_block), block_impl(new_block));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	refresh_lengths(impl(track));
	return OAKNODE_OK;
}

int oaknode_track_get_block_index(OakNodeTrack *track, OakNodeBlock *block,
								  int *index)
{
	if (!track || !block || !index) {
		return OAKNODE_E_INVALID;
	}
	int i = impl(track)->get_array_index_from_block(block_impl(block));
	if (i < 0) {
		return OAKNODE_E_NOT_FOUND;
	}
	*index = i;
	return OAKNODE_OK;
}

int oaknode_track_get_block_containing_time(OakNodeTrack *track, int numerator,
											int denominator,
											OakNodeBlock **out)
{
	if (!track || !out) {
		return OAKNODE_E_INVALID;
	}
	olive::Block *b = impl(track)->block_containing_time(
		olive::core::Rational(numerator, denominator));
	if (!b) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = wrap_block(b);
	return OAKNODE_OK;
}

int oaknode_track_get_visible_block_at_time(OakNodeTrack *track, int numerator,
											int denominator,
											OakNodeBlock **out)
{
	if (!track || !out) {
		return OAKNODE_E_INVALID;
	}
	olive::Block *b = impl(track)->visible_block_at_time(
		olive::core::Rational(numerator, denominator));
	if (!b) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = wrap_block(b);
	return OAKNODE_OK;
}

int oaknode_track_is_range_free(OakNodeTrack *track, int in_num, int in_den,
								int out_num, int out_den, int *is_free)
{
	if (!track || !is_free) {
		return OAKNODE_E_INVALID;
	}
	*is_free = impl(track)->is_range_free(
					   olive::core::TimeRange(
						   olive::core::Rational(in_num, in_den),
						   olive::core::Rational(out_num, out_den))) ?
				   1 :
				   0;
	return OAKNODE_OK;
}

/* ------------------------------------------------------------ TrackList */

int oaknode_tracklist_get_type(OakNodeTrackList *list, int *type)
{
	if (!list || !type) {
		return OAKNODE_E_INVALID;
	}
	*type = int(list_impl(list)->type());
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_count(OakNodeTrackList *list, int *count)
{
	if (!list || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = list_impl(list)->get_track_count();
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_at(OakNodeTrackList *list, int index,
								   OakNodeTrack **out)
{
	if (!list || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	if (index >= list_impl(list)->get_track_count()) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = wrap(list_impl(list)->get_track_at(index));
	return OAKNODE_OK;
}

int oaknode_tracklist_get_total_length(OakNodeTrackList *list, int *numerator,
									   int *denominator)
{
	if (!list) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(list_impl(list)->get_total_length(), numerator,
						denominator);
}

int oaknode_tracklist_get_array_size(OakNodeTrackList *list, int *size)
{
	if (!list || !size) {
		return OAKNODE_E_INVALID;
	}
	*size = list_impl(list)->array_size();
	return OAKNODE_OK;
}

int oaknode_tracklist_add_track(OakNodeTrackList *list, OakNodeTrack *track)
{
	if (!list || !track) {
		return OAKNODE_E_INVALID;
	}
	olive::TrackList *l = list_impl(list);
	olive::Track *t = impl(track);
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
	return OAKNODE_OK;
}

int oaknode_tracklist_remove_track(OakNodeTrackList *list, OakNodeTrack *track)
{
	if (!list || !track) {
		return OAKNODE_E_INVALID;
	}
	olive::TrackList *l = list_impl(list);
	olive::Track *t = impl(track);
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

int oaknode_track_get_nearest_block_before_or_at(OakNodeTrack *track,
		int numerator, int denominator, OakNodeBlock **out)
{
	olive::Track *t = impl(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap_block(
		t->nearest_block_before_or_at(olive::core::Rational(numerator, denominator)));
	return OAKNODE_OK;
}

int oaknode_track_get_nearest_block_after_or_at(OakNodeTrack *track,
		int numerator, int denominator, OakNodeBlock **out)
{
	olive::Track *t = impl(track);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap_block(
		t->nearest_block_after_or_at(olive::core::Rational(numerator, denominator)));
	return OAKNODE_OK;
}

int oaknode_tracklist_get_sequence(OakNodeTrackList *list,
								   OakNodeSequence **out)
{
	if (!list || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeSequence *>(list_impl(list)->parent());
	return OAKNODE_OK;
}

int oaknode_tracklist_get_track_input_id(OakNodeTrackList *list, char *buf,
										 int buf_size)
{
	if (!list) {
		return OAKNODE_E_INVALID;
	}
	return oaknode_c_api::copy_string(list_impl(list)->track_input(), buf,
										   buf_size);
}

int oaknode_tracklist_array_append(OakNodeTrackList *list)
{
	if (!list) {
		return OAKNODE_E_INVALID;
	}
	try {
		list_impl(list)->array_append();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_tracklist_array_remove_last(OakNodeTrackList *list)
{
	if (!list) {
		return OAKNODE_E_INVALID;
	}
	try {
		list_impl(list)->array_remove_last();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_tracklist_get_array_index_from_cache_index(
	OakNodeTrackList *list, int cache_index, int *out_index)
{
	if (!list || !out_index) {
		return OAKNODE_E_INVALID;
	}
	*out_index =
		list_impl(list)->get_array_index_from_cache_index(cache_index);
	return OAKNODE_OK;
}

OakNodeNode *oaknode_track_as_node(OakNodeTrack *track)
{
	return reinterpret_cast<OakNodeNode *>(track);
}
