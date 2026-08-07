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

#include <cstdint>
#include <string>
#include <vector>

#include "node/footage.h"
#include "node/project.h"

namespace
{

/**
 * @brief Two-stage string getter helper: query, then fetch.
 */
std::string get_string(int (*fn)(OakNodeFootage, char *, int),
					   OakNodeFootage footage)
{
	int required = fn(footage, nullptr, 0);
	if (required <= 0) {
		return std::string();
	}
	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(fn(footage, buf.data(), required), required);
	return std::string(buf.data());
}

} // namespace

TEST(NodeFootage, CreateAndFilename)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	// Creating footage requires a project
	EXPECT_EQ(oaknode_footage_create(OakNodeProject{}, nullptr).ctx, nullptr);

	OakNodeFootage footage = oaknode_footage_create(project, nullptr);
	ASSERT_NE(footage.ctx, nullptr);
	EXPECT_EQ(get_string(oaknode_footage_filename, footage), "");

	// A nonexistent path keeps the footage invalid but stores the name
	// (no probe is triggered for missing files).
	EXPECT_EQ(oaknode_footage_set_filename(
				  footage, "/nonexistent/dir/clip.mp4"),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_footage_filename, footage),
			  "/nonexistent/dir/clip.mp4");
	EXPECT_EQ(oaknode_footage_is_valid(footage), 0);

	EXPECT_EQ(oaknode_footage_set_filename(footage, nullptr),
			  OAKNODE_E_INVALID);

	oaknode_project_free(&project);
}

TEST(NodeFootage, TimestampAndMetadataDefaults)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	OakNodeFootage footage = oaknode_footage_create(project, nullptr);
	ASSERT_NE(footage.ctx, nullptr);

	int64_t timestamp = -1;
	EXPECT_EQ(oaknode_footage_timestamp(footage, &timestamp), OAKNODE_OK);
	EXPECT_EQ(timestamp, 0);

	EXPECT_EQ(oaknode_footage_set_timestamp(footage, 123456789), OAKNODE_OK);
	EXPECT_EQ(oaknode_footage_timestamp(footage, &timestamp), OAKNODE_OK);
	EXPECT_EQ(timestamp, 123456789);

	// Unprobed footage has no decoder and no streams
	EXPECT_EQ(get_string(oaknode_footage_decoder, footage), "");
	EXPECT_EQ(oaknode_footage_total_stream_count(footage), 0);
	EXPECT_EQ(oaknode_footage_video_stream_count(footage), 0);
	EXPECT_EQ(oaknode_footage_audio_stream_count(footage), 0);
	EXPECT_EQ(oaknode_footage_subtitle_stream_count(footage), 0);

	int numerator = -1;
	int denominator = -1;
	EXPECT_EQ(oaknode_footage_duration(footage, &numerator, &denominator),
			  OAKNODE_OK);
	EXPECT_EQ(numerator, 0);

	oaknode_project_free(&project);
}

TEST(NodeFootage, Proxy)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	OakNodeFootage footage = oaknode_footage_create(project, nullptr);
	ASSERT_NE(footage.ctx, nullptr);

	EXPECT_EQ(oaknode_footage_proxy_enabled(footage), 0);
	EXPECT_EQ(get_string(oaknode_footage_proxy_path, footage), "");
	// ProxyManager::k_proxy_missing
	EXPECT_EQ(oaknode_footage_proxy_state(footage), 0);

	EXPECT_EQ(oaknode_footage_set_proxy_enabled(footage, 1), OAKNODE_OK);
	EXPECT_EQ(oaknode_footage_proxy_enabled(footage), 1);

	// ProxyManager::k_proxy_ready == 2
	EXPECT_EQ(oaknode_footage_set_proxy(footage, "/tmp/proxy.mov", 2, 0, 1, 1),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_footage_proxy_path, footage),
			  "/tmp/proxy.mov");
	EXPECT_EQ(oaknode_footage_proxy_state(footage), 2);
	EXPECT_EQ(oaknode_footage_proxy_enabled(footage), 1);

	EXPECT_EQ(oaknode_footage_clear_proxy(footage), OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_footage_proxy_path, footage), "");
	EXPECT_EQ(oaknode_footage_proxy_state(footage), 0);

	oaknode_project_free(&project);
}

TEST(NodeFootage, NullHandleErrors)
{
	int64_t timestamp = 0;
	int num = 0;
	int den = 0;

	EXPECT_EQ(oaknode_footage_filename(OakNodeFootage{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_set_filename(OakNodeFootage{}, "/tmp/x"),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_is_valid(OakNodeFootage{}), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_timestamp(OakNodeFootage{}, &timestamp),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_timestamp(OakNodeFootage{}, nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_set_timestamp(OakNodeFootage{}, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_decoder(OakNodeFootage{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_total_stream_count(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_video_stream_count(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_audio_stream_count(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_subtitle_stream_count(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_duration(OakNodeFootage{}, &num, &den),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_proxy_enabled(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_set_proxy_enabled(OakNodeFootage{}, 1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_proxy_path(OakNodeFootage{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_proxy_state(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_set_proxy(OakNodeFootage{}, "/tmp/p", 2, 0, 1,
										1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_footage_clear_proxy(OakNodeFootage{}),
			  OAKNODE_E_INVALID);
}
