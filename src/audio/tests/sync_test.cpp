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

#include "audio/sync.h"

namespace
{

// 8-window synthetic envelope with a distinctive shape
std::vector<double> make_envelope()
{
	return { 0.1, 0.5, 0.9, 0.3, 0.2, 0.8, 0.4, 0.1 };
}

} // namespace

TEST(OakAudioSync, ExtractRmsEnvelope)
{
	// Mono, window = 4 frames: window 0 constant 0.5 -> RMS 0.5
	std::vector<float> ch(8, 0.0f);
	for (int i = 0; i < 4; i++)
		ch[size_t(i)] = 0.5f;
	const float *planes[1] = { ch.data() };

	// Query mode
	const int windows =
		oakaudio_sync_extract_rms_envelope(planes, 1, 8, 4, nullptr, 0);
	ASSERT_EQ(windows, 2);

	double envelope[2];
	ASSERT_EQ(oakaudio_sync_extract_rms_envelope(planes, 1, 8, 4, envelope, 2),
			  2);
	EXPECT_DOUBLE_EQ(envelope[0], 0.5);
	EXPECT_DOUBLE_EQ(envelope[1], 0.0);
}

TEST(OakAudioSync, ExtractRmsEnvelopeErrorPaths)
{
	float v = 0.0f;
	const float *planes[1] = { &v };
	double out[1];

	EXPECT_EQ(oakaudio_sync_extract_rms_envelope(nullptr, 1, 8, 4, out, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_extract_rms_envelope(planes, 0, 8, 4, out, 1),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_extract_rms_envelope(planes, 1, 8, 0, out, 1),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioSync, EstimateEnvelopeOffset)
{
	const std::vector<double> ref = make_envelope();
	// Candidate = reference shifted right by 2 windows
	std::vector<double> cand = { 0.0, 0.0 };
	cand.insert(cand.end(), ref.begin(), ref.end());

	oakaudio_offset_result out;
	ASSERT_EQ(oakaudio_sync_estimate_envelope_offset(
				  ref.data(), int(ref.size()), cand.data(), int(cand.size()),
				  nullptr, nullptr, 100, 8, &out),
			  OAKAUDIO_OK);
	EXPECT_EQ(out.valid, 1);
	EXPECT_EQ(out.offset_samples, 2 * 100);
	EXPECT_GT(out.confidence, 0.9);
}

TEST(OakAudioSync, EstimateEnvelopeOffsetWithMasks)
{
	const std::vector<double> ref = make_envelope();
	std::vector<double> cand = { 0.0, 0.0 };
	cand.insert(cand.end(), ref.begin(), ref.end());

	std::vector<uint8_t> all_valid_ref(ref.size(), 1);
	std::vector<uint8_t> all_valid_cand(cand.size(), 1);

	oakaudio_offset_result out;
	ASSERT_EQ(oakaudio_sync_estimate_envelope_offset(
				  ref.data(), int(ref.size()), cand.data(), int(cand.size()),
				  all_valid_ref.data(), all_valid_cand.data(), 100, 8, &out),
			  OAKAUDIO_OK);
	EXPECT_EQ(out.valid, 1);
	EXPECT_EQ(out.offset_samples, 200);
}

TEST(OakAudioSync, EstimateEnvelopeOffsetErrorPaths)
{
	double env[4] = { 0.1, 0.2, 0.3, 0.4 };
	oakaudio_offset_result out;

	EXPECT_EQ(oakaudio_sync_estimate_envelope_offset(nullptr, 4, env, 4,
												   nullptr, nullptr, 100, 4,
												   &out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_estimate_envelope_offset(env, 0, env, 4, nullptr,
												   nullptr, 100, 4, &out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_estimate_envelope_offset(env, 4, env, 4, nullptr,
												   nullptr, 0, 4, &out),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_estimate_envelope_offset(env, 4, env, 4, nullptr,
												   nullptr, 100, 4, nullptr),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioSync, EstimateStretchAndOffset)
{
	const std::vector<double> ref = make_envelope();
	// Candidate runs at half speed: each ref window duplicated
	std::vector<double> cand;
	for (double v : ref) {
		cand.push_back(v);
		cand.push_back(v);
	}

	oakaudio_stretch_offset_result out;
	ASSERT_EQ(oakaudio_sync_estimate_stretch_and_offset(
				  ref.data(), int(ref.size()), cand.data(), int(cand.size()),
				  nullptr, nullptr, 100, 4, 1.0, 2.0, 0.25, &out),
			  OAKAUDIO_OK);
	EXPECT_EQ(out.valid, 1);
	EXPECT_NEAR(out.rate, 2.0, 0.13);
	EXPECT_GT(out.confidence, 0.9);
}

TEST(OakAudioSync, EstimateStretchAndOffsetErrorPaths)
{
	double env[4] = { 0.1, 0.2, 0.3, 0.4 };
	oakaudio_stretch_offset_result out;

	EXPECT_EQ(oakaudio_sync_estimate_stretch_and_offset(
				  nullptr, 4, env, 4, nullptr, nullptr, 100, 4, 1.0, 2.0, 0.5,
				  &out),
			  OAKAUDIO_E_INVALID);
	// min_rate <= 0
	EXPECT_EQ(oakaudio_sync_estimate_stretch_and_offset(
				  env, 4, env, 4, nullptr, nullptr, 100, 4, 0.0, 2.0, 0.5, &out),
			  OAKAUDIO_E_INVALID);
	// max < min
	EXPECT_EQ(oakaudio_sync_estimate_stretch_and_offset(
				  env, 4, env, 4, nullptr, nullptr, 100, 4, 2.0, 1.0, 0.5, &out),
			  OAKAUDIO_E_INVALID);
	// NULL out
	EXPECT_EQ(oakaudio_sync_estimate_stretch_and_offset(
				  env, 4, env, 4, nullptr, nullptr, 100, 4, 1.0, 2.0, 0.5,
				  nullptr),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioSync, PlaceBySourceTime)
{
	oakaudio_source_clip ref = {};
	ref.source_start_time_num = 10; // source clock at 10s
	ref.source_start_time_den = 1;
	ref.media_in_num = 2; // clip head is 2s into the media
	ref.media_in_den = 1;
	ref.has_source_start_time = 1;

	oakaudio_source_clip cand = {};
	cand.source_start_time_num = 14; // 4s later on the same source clock
	cand.source_start_time_den = 1;
	cand.media_in_num = 0;
	cand.media_in_den = 1;
	cand.has_source_start_time = 1;

	int64_t num, den;
	int valid;
	ASSERT_EQ(oakaudio_sync_place_by_source_time(&ref, &cand, 5, 1, &num, &den,
											   &valid),
			  OAKAUDIO_OK);
	EXPECT_EQ(valid, 1);
	// candidate head source = 14+0, reference head source = 10+2 = 12;
	// timeline_in = 5 + 14 - 12 = 7
	EXPECT_EQ(num, 7);
	EXPECT_EQ(den, 1);

	// Missing source start time -> invalid placement, still OAKAUDIO_OK
	cand.has_source_start_time = 0;
	ASSERT_EQ(oakaudio_sync_place_by_source_time(&ref, &cand, 5, 1, &num, &den,
											   &valid),
			  OAKAUDIO_OK);
	EXPECT_EQ(valid, 0);
}

TEST(OakAudioSync, PlaceBySourceTimeErrorPaths)
{
	oakaudio_source_clip clip = {};
	clip.source_start_time_den = 1;
	clip.media_in_den = 1;
	clip.has_source_start_time = 1;

	int64_t num, den;
	int valid;
	EXPECT_EQ(oakaudio_sync_place_by_source_time(nullptr, &clip, 5, 1, &num,
											   &den, &valid),
			  OAKAUDIO_E_INVALID);
	// Zero denominators
	oakaudio_source_clip bad = clip;
	bad.media_in_den = 0;
	EXPECT_EQ(oakaudio_sync_place_by_source_time(&clip, &bad, 5, 1, &num, &den,
											   &valid),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_place_by_source_time(&clip, &clip, 5, 0, &num, &den,
											   &valid),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_place_by_source_time(&clip, &clip, 5, 1, nullptr,
											   &den, &valid),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioSync, PlaceByWaveformOffset)
{
	int64_t num, den;
	int valid;

	// Reference at 5s, candidate is 24000 samples late at 48k -> 5.5s
	ASSERT_EQ(oakaudio_sync_place_by_waveform_offset(5, 1, 24000, 48000, &num,
												  &den, &valid),
			  OAKAUDIO_OK);
	EXPECT_EQ(valid, 1);
	EXPECT_NEAR(double(num) / double(den), 5.5, 1e-6);

	// Bad sample rate -> invalid placement
	ASSERT_EQ(oakaudio_sync_place_by_waveform_offset(5, 1, 24000, 0, &num, &den,
												  &valid),
			  OAKAUDIO_OK);
	EXPECT_EQ(valid, 0);

	// Error path: NULL outs / zero denominator
	EXPECT_EQ(oakaudio_sync_place_by_waveform_offset(5, 0, 24000, 48000, &num,
												  &den, &valid),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_sync_place_by_waveform_offset(5, 1, 24000, 48000,
												   nullptr, &den, &valid),
			  OAKAUDIO_E_INVALID);
}
