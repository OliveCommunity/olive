#include <gtest/gtest.h>

#include "codec/exportformat.h"

TEST(CodecExportFormat, NamesAndExtensions)
{
	using olive::ExportFormat;

	EXPECT_EQ(ExportFormat::get_name(ExportFormat::k_format_d_nx_hd),
			  QStringLiteral("DNxHD"));
	EXPECT_EQ(ExportFormat::get_extension(ExportFormat::k_format_d_nx_hd),
			  QStringLiteral("mxf"));
	EXPECT_EQ(ExportFormat::get_name(ExportFormat::k_format_count),
			  QStringLiteral("Unknown"));
	EXPECT_TRUE(
		ExportFormat::get_extension(ExportFormat::k_format_count).isEmpty());
}

TEST(CodecExportFormat, AllFormatsHaveNames)
{
	using olive::ExportFormat;

	for (int i = 0; i < ExportFormat::k_format_count; ++i) {
		const auto fmt = static_cast<ExportFormat::Format>(i);
		EXPECT_FALSE(ExportFormat::get_name(fmt).isEmpty()) << "Format " << i;
	}
}

TEST(CodecExportFormat, AllFormatsHaveAtLeastOneCodecList)
{
	using olive::ExportFormat;

	for (int i = 0; i < ExportFormat::k_format_count; ++i) {
		const auto fmt = static_cast<ExportFormat::Format>(i);
		EXPECT_FALSE(ExportFormat::get_video_codecs(fmt).isEmpty() &&
					 ExportFormat::get_audio_codecs(fmt).isEmpty() &&
					 ExportFormat::get_subtitle_codecs(fmt).isEmpty())
			<< "Format " << i;
	}
}

TEST(CodecExportFormat, CodecLists)
{
	using olive::ExportCodec;
	using olive::ExportFormat;

	const QList<ExportCodec::Codec> matroska_video =
		ExportFormat::get_video_codecs(ExportFormat::k_format_matroska);
	EXPECT_TRUE(matroska_video.contains(ExportCodec::k_codec_h264));
	EXPECT_TRUE(matroska_video.contains(ExportCodec::k_codec_v_p9));

	const QList<ExportCodec::Codec> ogg_audio =
		ExportFormat::get_audio_codecs(ExportFormat::k_format_ogg);
	EXPECT_TRUE(ogg_audio.contains(ExportCodec::k_codec_opus));
	EXPECT_TRUE(ogg_audio.contains(ExportCodec::k_codec_vorbis));

	const QList<ExportCodec::Codec> png_video =
		ExportFormat::get_video_codecs(ExportFormat::k_format_png);
	EXPECT_EQ(png_video, QList<ExportCodec::Codec>{ ExportCodec::k_codec_png });

	const QList<ExportCodec::Codec> srt_subs =
		ExportFormat::get_subtitle_codecs(ExportFormat::k_format_matroska);
	EXPECT_EQ(srt_subs, QList<ExportCodec::Codec>{ ExportCodec::k_codec_srt });

	const QList<ExportCodec::Codec> wav_audio =
		ExportFormat::get_audio_codecs(ExportFormat::k_format_wav);
	EXPECT_EQ(wav_audio, QList<ExportCodec::Codec>{ ExportCodec::k_codec_pcm });
}

TEST(CodecExportFormat, MPEG4ContainsH264AndAAC)
{
	using olive::ExportCodec;
	using olive::ExportFormat;

	EXPECT_TRUE(ExportFormat::get_video_codecs(ExportFormat::k_format_mpe_g4_video)
					.contains(ExportCodec::k_codec_h264));
	EXPECT_TRUE(ExportFormat::get_audio_codecs(ExportFormat::k_format_mpe_g4_audio)
					.contains(ExportCodec::k_codec_aac));
}
