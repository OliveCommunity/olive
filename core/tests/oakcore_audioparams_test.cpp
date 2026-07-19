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

// Pure C API test for oakcore/audioparams.h: no gtest, no C++ wrappers.
// oakcore/audioparams.h includes oakcore/rational.h for the OakRational
// handles crossing the boundary.

#include <cassert>
#include <cstdint>
#include <cstdio>

#include "olive/core/oakcore/audioparams.h"

// Mirror of render/channellayout.h values, spelled out so this test only
// includes the C API header.
static const uint64_t k_layout_mono = 0x4;
static const uint64_t k_layout_stereo = 0x3;
static const uint64_t k_layout_2_1 = 0x103;
static const uint64_t k_layout_5_point1 = 0x60F;
static const uint64_t k_layout_7_point1 = 0x63F;

// Mirror of render/sampleformat.h enum values.
static const int k_format_invalid = -1;
static const int k_format_u8_p = 0;
static const int k_format_f32_p = 4;
static const int k_format_s16 = 7;
static const int k_format_f64 = 11;

int main()
{
	// Default constructor: invalid parameters with footage defaults
	{
		OakAudioParams *p = oakcore_audioparams_create_invalid();
		assert(p != nullptr);
		assert(oakcore_audioparams_is_valid(p) == 0);
		assert(oakcore_audioparams_sample_rate(p) == 0);
		assert(oakcore_audioparams_channel_layout(p) == 0);
		assert(oakcore_audioparams_channel_count(p) == 0);
		assert(oakcore_audioparams_format(p) == k_format_invalid);
		assert(oakcore_audioparams_bytes_per_sample_per_channel(p) == 0);
		assert(oakcore_audioparams_bits_per_sample(p) == 0);
		assert(oakcore_audioparams_enabled(p) == 1);
		assert(oakcore_audioparams_stream_index(p) == 0);
		assert(oakcore_audioparams_duration(p) == 0);

		OakRational *tb = oakcore_audioparams_time_base(p);
		assert(oakcore_rational_is_null(tb) == 1);
		oakcore_rational_free(tb);

		oakcore_audioparams_free(p);
	}

	// Full constructor: 48000 Hz stereo s16
	OakAudioParams *p =
		oakcore_audioparams_create(48000, k_layout_stereo, k_format_s16);
	assert(p != nullptr);
	assert(oakcore_audioparams_is_valid(p) == 1);
	assert(oakcore_audioparams_sample_rate(p) == 48000);
	assert(oakcore_audioparams_channel_layout(p) == k_layout_stereo);
	assert(oakcore_audioparams_channel_count(p) == 2);
	assert(oakcore_audioparams_format(p) == k_format_s16);
	assert(oakcore_audioparams_bytes_per_sample_per_channel(p) == 2);
	assert(oakcore_audioparams_bits_per_sample(p) == 16);
	assert(oakcore_audioparams_enabled(p) == 1);
	assert(oakcore_audioparams_stream_index(p) == 0);
	assert(oakcore_audioparams_duration(p) == 0);

	// Timebase defaults to 1/sample_rate
	{
		OakRational *tb = oakcore_audioparams_time_base(p);
		assert(oakcore_rational_numerator(tb) == 1);
		assert(oakcore_rational_denominator(tb) == 48000);
		oakcore_rational_free(tb);

		OakRational *srtb = oakcore_audioparams_sample_rate_as_time_base(p);
		assert(oakcore_rational_numerator(srtb) == 1);
		assert(oakcore_rational_denominator(srtb) == 48000);
		oakcore_rational_free(srtb);
	}

	// Setters / getters
	oakcore_audioparams_set_sample_rate(p, 44100);
	assert(oakcore_audioparams_sample_rate(p) == 44100);
	oakcore_audioparams_set_sample_rate(p, 48000);
	assert(oakcore_audioparams_sample_rate(p) == 48000);

	// Changing the layout recalculates the channel count
	oakcore_audioparams_set_channel_layout(p, k_layout_mono);
	assert(oakcore_audioparams_channel_layout(p) == k_layout_mono);
	assert(oakcore_audioparams_channel_count(p) == 1);
	oakcore_audioparams_set_channel_layout(p, k_layout_5_point1);
	assert(oakcore_audioparams_channel_count(p) == 6);
	oakcore_audioparams_set_channel_layout(p, k_layout_stereo);
	assert(oakcore_audioparams_channel_count(p) == 2);

	{
		OakRational *tb = oakcore_rational_create_nd(1, 1000);
		oakcore_audioparams_set_time_base(p, tb);
		oakcore_rational_free(tb);

		OakRational *got = oakcore_audioparams_time_base(p);
		assert(oakcore_rational_numerator(got) == 1);
		assert(oakcore_rational_denominator(got) == 1000);
		oakcore_rational_free(got);

		// Restore the default 1/48000 timebase
		tb = oakcore_rational_create_nd(1, 48000);
		oakcore_audioparams_set_time_base(p, tb);
		oakcore_rational_free(tb);
	}

	oakcore_audioparams_set_format(p, k_format_f32_p);
	assert(oakcore_audioparams_format(p) == k_format_f32_p);
	assert(oakcore_audioparams_bytes_per_sample_per_channel(p) == 4);
	assert(oakcore_audioparams_bits_per_sample(p) == 32);
	oakcore_audioparams_set_format(p, k_format_s16);
	assert(oakcore_audioparams_bytes_per_sample_per_channel(p) == 2);

	oakcore_audioparams_set_enabled(p, 0);
	assert(oakcore_audioparams_enabled(p) == 0);
	oakcore_audioparams_set_enabled(p, 1);
	assert(oakcore_audioparams_enabled(p) == 1);

	oakcore_audioparams_set_stream_index(p, 3);
	assert(oakcore_audioparams_stream_index(p) == 3);
	oakcore_audioparams_set_stream_index(p, 0);

	const int64_t big_duration = int64_t(1) << 40;
	oakcore_audioparams_set_duration(p, big_duration);
	assert(oakcore_audioparams_duration(p) == big_duration);
	oakcore_audioparams_set_duration(p, 0);

	// Time/sample/byte conversions (48000 Hz, 2 channels, 2 bytes/sample)
	assert(oakcore_audioparams_time_to_samples(p, 1.0) == 48000);
	assert(oakcore_audioparams_time_to_samples(p, 0.5) == 24000);
	assert(oakcore_audioparams_time_to_samples(p, -1.0) == -48000);
	assert(oakcore_audioparams_time_to_samples(p, 0.0) == 0);

	assert(oakcore_audioparams_samples_to_bytes_per_channel(p, 100) == 200);
	assert(oakcore_audioparams_samples_to_bytes(p, 100) == 400);
	assert(oakcore_audioparams_samples_to_bytes(p, 0) == 0);

	assert(oakcore_audioparams_time_to_bytes_per_channel(p, 1.0) == 96000);
	assert(oakcore_audioparams_time_to_bytes(p, 1.0) == 192000);

	assert(oakcore_audioparams_bytes_to_samples(p, 400) == 100);
	assert(oakcore_audioparams_bytes_to_samples(p, 0) == 0);

	// Rational-taking overloads
	{
		OakRational *half = oakcore_rational_create_nd(1, 2);
		assert(oakcore_audioparams_time_to_samples_rational(p, half) == 24000);
		assert(oakcore_audioparams_time_to_bytes_rational(p, half) == 96000);
		assert(oakcore_audioparams_time_to_bytes_per_channel_rational(p, half) ==
			   48000);
		oakcore_rational_free(half);
	}

	// Rational-returning conversions
	{
		OakRational *t = oakcore_audioparams_samples_to_time(p, 48000);
		assert(oakcore_rational_numerator(t) == 1);
		assert(oakcore_rational_denominator(t) == 1);
		oakcore_rational_free(t);

		t = oakcore_audioparams_samples_to_time(p, 24000);
		assert(oakcore_rational_numerator(t) == 1);
		assert(oakcore_rational_denominator(t) == 2);
		oakcore_rational_free(t);

		t = oakcore_audioparams_bytes_to_time(p, 192000);
		assert(oakcore_rational_numerator(t) == 1);
		assert(oakcore_rational_denominator(t) == 1);
		oakcore_rational_free(t);

		t = oakcore_audioparams_bytes_per_channel_to_time(p, 96000);
		assert(oakcore_rational_numerator(t) == 1);
		assert(oakcore_rational_denominator(t) == 1);
		oakcore_rational_free(t);
	}

	// Copy + equality
	{
		OakAudioParams *copy = oakcore_audioparams_copy(p);
		assert(copy != nullptr);
		assert(oakcore_audioparams_equals(p, copy) == 1);
		assert(oakcore_audioparams_equals(copy, copy) == 1);

		oakcore_audioparams_set_sample_rate(copy, 96000);
		assert(oakcore_audioparams_equals(p, copy) == 0);
		oakcore_audioparams_set_sample_rate(copy, 48000);
		assert(oakcore_audioparams_equals(p, copy) == 1);

		OakAudioParams *invalid = oakcore_audioparams_create_invalid();
		assert(oakcore_audioparams_equals(p, invalid) == 0);
		assert(oakcore_audioparams_equals(invalid, invalid) == 1);
		oakcore_audioparams_free(invalid);

		oakcore_audioparams_free(copy);
	}

	// Supported channel layouts / sample rates
	{
		assert(oakcore_audioparams_supported_channel_layout_count() == 5);
		assert(oakcore_audioparams_supported_channel_layout_at(0) == k_layout_mono);
		assert(oakcore_audioparams_supported_channel_layout_at(1) ==
			   k_layout_stereo);
		assert(oakcore_audioparams_supported_channel_layout_at(2) == k_layout_2_1);
		assert(oakcore_audioparams_supported_channel_layout_at(3) ==
			   k_layout_5_point1);
		assert(oakcore_audioparams_supported_channel_layout_at(4) ==
			   k_layout_7_point1);
		// Out-of-range indices return 0
		assert(oakcore_audioparams_supported_channel_layout_at(-1) == 0);
		assert(oakcore_audioparams_supported_channel_layout_at(5) == 0);

		assert(oakcore_audioparams_supported_sample_rate_count() == 10);
		assert(oakcore_audioparams_supported_sample_rate_at(0) == 8000);
		assert(oakcore_audioparams_supported_sample_rate_at(6) == 44100);
		assert(oakcore_audioparams_supported_sample_rate_at(7) == 48000);
		assert(oakcore_audioparams_supported_sample_rate_at(9) == 96000);
		assert(oakcore_audioparams_supported_sample_rate_at(-1) == 0);
		assert(oakcore_audioparams_supported_sample_rate_at(10) == 0);
	}

	oakcore_audioparams_free(p);

	std::printf("oakcore_audioparams_test: all assertions passed\n");
	return 0;
}
