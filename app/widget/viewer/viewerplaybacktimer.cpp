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

#include "viewerplaybacktimer.h"

#include <QtMath>

namespace olive
{

void ViewerPlaybackTimer::start(const int64_t &start_timestamp,
								const int &playback_speed,
								const double &timebase,
								const PlaybackAudioClock *audio_clock)
{
	timer_.start();
	start_timestamp_ = start_timestamp;
	playback_speed_ = playback_speed;
	timebase_ = timebase * 1000;
	audio_clock_ = audio_clock;
}

int64_t ViewerPlaybackTimer::get_timestamp_now() const
{
	// The audio output clock is the master clock when available: sound card
	// consumption is what the viewer must stay in sync with, and unlike the
	// wall clock it cannot drift away from what is actually heard
	if (audio_clock_) {
		const double audio_seconds = audio_clock_->seconds();
		if (audio_seconds >= 0.0) {
			// At speeds other than 1x the audio tempo is scaled, so one
			// output second corresponds to |speed| timeline seconds. The
			// result is already in timeline frames, so it is applied
			// signed rather than multiplied by the speed again.
			const double timeline_ms =
				audio_seconds * 1000.0 * qAbs(playback_speed_);
			const int64_t frames_since_start = qFloor(timeline_ms / timebase_);
			return start_timestamp_ +
				frames_since_start * (playback_speed_ < 0 ? -1 : 1);
		}
	}

	int64_t real_time = timer_.elapsed();

	int64_t frames_since_start =
		qFloor(static_cast<double>(real_time) / (timebase_));

	return start_timestamp_ + frames_since_start * playback_speed_;
}

}
