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

#ifndef OAK_NODEGLOBALS_H
#define OAK_NODEGLOBALS_H

#include "mathtypes.h"

#include "olive/core/util/timerange.h"
#include "olive/core/render/audioparams.h"
#include "loopmode.h"
#include "videoparams.h"

namespace olive
{

using core::AudioParams;
using core::TimeRange;

class NodeGlobals {
public:
	NodeGlobals()
	{
	}

	NodeGlobals(const VideoParams &vparam, const AudioParams &aparam,
				const TimeRange &time, LoopMode loop_mode)
		: video_params_(vparam)
		, audio_params_(aparam)
		, time_(time)
		, loop_mode_(loop_mode)
	{
	}

	NodeGlobals(const VideoParams &vparam, const AudioParams &aparam,
				const Rational &time, LoopMode loop_mode)
		: NodeGlobals(vparam, aparam,
					  TimeRange(time, time + vparam.frame_rate_as_time_base()),
					  loop_mode)
	{
	}

	Vector2D square_resolution() const
	{
		return Vector2D(float(video_params_.square_pixel_width()), float(video_params_.height()));
	}
	Vector2D nonsquare_resolution() const
	{
		return Vector2D(float(video_params_.width()), float(video_params_.height()));
	}
	const AudioParams &aparams() const
	{
		return audio_params_;
	}
	const VideoParams &vparams() const
	{
		return video_params_;
	}
	const TimeRange &time() const
	{
		return time_;
	}
	LoopMode loop_mode() const
	{
		return loop_mode_;
	}

private:
	VideoParams video_params_;
	AudioParams audio_params_;
	TimeRange time_;
	LoopMode loop_mode_;
};

}

#endif // OAK_NODEGLOBALS_H
