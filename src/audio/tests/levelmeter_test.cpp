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

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "audio/levelmeter.h"

TEST(OakAudioLevelMeter, AnalyzeConstantSignal)
{
	// Constant 0.5 on both channels: peak = rms = 0.5, dB = 20*log10(0.5)
	std::vector<float> ch(1024, 0.5f);
	const float *planes[2] = { ch.data(), ch.data() };
	oakaudio_channel_stats channels[2];
	oakaudio_meter_stats summary;

	ASSERT_EQ(oakaudio_levelmeter_analyze(planes, 2, 1024, channels, 2,
										&summary),
			  OAKAUDIO_OK);

	for (int c = 0; c < 2; c++) {
		EXPECT_DOUBLE_EQ(channels[c].peak_linear, 0.5);
		EXPECT_DOUBLE_EQ(channels[c].rms_linear, 0.5);
		EXPECT_NEAR(channels[c].peak_db, 20.0 * std::log10(0.5), 1e-9);
		EXPECT_NEAR(channels[c].rms_db, 20.0 * std::log10(0.5), 1e-9);
		EXPECT_DOUBLE_EQ(channels[c].vu_db, channels[c].rms_db);
	}

	EXPECT_DOUBLE_EQ(summary.max_peak_linear, 0.5);
	EXPECT_EQ(summary.silence, 0);
	// LUFS = -0.691 + 10*log10(mean square) = -0.691 + 10*log10(0.25)
	EXPECT_NEAR(summary.integrated_lufs, -0.691 + 10.0 * std::log10(0.25),
				1e-9);
}

TEST(OakAudioLevelMeter, AnalyzeSilence)
{
	std::vector<float> ch(512, 0.0f);
	const float *planes[1] = { ch.data() };
	oakaudio_meter_stats summary;

	ASSERT_EQ(oakaudio_levelmeter_analyze(planes, 1, 512, nullptr, 0,
										&summary),
			  OAKAUDIO_OK);
	EXPECT_EQ(summary.silence, 1);
	EXPECT_DOUBLE_EQ(summary.max_peak_linear, 0.0);
	EXPECT_DOUBLE_EQ(summary.integrated_lufs, -200.0);
}

TEST(OakAudioLevelMeter, AnalyzePeakPerChannel)
{
	std::vector<float> quiet(256, 0.1f);
	std::vector<float> loud(256, 0.0f);
	loud[7] = -0.8f; // single peak
	const float *planes[2] = { quiet.data(), loud.data() };
	oakaudio_channel_stats channels[2];

	ASSERT_EQ(oakaudio_levelmeter_analyze(planes, 2, 256, channels, 2,
										nullptr),
			  OAKAUDIO_OK);
	EXPECT_NEAR(channels[0].peak_linear, 0.1, 1e-6);
	EXPECT_NEAR(channels[1].peak_linear, 0.8, 1e-6);
	// dB floor: the zero samples dominate, but the peak channel has signal
	EXPECT_GT(channels[1].peak_db, channels[0].peak_db);
}

TEST(OakAudioLevelMeter, AnalyzeErrorPaths)
{
	std::vector<float> ch(64, 0.5f);
	const float *planes[1] = { ch.data() };
	oakaudio_channel_stats channels[1];
	oakaudio_meter_stats summary;

	// NULL planes
	EXPECT_EQ(oakaudio_levelmeter_analyze(nullptr, 1, 64, channels, 1,
										&summary),
			  OAKAUDIO_E_INVALID);
	// Zero channels
	EXPECT_EQ(oakaudio_levelmeter_analyze(planes, 0, 64, channels, 1,
										&summary),
			  OAKAUDIO_E_INVALID);
	// Negative frame count
	EXPECT_EQ(oakaudio_levelmeter_analyze(planes, 1, -1, channels, 1,
										&summary),
			  OAKAUDIO_E_INVALID);
	// Insufficient channel capacity
	EXPECT_EQ(oakaudio_levelmeter_analyze(planes, 1, 64, channels, 0,
										&summary),
			  OAKAUDIO_E_INVALID);
	// Both outs NULL
	EXPECT_EQ(oakaudio_levelmeter_analyze(planes, 1, 64, nullptr, 0, nullptr),
			  OAKAUDIO_E_INVALID);
	// NULL plane inside array
	const float *bad_planes[1] = { nullptr };
	EXPECT_EQ(oakaudio_levelmeter_analyze(bad_planes, 1, 64, channels, 1,
										&summary),
			  OAKAUDIO_E_INVALID);
}
