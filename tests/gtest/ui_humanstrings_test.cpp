#include <gtest/gtest.h>

#include "ui/humanstrings.h"

TEST(UIHumanStrings, SampleRateToStringAppendsHz)
{
	EXPECT_EQ(olive::HumanStrings::sample_rate_to_string(44100),
			  QStringLiteral("44100 Hz"));
	EXPECT_EQ(olive::HumanStrings::sample_rate_to_string(48000),
			  QStringLiteral("48000 Hz"));
	EXPECT_EQ(olive::HumanStrings::sample_rate_to_string(0), QStringLiteral("0 Hz"));
}

TEST(UIHumanStrings, KnownChannelLayoutsHaveNames)
{
	EXPECT_EQ(olive::HumanStrings::channel_layout_to_string(
				  olive::k_channel_layout_mono),
			  QStringLiteral("Mono"));
	EXPECT_EQ(olive::HumanStrings::channel_layout_to_string(
				  olive::k_channel_layout_stereo),
			  QStringLiteral("Stereo"));
	EXPECT_EQ(olive::HumanStrings::channel_layout_to_string(
				  olive::k_channel_layout2_1),
			  QStringLiteral("2.1"));
	EXPECT_EQ(olive::HumanStrings::channel_layout_to_string(
				  olive::k_channel_layout5_point1),
			  QStringLiteral("5.1"));
	EXPECT_EQ(olive::HumanStrings::channel_layout_to_string(
				  olive::k_channel_layout7_point1),
			  QStringLiteral("7.1"));
}

TEST(UIHumanStrings, UnknownChannelLayoutFallsBackToHex)
{
	const QString s = olive::HumanStrings::channel_layout_to_string(0x1234);

	EXPECT_TRUE(s.startsWith(QStringLiteral("Unknown (0x")));
	EXPECT_TRUE(s.contains(QStringLiteral("1234")));
}

TEST(UIHumanStrings, AllSampleFormatsHaveNonEmptyNames)
{
	using olive::core::SampleFormat;

	for (SampleFormat fmt :
		 { SampleFormat::u8, SampleFormat::s16, SampleFormat::s32,
		   SampleFormat::s64, SampleFormat::f32, SampleFormat::f64,
		   SampleFormat::u8_p, SampleFormat::s16_p, SampleFormat::s32_p,
		   SampleFormat::s64_p, SampleFormat::f32_p, SampleFormat::f64_p }) {
		const QString s = olive::HumanStrings::format_to_string(fmt);
		EXPECT_FALSE(s.isEmpty());
		EXPECT_FALSE(s.startsWith(QStringLiteral("Unknown")));
	}
}

TEST(UIHumanStrings, PackedAndPlanarFormatsAreDistinguished)
{
	using olive::core::SampleFormat;

	EXPECT_TRUE(olive::HumanStrings::format_to_string(SampleFormat::f32)
					.contains(QStringLiteral("Packed")));
	EXPECT_TRUE(olive::HumanStrings::format_to_string(SampleFormat::f32_p)
					.contains(QStringLiteral("Planar")));
}

TEST(UIHumanStrings, InvalidSampleFormatFallsBackToHex)
{
	const QString s = olive::HumanStrings::format_to_string(
		olive::core::SampleFormat::invalid);

	EXPECT_TRUE(s.startsWith(QStringLiteral("Unknown (0x")));
}
