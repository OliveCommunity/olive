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

#include "codec/frame.h"

namespace
{

OakVideoParams make_params(int width, int height)
{
	return oakcommon_videoparams_init_with_time_base(
		width, height, 1001, 30000, OAKCOMMON_PIXEL_FORMAT_U8, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
}

} // namespace

TEST(OakCodecFrame, InitAndFree)
{
	int before = oakcodec_debug_alive_count();

	OakFrame f = oakcodec_frame_init();
	ASSERT_NE(f.ctx, nullptr);
	EXPECT_EQ(f.abi_version, OAKCODEC_ABI_VERSION);
	EXPECT_EQ(oakcodec_debug_alive_count(), before + 1);

	oakcodec_frame_free(&f);
	EXPECT_EQ(f.ctx, nullptr);
	EXPECT_EQ(oakcodec_debug_alive_count(), before);

	// NULL / empty no-ops
	oakcodec_frame_free(nullptr);
	oakcodec_frame_free(&f);
}

TEST(OakCodecFrame, ParamsRoundTrip)
{
	OakFrame f = oakcodec_frame_init();
	ASSERT_NE(f.ctx, nullptr);

	OakVideoParams p = make_params(320, 240);
	ASSERT_EQ(oakcodec_frame_set_params(f, p), OAKCODEC_OK);
	oakcommon_videoparams_free(&p);

	OakVideoParams out = {};
	ASSERT_EQ(oakcodec_frame_get_params(f, &out), OAKCODEC_OK);

	int w = 0, h = 0, fmt = -2, ch = 0, tb_num = 0, tb_den = 0;
	oakcommon_videoparams_get_width(out, &w);
	oakcommon_videoparams_get_height(out, &h);
	oakcommon_videoparams_get_format(out, &fmt);
	oakcommon_videoparams_get_channel_count(out, &ch);
	oakcommon_videoparams_get_time_base(out, &tb_num, &tb_den);
	oakcommon_videoparams_free(&out);

	EXPECT_EQ(w, 320);
	EXPECT_EQ(h, 240);
	EXPECT_EQ(fmt, OAKCOMMON_PIXEL_FORMAT_U8);
	EXPECT_EQ(ch, 4);
	EXPECT_EQ(tb_num, 1001);
	EXPECT_EQ(tb_den, 30000);

	oakcodec_frame_free(&f);
}

TEST(OakCodecFrame, AllocateDataLinesize)
{
	OakVideoParams p = make_params(64, 48);
	OakFrame f = oakcodec_frame_init_with_params(p);
	oakcommon_videoparams_free(&p);
	ASSERT_NE(f.ctx, nullptr);

	EXPECT_EQ(oakcodec_frame_is_allocated(f), 0);
	EXPECT_EQ(oakcodec_frame_data(f), nullptr);

	ASSERT_EQ(oakcodec_frame_allocate(f), OAKCODEC_OK);
	EXPECT_EQ(oakcodec_frame_is_allocated(f), 1);
	ASSERT_NE(oakcodec_frame_data(f), nullptr);
	EXPECT_EQ(oakcodec_frame_const_data(f), oakcodec_frame_data(f));

	// u8 rgba = 4 bytes/px, width 64 aligned to 32 -> 64 * 4
	EXPECT_EQ(oakcodec_frame_linesize_bytes(f), 64 * 4);
	EXPECT_EQ(oakcodec_frame_linesize_pixels(f), 64);
	EXPECT_EQ(oakcodec_frame_allocated_size(f), 64 * 4 * 48);
	EXPECT_EQ(oakcodec_frame_width(f), 64);
	EXPECT_EQ(oakcodec_frame_height(f), 48);
	EXPECT_EQ(oakcodec_frame_format(f), OAKCOMMON_PIXEL_FORMAT_U8);
	EXPECT_EQ(oakcodec_frame_channel_count(f), 4);

	// Allocating again is a successful no-op
	EXPECT_EQ(oakcodec_frame_allocate(f), OAKCODEC_OK);

	oakcodec_frame_free(&f);
}

TEST(OakCodecFrame, RefCounting)
{
	int before = oakcodec_debug_alive_count();

	OakFrame f = oakcodec_frame_init();
	ASSERT_NE(f.ctx, nullptr);

	// Copy the struct and addref: two references, one object.
	OakFrame copy = f;
	copy.addref(copy.ctx);

	// Release the copy; object stays alive.
	copy.release(copy.ctx);
	EXPECT_EQ(oakcodec_debug_alive_count(), before + 1);
	EXPECT_EQ(oakcodec_frame_width(f), 0); // still valid, default params

	oakcodec_frame_free(&f);
	EXPECT_EQ(oakcodec_debug_alive_count(), before);
}

TEST(OakCodecFrame, Timestamp)
{
	OakFrame f = oakcodec_frame_init();
	ASSERT_NE(f.ctx, nullptr);

	ASSERT_EQ(oakcodec_frame_set_timestamp(f, 1001, 30000), OAKCODEC_OK);
	int num = 0, den = 0;
	ASSERT_EQ(oakcodec_frame_get_timestamp(f, &num, &den), OAKCODEC_OK);
	EXPECT_EQ(num, 1001);
	EXPECT_EQ(den, 30000);

	oakcodec_frame_free(&f);
}

TEST(OakCodecFrame, EmptyHandleSemantics)
{
	OakFrame empty = {};
	EXPECT_EQ(oakcodec_frame_get_params(empty, nullptr), OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_frame_allocate(empty), OAKCODEC_E_INVALID);
	EXPECT_EQ(oakcodec_frame_is_allocated(empty), 0);
	EXPECT_EQ(oakcodec_frame_data(empty), nullptr);
	EXPECT_EQ(oakcodec_frame_width(empty), 0);
}
