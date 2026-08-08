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

#include <cstring>

#include "codec/format.h"

// Format/codec values mirror oakengine/encoding.h (olive::ExportFormat /
// olive::ExportCodec). The named constants live in codec/format.h; the
// still-image and lossless checks use raw values PNG=5 / H264=1 / PCM=13 /
// AAC=12.
#define TEST_FORMAT_MATROSKA OAKCODEC_ENCODING_FORMAT_MATROSKA
#define TEST_FORMAT_MPEG4 OAKCODEC_ENCODING_FORMAT_MPEG4_VIDEO
#define TEST_FORMAT_WAV OAKCODEC_ENCODING_FORMAT_WAV
#define TEST_FORMAT_SRT OAKCODEC_ENCODING_FORMAT_SRT
#define TEST_CODEC_H264 OAKCODEC_ENCODING_CODEC_H264
#define TEST_CODEC_PCM OAKCODEC_ENCODING_CODEC_PCM
#define TEST_CODEC_SRT OAKCODEC_ENCODING_CODEC_SRT

TEST(OakCodecFormat, FormatMetadata)
{
	char buf[256];

	// The enumeration count matches the 15-entry ExportFormat table
	// (0..=14 real formats, k_format_count = 15).
	const int count = oakcodec_encoding_format_count();
	EXPECT_EQ(count, 15);

	// Matroska
	EXPECT_EQ(oakcodec_encoding_format_name(TEST_FORMAT_MATROSKA, buf,
											sizeof(buf)),
			  15); // "Matroska Video" (14) + NUL
	EXPECT_STREQ(buf, "Matroska Video");
	EXPECT_EQ(oakcodec_encoding_format_extension(TEST_FORMAT_MATROSKA, buf,
												 sizeof(buf)),
			  4); // "mkv" + NUL
	EXPECT_STREQ(buf, "mkv");

	// Every valid format has a name and an extension.
	for (int i = 0; i < count; i++) {
		EXPECT_GT(oakcodec_encoding_format_name(i, buf, sizeof(buf)), 0);
		EXPECT_GT(oakcodec_encoding_format_extension(i, buf, sizeof(buf)), 0);
	}

	// Invalid formats -> OAKCODEC_E_INVALID (module error family, not -1).
	EXPECT_EQ(oakcodec_encoding_format_name(-1, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_format_name(9999, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_format_extension(count, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID); // k_format_count is not a real format
}

TEST(OakCodecFormat, FormatCodecLists)
{
	char buf[256];

	// MP4 carries H.264/H.264RGB/H.265 video.
	const int vcount = oakcodec_encoding_format_video_codec_count(TEST_FORMAT_MPEG4);
	EXPECT_EQ(vcount, 3);
	int found_h264 = 0;
	for (int i = 0; i < vcount; i++) {
		if (oakcodec_encoding_format_video_codec_at(TEST_FORMAT_MPEG4, i) ==
			TEST_CODEC_H264) {
			found_h264 = 1;
		}
	}
	EXPECT_EQ(found_h264, 1);

	// WAV is audio-only and carries PCM.
	EXPECT_EQ(oakcodec_encoding_format_video_codec_count(TEST_FORMAT_WAV), 0);
	const int acount = oakcodec_encoding_format_audio_codec_count(TEST_FORMAT_WAV);
	EXPECT_EQ(acount, 1);
	EXPECT_EQ(oakcodec_encoding_format_audio_codec_at(TEST_FORMAT_WAV, 0),
			  TEST_CODEC_PCM);

	// SRT is subtitle-only and carries the SRT codec.
	EXPECT_EQ(oakcodec_encoding_format_audio_codec_count(TEST_FORMAT_SRT), 0);
	EXPECT_EQ(oakcodec_encoding_format_subtitle_codec_count(TEST_FORMAT_SRT), 1);
	EXPECT_EQ(oakcodec_encoding_format_subtitle_codec_at(TEST_FORMAT_SRT, 0),
			  TEST_CODEC_SRT);
	EXPECT_EQ(oakcodec_encoding_format_subtitle_codec_count(TEST_FORMAT_MATROSKA),
			  1);

	// Out-of-range indices -> OAKCODEC_E_NOT_FOUND.
	EXPECT_EQ(oakcodec_encoding_format_video_codec_at(TEST_FORMAT_MPEG4, vcount),
			  OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_encoding_format_video_codec_at(TEST_FORMAT_MPEG4, -1),
			  OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_encoding_format_audio_codec_at(TEST_FORMAT_WAV, acount),
			  OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_encoding_format_subtitle_codec_at(TEST_FORMAT_SRT, -1),
			  OAKCODEC_E_NOT_FOUND);

	// Invalid formats -> OAKCODEC_E_INVALID.
	EXPECT_EQ(oakcodec_encoding_format_video_codec_count(-1), OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_format_audio_codec_at(9999, 0),
			  OAKCODEC_E_INVALID);

	// Enumeration consistency: every codec listed for a format is a valid
	// codec value (codec_name accepts it).
	EXPECT_GT(oakcodec_encoding_codec_name(TEST_CODEC_H264, buf, sizeof(buf)), 0);
}

TEST(OakCodecFormat, CodecMetadata)
{
	char buf[256];

	EXPECT_EQ(oakcodec_encoding_codec_name(TEST_CODEC_H264, buf, sizeof(buf)),
			  6); // "H.264" (5) + NUL
	EXPECT_STREQ(buf, "H.264");

	// Every codec in [0, k_codec_count) has a name.
	const int count = 19; // olive::ExportCodec::k_codec_count
	for (int i = 0; i < count; i++) {
		EXPECT_GT(oakcodec_encoding_codec_name(i, buf, sizeof(buf)), 0);
	}

	// Invalid codec -> OAKCODEC_E_INVALID.
	EXPECT_EQ(oakcodec_encoding_codec_name(-1, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_codec_name(count, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID);

	// Still-image codecs: PNG (5) yes, H.264 (1) no; invalid -> 0.
	EXPECT_EQ(oakcodec_encoding_codec_is_still_image(5), 1);
	EXPECT_EQ(oakcodec_encoding_codec_is_still_image(TEST_CODEC_H264), 0);
	EXPECT_EQ(oakcodec_encoding_codec_is_still_image(9999), 0);

	// Lossless codecs: PCM (13) yes, AAC (12) no; invalid -> 0.
	EXPECT_EQ(oakcodec_encoding_codec_is_lossless(TEST_CODEC_PCM), 1);
	EXPECT_EQ(oakcodec_encoding_codec_is_lossless(OAKCODEC_ENCODING_CODEC_AAC),
			  0);
	EXPECT_EQ(oakcodec_encoding_codec_is_lossless(-1), 0);
}

TEST(OakCodecFormat, PixelAndSampleFormats)
{
	char buf[256];

	// Encoded pixel formats of H.264 in MP4: yuv420p is the preferred one
	// (bridge-backed query; works without any init in this test binary).
	const int pcount =
		oakcodec_encoding_pix_fmt_count(TEST_FORMAT_MPEG4, TEST_CODEC_H264);
	EXPECT_GT(pcount, 0);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_at(TEST_FORMAT_MPEG4, TEST_CODEC_H264, 0,
										   buf, sizeof(buf)),
			  8); // "yuv420p" (7) + NUL
	EXPECT_STREQ(buf, "yuv420p");
	EXPECT_EQ(oakcodec_encoding_pix_fmt_at(TEST_FORMAT_MPEG4, TEST_CODEC_H264,
										   pcount, buf, sizeof(buf)),
			  OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_at(TEST_FORMAT_MPEG4, TEST_CODEC_H264,
										   -1, buf, sizeof(buf)),
			  OAKCODEC_E_NOT_FOUND);

	// pix_fmt_index: found -> index, absent/NULL/empty/invalid -> 0.
	EXPECT_EQ(oakcodec_encoding_pix_fmt_index(TEST_CODEC_H264, "yuv420p"), 0);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_index(TEST_CODEC_H264, "no-such-format"),
			  0);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_index(TEST_CODEC_H264, NULL), 0);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_index(TEST_CODEC_H264, ""), 0);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_index(9999, "yuv420p"), 0);

	// Bad arguments -> OAKCODEC_E_INVALID.
	EXPECT_EQ(oakcodec_encoding_pix_fmt_count(-1, TEST_CODEC_H264),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_count(TEST_FORMAT_MPEG4, -1),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_pix_fmt_at(-1, TEST_CODEC_H264, 0, buf,
										   sizeof(buf)),
			  OAKCODEC_E_INVALID);

	// Sample formats of PCM in WAV: the six native PCM layouts, the first
	// being signed-16 (the export dialog's default).
	const int scount =
		oakcodec_encoding_sample_format_count(TEST_FORMAT_WAV, TEST_CODEC_PCM);
	EXPECT_EQ(scount, 6);
	for (int i = 0; i < scount; i++) {
		EXPECT_GE(oakcodec_encoding_sample_format_at(TEST_FORMAT_WAV,
													 TEST_CODEC_PCM, i),
				  0);
	}
	EXPECT_EQ(oakcodec_encoding_sample_format_at(TEST_FORMAT_WAV, TEST_CODEC_PCM,
												 scount),
			  OAKCODEC_E_NOT_FOUND);
	EXPECT_EQ(oakcodec_encoding_sample_format_count(-1, TEST_CODEC_PCM),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_sample_format_at(TEST_FORMAT_WAV, -1, 0),
			  OAKCODEC_E_INVALID);
}

