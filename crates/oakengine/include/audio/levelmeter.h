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

#ifndef OAK_EDITOR_AUDIO_LEVELMETER_H
#define OAK_EDITOR_AUDIO_LEVELMETER_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file levelmeter.h
 * @brief C ABI for the oakaudio level meter (olive::AudioLevelMeter):
 *        stateless peak/RMS/VU/LUFS analysis of planar float audio.
 */

/** Per-channel analysis results. dB fields floor at -200. */
typedef struct oakaudio_channel_stats {
	double peak_linear;
	double peak_db;
	double rms_linear;
	double rms_db;
	double vu_db;
} oakaudio_channel_stats;

/** Buffer-wide summary. */
typedef struct oakaudio_meter_stats {
	double max_peak_linear;
	double integrated_lufs; /**< BS.1770-compatible unit (no K-weighting). */
	int silence; /**< 1 when the buffer is (near-)silent. */
} oakaudio_meter_stats;

/**
 * @brief Analyze a planar float buffer.
 *
 * @param planar Per-channel float planes.
 * @param channel_count Number of channels (> 0).
 * @param frame_count Frames per channel (>= 0).
 * @param channels Receives per-channel stats; may be NULL.
 * @param channels_capacity Capacity of `channels` (must be >=
 *        channel_count when channels is non-NULL).
 * @param summary Receives the buffer-wide summary; may be NULL.
 * @return OAKAUDIO_OK or OAKAUDIO_E_INVALID.
 */
OAKAUDIO_API int oakaudio_levelmeter_analyze(const float *const *planar,
		int channel_count, int frame_count,
		oakaudio_channel_stats *channels, int channels_capacity,
		oakaudio_meter_stats *summary);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_AUDIO_LEVELMETER_H
