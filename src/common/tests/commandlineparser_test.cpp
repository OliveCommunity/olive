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

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "common/commandlineparser.h"

TEST(CommandLineParser, InitFree)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, FreeNull)
{
	oakcommon_commandlineparser_free(nullptr);
	oakcommon_commandlineoption_free(nullptr);
	oakcommon_commandlinepositionalargument_free(nullptr);
}

TEST(CommandLineParser, AddOptionInvalidArgs)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	const char *names[] = { "h", "-help" };
	OakCommonCommandLineOption *option = nullptr;

	EXPECT_EQ(oakcommon_commandlineparser_add_option(
				  nullptr, names, 2, "desc", 0, nullptr, 0, &option),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_commandlineparser_add_option(
				  parser, nullptr, 2, "desc", 0, nullptr, 0, &option),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 0, "desc", 0, nullptr, 0, &option),
			  OAKCOMMON_E_INVALID);

	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, ProcessOptionHitAndMiss)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	const char *names[] = { "h", "-help" };
	OakCommonCommandLineOption *hit = nullptr;
	OakCommonCommandLineOption *miss = nullptr;

	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 2, "Show help", 0, nullptr, 0, &hit),
			  OAKCOMMON_OK);
	ASSERT_NE(hit, nullptr);

	const char *other_names[] = { "v", "-version" };
	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, other_names, 2, "Show version", 0, nullptr, 0, &miss),
			  OAKCOMMON_OK);
	ASSERT_NE(miss, nullptr);

	const char *argv[] = { "oak", "-h" };
	ASSERT_EQ(oakcommon_commandlineparser_process(parser, argv, 2),
			  OAKCOMMON_OK);

	bool is_set = true;
	ASSERT_EQ(oakcommon_commandlineoption_is_set(hit, &is_set), OAKCOMMON_OK);
	EXPECT_TRUE(is_set);

	is_set = true;
	ASSERT_EQ(oakcommon_commandlineoption_is_set(miss, &is_set), OAKCOMMON_OK);
	EXPECT_FALSE(is_set);

	oakcommon_commandlineoption_free(hit);
	oakcommon_commandlineoption_free(miss);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, ProcessMatchesSecondAliasCaseInsensitive)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	const char *names[] = { "h", "-help" };
	OakCommonCommandLineOption *option = nullptr;
	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 2, "Show help", 0, nullptr, 0, &option),
			  OAKCOMMON_OK);

	// Only the first dash is stripped; matching is case-insensitive
	const char *argv[] = { "oak", "--HELP" };
	ASSERT_EQ(oakcommon_commandlineparser_process(parser, argv, 2),
			  OAKCOMMON_OK);

	bool is_set = false;
	ASSERT_EQ(oakcommon_commandlineoption_is_set(option, &is_set),
			  OAKCOMMON_OK);
	EXPECT_TRUE(is_set);

	oakcommon_commandlineoption_free(option);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, OptionTakesArg)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	const char *names[] = { "e", "-export" };
	OakCommonCommandLineOption *option = nullptr;
	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 2, "Export", 1, "filename", 0, &option),
			  OAKCOMMON_OK);

	const char *argv[] = { "oak", "-e", "/tmp/out.mp4" };
	ASSERT_EQ(oakcommon_commandlineparser_process(parser, argv, 3),
			  OAKCOMMON_OK);

	bool is_set = false;
	ASSERT_EQ(oakcommon_commandlineoption_is_set(option, &is_set),
			  OAKCOMMON_OK);
	EXPECT_TRUE(is_set);

	int required =
		oakcommon_commandlineoption_get_setting(option, nullptr, 0);
	ASSERT_GT(required, 0);
	EXPECT_EQ(required, static_cast<int>(strlen("/tmp/out.mp4")) + 1);

	std::vector<char> buf(static_cast<size_t>(required));
	ASSERT_EQ(oakcommon_commandlineoption_get_setting(option, buf.data(),
													  required),
			  required);
	EXPECT_STREQ(buf.data(), "/tmp/out.mp4");

	oakcommon_commandlineoption_free(option);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, PositionalArgument)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	OakCommonCommandLinePositionalArgument *arg = nullptr;
	ASSERT_EQ(oakcommon_commandlineparser_add_positional_argument(
				  parser, "project", "Project file", 0, &arg),
			  OAKCOMMON_OK);
	ASSERT_NE(arg, nullptr);

	const char *argv[] = { "oak", "/tmp/project.ove" };
	ASSERT_EQ(oakcommon_commandlineparser_process(parser, argv, 2),
			  OAKCOMMON_OK);

	int required =
		oakcommon_commandlinepositionalargument_get_setting(arg, nullptr, 0);
	ASSERT_EQ(required, static_cast<int>(strlen("/tmp/project.ove")) + 1);

	std::vector<char> buf(static_cast<size_t>(required));
	ASSERT_EQ(oakcommon_commandlinepositionalargument_get_setting(
				  arg, buf.data(), required),
			  required);
	EXPECT_STREQ(buf.data(), "/tmp/project.ove");

	oakcommon_commandlinepositionalargument_free(arg);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, PositionalArgumentSetGetSetting)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	OakCommonCommandLinePositionalArgument *arg = nullptr;
	ASSERT_EQ(oakcommon_commandlineparser_add_positional_argument(
				  parser, "project", "Project file", 0, &arg),
			  OAKCOMMON_OK);
	ASSERT_NE(arg, nullptr);

	ASSERT_EQ(oakcommon_commandlinepositionalargument_set_setting(arg,
																  "hello.ove"),
			  OAKCOMMON_OK);

	char buf[64];
	int required =
		oakcommon_commandlinepositionalargument_get_setting(arg, buf,
															sizeof(buf));
	EXPECT_EQ(required, static_cast<int>(strlen("hello.ove")) + 1);
	EXPECT_STREQ(buf, "hello.ove");

	// Error paths
	EXPECT_EQ(oakcommon_commandlinepositionalargument_set_setting(nullptr,
																  "x"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_commandlinepositionalargument_set_setting(arg,
																  nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_commandlinepositionalargument_get_setting(nullptr,
																  buf,
																  sizeof(buf)),
			  OAKCOMMON_E_INVALID);

	oakcommon_commandlinepositionalargument_free(arg);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, TwoStageStringGetterSmallBuffer)
{
	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);

	const char *names[] = { "e" };
	OakCommonCommandLineOption *option = nullptr;
	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 1, "Export", 1, "filename", 0, &option),
			  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_commandlineoption_set_setting(option, "abcdef"),
			  OAKCOMMON_OK);

	// Buffer too small: return value reports the required size (7) and
	// the written contents are truncated but NUL-terminated
	char buf[4];
	int required = oakcommon_commandlineoption_get_setting(option, buf,
														   sizeof(buf));
	EXPECT_EQ(required, 7);
	EXPECT_STREQ(buf, "abc");

	// NULL buffer / zero size only reports the required size
	EXPECT_EQ(oakcommon_commandlineoption_get_setting(option, nullptr, 0), 7);

	// Error paths
	EXPECT_EQ(oakcommon_commandlineoption_get_setting(nullptr, buf,
													  sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_commandlineoption_is_set(option, nullptr),
			  OAKCOMMON_E_INVALID);
	bool dummy = false;
	EXPECT_EQ(oakcommon_commandlineoption_is_set(nullptr, &dummy),
			  OAKCOMMON_E_INVALID);

	oakcommon_commandlineoption_free(option);
	oakcommon_commandlineparser_free(parser);
}

TEST(CommandLineParser, ProcessInvalidArgs)
{
	EXPECT_EQ(oakcommon_commandlineparser_process(nullptr, nullptr, 0),
			  OAKCOMMON_E_INVALID);

	OakCommonCommandLineParser *parser = oakcommon_commandlineparser_init();
	ASSERT_NE(parser, nullptr);
	EXPECT_EQ(oakcommon_commandlineparser_process(parser, nullptr, 1),
			  OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(parser);
}
