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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/debug.h"

// Native C++ header (exported through the oakcommon target's public
// include dirs) for sink injection.
#include "debug.h"

namespace
{

/**
 * @brief RAII helper: captures all filtered log lines into a vector and
 *        restores the default stderr sink and level on destruction.
 */
class LogCapture {
public:
	LogCapture()
	{
		olive::set_log_sink([this](const std::string &line) {
			lines.push_back(line);
		});
	}

	~LogCapture()
	{
		olive::set_log_sink(nullptr);
		olive::set_log_level(olive::k_debug_info);
	}

	std::vector<std::string> lines;
};

} // namespace

TEST(OakLog, LevelSetGetRoundTrip)
{
	ASSERT_EQ(oakcommon_log_set_level(OAKCOMMON_DEBUG_WARNING),
			  OAKCOMMON_OK);
	int level = -1;
	ASSERT_EQ(oakcommon_log_get_level(&level), OAKCOMMON_OK);
	EXPECT_EQ(level, OAKCOMMON_DEBUG_WARNING);

	ASSERT_EQ(oakcommon_log_set_level(OAKCOMMON_DEBUG_DEBUG),
			  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_log_get_level(&level), OAKCOMMON_OK);
	EXPECT_EQ(level, OAKCOMMON_DEBUG_DEBUG);

	// Restore default for other tests.
	ASSERT_EQ(oakcommon_log_set_level(OAKCOMMON_DEBUG_INFO), OAKCOMMON_OK);
}

TEST(OakLog, LevelSetGetInvalidArgs)
{
	EXPECT_EQ(oakcommon_log_set_level(-1), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_log_set_level(999), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_log_get_level(nullptr), OAKCOMMON_E_INVALID);

	// The failed sets above must not have changed the level.
	int level = -1;
	ASSERT_EQ(oakcommon_log_get_level(&level), OAKCOMMON_OK);
	EXPECT_EQ(level, OAKCOMMON_DEBUG_INFO);
}

TEST(OakLog, LevelFilterDropsLowerLevels)
{
	LogCapture capture;

	ASSERT_EQ(oakcommon_log_set_level(OAKCOMMON_DEBUG_WARNING),
			  OAKCOMMON_OK);

	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_DEBUG, "dbg"), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_INFO, "inf"), OAKCOMMON_OK);
	EXPECT_TRUE(capture.lines.empty());

	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_WARNING, "wrn"), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_ERROR, "err"), OAKCOMMON_OK);
	ASSERT_EQ(capture.lines.size(), 2u);
	EXPECT_EQ(capture.lines[0], "[WARNING] wrn\n");
	EXPECT_EQ(capture.lines[1], "[ERROR] err\n");
}

TEST(OakLog, DefaultLevelIsInfo)
{
	// The default filter is INFO: DEBUG is dropped, INFO passes.
	LogCapture capture;

	olive::log_debug("invisible");
	olive::log_info("visible");
	ASSERT_EQ(capture.lines.size(), 1u);
	EXPECT_EQ(capture.lines[0], "[INFO] visible\n");
}

TEST(OakLog, ConvenienceWrappersUseTheirLevels)
{
	LogCapture capture;
	olive::set_log_level(olive::k_debug_debug);

	olive::log_debug("d");
	olive::log_info("i");
	olive::log_warning("w");
	olive::log_critical("c");
	ASSERT_EQ(capture.lines.size(), 4u);
	EXPECT_EQ(capture.lines[0], "[DEBUG] d\n");
	EXPECT_EQ(capture.lines[1], "[INFO] i\n");
	EXPECT_EQ(capture.lines[2], "[WARNING] w\n");
	EXPECT_EQ(capture.lines[3], "[ERROR] c\n");
}

TEST(OakLog, PrintfFormatting)
{
	LogCapture capture;
	olive::set_log_level(olive::k_debug_debug);

	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_INFO, "w=%d h=%d name=%s",
							1920, 1080, "clip"),
			  OAKCOMMON_OK);
	ASSERT_EQ(capture.lines.size(), 1u);
	EXPECT_EQ(capture.lines[0], "[INFO] w=1920 h=1080 name=clip\n");
}

TEST(OakLog, PrintfLongMessageNotTruncated)
{
	LogCapture capture;

	std::string long_msg(100 * 1024, 'x');
	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_WARNING, "%s",
							long_msg.c_str()),
			  OAKCOMMON_OK);
	ASSERT_EQ(capture.lines.size(), 1u);
	EXPECT_EQ(capture.lines[0],
			  "[WARNING] " + long_msg + "\n");
}

TEST(OakLog, PrintfNullFormat)
{
	EXPECT_EQ(oakcommon_log(OAKCOMMON_DEBUG_WARNING, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(OakLog, OutOfRangeLevelPrintsUnknown)
{
	LogCapture capture;
	olive::set_log_level(olive::k_debug_debug);

	// Out-of-range levels are tolerated and print as UNKNOWN (same as
	// oakcommon_debug_log).
	EXPECT_EQ(oakcommon_log(999, "odd"), OAKCOMMON_OK);
	ASSERT_EQ(capture.lines.size(), 1u);
	EXPECT_EQ(capture.lines[0], "[UNKNOWN] odd\n");
}
