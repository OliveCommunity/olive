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
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "audio/levelmeter.h"
#include "audio/manager.h"
#include "audio/waveform.h"
#include "codec/decoder.h"

namespace
{

struct WaveformHandle {
	OakAudioWaveform h = oakaudio_waveform_init();
	~WaveformHandle() { oakaudio_waveform_free(&h); }
};

std::string demo_file()
{
	return std::string(OAKAUDIO_TEST_DATA_DIR) + "/demo.mp4";
}

} // namespace

TEST(OakAudioWaveform, InitFree)
{
	const int before = oakaudio_debug_alive_count();
	{
		WaveformHandle w;
		ASSERT_NE(w.h.ctx, nullptr);
		EXPECT_EQ(oakaudio_debug_alive_count(), before + 1);
		EXPECT_EQ(oakaudio_waveform_get_channel_count(w.h), 0);
	}
	EXPECT_EQ(oakaudio_debug_alive_count(), before);

	oakaudio_waveform_free(nullptr);
	OakAudioWaveform empty = {};
	oakaudio_waveform_free(&empty);
}

TEST(OakAudioWaveform, ChannelCountAndLength)
{
	WaveformHandle w;
	EXPECT_EQ(oakaudio_waveform_set_channel_count(w.h, 2), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_waveform_get_channel_count(w.h), 2);

	int64_t num = -1, den = -1;
	EXPECT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_EQ(num, 0);

	// Error paths
	OakAudioWaveform empty = {};
	EXPECT_EQ(oakaudio_waveform_get_channel_count(empty), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_set_channel_count(empty, 2), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_set_channel_count(w.h, -1), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_length(empty, &num, &den), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_length(w.h, nullptr, &den), OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, OverwriteSamplesAndSummary)
{
	WaveformHandle w;
	ASSERT_EQ(oakaudio_waveform_set_channel_count(w.h, 1), OAKAUDIO_OK);

	// One second at 48k: first half +0.5, second half -0.5
	std::vector<float> data(48000);
	for (int i = 0; i < 48000; i++) {
		data[size_t(i)] = (i < 24000) ? 0.5f : -0.5f;
	}
	const float *planes[1] = { data.data() };
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(w.h, planes, 48000, 48000, 0,
											   1),
			  OAKAUDIO_OK);

	int64_t num, den;
	ASSERT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 1.0, 1e-9);

	// Summary of the whole second: min -0.5, max +0.5
	oakaudio_min_max pairs[2];
	const int points =
		oakaudio_waveform_get_summary(w.h, 0, 1, 1, 1, pairs, 2);
	ASSERT_EQ(points, 1);
	EXPECT_FLOAT_EQ(pairs[0].min, -0.5f);
	EXPECT_FLOAT_EQ(pairs[0].max, 0.5f);

	// Summary of the first half only: all +0.5
	const int first_half =
		oakaudio_waveform_get_summary(w.h, 0, 1, 1, 2, pairs, 2);
	ASSERT_EQ(first_half, 1);
	EXPECT_FLOAT_EQ(pairs[0].min, 0.5f);
	EXPECT_FLOAT_EQ(pairs[0].max, 0.5f);

	// Query mode: NULL out returns the point count
	EXPECT_EQ(oakaudio_waveform_get_summary(w.h, 0, 1, 1, 1, nullptr, 0), 1);
}

