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

#include "vieweroutpututils.h"

namespace olive {

oak::VideoParams video_params_from_pod(const oak_video_params &pod)
{
	return oak::VideoParams(pod);
}

oak::VideoParams empty_video_params()
{
	return oak::VideoParams();
}

Rational sequence_timebase(const void *sequence)
{
	oak_video_params vp = {};
	if (oakengine_viewer_get_video_params(
			reinterpret_cast<const OakEngineNode *>(sequence), 0, &vp) < 0 ||
		vp.time_base_den <= 0) {
		return Rational();
	}
	return Rational(vp.time_base_num, vp.time_base_den);
}

namespace {

Rational viewer_rational(const void *viewer,
						 int (*getter)(const OakEngineNode *, int64_t *,
									   int64_t *))
{
	int64_t num = 0, den = 1;
	if (getter(reinterpret_cast<const OakEngineNode *>(viewer), &num, &den) <
		0) {
		return Rational();
	}
	return Rational(num, den);
}

} // namespace

Rational viewer_output_playhead(const void *viewer)
{
	return viewer_rational(viewer, oakengine_viewer_get_playhead);
}

Rational viewer_output_length(const void *viewer)
{
	return viewer_rational(viewer, oakengine_viewer_get_length);
}

Rational viewer_output_video_length(const void *viewer)
{
	return viewer_rational(viewer, oakengine_viewer_get_video_length);
}

Rational viewer_output_audio_length(const void *viewer)
{
	return viewer_rational(viewer, oakengine_viewer_get_audio_length);
}

oak::VideoParams viewer_output_video_params(const void *viewer, int index)
{
	oak_video_params vp;
	if (oakengine_viewer_get_video_params(
			reinterpret_cast<const OakEngineNode *>(viewer), index, &vp) < 0 ||
		vp.width <= 0 || vp.height <= 0) {
		return empty_video_params();
	}
	return video_params_from_pod(vp);
}

AudioParams viewer_output_audio_params(const void *viewer, int index)
{
	int sample_rate = 0;
	int format = 0;
	uint64_t channel_layout = 0;
	if (oakengine_viewer_get_audio_params(
			reinterpret_cast<const OakEngineNode *>(viewer), index, &sample_rate,
			&channel_layout, &format) < 0 ||
		sample_rate <= 0) {
		return AudioParams();
	}
	return AudioParams(sample_rate, channel_layout,
					   static_cast<SampleFormat::Format>(format));
}

} // namespace olive
