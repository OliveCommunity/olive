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
#include <string>
#include <vector>

#include "common/subtitleparams.h"

namespace
{

std::string read_indexed_string(
	int (*fn)(OakSubtitleParams, int, char *, int),
	OakSubtitleParams p, int index)
{
	int needed = fn(p, index, nullptr, 0);
	EXPECT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(fn(p, index, buf.data(), needed), needed);
	return std::string(buf.data());
}

std::string read_static_string(int (*fn)(char *, int))
{
	int needed = fn(nullptr, 0);
	EXPECT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(fn(buf.data(), needed), needed);
	return std::string(buf.data());
}

} // namespace

TEST(CommonSubtitleParamsCApi, InitFree)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);

	int index = -1;
	EXPECT_EQ(oakcommon_subtitleparams_get_stream_index(p, &index),
			  OAKCOMMON_OK);
	EXPECT_EQ(index, 0);

	int enabled = 0;
	EXPECT_EQ(oakcommon_subtitleparams_get_enabled(p, &enabled),
			  OAKCOMMON_OK);
	EXPECT_EQ(enabled, 1);

	oakcommon_subtitleparams_free(&p);
}

TEST(CommonSubtitleParamsCApi, FreeNull)
{
	oakcommon_subtitleparams_free(nullptr);
}

TEST(CommonSubtitleParamsCApi, SetStreamIndex)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);
	EXPECT_EQ(oakcommon_subtitleparams_set_stream_index(p, 3), OAKCOMMON_OK);
	int index = 0;
	EXPECT_EQ(oakcommon_subtitleparams_get_stream_index(p, &index),
			  OAKCOMMON_OK);
	EXPECT_EQ(index, 3);
	oakcommon_subtitleparams_free(&p);
}

TEST(CommonSubtitleParamsCApi, SetStreamIndexNullHandle)
{
	EXPECT_EQ(oakcommon_subtitleparams_set_stream_index(OakSubtitleParams{}, 3),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_get_stream_index(OakSubtitleParams{}, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonSubtitleParamsCApi, SetEnabled)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);
	EXPECT_EQ(oakcommon_subtitleparams_set_enabled(p, 0), OAKCOMMON_OK);
	int enabled = 1;
	EXPECT_EQ(oakcommon_subtitleparams_get_enabled(p, &enabled),
			  OAKCOMMON_OK);
	EXPECT_EQ(enabled, 0);
	oakcommon_subtitleparams_free(&p);
}

