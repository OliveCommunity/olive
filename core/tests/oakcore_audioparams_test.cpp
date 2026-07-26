#include <gtest/gtest.h>
#include <cstdint>

#include "olive/core/oakcore/audioparams.h"

static const uint64_t k_layout_mono = 0x4;
static const uint64_t k_layout_stereo = 0x3;
static const uint64_t k_layout_2_1 = 0x103;
static const uint64_t k_layout_5_point1 = 0x60F;
static const uint64_t k_layout_7_point1 = 0x63F;

static const int k_format_invalid = -1;
static const int k_format_u8_p = 0;
static const int k_format_f32_p = 4;
static const int k_format_s16 = 7;

TEST(OakcoreAudioParams, InvalidConstruction)
{
	OakAudioParams *p = oakcore_audioparams_create_invalid();
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakcore_audioparams_is_valid(p), 0);
	EXPECT_EQ(oakcore_audioparams_sample_rate(p), 0);
	EXPECT_EQ(oakcore_audioparams_channel_layout(p), (uint64_t)0);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 0);
	EXPECT_EQ(oakcore_audioparams_format(p), k_format_invalid);
	EXPECT_EQ(oakcore_audioparams_bytes_per_sample_per_channel(p), 0);
	EXPECT_EQ(oakcore_audioparams_bits_per_sample(p), 0);
	EXPECT_EQ(oakcore_audioparams_enabled(p), 1);
	EXPECT_EQ(oakcore_audioparams_stream_index(p), 0);
	EXPECT_EQ(oakcore_audioparams_duration(p), 0);

	OakRational *tb = oakcore_audioparams_time_base(p);
	EXPECT_EQ(oakcore_rational_is_null(tb), 1);
	oakcore_rational_free(tb);
	oakcore_audioparams_free(p);
}

TEST(OakcoreAudioParams, FullConstruction)
{
	OakAudioParams *p = oakcore_audioparams_create(48000, k_layout_stereo, k_format_s16);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakcore_audioparams_is_valid(p), 1);
	EXPECT_EQ(oakcore_audioparams_sample_rate(p), 48000);
	EXPECT_EQ(oakcore_audioparams_channel_layout(p), k_layout_stereo);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 2);
	EXPECT_EQ(oakcore_audioparams_format(p), k_format_s16);
	EXPECT_EQ(oakcore_audioparams_bytes_per_sample_per_channel(p), 2);
	EXPECT_EQ(oakcore_audioparams_bits_per_sample(p), 16);

	OakRational *tb = oakcore_audioparams_time_base(p);
	EXPECT_EQ(oakcore_rational_numerator(tb), 1);
	EXPECT_EQ(oakcore_rational_denominator(tb), 48000);
	oakcore_rational_free(tb);
	oakcore_audioparams_free(p);
}

TEST(OakcoreAudioParams, SettersGetters)
{
	OakAudioParams *p = oakcore_audioparams_create(48000, k_layout_stereo, k_format_s16);

	oakcore_audioparams_set_sample_rate(p, 44100);
	EXPECT_EQ(oakcore_audioparams_sample_rate(p), 44100);
	oakcore_audioparams_set_sample_rate(p, 48000);

	oakcore_audioparams_set_channel_layout(p, k_layout_mono);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 1);
	oakcore_audioparams_set_channel_layout(p, k_layout_5_point1);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 6);
	oakcore_audioparams_set_channel_layout(p, k_layout_stereo);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 2);

	oakcore_audioparams_set_format(p, k_format_f32_p);
	EXPECT_EQ(oakcore_audioparams_format(p), k_format_f32_p);
	EXPECT_EQ(oakcore_audioparams_bytes_per_sample_per_channel(p), 4);
	EXPECT_EQ(oakcore_audioparams_bits_per_sample(p), 32);
	oakcore_audioparams_set_format(p, k_format_s16);

	oakcore_audioparams_set_enabled(p, 0);
	EXPECT_EQ(oakcore_audioparams_enabled(p), 0);
	oakcore_audioparams_set_enabled(p, 1);

	oakcore_audioparams_set_stream_index(p, 3);
	EXPECT_EQ(oakcore_audioparams_stream_index(p), 3);

	const int64_t big_duration = int64_t(1) << 40;
	oakcore_audioparams_set_duration(p, big_duration);
	EXPECT_EQ(oakcore_audioparams_duration(p), big_duration);

	oakcore_audioparams_free(p);
}