TEST(OakCodecFormat, FilenameHelpers)
{
	char buf[4096];

	EXPECT_EQ(oakcodec_encoding_filename_contains_digit_placeholder(
				  "/tmp/out_[#####].png"),
			  1);
	EXPECT_EQ(oakcodec_encoding_filename_contains_digit_placeholder(
				  "/tmp/out.png"),
			  0);
	EXPECT_EQ(oakcodec_encoding_filename_contains_digit_placeholder(NULL), 0);

	EXPECT_EQ(oakcodec_encoding_image_sequence_digit_count(
				  "/tmp/out_[#####].png"),
			  5);
	EXPECT_EQ(oakcodec_encoding_image_sequence_digit_count("/tmp/out.png"), 0);
	EXPECT_EQ(oakcodec_encoding_image_sequence_digit_count(NULL), 0);

	// The separator immediately before the placeholder is removed along
	// with it (C++ regex `[\-\.\ \_]?\[[#]+\]`).
	EXPECT_EQ(oakcodec_encoding_filename_remove_digit_placeholder(
				  "/tmp/out_[#####].png", buf, sizeof(buf)),
			  13); // "/tmp/out.png" (12) + NUL
	EXPECT_STREQ(buf, "/tmp/out.png");
	EXPECT_EQ(oakcodec_encoding_filename_remove_digit_placeholder(
				  "/tmp/out[#####].png", buf, sizeof(buf)),
			  13); // "/tmp/out.png" (12) + NUL
	EXPECT_STREQ(buf, "/tmp/out.png");

	// NULL filename -> OAKCODEC_E_INVALID; no placeholder -> unchanged.
	EXPECT_EQ(oakcodec_encoding_filename_remove_digit_placeholder(NULL, buf,
																  sizeof(buf)),
			  OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_encoding_filename_remove_digit_placeholder("/tmp/out.png",
																  buf,
																  sizeof(buf)),
			  13);
	EXPECT_STREQ(buf, "/tmp/out.png");

	// Two-stage truncation: small buffer writes buf_size-1 chars + NUL and
	// keeps reporting the required size.
	EXPECT_EQ(oakcodec_encoding_filename_remove_digit_placeholder(
				  "/tmp/out_[#####].png", buf, 5),
			  13);
	EXPECT_STREQ(buf, "/tmp");
}
