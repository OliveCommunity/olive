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
#include <string>

#include "codec/decoder.h"

#ifndef OAKCODEC_TEST_DATA_DIR
#define OAKCODEC_TEST_DATA_DIR "tests"
#endif

namespace
{

std::string demo_path()
{
	return std::string(OAKCODEC_TEST_DATA_DIR) + "/demo.mp4";
}

bool demo_exists()
{
	FILE *f = fopen(demo_path().c_str(), "rb");
	if (f) {
		fclose(f);
		return true;
	}
	return false;
}

} // namespace

TEST(OakCodecDecoder, ProbeDemoMp4)
{
	if (!demo_exists()) {
		GTEST_SKIP() << "tests/demo.mp4 not available";
	}

	int before = oakcodec_debug_alive_count();

	OakDecoder probe = oakcodec_decoder_probe(demo_path().c_str());
	ASSERT_NE(probe.ctx, nullptr);

	char name[64] = {};
	EXPECT_GT(oakcodec_decoder_probe_decoder_name(probe, name, sizeof(name)),
			  0);
	EXPECT_STRNE(name, "");

	int video_count = oakcodec_decoder_probe_video_stream_count(probe);
	EXPECT_GE(video_count, 1);

	if (video_count >= 1) {
		oakcodec_video_stream_info info = {};
		ASSERT_EQ(oakcodec_decoder_probe_get_video_stream(probe, 0, &info),
				  OAKCODEC_OK);
		EXPECT_GT(info.width, 0);
		EXPECT_GT(info.height, 0);
		EXPECT_GT(info.time_base_den, 0);

		// Out-of-range index
		EXPECT_EQ(oakcodec_decoder_probe_get_video_stream(
					  probe, video_count, &info),
				  OAKCODEC_E_NOT_FOUND);
	}

	// Audio stream enumeration must not crash (count may be 0)
	int audio_count = oakcodec_decoder_probe_audio_stream_count(probe);
	EXPECT_GE(audio_count, 0);
	if (audio_count >= 1) {
		oakcodec_audio_stream_info ainfo = {};
		ASSERT_EQ(oakcodec_decoder_probe_get_audio_stream(probe, 0, &ainfo),
				  OAKCODEC_OK);
		EXPECT_GT(ainfo.sample_rate, 0);
	}

	oakcodec_decoder_free(&probe);
	EXPECT_EQ(probe.ctx, nullptr);
	EXPECT_EQ(oakcodec_debug_alive_count(), before);
}

TEST(OakCodecDecoder, ProbeMissingFile)
{
	OakDecoder probe =
		oakcodec_decoder_probe("/nonexistent/path/to/file.mp4");
	EXPECT_EQ(probe.ctx, nullptr);

	char err[256] = {};
	EXPECT_GT(oakcodec_probe_last_error(err, sizeof(err)), 1);
	EXPECT_STRNE(err, "");

	oakcodec_decoder_free(&probe); // no-op
}

TEST(OakCodecDecoder, OpenAndDecodeFirstFrame)
{
	if (!demo_exists()) {
		GTEST_SKIP() << "tests/demo.mp4 not available";
	}

	// Find the first video stream index via a probe.
	OakDecoder probe = oakcodec_decoder_probe(demo_path().c_str());
	ASSERT_NE(probe.ctx, nullptr);
	ASSERT_GE(oakcodec_decoder_probe_video_stream_count(probe), 1);
	oakcodec_video_stream_info info = {};
	ASSERT_EQ(oakcodec_decoder_probe_get_video_stream(probe, 0, &info),
			  OAKCODEC_OK);
	int stream_index = info.stream_index;
	oakcodec_decoder_free(&probe);

	int before = oakcodec_debug_alive_count();

	OakDecoder d = oakcodec_decoder_init();
	ASSERT_NE(d.ctx, nullptr);
	EXPECT_EQ(oakcodec_decoder_is_open(d), 0);

	ASSERT_EQ(oakcodec_decoder_open(d, demo_path().c_str(), stream_index),
			  OAKCODEC_OK);
	EXPECT_EQ(oakcodec_decoder_is_open(d), 1);

	OakFrame frame = oakcodec_decoder_decode_video(d, 0, 1);
	ASSERT_NE(frame.ctx, nullptr);
	EXPECT_EQ(oakcodec_frame_is_allocated(frame), 1);
	EXPECT_NE(oakcodec_frame_const_data(frame), nullptr);
	EXPECT_GT(oakcodec_frame_width(frame), 0);
	EXPECT_GT(oakcodec_frame_height(frame), 0);

	oakcodec_frame_free(&frame);

	EXPECT_EQ(oakcodec_decoder_close(d), OAKCODEC_OK);
	EXPECT_EQ(oakcodec_decoder_is_open(d), 0);

	oakcodec_decoder_free(&d);
	EXPECT_EQ(oakcodec_debug_alive_count(), before);
}

TEST(OakCodecDecoder, OpenMissingFile)
{
	OakDecoder d = oakcodec_decoder_init();
	ASSERT_NE(d.ctx, nullptr);

	int rc = oakcodec_decoder_open(d, "/nonexistent/video.mp4", 0);
	EXPECT_EQ(rc, OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_decoder_is_open(d), 0);

	char err[256] = {};
	EXPECT_GT(oakcodec_decoder_last_error(d, err, sizeof(err)), 1);
	EXPECT_STRNE(err, "");

	oakcodec_decoder_free(&d);
}

TEST(OakCodecDecoder, EmptyHandleSemantics)
{
	OakDecoder empty = {};
	EXPECT_EQ(oakcodec_decoder_is_open(empty), 0);
	EXPECT_EQ(oakcodec_decoder_probe_video_stream_count(empty), 0);
	OakFrame f = oakcodec_decoder_decode_video(empty, 0, 1);
	EXPECT_EQ(f.ctx, nullptr);
	oakcodec_decoder_free(nullptr);
	oakcodec_decoder_free(&empty);
}
