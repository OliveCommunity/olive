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

#include "node/block.h"

#include "alivecount.h"

#include "block/block.h"
#include "block/clip/clip.h"
#include "block/gap/gap.h"
#include "block/transition/crossdissolve/crossdissolvetransition.h"
#include "block/transition/diptocolor/diptocolortransition.h"
#include "block/transition/transition.h"
#include "output/track/track.h"

namespace
{

olive::Block *impl(OakNodeBlock *h)
{
	return reinterpret_cast<olive::Block *>(h);
}

olive::ClipBlock *clip_impl(OakNodeBlock *h)
{
	return h ? dynamic_cast<olive::ClipBlock *>(impl(h)) : nullptr;
}

olive::TransitionBlock *transition_impl(OakNodeBlock *h)
{
	return h ? dynamic_cast<olive::TransitionBlock *>(impl(h)) : nullptr;
}

OakNodeBlock *wrap(olive::Block *b)
{
	return reinterpret_cast<OakNodeBlock *>(b);
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

template <typename T, typename... Args>
OakNodeBlock *create_block(Args &&...args)
{
	try {
		T *b = new T(std::forward<Args>(args)...);
		oaknode_c_api::alive_inc();
		return wrap(b);
	} catch (...) {
		return nullptr;
	}
}

} // namespace

OakNodeBlock *oaknode_block_clip_create(void)
{
	return create_block<olive::ClipBlock>();
}

OakNodeBlock *oaknode_block_gap_create(void)
{
	return create_block<olive::GapBlock>();
}

OakNodeBlock *oaknode_block_transition_create(int kind)
{
	switch (kind) {
	case OAKNODE_TRANSITION_CROSS_DISSOLVE:
		return create_block<olive::CrossDissolveTransition>();
	case OAKNODE_TRANSITION_DIP_TO_COLOR:
		return create_block<olive::DipToColorTransition>();
	default:
		return nullptr;
	}
}

void oaknode_block_free(OakNodeBlock *block)
{
	if (!block) {
		return;
	}
	delete impl(block);
	oaknode_c_api::alive_dec();
}

int oaknode_block_get_in(OakNodeBlock *block, int *numerator, int *denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(block)->in(), numerator, denominator);
}

int oaknode_block_set_in(OakNodeBlock *block, int numerator, int denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	impl(block)->set_in(olive::core::Rational(numerator, denominator));
	return OAKNODE_OK;
}

int oaknode_block_get_out(OakNodeBlock *block, int *numerator, int *denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(block)->out(), numerator, denominator);
}

int oaknode_block_set_out(OakNodeBlock *block, int numerator, int denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	impl(block)->set_out(olive::core::Rational(numerator, denominator));
	return OAKNODE_OK;
}

int oaknode_block_get_length(OakNodeBlock *block, int *numerator,
							 int *denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(impl(block)->length(), numerator, denominator);
}

int oaknode_block_set_length_and_media_out(OakNodeBlock *block, int numerator,
										   int denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(block)->set_length_and_media_out(
			olive::core::Rational(numerator, denominator));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_block_set_length_and_media_in(OakNodeBlock *block, int numerator,
										  int denominator)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	try {
		impl(block)->set_length_and_media_in(
			olive::core::Rational(numerator, denominator));
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_block_get_enabled(OakNodeBlock *block, int *enabled)
{
	if (!block || !enabled) {
		return OAKNODE_E_INVALID;
	}
	*enabled = impl(block)->is_enabled() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_block_set_enabled(OakNodeBlock *block, int enabled)
{
	if (!block) {
		return OAKNODE_E_INVALID;
	}
	impl(block)->set_enabled(enabled != 0);
	return OAKNODE_OK;
}

int oaknode_block_get_previous(OakNodeBlock *block, OakNodeBlock **out)
{
	if (!block || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap(impl(block)->previous());
	return OAKNODE_OK;
}

int oaknode_block_get_next(OakNodeBlock *block, OakNodeBlock **out)
{
	if (!block || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap(impl(block)->next());
	return OAKNODE_OK;
}

int oaknode_block_get_track(OakNodeBlock *block, OakNodeTrack **out)
{
	if (!block || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeTrack *>(impl(block)->track());
	return OAKNODE_OK;
}

int oaknode_block_link(OakNodeBlock *a, OakNodeBlock *b)
{
	if (!a || !b) {
		return OAKNODE_E_INVALID;
	}
	return olive::Node::link(impl(a), impl(b)) ? OAKNODE_OK : OAKNODE_E_FAILED;
}

int oaknode_block_unlink(OakNodeBlock *a, OakNodeBlock *b)
{
	if (!a || !b) {
		return OAKNODE_E_INVALID;
	}
	return olive::Node::unlink(impl(a), impl(b)) ? OAKNODE_OK : OAKNODE_E_FAILED;
}

int oaknode_block_are_linked(OakNodeBlock *a, OakNodeBlock *b, int *linked)
{
	if (!a || !b || !linked) {
		return OAKNODE_E_INVALID;
	}
	*linked = olive::Node::are_linked(impl(a), impl(b)) ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_block_get_link_count(OakNodeBlock *block, int *count)
{
	if (!block || !count) {
		return OAKNODE_E_INVALID;
	}
	*count = int(impl(block)->links().size());
	return OAKNODE_OK;
}

int oaknode_block_get_link_at(OakNodeBlock *block, int index,
							  OakNodeBlock **out)
{
	if (!block || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}
	const auto &links = impl(block)->links();
	if (index >= int(links.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	*out = wrap(static_cast<olive::Block *>(links.at(index)));
	return OAKNODE_OK;
}

/* ---------------------------------------------------------------- Clip */

int oaknode_clip_get_media_in(OakNodeBlock *clip, int *numerator,
							  int *denominator)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(c->media_in(), numerator, denominator);
}

int oaknode_clip_set_media_in(OakNodeBlock *clip, int numerator,
							  int denominator)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	c->set_media_in(olive::core::Rational(numerator, denominator));
	return OAKNODE_OK;
}

int oaknode_clip_get_speed(OakNodeBlock *clip, double *speed)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c || !speed) {
		return OAKNODE_E_INVALID;
	}
	*speed = c->speed();
	return OAKNODE_OK;
}

int oaknode_clip_set_speed(OakNodeBlock *clip, double speed)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	c->set_standard_value(olive::ClipBlock::k_speed_input, speed);
	return OAKNODE_OK;
}

int oaknode_clip_get_reverse(OakNodeBlock *clip, int *reverse)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c || !reverse) {
		return OAKNODE_E_INVALID;
	}
	*reverse = c->reverse() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_clip_set_reverse(OakNodeBlock *clip, int reverse)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	c->set_reverse(reverse != 0);
	return OAKNODE_OK;
}

int oaknode_clip_get_maintain_audio_pitch(OakNodeBlock *clip, int *maintain)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c || !maintain) {
		return OAKNODE_E_INVALID;
	}
	*maintain = c->maintain_audio_pitch() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_clip_set_maintain_audio_pitch(OakNodeBlock *clip, int maintain)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	c->set_maintain_audio_pitch(maintain != 0);
	return OAKNODE_OK;
}

