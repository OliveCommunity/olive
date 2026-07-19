#include <gtest/gtest.h>

#include "codec/encoder.h"

namespace
{
class TestEncoder final : public olive::Encoder {
public:
	explicit TestEncoder(const olive::EncodingParams &params)
		: olive::Encoder(params)
	{
	}

	bool open() override
	{
		return true;
	}

	bool write_frame(olive::FramePtr, olive::core::Rational) override
	{
		return true;
	}

	bool write_audio(const olive::SampleBuffer &) override
	{
		return true;
	}

	bool write_subtitle(const olive::SubtitleBlock *) override
	{
		return true;
	}

	void close() override
	{
	}
};
}

TEST(CodecEncoder, ImageSequenceFilenames)
{
	olive::EncodingParams params;
	params.set_filename(QStringLiteral("frame_[####].png"));
	params.set_video_is_image_sequence(true);

	olive::VideoParams video_params;
	video_params.set_frame_rate(olive::core::Rational(24, 1));
	params.enable_video(video_params, olive::ExportCodec::k_codec_png);

	TestEncoder encoder(params);

	EXPECT_TRUE(olive::Encoder::filename_contains_digit_placeholder(
		QStringLiteral("frame_[####].png")));
	EXPECT_EQ(olive::Encoder::get_image_sequence_placeholder_digit_count(
				  QStringLiteral("frame_[####].png")),
			  4);
	EXPECT_EQ(olive::Encoder::filename_remove_digit_placeholder(
				  QStringLiteral("frame_[####].png")),
			  QStringLiteral("frame.png"));

	const QString filename =
		encoder.get_filename_for_frame(olive::core::Rational(1, 24));
	EXPECT_EQ(filename, QStringLiteral("frame_0001.png"));
}

TEST(CodecEncoder, MatrixGeneration)
{
	using Method = olive::EncodingParams::VideoScalingMethod;

	QMatrix4x4 stretch = olive::EncodingParams::generate_matrix(
		Method::k_stretch, 1920, 1080, 1280, 720);
	EXPECT_TRUE(qFuzzyCompare(stretch(0, 0), 1.0f));
	EXPECT_TRUE(qFuzzyCompare(stretch(1, 1), 1.0f));

	QMatrix4x4 fit = olive::EncodingParams::generate_matrix(Method::k_fit, 1920,
														   1080, 1024, 1024);
	EXPECT_TRUE(qFuzzyCompare(fit(0, 0), 1.0f));
	EXPECT_FALSE(qFuzzyCompare(fit(1, 1), 1.0f));

	QMatrix4x4 crop = olive::EncodingParams::generate_matrix(Method::k_crop, 1920,
															1080, 1024, 1024);
	EXPECT_FALSE(qFuzzyCompare(crop(0, 0), 1.0f));
	EXPECT_TRUE(qFuzzyCompare(crop(1, 1), 1.0f));
}

TEST(CodecEncoder, TypeFromFormat)
{
	using olive::Encoder;
	using olive::ExportFormat;

	EXPECT_EQ(Encoder::get_type_from_format(ExportFormat::k_format_png),
			  Encoder::k_encoder_type_oiio);
	EXPECT_EQ(Encoder::get_type_from_format(ExportFormat::k_format_d_nx_hd),
			  Encoder::k_encoder_type_f_fmpeg);
	EXPECT_EQ(Encoder::get_type_from_format(ExportFormat::k_format_count),
			  Encoder::k_encoder_type_none);
}
