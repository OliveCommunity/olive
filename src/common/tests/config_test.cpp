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

#include "common/config.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace
{

namespace fs = std::filesystem;

/**
 * @brief Redirects the config file into a fresh per-test temp directory
 *
 * ConfigStore resolves its file through
 * FileFunctions::get_configuration_location() on every load/save, and
 * that honors OAK_CONFIG_DIR, so setting the env var per test isolates
 * the on-disk state. The store itself is reset to defaults in SetUp.
 */
class ConfigTest : public testing::Test {
protected:
	void SetUp() override
	{
		dir_ = fs::temp_directory_path() /
			   fs::path("oakconfig_test_" + std::to_string(
											 ::testing::UnitTest::GetInstance()
												 ->random_seed()) +
					 "_" + std::to_string(counter_++));
		fs::create_directories(dir_);
		setenv("OAK_CONFIG_DIR", dir_.string().c_str(), 1);
		ASSERT_EQ(oakcommon_config_reset_defaults(), OAKCOMMON_OK);
		ASSERT_EQ(oakcommon_config_set_error_handler(nullptr, nullptr),
				  OAKCOMMON_OK);
	}

	void TearDown() override
	{
		unsetenv("OAK_CONFIG_DIR");
		oakcommon_config_reset_defaults();
		oakcommon_config_set_error_handler(nullptr, nullptr);
		std::error_code ec;
		fs::remove_all(dir_, ec);
	}

	std::string ini_contents()
	{
		std::ifstream in((dir_ / "config.ini").string());
		return std::string(std::istreambuf_iterator<char>(in),
						   std::istreambuf_iterator<char>());
	}

	fs::path dir_;
	static int counter_;
};

int ConfigTest::counter_ = 0;

// --- compiled-in defaults -------------------------------------------------

TEST_F(ConfigTest, DefaultsAreRegistered)
{
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", -1),
			  1920);
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "SplitClipsCopyNodes", -1),
			  1);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "CatColor11", -1), 11);

	char buf[64];
	ASSERT_GT(oakcommon_config_get(nullptr, "GraphicsBackend", buf,
								   sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "opengl");

	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "DefaultSequenceWidth"),
			  OAKCOMMON_CONFIG_ENTRY_INT);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "GraphicsBackend"),
			  OAKCOMMON_CONFIG_ENTRY_STRING);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "UseProxyMedia"),
			  OAKCOMMON_CONFIG_ENTRY_BOOL);
}

// --- oakcommon_config_set / oakcommon_config_get (string) ------------------

TEST_F(ConfigTest, SetGetStringRoundtripTwoStage)
{
	oakcommon_config_set(nullptr, "TestStringKey", "hello world");

	// Stage 1: query the required size with a NULL buffer.
	const int required =
		oakcommon_config_get(nullptr, "TestStringKey", nullptr, 0);
	ASSERT_EQ(required, int(strlen("hello world")) + 1);

	// Stage 2: fetch into a sufficiently large buffer.
	std::string buf(required, '\0');
	ASSERT_EQ(oakcommon_config_get(nullptr, "TestStringKey", buf.data(),
								   required),
			  required);
	EXPECT_STREQ(buf.c_str(), "hello world");

	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "TestStringKey"),
			  OAKCOMMON_CONFIG_ENTRY_STRING);
}