TEST(OakAudioWaveform, OverwriteSamplesErrorPaths)
{
	WaveformHandle w;
	float v = 0.0f;
	const float *planes[1] = { &v };

	// Channel count not set
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(w.h, planes, 16, 48000, 0, 1),
			  OAKAUDIO_E_STATE);

	ASSERT_EQ(oakaudio_waveform_set_channel_count(w.h, 1), OAKAUDIO_OK);
	// Zero denominator
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(w.h, planes, 16, 48000, 0, 0),
			  OAKAUDIO_E_INVALID);
	// NULL planes / bad counts
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(w.h, nullptr, 16, 48000, 0, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(w.h, planes, 0, 48000, 0, 1),
			  OAKAUDIO_E_INVALID);

	OakAudioWaveform empty = {};
	EXPECT_EQ(oakaudio_waveform_overwrite_samples(empty, planes, 16, 48000, 0,
												1),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, OverwriteSilenceAndTrim)
{
	WaveformHandle w;
	ASSERT_EQ(oakaudio_waveform_set_channel_count(w.h, 1), OAKAUDIO_OK);

	// 2 seconds of silence
	EXPECT_EQ(oakaudio_waveform_overwrite_silence(w.h, 0, 1, 2, 1), OAKAUDIO_OK);
	int64_t num, den;
	ASSERT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 2.0, 1e-9);

	oakaudio_min_max pairs[1];
	ASSERT_EQ(oakaudio_waveform_get_summary(w.h, 0, 1, 2, 1, pairs, 1), 1);
	EXPECT_FLOAT_EQ(pairs[0].min, 0.0f);
	EXPECT_FLOAT_EQ(pairs[0].max, 0.0f);

	// Trim away the first second
	EXPECT_EQ(oakaudio_waveform_trim_in(w.h, 1, 1), OAKAUDIO_OK);
	ASSERT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 1.0, 1e-9);

	// Resize to half a second
	EXPECT_EQ(oakaudio_waveform_resize(w.h, 1, 2), OAKAUDIO_OK);
	ASSERT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 0.5, 1e-9);

	// trim_range: in 0, length 1s
	EXPECT_EQ(oakaudio_waveform_trim_range(w.h, 0, 1, 1, 1), OAKAUDIO_OK);
	ASSERT_EQ(oakaudio_waveform_length(w.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 1.0, 1e-9);

	// Error paths
	EXPECT_EQ(oakaudio_waveform_overwrite_silence(w.h, 0, 0, 1, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_trim_in(w.h, 1, 0), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_resize(w.h, 1, 0), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_trim_range(w.h, 0, 1, 1, 0), OAKAUDIO_E_INVALID);
	OakAudioWaveform empty = {};
	EXPECT_EQ(oakaudio_waveform_trim_in(empty, 1, 1), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_resize(empty, 1, 1), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_trim_range(empty, 0, 1, 1, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_overwrite_silence(empty, 0, 1, 1, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_get_summary(empty, 0, 1, 1, 1, pairs, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_get_summary(w.h, 0, 1, 1, 0, pairs, 1),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, OverwriteSums)
{
	WaveformHandle src, dst;
	ASSERT_EQ(oakaudio_waveform_set_channel_count(src.h, 1), OAKAUDIO_OK);
	ASSERT_EQ(oakaudio_waveform_set_channel_count(dst.h, 1), OAKAUDIO_OK);

	std::vector<float> data(48000, 0.25f);
	const float *planes[1] = { data.data() };
	ASSERT_EQ(oakaudio_waveform_overwrite_samples(src.h, planes, 48000, 48000,
											   0, 1),
			  OAKAUDIO_OK);

	// Copy all of src into dst at t=0
	EXPECT_EQ(oakaudio_waveform_overwrite_sums(dst.h, src.h, 0, 1, 0, 1, 0, 1),
			  OAKAUDIO_OK);
	int64_t num, den;
	ASSERT_EQ(oakaudio_waveform_length(dst.h, &num, &den), OAKAUDIO_OK);
	EXPECT_NEAR(double(num) / double(den), 1.0, 1e-9);

	oakaudio_min_max pairs[1];
	ASSERT_EQ(oakaudio_waveform_get_summary(dst.h, 0, 1, 1, 1, pairs, 1), 1);
	EXPECT_FLOAT_EQ(pairs[0].max, 0.25f);

	// Error paths
	OakAudioWaveform empty = {};
	EXPECT_EQ(oakaudio_waveform_overwrite_sums(dst.h, empty, 0, 1, 0, 1, 0, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_overwrite_sums(empty, src.h, 0, 1, 0, 1, 0, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_overwrite_sums(dst.h, src.h, 0, 0, 0, 1, 0, 1),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, SumSamplesStatic)
{
	std::vector<float> ch0 = { 0.1f, -0.4f, 0.3f, 0.2f };
	std::vector<float> ch1 = { -0.9f, 0.5f, 0.1f, 0.0f };
	const float *planes[2] = { ch0.data(), ch1.data() };
	oakaudio_min_max out[2];

	EXPECT_EQ(oakaudio_waveform_sum_samples_s(planes, 2, 0, 4, out),
			  OAKAUDIO_OK);
	EXPECT_FLOAT_EQ(out[0].min, -0.4f);
	EXPECT_FLOAT_EQ(out[0].max, 0.3f);
	EXPECT_FLOAT_EQ(out[1].min, -0.9f);
	EXPECT_FLOAT_EQ(out[1].max, 0.5f);

	// Error paths
	EXPECT_EQ(oakaudio_waveform_sum_samples_s(nullptr, 2, 0, 4, out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_sum_samples_s(planes, 0, 0, 4, out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_sum_samples_s(planes, 2, 0, 0, out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_sum_samples_s(planes, 2, 0, 4, nullptr),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, ReSumStatic)
{
	oakaudio_min_max in[4] = { { -0.5f, 0.4f }, { -0.2f, 0.9f },
						   { -0.7f, 0.1f }, { 0.0f, 0.3f } };
	oakaudio_min_max out[2];

	EXPECT_EQ(oakaudio_waveform_re_sum_s(in, 4, 2, out), OAKAUDIO_OK);
	EXPECT_FLOAT_EQ(out[0].min, -0.7f);
	EXPECT_FLOAT_EQ(out[0].max, 0.4f);
	EXPECT_FLOAT_EQ(out[1].min, -0.2f);
	EXPECT_FLOAT_EQ(out[1].max, 0.9f);

	// Error paths
	EXPECT_EQ(oakaudio_waveform_re_sum_s(nullptr, 4, 2, out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_re_sum_s(in, 0, 2, out), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_re_sum_s(in, 4, 0, out), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_waveform_re_sum_s(in, 4, 2, nullptr),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioWaveform, ExtractFromMediaFile)
{
	const std::string file = demo_file();

	// Probe the expected duration independently
	OakDecoder probe = oakcodec_decoder_probe(file.c_str());
	ASSERT_NE(probe.ctx, nullptr) << "demo.mp4 not decodable";
	oakcodec_audio_stream_info info;
	ASSERT_EQ(oakcodec_decoder_probe_get_audio_stream(probe, 0, &info),
			  OAKCODEC_OK);
	// The audio stream's duration_ts is not populated for this file; the
	// video stream carries the clip duration.
	oakcodec_video_stream_info vinfo;
	double duration = 0.0;
	if (oakcodec_decoder_probe_get_video_stream(probe, 0, &vinfo) ==
			OAKCODEC_OK &&
		vinfo.time_base_den > 0) {
		duration = double(vinfo.duration_ts) * vinfo.time_base_num /
				   vinfo.time_base_den;
	}
	oakcodec_decoder_free(&probe);
	ASSERT_GT(duration, 0.0);

	const int before = oakaudio_debug_alive_count();

	constexpr int kSamplesPerPoint = 1024;
	// Two-stage sizing: NULL out returns the required point count
	const int required = oakaudio_waveform_extract(
		file.c_str(), 0, kSamplesPerPoint, nullptr, 0, nullptr);
	ASSERT_GT(required, 0);

	std::vector<oakaudio_min_max> pairs(size_t(required) * 4);
	int channels = 0;
	const int points =
		oakaudio_waveform_extract(file.c_str(), 0, kSamplesPerPoint,
							   pairs.data(), int(pairs.size()), &channels);
	ASSERT_EQ(points, required);
	EXPECT_EQ(channels, info.channel_count);

	// Length consistency with the stream duration (generous tolerance for
	// container/decoder rounding)
	const double covered =
		double(points) * kSamplesPerPoint / info.sample_rate;
	EXPECT_NEAR(covered, duration, std::max(0.5, duration * 0.1));

	// Non-trivial content: at least one point carries signal
	bool any_signal = false;
	for (int i = 0; i < points * channels; i++) {
		if (pairs[size_t(i)].max > 0.0f || pairs[size_t(i)].min < 0.0f) {
			any_signal = true;
			break;
		}
	}
	EXPECT_TRUE(any_signal);

	EXPECT_EQ(oakaudio_debug_alive_count(), before);
}

TEST(OakAudioWaveform, ExtractErrorPaths)
{
	int channels = 0;
	oakaudio_min_max pairs[8];

	// Nonexistent file
	EXPECT_EQ(oakaudio_waveform_extract("/nonexistent/file.mp4", 0, 1024,
									 pairs, 8, &channels),
			  OAKAUDIO_E_NOT_FOUND);
	// NULL filename
	EXPECT_EQ(oakaudio_waveform_extract(nullptr, 0, 1024, pairs, 8, &channels),
			  OAKAUDIO_E_INVALID);
	// Bad stream index
	EXPECT_EQ(oakaudio_waveform_extract(demo_file().c_str(), 99, 1024, pairs, 8,
									  &channels),
			  OAKAUDIO_E_NOT_FOUND);
	// Bad samples-per-point
	EXPECT_EQ(oakaudio_waveform_extract(demo_file().c_str(), 0, 0, pairs, 8,
									  &channels),
			  OAKAUDIO_E_INVALID);
}
