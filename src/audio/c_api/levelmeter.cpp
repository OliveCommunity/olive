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

#include "audio/levelmeter.h"

#include <cstring>

#include "audiolevelmeter.h"
#include "ffmpeg_bridge/ffmpeg_bridge.h"

using olive::AudioLevelMeter;
using olive::core::AudioParams;
using olive::core::Rational;
using olive::core::SampleBuffer;
using olive::core::SampleFormat;

extern "C" int oakaudio_levelmeter_analyze(const float *const *planar,
		int channel_count, int frame_count,
		oakaudio_channel_stats *channels, int channels_capacity,
		oakaudio_meter_stats *summary)
{
	if (!planar || channel_count <= 0 || frame_count < 0 ||
		(channels && channels_capacity < channel_count)) {
		return OAKAUDIO_E_INVALID;
	}
	if (!channels && !summary) {
		return OAKAUDIO_E_INVALID;
	}

	// Repack into a SampleBuffer (planar f32) for the C++ implementation.
	AudioParams params(48000, fb_channel_layout_default(channel_count),
					SampleFormat(SampleFormat::f32_p));
	SampleBuffer buffer(params, Rational(frame_count, 48000));
	for (int ch = 0; ch < channel_count; ch++) {
		if (!planar[ch]) {
			return OAKAUDIO_E_INVALID;
		}
		if (frame_count > 0) {
			memcpy(buffer.data(ch), planar[ch],
				   size_t(frame_count) * sizeof(float));
		}
	}

	const AudioLevelMeter::Stats stats =
		AudioLevelMeter::analyze_sample_buffer(buffer);

	if (channels) {
		for (int ch = 0; ch < channel_count; ch++) {
			const AudioLevelMeter::ChannelStats &s =
				stats.channels[size_t(ch)];
			oakaudio_channel_stats &dst = channels[ch];
			dst.peak_linear = s.peak_linear;
			dst.peak_db = s.peak_db;
			dst.rms_linear = s.rms_linear;
			dst.rms_db = s.rms_db;
			dst.vu_db = s.vu_db;
		}
	}

	if (summary) {
		summary->max_peak_linear = stats.max_peak_linear;
		summary->integrated_lufs = stats.integrated_lufs;
		summary->silence = stats.silence ? 1 : 0;
	}

	return OAKAUDIO_OK;
}