TEST_F(ConfigTest, GetStringErrorPaths)
{
	char buf[8];
	// Missing key.
	EXPECT_EQ(oakcommon_config_get(nullptr, "NoSuchKey", buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	// NULL key.
	EXPECT_EQ(oakcommon_config_get(nullptr, nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	// Negative buffer size.
	EXPECT_EQ(oakcommon_config_get(nullptr, "GraphicsBackend", buf, -1),
			  OAKCOMMON_E_INVALID);
	// set with NULL value is a no-op (void return, entry must not appear).
	oakcommon_config_set(nullptr, "IgnoredKey", nullptr);
	EXPECT_EQ(oakcommon_config_get(nullptr, "IgnoredKey", buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
}

// --- group/key (§2.1 two-argument form) ------------------------------------

TEST_F(ConfigTest, GroupedKeyMapsToIniSection)
{
	oakcommon_config_set("Audio", "Output", "coreaudio");

	char buf[32];
	ASSERT_GT(oakcommon_config_get("Audio", "Output", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "coreaudio");

	ASSERT_EQ(oakcommon_config_save(), OAKCOMMON_OK);
	const std::string ini = ini_contents();
	EXPECT_NE(ini.find("[Audio]"), std::string::npos);
	EXPECT_NE(ini.find("Output=coreaudio"), std::string::npos);

	// Empty group behaves like NULL group (top-level key).
	oakcommon_config_set("", "FlatKey", "flat");
	ASSERT_GT(oakcommon_config_get(nullptr, "FlatKey", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "flat");
}

// --- int family -------------------------------------------------------------

TEST_F(ConfigTest, IntRoundtrip)
{
	oakcommon_config_set_int(nullptr, "TestIntKey", -42);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "TestIntKey", 0), -42);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "TestIntKey"),
			  OAKCOMMON_CONFIG_ENTRY_INT);
	// Overrides a compiled-in default.
	oakcommon_config_set_int(nullptr, "DefaultSequenceWidth", 3840);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", 0),
			  3840);
}

TEST_F(ConfigTest, IntFallbackOnMissingOrWrongType)
{
	// Missing key -> fallback.
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "NoSuchKey", 7), 7);
	// Wrong type (string entry) -> fallback.
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "GraphicsBackend", 9), 9);
}

TEST_F(ConfigTest, Int64RoundtripAndFallback)
{
	oakcommon_config_set_int64(nullptr, "TestInt64Key",
							   INT64_C(5000000000));
	EXPECT_EQ(oakcommon_config_get_int64(nullptr, "TestInt64Key", 0),
			  INT64_C(5000000000));
	// Missing key -> fallback; NULL key -> fallback.
	EXPECT_EQ(oakcommon_config_get_int64(nullptr, "NoSuchKey",
										 INT64_C(-1)),
			  INT64_C(-1));
	EXPECT_EQ(oakcommon_config_get_int64(nullptr, nullptr, INT64_C(-2)),
			  INT64_C(-2));
}

// --- double family ----------------------------------------------------------

TEST_F(ConfigTest, DoubleRoundtrip)
{
	oakcommon_config_set_double(nullptr, "TestDoubleKey", 2.5);
	EXPECT_DOUBLE_EQ(oakcommon_config_get_double(nullptr, "TestDoubleKey", 0),
					 2.5);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "TestDoubleKey"),
			  OAKCOMMON_CONFIG_ENTRY_DOUBLE);
}

TEST_F(ConfigTest, DoubleFallbackOnMissingOrWrongType)
{
	EXPECT_DOUBLE_EQ(oakcommon_config_get_double(nullptr, "NoSuchKey", 1.5),
					 1.5);
	// Wrong type (int entry) -> fallback.
	EXPECT_DOUBLE_EQ(
		oakcommon_config_get_double(nullptr, "DefaultSequenceWidth", 3.5),
		3.5);
}

// --- bool family ------------------------------------------------------------

TEST_F(ConfigTest, BoolRoundtrip)
{
	oakcommon_config_set_bool(nullptr, "TestBoolKey", 1);
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "TestBoolKey", 0), 1);
	oakcommon_config_set_bool(nullptr, "TestBoolKey", 0);
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "TestBoolKey", 1), 0);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "TestBoolKey"),
			  OAKCOMMON_CONFIG_ENTRY_BOOL);
}

TEST_F(ConfigTest, BoolFallbackOnMissingOrWrongType)
{
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "NoSuchKey", 1), 1);
	// Wrong type (string entry) -> fallback.
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "GraphicsBackend", 1), 1);
}

// --- typed set through oakcommon_config_set ---------------------------------

TEST_F(ConfigTest, SetStringParsesIntoDeclaredType)
{
	// Existing INT entry: a parseable string updates the value...
	oakcommon_config_set(nullptr, "DefaultSequenceWidth", "2560");
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", 0),
			  2560);
	// ...an unparseable one leaves the entry unchanged.
	oakcommon_config_set(nullptr, "DefaultSequenceWidth", "not-a-number");
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", 0),
			  2560);
}

// --- load / save / reset_defaults -------------------------------------------

