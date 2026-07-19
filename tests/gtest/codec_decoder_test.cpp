#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>

#include "codec/decoder.h"

TEST(CodecDecoder, RetrieveVideoFrameFromDemoMp4)
{
	const QString path = QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
							 .filePath(QStringLiteral("tests/demo.mp4"));
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::DecoderPtr decoder =
		olive::Decoder::create_from_id(QStringLiteral("ffmpeg"));
	ASSERT_TRUE(decoder);

	ASSERT_TRUE(decoder->open(olive::Decoder::CodecStream(path, 0, nullptr)));

	olive::Decoder::RetrieveVideoParams params;
	params.time = olive::Rational(0);
	params.maximum_format = olive::core::PixelFormat::u8;

	olive::FramePtr frame = decoder->retrieve_video_frame(params);
	ASSERT_TRUE(frame);
	ASSERT_TRUE(frame->is_allocated());
	EXPECT_EQ(frame->width(), 1920);
	EXPECT_EQ(frame->height(), 1080);
	EXPECT_NE(frame->format(), olive::core::PixelFormat::invalid);
	EXPECT_GT(frame->channel_count(), 0);
	EXPECT_GT(frame->allocated_size(), 0);
	EXPECT_GT(frame->linesize_bytes(), 0);

	bool has_nonzero_byte = false;
	const char *data = frame->const_data();
	for (int i = 0; i < frame->allocated_size(); i++) {
		if (data[i] != 0) {
			has_nonzero_byte = true;
			break;
		}
	}
	EXPECT_TRUE(has_nonzero_byte);
}
