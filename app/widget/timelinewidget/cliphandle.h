/***

  Oak - Non-Linear Video Editor
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

#ifndef OAK_CLIPHANDLE_H
#define OAK_CLIPHANDLE_H

#include <cstring>

#include "node/block/clip/clip.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"

namespace olive
{

/**
 * @brief Facade accessors for ClipBlock pointers held by the timeline UI.
 *
 * ClipBlock's header-inline convenience accessors (speed()/loop_mode()/
 * thumbnails()/waveform()/connected_video_cache()/...) reference the
 * input-id statics (k_speed_input/k_buffer_in/...), which are engine
 * symbols the app must no longer pull across the liboakengine boundary.
 * These helpers route the same queries through the C ABI instead (same
 * pattern as app/widget/keyframeview/keyframehandle.h). The ClipBlock*
 * itself stays an opaque identity pointer.
 */

inline OakEngineClip *cliphandle(ClipBlock *clip)
{
	return reinterpret_cast<OakEngineClip *>(clip);
}

/** @brief The node feeding the clip's buffer input (ClipBlock's inline
 * get_connected_output(k_buffer_in) uses; borrowed, may be null). */
inline Node *clip_connected_node(ClipBlock *clip)
{
	return reinterpret_cast<Node *>(
		oakengine_node_input_get_connected_node(
			reinterpret_cast<OakEngineNode *>(clip),
			oakengine_clip_buffer_input_id(), -1));
}

inline FrameHashCache *clip_thumbnails(ClipBlock *clip)
{
	Node *n = clip_connected_node(clip);
	return n ? n->thumbnail_cache() : nullptr;
}

inline AudioWaveformCache *clip_waveform(ClipBlock *clip)
{
	Node *n = clip_connected_node(clip);
	return n ? n->waveform_cache() : nullptr;
}

inline FrameHashCache *clip_connected_video_cache(ClipBlock *clip)
{
	Node *n = clip_connected_node(clip);
	return n ? n->video_frame_cache() : nullptr;
}

/** @brief ClipBlock::speed() through the facade input getter. */
inline double clip_speed(ClipBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	if (oakengine_node_get_input(reinterpret_cast<OakEngineNode *>(clip),
								 oakengine_clip_speed_input_id(),
								 &v) != OAKENGINE_OK) {
		return 1.0;
	}
	return v.f[0];
}

/** @brief ClipBlock::loop_mode() value (an olive::LoopMode int). */
inline int clip_loop_mode(ClipBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	if (oakengine_node_get_input(reinterpret_cast<OakEngineNode *>(clip),
								 oakengine_clip_loop_mode_input_id(),
								 &v) != OAKENGINE_OK) {
		return 0;
	}
	return int(v.num);
}

/** @brief ClipBlock::is_reversed() through the facade input getter. */
inline bool clip_is_reversed(ClipBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(reinterpret_cast<OakEngineNode *>(clip),
									oakengine_clip_reverse_input_id(),
									&v) == OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief ClipBlock::maintain_audio_pitch() through the facade. */
inline bool clip_maintain_audio_pitch(ClipBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(
			   reinterpret_cast<OakEngineNode *>(clip),
			   oakengine_clip_maintain_audio_pitch_input_id(), &v) ==
			   OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief Create a new empty ClipBlock through the C ABI. */
inline ClipBlock *clip_create_empty(const char *label = nullptr)
{
	return reinterpret_cast<ClipBlock *>(oakengine_clip_create_empty(label));
}

/** @brief ClipBlock::media_in() through the facade. */
inline Rational clip_media_in(ClipBlock *clip)
{
	int64_t num = 0, den = 1;
	if (oakengine_clip_get_media_in_rational(cliphandle(clip), &num, &den) ==
		OAKENGINE_OK) {
		return Rational(static_cast<int>(num), static_cast<int>(den));
	}
	return Rational(0, 1);
}

/** @brief ClipBlock::media_range() through the facade. */
inline TimeRange clip_media_range(ClipBlock *clip)
{
	int64_t in_num = 0, in_den = 1, out_num = 0, out_den = 1;
	if (oakengine_clip_get_media_range_rational(
			cliphandle(clip), &in_num, &in_den, &out_num, &out_den) ==
		OAKENGINE_OK) {
		return TimeRange(Rational(static_cast<int>(in_num),
							  static_cast<int>(in_den)),
						 Rational(static_cast<int>(out_num),
							  static_cast<int>(out_den)));
	}
	return TimeRange(0, 0);
}

/** @brief Set the clip's media in-point directly (rational seconds). */
inline void clip_set_media_in(ClipBlock *clip, const Rational &media_in,
							  bool undoable = false)
{
	if (!clip) {
		return;
	}
	oakengine_clip_set_media_in_rational(cliphandle(clip),
									 media_in.numerator(),
									 media_in.denominator(),
									 undoable ? 1 : 0);
}

/** @brief ClipBlock::is_autocaching() through the facade. */
inline bool clip_is_autocaching(ClipBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(
			   reinterpret_cast<OakEngineNode *>(clip),
			   oakengine_clip_auto_cache_input_id(), &v) ==
			   OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief ClipBlock::request_invalidated_from_connected() through the facade. */
inline void clip_request_invalidate_connected(
	ClipBlock *clip, bool force_all = false,
	const TimeRange &intersect = TimeRange())
{
	if (!clip) {
		return;
	}

	int64_t in_num = 0, in_den = 0, out_num = 0, out_den = 0;
	if (!intersect.length().isNull()) {
		const Rational in = intersect.in();
		const Rational out = intersect.out();
		in_num = in.numerator();
		in_den = in.denominator();
		out_num = out.numerator();
		out_den = out.denominator();
	}

	oakengine_clip_request_invalidate_connected(cliphandle(clip), force_all ? 1 : 0,
											in_num, in_den, out_num,
											out_den);
}

} // namespace olive

#endif // OAK_CLIPHANDLE_H
