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

#include <olive/core/core.h>

#include "oakengine/node.h"
#include "oakengine/timeline.h"

namespace olive
{

class FrameHashCache;
class AudioWaveformCache;

using olive::core::Rational;
using olive::core::TimeRange;

/**
 * @brief Facade accessors for clip blocks held by the timeline UI.
 *
 * The engine's header-inline clip convenience accessors (speed()/loop_mode()/
 * thumbnails()/waveform()/connected_video_cache()/...) reference the
 * input-id statics (k_speed_input/k_buffer_in/...), which are engine
 * symbols the app must no longer pull across the liboakengine boundary.
 * These helpers route the same queries through the C ABI instead (same
 * pattern as app/widget/keyframeview/keyframehandle.h). Clips are passed
 * as OakEngineBlock* handles; the engine cache types
 * (FrameHashCache/AudioWaveformCache) are forward-declared here and only
 * dereferenced by callers that still see the engine class definition.
 */

inline OakEngineClip *cliphandle(OakEngineBlock *clip)
{
	return reinterpret_cast<OakEngineClip *>(clip);
}

/** @brief The node feeding the clip's buffer input (the inline
 * get_connected_output(k_buffer_in) uses; borrowed, may be null). */
inline OakEngineNode *clip_connected_node(OakEngineBlock *clip)
{
	return oakengine_node_input_get_connected_node(
		reinterpret_cast<OakEngineNode *>(clip),
		oakengine_clip_buffer_input_id(), -1);
}

inline FrameHashCache *clip_thumbnails(OakEngineBlock *clip)
{
	OakEngineNode *n = clip_connected_node(clip);
	return n ? reinterpret_cast<FrameHashCache *>(
				   oakengine_node_get_thumbnail_cache(n)) :
			   nullptr;
}

inline AudioWaveformCache *clip_waveform(OakEngineBlock *clip)
{
	OakEngineNode *n = clip_connected_node(clip);
	return n ? reinterpret_cast<AudioWaveformCache *>(
				   oakengine_node_get_waveform_cache(n)) :
			   nullptr;
}

inline FrameHashCache *clip_connected_video_cache(OakEngineBlock *clip)
{
	OakEngineNode *n = clip_connected_node(clip);
	return n ? reinterpret_cast<FrameHashCache *>(
				   oakengine_node_get_video_frame_cache(n)) :
			   nullptr;
}

/**
 * @brief Owning track handle of a block of any kind (the block's track).
 *
 * Wraps the generic oakengine_block_get_track(); the timeline family's
 * OakEngineTrack* is reinterpreted to the node family's OakEngineNode*
 * (same underlying track object; the track accessors in node.h take the
 * latter). NULL when the block is not on a track.
 */
inline OakEngineNode *block_track_handle(OakEngineBlock *block)
{
	return reinterpret_cast<OakEngineNode *>(oakengine_block_get_track(block));
}

/** @brief The clip's speed through the facade input getter. */
inline double clip_speed(OakEngineBlock *clip)
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

/** @brief The clip's loop mode value (an OAKENGINE_LOOP_MODE_* int). */
inline int clip_loop_mode(OakEngineBlock *clip)
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

/** @brief The clip's reverse flag through the facade input getter. */
inline bool clip_is_reversed(OakEngineBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(reinterpret_cast<OakEngineNode *>(clip),
									oakengine_clip_reverse_input_id(),
									&v) == OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief The clip's maintain-audio-pitch flag through the facade. */
inline bool clip_maintain_audio_pitch(OakEngineBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(
			   reinterpret_cast<OakEngineNode *>(clip),
			   oakengine_clip_maintain_audio_pitch_input_id(), &v) ==
			   OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief Create a new empty clip block through the C ABI. */
inline OakEngineBlock *clip_create_empty(const char *label = nullptr)
{
	return reinterpret_cast<OakEngineBlock *>(oakengine_clip_create_empty(label));
}

/** @brief The clip's media in-point through the facade. */
inline Rational clip_media_in(OakEngineBlock *clip)
{
	int64_t num = 0, den = 1;
	if (oakengine_clip_get_media_in_rational(cliphandle(clip), &num, &den) ==
		OAKENGINE_OK) {
		return Rational(static_cast<int>(num), static_cast<int>(den));
	}
	return Rational(0, 1);
}

/** @brief The clip's media range through the facade. */
inline TimeRange clip_media_range(OakEngineBlock *clip)
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
inline void clip_set_media_in(OakEngineBlock *clip, const Rational &media_in,
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

/** @brief The clip's auto-cache flag through the facade. */
inline bool clip_is_autocaching(OakEngineBlock *clip)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	return oakengine_node_get_input(
			   reinterpret_cast<OakEngineNode *>(clip),
			   oakengine_clip_auto_cache_input_id(), &v) ==
			   OAKENGINE_OK &&
		   v.num != 0;
}

/** @brief Request invalidation from the clip's connected node, through the
 * facade. */
inline void clip_request_invalidate_connected(
	OakEngineBlock *clip, bool force_all = false,
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
