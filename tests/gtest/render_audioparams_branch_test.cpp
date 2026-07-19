#include <gtest/gtest.h>

#include "olive/core/render/audioparams.h"

TEST(RenderAudioParams, ValidityAndEquality)
{
	olive::core::AudioParams invalid;
	EXPECT_FALSE(invalid.is_valid());

	olive::core::AudioParams params(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::s16);
	EXPECT_TRUE(params.is_valid());

	olive::core::AudioParams other(48000, olive::core::k_channel_layout_stereo,
								   olive::core::SampleFormat::s16);
	EXPECT_TRUE(params == other);

	other.set_sample_rate(44100);
	EXPECT_TRUE(params != other);
}

TEST(RenderAudioParams, TimeAndSampleConversions)
{
	olive::core::AudioParams params(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::s16);

	EXPECT_EQ(params.channel_count(), 2);
	EXPECT_EQ(params.bytes_per_sample_per_channel(), 2);
	EXPECT_EQ(params.bits_per_sample(), 16);

	EXPECT_EQ(params.time_to_samples(1.0), 48000);
	EXPECT_EQ(params.time_to_bytes_per_channel(1.0), 96000);
	EXPECT_EQ(params.time_to_bytes(1.0), 192000);

	EXPECT_EQ(params.samples_to_bytes(48000), 192000);
	EXPECT_EQ(params.samples_to_bytes_per_channel(48000), 96000);

	EXPECT_EQ(params.bytes_to_samples(192000), 48000);
	EXPECT_EQ(params.bytes_to_time(192000), olive::core::Rational(1, 1));
	EXPECT_EQ(params.bytes_per_channel_to_time(96000),
			  olive::core::Rational(1, 1));
}

TEST(RenderAudioParams, ChannelLayoutCount)
{
	olive::core::AudioParams mono(48000, olive::core::k_channel_layout_mono,
								  olive::core::SampleFormat::f32);
	EXPECT_EQ(mono.channel_count(), 1);

	olive::core::AudioParams surround(48000, olive::core::k_channel_layout5_point1,
									  olive::core::SampleFormat::f32);
	EXPECT_EQ(surround.channel_count(), 6);
}

TEST(RenderAudioParams, SampleFormatSizes)
{
	olive::core::AudioParams u8(48000, olive::core::k_channel_layout_mono,
								olive::core::SampleFormat::u8);
	EXPECT_EQ(u8.bytes_per_sample_per_channel(), 1);

	olive::core::AudioParams f32(48000, olive::core::k_channel_layout_mono,
								 olive::core::SampleFormat::f32);
	EXPECT_EQ(f32.bytes_per_sample_per_channel(), 4);

	olive::core::AudioParams f64(48000, olive::core::k_channel_layout_mono,
								 olive::core::SampleFormat::f64);
	EXPECT_EQ(f64.bytes_per_sample_per_channel(), 8);
}

TEST(RenderAudioParams, CopyAndAssignment)
{
	olive::core::AudioParams params(96000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::f32);

	olive::core::AudioParams copy(params);
	EXPECT_EQ(copy.sample_rate(), 96000);
	EXPECT_EQ(copy.channel_count(), 2);
	EXPECT_EQ(copy.format(), olive::core::SampleFormat::f32);

	olive::core::AudioParams assigned;
	assigned = params;
	EXPECT_EQ(assigned.sample_rate(), 96000);
	EXPECT_TRUE(assigned == params);
}

TEST(RenderAudioParams, SettersModifyState)
{
	olive::core::AudioParams params(44100, olive::core::k_channel_layout_mono,
									olive::core::SampleFormat::s16);
	EXPECT_TRUE(params.is_valid());

	params.set_sample_rate(48000);
	params.set_format(olive::core::SampleFormat::f32);

	EXPECT_EQ(params.sample_rate(), 48000);
	EXPECT_EQ(params.format(), olive::core::SampleFormat::f32);
}