TEST(CommonSubtitleParamsCApi, SetEnabledNullHandle)
{
	EXPECT_EQ(oakcommon_subtitleparams_set_enabled(OakSubtitleParams{}, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_get_enabled(OakSubtitleParams{}, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonSubtitleParamsCApi, AddAndReadSubtitles)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);

	int is_valid = 1;
	EXPECT_EQ(oakcommon_subtitleparams_is_valid(p, &is_valid), OAKCOMMON_OK);
	EXPECT_EQ(is_valid, 0);

	EXPECT_EQ(oakcommon_subtitleparams_add_subtitle(p, 1, 2, 3, 2, "Hello"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_subtitleparams_add_subtitle(p, 2, 1, 3, 1, "World"),
			  OAKCOMMON_OK);

	EXPECT_EQ(oakcommon_subtitleparams_is_valid(p, &is_valid), OAKCOMMON_OK);
	EXPECT_EQ(is_valid, 1);

	int count = 0;
	EXPECT_EQ(oakcommon_subtitleparams_count(p, &count), OAKCOMMON_OK);
	EXPECT_EQ(count, 2);

	int in_num, in_den, out_num, out_den;
	EXPECT_EQ(oakcommon_subtitleparams_get_subtitle(p, 0, &in_num, &in_den,
													&out_num, &out_den),
			  OAKCOMMON_OK);
	EXPECT_EQ(in_num, 1);
	EXPECT_EQ(in_den, 2);
	EXPECT_EQ(out_num, 3);
	EXPECT_EQ(out_den, 2);
	EXPECT_EQ(read_indexed_string(
				  oakcommon_subtitleparams_get_subtitle_text, p, 0),
			  "Hello");

	int num, den;
	EXPECT_EQ(oakcommon_subtitleparams_duration(p, &num, &den), OAKCOMMON_OK);
	EXPECT_EQ(num, 3);
	EXPECT_EQ(den, 1);

	EXPECT_EQ(oakcommon_subtitleparams_clear(p), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_subtitleparams_count(p, &count), OAKCOMMON_OK);
	EXPECT_EQ(count, 0);

	oakcommon_subtitleparams_free(&p);
}

TEST(CommonSubtitleParamsCApi, SubtitleErrorPaths)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);

	int i = 0;
	char buf[16];
	EXPECT_EQ(oakcommon_subtitleparams_add_subtitle(OakSubtitleParams{}, 0, 1, 1, 1, "x"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_add_subtitle(p, 0, 1, 1, 1, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_get_subtitle(p, 0, &i, &i, &i, &i),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_subtitleparams_get_subtitle(OakSubtitleParams{}, 0, &i, &i, &i,
													&i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_get_subtitle_text(p, 5, buf,
														 sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_subtitleparams_get_subtitle_text(OakSubtitleParams{}, 0, buf,
														 sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_is_valid(OakSubtitleParams{}, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_count(OakSubtitleParams{}, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_duration(OakSubtitleParams{}, &i, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_clear(OakSubtitleParams{}), OAKCOMMON_E_INVALID);

	oakcommon_subtitleparams_free(&p);
}

TEST(CommonSubtitleParamsCApi, GenerateAssHeader)
{
	std::string header =
		read_static_string(oakcommon_subtitleparams_generate_ass_header);
	EXPECT_NE(header.find("[Script Info]"), std::string::npos);
	EXPECT_NE(header.find("[V4+ Styles]"), std::string::npos);
	EXPECT_NE(header.find("[Events]"), std::string::npos);
}

TEST(CommonSubtitleParamsCApi, XmlRoundTrip)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);
	EXPECT_EQ(oakcommon_subtitleparams_set_stream_index(p, 2), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_subtitleparams_add_subtitle(p, 1, 25, 2, 25,
													"First <line>"),
			  OAKCOMMON_OK);

	int needed = oakcommon_subtitleparams_save_xml(p, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(needed);
	ASSERT_EQ(oakcommon_subtitleparams_save_xml(p, buf.data(), needed), needed);

	OakSubtitleParams q = oakcommon_subtitleparams_init();
	ASSERT_NE(q.ctx, nullptr);
	ASSERT_EQ(oakcommon_subtitleparams_load_xml(q, buf.data()), OAKCOMMON_OK);

	int index = 0;
	EXPECT_EQ(oakcommon_subtitleparams_get_stream_index(q, &index),
			  OAKCOMMON_OK);
	EXPECT_EQ(index, 2);
	int count = 0;
	EXPECT_EQ(oakcommon_subtitleparams_count(q, &count), OAKCOMMON_OK);
	EXPECT_EQ(count, 1);
	EXPECT_EQ(read_indexed_string(
				  oakcommon_subtitleparams_get_subtitle_text, q, 0),
			  "First <line>");

	oakcommon_subtitleparams_free(&p);
	oakcommon_subtitleparams_free(&q);
}

TEST(CommonSubtitleParamsCApi, XmlErrorPaths)
{
	OakSubtitleParams p = oakcommon_subtitleparams_init();
	ASSERT_NE(p.ctx, nullptr);
	char buf[16];
	EXPECT_EQ(oakcommon_subtitleparams_load_xml(OakSubtitleParams{}, "<a/>"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_load_xml(p, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_subtitleparams_load_xml(p, "not xml"),
			  OAKCOMMON_E_FAILED);
	EXPECT_EQ(oakcommon_subtitleparams_save_xml(OakSubtitleParams{}, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	oakcommon_subtitleparams_free(&p);
}
