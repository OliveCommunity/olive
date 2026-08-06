/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "codec/decoder.h"
#include "codec/encoder.h"

// Format/codec values mirror oakengine/encoding.h (olive::ExportFormat /
// olive::ExportCodec).
#define TEST_FORMAT_MPEG4 2
#define TEST_CODEC_H264 1

namespace
{

std::string temp_mp4_path()
{
	std::string p = std::string("/tmp/oakcodec_encoder_test.mp4");
	remove(p.c_str());
	return p;
}

} // namespace

TEST(OakCodecEncoder, EncodeMp4RoundTrip)
{
	std::string path = temp_mp4_path();

	int before = oakcodec_debug_alive_count();

	oakcodec_encoding_params params = {};
	snprintf(params.filename, sizeof(params.filename), "%s", path.c_str());
	params.format = TEST_FORMAT_MPEG4;
	params.video_enabled = 1;
	params.video_codec = TEST_CODEC_H264;
	params.video_width = 64;
	params.video_height = 64;
	params.video_time_base_num = 1;
	params.video_time_base_den = 25;
	params.video_pixel_format = OAKCOMMON_PIXEL_FORMAT_U8;
	params.video_interlacing = OAKCODEC_INTERLACE_NONE;
	params.video_pixel_aspect_num = 1;
	params.video_pixel_aspect_den = 1;
	params.video_bit_rate = 200000;
	snprintf(params.video_pix_fmt, sizeof(params.video_pix_fmt), "yuv420p");

	OakEncoder enc = oakcodec_encoder_init(&params);
	ASSERT_NE(enc.ctx, nullptr);

	if (oakcodec_encoder_open(enc) != OAKCODEC_OK) {
		char err[512] = {};
		oakcodec_encoder_last_error(enc, err, sizeof(err));
		oakcodec_encoder_free(&enc);
		GTEST_SKIP() << "encoder not available in this environment: " << err;
	}

	// Write 10 solid frames.
	OakVideoParams vp = oakcommon_videoparams_init_with_time_base(
		64, 64, 1, 25, OAKCOMMON_PIXEL_FORMAT_U8, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	OakFrame frame = oakcodec_frame_init_with_params(vp);
	oakcommon_videoparams_free(&vp);
	ASSERT_NE(frame.ctx, nullptr);
	ASSERT_EQ(oakcodec_frame_allocate(frame), OAKCODEC_OK);

	int linesize = oakcodec_frame_linesize_bytes(frame);
	for (int i = 0; i < 10; i++) {
		memset(oakcodec_frame_data(frame), 16 + i * 10,
			   static_cast<size_t>(linesize) * 64);
		ASSERT_EQ(oakcodec_frame_set_timestamp(frame, i, 25), OAKCODEC_OK);
		ASSERT_EQ(oakcodec_encoder_write_video(enc, frame), OAKCODEC_OK);
	}

	EXPECT_EQ(oakcodec_encoder_flush(enc), OAKCODEC_OK);
	// Writing after flush is a state error.
	EXPECT_EQ(oakcodec_encoder_write_video(enc, frame), OAKCODEC_E_STATE);

	oakcodec_frame_free(&frame);
	oakcodec_encoder_free(&enc);
	EXPECT_EQ(oakcodec_debug_alive_count(), before);

	// Round-trip: the decoder must open the file and decode a frame.
	OakDecoder probe = oakcodec_decoder_probe(path.c_str());
	ASSERT_NE(probe.ctx, nullptr);
	ASSERT_GE(oakcodec_decoder_probe_video_stream_count(probe), 1);
	oakcodec_video_stream_info info = {};
	ASSERT_EQ(oakcodec_decoder_probe_get_video_stream(probe, 0, &info),
			  OAKCODEC_OK);
	EXPECT_EQ(info.width, 64);
	EXPECT_EQ(info.height, 64);
	int stream_index = info.stream_index;
	oakcodec_decoder_free(&probe);

	OakDecoder dec = oakcodec_decoder_init();
	ASSERT_NE(dec.ctx, nullptr);
	ASSERT_EQ(oakcodec_decoder_open(dec, path.c_str(), stream_index),
			  OAKCODEC_OK);
	OakFrame decoded = oakcodec_decoder_decode_video(dec, 0, 1);
	ASSERT_NE(decoded.ctx, nullptr);
	EXPECT_NE(oakcodec_frame_const_data(decoded), nullptr);
	oakcodec_frame_free(&decoded);
	oakcodec_decoder_free(&dec);

	remove(path.c_str());

	EXPECT_EQ(oakcodec_debug_alive_count(), before);
}

TEST(OakCodecEncoder, EmptyHandleSemantics)
{
	OakEncoder empty = {};
	EXPECT_EQ(oakcodec_encoder_open(empty), OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoder_flush(empty), OAKCODEC_E_INVALID);
	oakcodec_encoder_free(nullptr);
	oakcodec_encoder_free(&empty);

	// An all-disabled params struct is invalid -> empty handle.
	oakcodec_encoding_params params = {};
	OakEncoder enc = oakcodec_encoder_init(&params);
	EXPECT_EQ(enc.ctx, nullptr);
}