TEST_F(ConfigTest, SaveLoadRoundtrip)
{
	oakcommon_config_set_int(nullptr, "DefaultSequenceHeight", 2160);
	oakcommon_config_set("Session", "LastDir", "/tmp/media");
	oakcommon_config_set_bool(nullptr, "UseGLFinish", 1);
	ASSERT_EQ(oakcommon_config_save(), OAKCOMMON_OK);
	ASSERT_TRUE(fs::exists(dir_ / "config.ini"));

	// Wipe in-memory state; defaults must come back...
	ASSERT_EQ(oakcommon_config_reset_defaults(), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceHeight", 0),
			  1080);
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "UseGLFinish", -1), 0);
	char buf[64];
	EXPECT_EQ(oakcommon_config_get("Session", "LastDir", buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);

	// ...and load() restores everything that was saved.
	ASSERT_EQ(oakcommon_config_load(), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceHeight", 0),
			  2160);
	EXPECT_EQ(oakcommon_config_get_bool(nullptr, "UseGLFinish", -1), 1);
	ASSERT_GT(oakcommon_config_get("Session", "LastDir", buf, sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "/tmp/media");
	EXPECT_EQ(oakcommon_config_entry_type("Session", "LastDir"),
			  OAKCOMMON_CONFIG_ENTRY_STRING);
}

TEST_F(ConfigTest, LoadMissingFileIsNotAnError)
{
	// Fresh directory, no config.ini: defaults stay, OAKCOMMON_OK.
	ASSERT_EQ(oakcommon_config_load(), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", -1),
			  1920);
}

TEST_F(ConfigTest, LoadErrorPathUnreadableFile)
{
	// Make config.ini a directory: it exists but cannot be read.
	fs::create_directories(dir_ / "config.ini");
	EXPECT_EQ(oakcommon_config_load(), OAKCOMMON_E_FAILED);
}

TEST_F(ConfigTest, ResetDefaultsDropsCustomKeys)
{
	oakcommon_config_set_int(nullptr, "TransientKey", 1);
	ASSERT_EQ(oakcommon_config_get_int(nullptr, "TransientKey", -1), 1);
	ASSERT_EQ(oakcommon_config_reset_defaults(), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "TransientKey", -1), -1);
	// Defaults survive the reset.
	EXPECT_EQ(oakcommon_config_get_int(nullptr, "DefaultSequenceWidth", -1),
			  1920);
}

// --- error handler ----------------------------------------------------------

namespace
{

struct HandlerLog {
	int calls = 0;
	std::string title;
	std::string message;
};

void recording_handler(const char *title, const char *message,
					   void *userdata)
{
	auto *log = static_cast<HandlerLog *>(userdata);
	log->calls++;
	log->title = title;
	log->message = message;
}

} // namespace

TEST_F(ConfigTest, ErrorHandlerFiresOnSaveFailure)
{
	HandlerLog log;
	ASSERT_EQ(
		oakcommon_config_set_error_handler(recording_handler, &log),
		OAKCOMMON_OK);

	// Point the config dir at a path that exists as a *file*: the temp
	// file cannot be created inside it, so save fails.
	const fs::path blocker = fs::temp_directory_path() /
							 "oakconfig_test_blocker";
	{
		std::ofstream out(blocker.string());
		out << "not a directory";
	}
	setenv("OAK_CONFIG_DIR", blocker.string().c_str(), 1);

	EXPECT_EQ(oakcommon_config_save(), OAKCOMMON_E_FAILED);
	EXPECT_EQ(log.calls, 1);
	EXPECT_FALSE(log.title.empty());
	EXPECT_FALSE(log.message.empty());

	// Restore the per-test dir for TearDown.
	setenv("OAK_CONFIG_DIR", dir_.string().c_str(), 1);
	std::error_code ec;
	fs::remove(blocker, ec);
}

TEST_F(ConfigTest, ClearErrorHandlerRestoresSilence)
{
	// Registering and clearing must both succeed.
	ASSERT_EQ(
		oakcommon_config_set_error_handler(recording_handler, nullptr),
		OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_config_set_error_handler(nullptr, nullptr),
			  OAKCOMMON_OK);
}

// --- entry_type error paths --------------------------------------------------

TEST_F(ConfigTest, EntryTypeErrorPaths)
{
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, "NoSuchKey"),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_config_entry_type(nullptr, nullptr),
			  OAKCOMMON_E_INVALID);
}

} // namespace
