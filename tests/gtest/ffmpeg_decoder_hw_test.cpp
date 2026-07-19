#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>

#include "codec/decoder.h"
#include "codec/ffmpeg/ffmpegdecoder.h"
#include "node/project/footage/footage.h"
#include "node/output/track/track.h"

using namespace olive;

TEST(FFmpegDecoderHW, H264_422_10bit_CPUFrame_IsNotBlack)
{
	const QString path = qEnvironmentVariable("OAK_TEST_HW_DECODE_FILE");

	if (path.isEmpty()) {
		GTEST_SKIP() << "Set OAK_TEST_HW_DECODE_FILE to a 10-bit 4:2:2 H.264 "
						"sample file to run this test";
	}

	if (!QFileInfo::exists(path)) {
		GTEST_SKIP() << "Test footage not available: " << path.toStdString();
	}

	DecoderPtr decoder = Decoder::create_from_id(QStringLiteral("ffmpeg"));
	ASSERT_TRUE(decoder);

	Footage footage(path);
	ASSERT_TRUE(footage.is_valid());

	Decoder::CodecStream stream(path, footage.get_stream_index(Track::k_video, 0),
								nullptr);
	ASSERT_TRUE(decoder->open(stream));

	Decoder::RetrieveVideoParams params;
	params.time = Rational(0);
	params.maximum_format = PixelFormat::u16;
	FramePtr frame = decoder->retrieve_video_frame(params);
	ASSERT_TRUE(frame);
	ASSERT_TRUE(frame->is_allocated());

	const int width = frame->width();
	const int height = frame->height();
	ASSERT_GT(width, 0);
	ASSERT_GT(height, 0);

	double avg = 0.0;
	int samples = 0;
	const int bpc = VideoParams::get_bytes_per_channel(frame->format());
	const int stride = frame->linesize_bytes();
	for (int y = 0; y < height && y < 1080; y += 120) {
		for (int x = 0; x < width && x < 1920; x += 240) {
			const uint8_t *p = reinterpret_cast<const uint8_t *>(
				frame->const_data() + y * stride + x * 4 * bpc);
			if (bpc == 1) {
				for (int c = 0; c < 3; ++c) {
					avg += p[c] / 255.0;
				}
			} else {
				const uint16_t *p16 = reinterpret_cast<const uint16_t *>(p);
				for (int c = 0; c < 3; ++c) {
					avg += p16[c] / 65535.0;
				}
			}
			samples += 3;
		}
	}

	const double brightness = samples ? avg / samples : 0.0;
	std::cerr << "Frame size: " << width << "x" << height
			  << " format: " << static_cast<int>(frame->format())
			  << " brightness: " << brightness << std::endl;

	EXPECT_GT(brightness, 0.01);
}
