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

#ifndef VIEWEROUTPUTUTILS_H
#define VIEWEROUTPUTUTILS_H

#include <olive/core/render/audioparams.h>

#include <cstring>

#include "oakengine/viewer.h"
#include "oakutil/oakvideo.h"

/**
 * @file vieweroutpututils.h
 * @brief Facade-backed replacements for ViewerOutput's header-inline
 * parameter getters
 *
 * ViewerOutput::get_video_params()/get_audio_params()/... are defined inline
 * in the engine header and reference the ViewerOutput::k_*_params_input
 * statics, so calling them from the app would leave undefined ViewerOutput
 * symbols in oak-editor. These helpers fetch the same values through the
 * oakengine C ABI instead. `viewer` may be any viewer handle (ViewerOutput,
 * Sequence, Footage).
 *
 * Video parameters are returned as oak::VideoParams (oakutil/oakvideo.h),
 * the app-side value type replacing the engine's olive::VideoParams.
 */
namespace olive {

// Same namespace bridge the engine's render/videoparams.h used to provide
// (unqualified Rational/AudioParams/SampleFormat inside namespace olive).
using namespace core;

oak::VideoParams viewer_output_video_params(const void *viewer, int index = 0);

AudioParams viewer_output_audio_params(const void *viewer, int index = 0);

/**
 * @brief Construct an oak::VideoParams from an oak_video_params POD.
 */
oak::VideoParams video_params_from_pod(const oak_video_params &pod);

/** @brief Equivalent to the default-constructed oak::VideoParams(). */
oak::VideoParams empty_video_params();

/**
 * @brief Frame-duration timebase of `sequence` as a Rational (frame rate flipped).
 *
 * Facade-backed replacement for ViewerOutput::get_video_params().frame_rate().
 * flipped(), used for Rational -> timestamp conversions without pulling in
 * ViewerOutput inline symbols.
 */
Rational sequence_timebase(const void *sequence);

/** @brief Facade-backed replacement for ViewerOutput::get_playhead()
 * (rational seconds). */
Rational viewer_output_playhead(const void *viewer);

/** @brief Facade-backed replacement for ViewerOutput::get_length()
 * (rational seconds). */
Rational viewer_output_length(const void *viewer);

/** @brief Facade-backed replacement for ViewerOutput::get_video_length()
 * (rational seconds). */
Rational viewer_output_video_length(const void *viewer);

/** @brief Facade-backed replacement for ViewerOutput::get_audio_length()
 * (rational seconds). */
Rational viewer_output_audio_length(const void *viewer);

/**
 * @brief 1 if `node`'s engine type id (Node::id()) equals `type_id`.
 * Facade-backed replacement for dynamic_cast-based type probes (a
 * dynamic_cast to/from ViewerOutput drags an undefined ViewerOutput
 * typeinfo reference into the app binary).
 */
inline bool viewer_output_node_type_is(const void *node, const char *type_id)
{
	if (!node) {
		return false;
	}
	char buf[128];
	const int len = oakengine_node_get_type_id(
		reinterpret_cast<const OakEngineNode *>(node), buf, sizeof(buf));
	return len >= 0 && len < int(sizeof(buf)) && strcmp(buf, type_id) == 0;
}

/** @brief 1 if `node` is a Sequence (Sequence::id()). */
inline bool viewer_output_is_sequence(const void *node)
{
	return viewer_output_node_type_is(node,
									  "org.olivevideoeditor.Olive.sequence");
}

/** @brief 1 if `node` is a Footage (Footage::id()). */
inline bool viewer_output_is_footage(const void *node)
{
	return viewer_output_node_type_is(node,
									  "org.olivevideoeditor.Olive.footage");
}

} // namespace olive

#endif // VIEWEROUTPUTUTILS_H