int oaknode_clip_get_loop_mode(OakNodeBlock *clip, int *loop_mode)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c || !loop_mode) {
		return OAKNODE_E_INVALID;
	}
	*loop_mode = int(c->loop_mode());
	return OAKNODE_OK;
}

int oaknode_clip_set_loop_mode(OakNodeBlock *clip, int loop_mode)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c) {
		return OAKNODE_E_INVALID;
	}
	c->set_loop_mode(static_cast<olive::LoopMode>(loop_mode));
	return OAKNODE_OK;
}

int oaknode_clip_get_track_type(OakNodeBlock *clip, int *type)
{
	olive::ClipBlock *c = clip_impl(clip);
	if (!c || !type) {
		return OAKNODE_E_INVALID;
	}
	*type = int(c->get_track_type());
	return OAKNODE_OK;
}

/* ----------------------------------------------------------- Transition */

int oaknode_transition_get_in_offset(OakNodeBlock *transition, int *numerator,
									 int *denominator)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(t->in_offset(), numerator, denominator);
}

int oaknode_transition_get_out_offset(OakNodeBlock *transition, int *numerator,
									  int *denominator)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(t->out_offset(), numerator, denominator);
}

int oaknode_transition_get_offset_center(OakNodeBlock *transition,
										 int *numerator, int *denominator)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	return get_rational(t->offset_center(), numerator, denominator);
}

int oaknode_transition_set_offset_center(OakNodeBlock *transition,
										 int numerator, int denominator)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_offset_center(olive::core::Rational(numerator, denominator));
	return OAKNODE_OK;
}

int oaknode_transition_set_offsets_and_length(OakNodeBlock *transition,
											  int in_num, int in_den,
											  int out_num, int out_den)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t) {
		return OAKNODE_E_INVALID;
	}
	t->set_offsets_and_length(olive::core::Rational(in_num, in_den),
							  olive::core::Rational(out_num, out_den));
	return OAKNODE_OK;
}

int oaknode_transition_is_dual(OakNodeBlock *transition, int *dual)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t || !dual) {
		return OAKNODE_E_INVALID;
	}
	*dual = t->is_dual_transition() ? 1 : 0;
	return OAKNODE_OK;
}

int oaknode_transition_get_connected_out_block(OakNodeBlock *transition,
											   OakNodeBlock **out)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap(t->connected_out_block());
	return OAKNODE_OK;
}

int oaknode_transition_get_connected_in_block(OakNodeBlock *transition,
											  OakNodeBlock **out)
{
	olive::TransitionBlock *t = transition_impl(transition);
	if (!t || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = wrap(t->connected_in_block());
	return OAKNODE_OK;
}

int oaknode_clip_add_cache_passthrough_from(OakNodeBlock *clip,
											OakNodeBlock *other)
{
	if (!clip || !other) {
		return OAKNODE_E_INVALID;
	}

	olive::ClipBlock *c = clip_impl(clip);
	olive::ClipBlock *o = clip_impl(other);
	if (!c || !o) {
		return OAKNODE_E_INVALID;
	}

	try {
		c->add_cache_passthrough_from(o);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode *oaknode_block_as_node(OakNodeBlock *block)
{
	return reinterpret_cast<OakNodeNode *>(block);
}

OakNodeBlock *oaknode_block_from_node(OakNodeNode *node)
{
	if (!node) {
		return NULL;
	}
	olive::Node *n = reinterpret_cast<olive::Node *>(node);
	return wrap(dynamic_cast<olive::Block *>(n));
}

int oaknode_block_get_kind(OakNodeBlock *block, int *out_kind)
{
	if (!block || !out_kind) {
		return OAKNODE_E_INVALID;
	}

	olive::Block *b = impl(block);
	if (dynamic_cast<olive::TransitionBlock *>(b)) {
		*out_kind = OAKNODE_BLOCK_TRANSITION;
	} else if (dynamic_cast<olive::ClipBlock *>(b)) {
		*out_kind = OAKNODE_BLOCK_CLIP;
	} else if (dynamic_cast<olive::GapBlock *>(b)) {
		*out_kind = OAKNODE_BLOCK_GAP;
	} else {
		*out_kind = OAKNODE_BLOCK_OTHER;
	}
	return OAKNODE_OK;
}