TEST(OakcoreAudioParams, TimeSampleByteConversions)
{
	OakAudioParams *p = oakcore_audioparams_create(48000, k_layout_stereo, k_format_s16);

	EXPECT_EQ(oakcore_audioparams_time_to_samples(p, 1.0), 48000);
	EXPECT_EQ(oakcore_audioparams_time_to_samples(p, 0.5), 24000);
	EXPECT_EQ(oakcore_audioparams_time_to_samples(p, -1.0), -48000);
	EXPECT_EQ(oakcore_audioparams_time_to_samples(p, 0.0), 0);

	EXPECT_EQ(oakcore_audioparams_samples_to_bytes_per_channel(p, 100), 200);
	EXPECT_EQ(oakcore_audioparams_samples_to_bytes(p, 100), 400);
	EXPECT_EQ(oakcore_audioparams_time_to_bytes_per_channel(p, 1.0), 96000);
	EXPECT_EQ(oakcore_audioparams_time_to_bytes(p, 1.0), 192000);
	EXPECT_EQ(oakcore_audioparams_bytes_to_samples(p, 400), 100);

	OakRational *half = oakcore_rational_create_nd(1, 2);
	EXPECT_EQ(oakcore_audioparams_time_to_samples_rational(p, half), 24000);
	EXPECT_EQ(oakcore_audioparams_time_to_bytes_rational(p, half), 96000);
	oakcore_rational_free(half);

	OakRational *t = oakcore_audioparams_samples_to_time(p, 48000);
	EXPECT_EQ(oakcore_rational_numerator(t), 1);
	EXPECT_EQ(oakcore_rational_denominator(t), 1);
	oakcore_rational_free(t);

	t = oakcore_audioparams_samples_to_time(p, 24000);
	EXPECT_EQ(oakcore_rational_numerator(t), 1);
	EXPECT_EQ(oakcore_rational_denominator(t), 2);
	oakcore_rational_free(t);

	oakcore_audioparams_free(p);
}

TEST(OakcoreAudioParams, CopyAndEquality)
{
	OakAudioParams *p = oakcore_audioparams_create(48000, k_layout_stereo, k_format_s16);
	OakAudioParams *copy = oakcore_audioparams_copy(p);
	ASSERT_NE(copy, nullptr);
	EXPECT_EQ(oakcore_audioparams_equals(p, copy), 1);

	oakcore_audioparams_set_sample_rate(copy, 96000);
	EXPECT_EQ(oakcore_audioparams_equals(p, copy), 0);
	oakcore_audioparams_set_sample_rate(copy, 48000);
	EXPECT_EQ(oakcore_audioparams_equals(p, copy), 1);

	OakAudioParams *invalid = oakcore_audioparams_create_invalid();
	EXPECT_EQ(oakcore_audioparams_equals(p, invalid), 0);
	EXPECT_EQ(oakcore_audioparams_equals(invalid, invalid), 1);
	oakcore_audioparams_free(invalid);
	oakcore_audioparams_free(copy);
	oakcore_audioparams_free(p);
}

TEST(OakcoreAudioParams, SupportedLayoutsAndRates)
{
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_count(), 5);
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_at(0), k_layout_mono);
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_at(1), k_layout_stereo);
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_at(4), k_layout_7_point1);
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_at(-1), (uint64_t)0);
	EXPECT_EQ(oakcore_audioparams_supported_channel_layout_at(5), (uint64_t)0);

	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_count(), 10);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(0), 8000);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(6), 44100);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(7), 48000);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(9), 96000);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(-1), 0);
	EXPECT_EQ(oakcore_audioparams_supported_sample_rate_at(10), 0);
}
