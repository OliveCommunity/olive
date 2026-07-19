#include <gtest/gtest.h>

#include <vector>

#include <QFileInfo>
#include <QTemporaryDir>

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "codec/ffmpeg/ffmpegencoder.h"

TEST(CodecFFmpegEncoder, PixelFormatsForCodec)
{
	olive::EncodingParams params;
	olive::FFmpegEncoder encoder(params);

	const QStringList png_fmts =
		encoder.get_pixel_formats_for_codec(olive::ExportCodec::k_codec_png);
	EXPECT_FALSE(png_fmts.isEmpty());
	EXPECT_TRUE(png_fmts.contains(QStringLiteral("rgba")));

	EXPECT_TRUE(
		encoder.get_pixel_formats_for_codec(olive::ExportCodec::k_codec_count)
			.isEmpty());
}

TEST(CodecFFmpegEncoder, SampleFormatsForCodec)
{
	olive::EncodingParams params;
	olive::FFmpegEncoder encoder(params);

	// PCM is handled with a custom list whose first element is the default
	const std::vector<olive::core::SampleFormat> pcm =
		encoder.get_sample_formats_for_codec(olive::ExportCodec::k_codec_pcm);
	ASSERT_FALSE(pcm.empty());
	EXPECT_EQ(pcm.front(), olive::core::SampleFormat::s16);

	EXPECT_FALSE(encoder.get_sample_formats_for_codec(olive::ExportCodec::k_codec_aac)
					 .empty());
	EXPECT_TRUE(encoder.get_sample_formats_for_codec(olive::ExportCodec::k_codec_count)
					.empty());
}

TEST(CodecFFmpegEncoder, OpenWithInvalidPixelFormatFails)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	olive::EncodingParams params;
	params.set_filename(dir.filePath(QStringLiteral("invalid.mkv")));
	// A default-constructed VideoParams has PixelFormat::INVALID, for which no
	// bridge pixel format exists
	params.enable_video(olive::VideoParams(), olive::ExportCodec::k_codec_png);

	olive::FFmpegEncoder encoder(params);
	EXPECT_FALSE(encoder.open());
	EXPECT_FALSE(encoder.get_error().isEmpty());
}

TEST(CodecFFmpegEncoder, EncodePngVideoAndProbeBack)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("encoder_test.mkv"));

	const int width = 64;
	const int height = 64;
	const int frame_count = 5;

	olive::EncodingParams params;
	params.set_filename(path);
	olive::VideoParams video_params(width, height, olive::core::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	video_params.set_frame_rate(olive::core::Rational(30, 1));
	params.enable_video(video_params, olive::ExportCodec::k_codec_png);
	params.set_video_pix_fmt(QStringLiteral("rgba"));

	olive::FFmpegEncoder encoder(params);
	ASSERT_TRUE(encoder.open()) << encoder.get_error().toStdString();
	EXPECT_EQ(encoder.get_desired_pixel_format(), olive::core::PixelFormat::u8);

	for (int f = 0; f < frame_count; f++) {
		olive::FramePtr frame = olive::Frame::create();
		frame->set_video_params(video_params);
		ASSERT_TRUE(frame->allocate());
		for (int i = 0; i < width * height; i++) {
			uint8_t *px = reinterpret_cast<uint8_t *>(frame->data()) + i * 4;
			px[0] = uint8_t(f * 40);
			px[1] = 128;
			px[2] = 64;
			px[3] = 255;
		}
		ASSERT_TRUE(encoder.write_frame(frame, olive::core::Rational(f, 30)))
			<< encoder.get_error().toStdString();
	}
	encoder.close();

	// The file must exist and probe back as a 64x64 video
	ASSERT_TRUE(QFileInfo::exists(path));

	FBProbe *probe = fb_probe_create();
	ASSERT_NE(probe, nullptr);
	ASSERT_EQ(fb_probe_open(probe, path.toUtf8().constData()), 0);

	bool found_video = false;
	const int stream_count = fb_probe_get_stream_count(probe);
	for (int i = 0; i < stream_count; i++) {
		FBStreamInfo info;
		if (fb_probe_get_stream_info(probe, i, &info) == 0 &&
			info.codec_type == fb_media_type_video) {
			found_video = true;
			EXPECT_EQ(info.width, width);
			EXPECT_EQ(info.height, height);
		}
	}
	EXPECT_TRUE(found_video);

	fb_probe_close(probe);
	fb_probe_free(&probe);
}
