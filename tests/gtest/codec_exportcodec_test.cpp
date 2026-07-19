#include <gtest/gtest.h>

#include "codec/exportcodec.h"

TEST(CodecExportCodec, NamesAndFlags)
{
	using olive::ExportCodec;

	EXPECT_EQ(ExportCodec::get_codec_name(ExportCodec::k_codec_h264),
			  QStringLiteral("H.264"));
	EXPECT_EQ(ExportCodec::get_codec_name(ExportCodec::k_codec_count),
			  QStringLiteral("Unknown"));

	EXPECT_TRUE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_png));
	EXPECT_FALSE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_h264));

	EXPECT_TRUE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_pcm));
	EXPECT_TRUE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_flac));
	EXPECT_FALSE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_h265));
}

TEST(CodecExportCodec, VideoCodecNamesAreNonEmpty)
{
	using olive::ExportCodec;

	for (int i = 0; i < ExportCodec::k_codec_count; ++i) {
		const auto codec = static_cast<ExportCodec::Codec>(i);
		const QString name = ExportCodec::get_codec_name(codec);
		EXPECT_FALSE(name.isEmpty()) << "Codec " << i;
	}
}

TEST(CodecExportCodec, AudioCodecsAreNotStillImages)
{
	using olive::ExportCodec;

	EXPECT_FALSE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_pcm));
	EXPECT_FALSE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_aac));
	EXPECT_FALSE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_flac));
	EXPECT_FALSE(ExportCodec::is_codec_a_still_image(ExportCodec::k_codec_opus));
}

TEST(CodecExportCodec, LossyCodecsAreNotLossless)
{
	using olive::ExportCodec;

	EXPECT_FALSE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_h264));
	EXPECT_FALSE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_h265));
	EXPECT_FALSE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_v_p9));
	EXPECT_FALSE(ExportCodec::is_codec_lossless(ExportCodec::k_codec_aac));
}
