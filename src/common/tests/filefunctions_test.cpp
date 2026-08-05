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

#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <string>
#include <vector>

#include "common/filefunctions.h"

namespace fs = std::filesystem;

class FileFunctionsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		handle_ = oakcommon_filefunctions_init();
		ASSERT_NE(handle_, nullptr);

		temp_dir_ = fs::temp_directory_path() /
			    fs::path("oakcommon_filefunctions_test_" +
				     std::to_string(::getpid()));
		fs::remove_all(temp_dir_);
		fs::create_directories(temp_dir_);
	}

	void TearDown() override
	{
		oakcommon_filefunctions_free(handle_);
		handle_ = nullptr;

		std::error_code ec;
		fs::remove_all(temp_dir_, ec);
	}

	std::string write_file(const std::string &name,
			       const std::string &content)
	{
		fs::path p = temp_dir_ / name;
		fs::create_directories(p.parent_path());
		std::ofstream f(p, std::ios::binary);
		f << content;
		return p.string();
	}

	/**
	 * @brief Two-stage string call helper: query size, then fetch
	 */
	template <typename Fn>
	std::string fetch_string(Fn &&fn, int *first_return)
	{
		int required = fn(nullptr, 0);
		if (first_return != nullptr) {
			*first_return = required;
		}
		EXPECT_GT(required, 0);
		if (required <= 0) {
			return std::string();
		}

		std::vector<char> buf(required);
		int second = fn(buf.data(), required);
		EXPECT_EQ(second, required);
		return std::string(buf.data());
	}

	OakCommonFileFunctions *handle_ = nullptr;
	fs::path temp_dir_;
};

TEST_F(FileFunctionsTest, FreeNullIsNoOp)
{
	// Must not crash
	oakcommon_filefunctions_free(nullptr);
}

TEST_F(FileFunctionsTest, NullHandleReturnsInvalid)
{
	char buf[16];
	int out = 0;
	EXPECT_EQ(oakcommon_filefunctions_get_configuration_location(
			  nullptr, buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_application_path(nullptr, buf,
							       sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_temp_file_path(nullptr, buf,
							     sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_auto_recovery_root(nullptr, buf,
								 sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_unique_file_identifier(
			  nullptr, "x", buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_ensure_filename_extension(
			  nullptr, "x", "y", buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_read_file_as_string(nullptr, "x",
							      buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_safe_temporary_filename(
			  nullptr, "x", buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_formatted_executable_for_platform(
			  nullptr, "x", buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(nullptr, "x", 1,
							     &out),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_can_copy_directory_without_overwriting(
			  nullptr, "a", "b", &out),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_copy_directory(nullptr, "a", "b", 0),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_rename_file_allow_overwrite(
			  nullptr, "a", "b", &out),
		  OAKCOMMON_E_INVALID);
}

TEST_F(FileFunctionsTest, NullArgumentsReturnInvalid)
{
	char buf[16];
	int out = 0;
	EXPECT_EQ(oakcommon_filefunctions_read_file_as_string(handle_, nullptr,
							      buf, sizeof(buf)),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(handle_, nullptr,
							     1, &out),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(handle_, "x", 1,
							     nullptr),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_get_configuration_location(
			  handle_, nullptr, -1),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_filefunctions_rename_file_allow_overwrite(
			  handle_, nullptr, "b", &out),
		  OAKCOMMON_E_INVALID);
}

TEST_F(FileFunctionsTest, ReadFileAsStringRoundTrip)
{
	std::string content = "hello oak\nline two\n";
	std::string path = write_file("read.txt", content);

	std::string result = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_read_file_as_string(
				handle_, path.c_str(), buf, size);
		},
		nullptr);
	EXPECT_EQ(result, content);

	// Buffer too small: required size is reported, nothing written
	char tiny[2] = {'z', '\0'};
	int required = oakcommon_filefunctions_read_file_as_string(
		handle_, path.c_str(), tiny, sizeof(tiny));
	EXPECT_EQ(required, static_cast<int>(content.size()) + 1);
}

TEST_F(FileFunctionsTest, ReadFileAsStringMissingFile)
{
	std::string path = (temp_dir_ / "no_such_file.txt").string();
	char buf[8] = {};
	int required = oakcommon_filefunctions_read_file_as_string(
		handle_, path.c_str(), buf, sizeof(buf));
	// Missing file yields an empty string (required size 1)
	EXPECT_EQ(required, 1);
	EXPECT_STREQ(buf, "");
}

TEST_F(FileFunctionsTest, EnsureFilenameExtension)
{
	std::string result = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_ensure_filename_extension(
				handle_, "project", "ove", buf, size);
		},
		nullptr);
	EXPECT_EQ(result, "project.ove");

	// Already present (case-insensitive): unchanged
	result = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_ensure_filename_extension(
				handle_, "project.OVE", "ove", buf, size);
		},
		nullptr);
	EXPECT_EQ(result, "project.OVE");
}

TEST_F(FileFunctionsTest, GetUniqueFileIdentifierStable)
{
	std::string path = write_file("id.txt", "some data");

	std::string id1 = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_unique_file_identifier(
				handle_, path.c_str(), buf, size);
		},
		nullptr);
	EXPECT_FALSE(id1.empty());

	std::string id2 = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_unique_file_identifier(
				handle_, path.c_str(), buf, size);
		},
		nullptr);
	EXPECT_EQ(id1, id2);
}

TEST_F(FileFunctionsTest, GetUniqueFileIdentifierMissingFile)
{
	std::string path = (temp_dir_ / "missing.bin").string();
	char buf[8] = {};
	int required = oakcommon_filefunctions_get_unique_file_identifier(
		handle_, path.c_str(), buf, sizeof(buf));
	EXPECT_EQ(required, 1);
	EXPECT_STREQ(buf, "");
}

TEST_F(FileFunctionsTest, DirectoryIsValidCreateAndCheck)
{
	int out = 0;

	// Existing directory
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(
			  handle_, temp_dir_.string().c_str(), 0, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 1);

	// Non-existing directory, allowed to create
	std::string sub = (temp_dir_ / "a" / "b").string();
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(
			  handle_, sub.c_str(), 1, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 1);
	EXPECT_TRUE(fs::is_directory(sub));

	// Non-existing directory, not allowed to create
	std::string sub2 = (temp_dir_ / "c").string();
	EXPECT_EQ(oakcommon_filefunctions_directory_is_valid(
			  handle_, sub2.c_str(), 0, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 0);
	EXPECT_FALSE(fs::exists(sub2));
}

TEST_F(FileFunctionsTest, GetSafeTemporaryFilenameDoesNotExist)
{
	std::string base = write_file("safe.ove", "data");

	std::string temp = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_safe_temporary_filename(
				handle_, base.c_str(), buf, size);
		},
		nullptr);
	EXPECT_FALSE(temp.empty());
	EXPECT_FALSE(fs::exists(temp));
	EXPECT_NE(temp, base);
	EXPECT_TRUE(temp.find(".tmp") != std::string::npos);
}

TEST_F(FileFunctionsTest, RenameFileAllowOverwrite)
{
	std::string from = write_file("from.txt", "new content");
	std::string to = write_file("to.txt", "old content");

	int out = 0;
	EXPECT_EQ(oakcommon_filefunctions_rename_file_allow_overwrite(
			  handle_, from.c_str(), to.c_str(), &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 1);
	EXPECT_FALSE(fs::exists(from));

	std::ifstream f(to, std::ios::binary);
	std::string content((std::istreambuf_iterator<char>(f)),
			    std::istreambuf_iterator<char>());
	EXPECT_EQ(content, "new content");
}

TEST_F(FileFunctionsTest, RenameFileAllowOverwriteMissingSource)
{
	std::string from = (temp_dir_ / "no_source.txt").string();
	std::string to = (temp_dir_ / "target.txt").string();

	int out = 1;
	EXPECT_EQ(oakcommon_filefunctions_rename_file_allow_overwrite(
			  handle_, from.c_str(), to.c_str(), &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 0);
}

TEST_F(FileFunctionsTest, CopyDirectoryAndOverwriteCheck)
{
	write_file("src/one.txt", "one");
	write_file("src/nested/two.txt", "two");
	std::string src = (temp_dir_ / "src").string();
	std::string dst = (temp_dir_ / "dst").string();

	// Nothing at dest: safe to copy
	int out = 0;
	EXPECT_EQ(oakcommon_filefunctions_can_copy_directory_without_overwriting(
			  handle_, src.c_str(), dst.c_str(), &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 1);

	EXPECT_EQ(oakcommon_filefunctions_copy_directory(handle_, src.c_str(),
							 dst.c_str(), 0),
		  OAKCOMMON_OK);
	EXPECT_TRUE(fs::exists(temp_dir_ / "dst" / "one.txt"));
	EXPECT_TRUE(fs::exists(temp_dir_ / "dst" / "nested" / "two.txt"));

	// Files exist at dest now: no longer safe
	EXPECT_EQ(oakcommon_filefunctions_can_copy_directory_without_overwriting(
			  handle_, src.c_str(), dst.c_str(), &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, 0);

	// Read back copied content through the C API
	std::string copied = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_read_file_as_string(
				handle_,
				(temp_dir_ / "dst" / "nested" / "two.txt")
					.string()
					.c_str(),
				buf, size);
		},
		nullptr);
	EXPECT_EQ(copied, "two");
}

TEST_F(FileFunctionsTest, CopyDirectoryMissingSource)
{
	std::string src = (temp_dir_ / "no_such_dir").string();
	std::string dst = (temp_dir_ / "dst2").string();

	// Underlying call logs and returns without copying; must not crash
	EXPECT_EQ(oakcommon_filefunctions_copy_directory(handle_, src.c_str(),
							 dst.c_str(), 0),
		  OAKCOMMON_OK);
	EXPECT_FALSE(fs::exists(temp_dir_ / "dst2" / "anything"));
}

TEST_F(FileFunctionsTest, LocationGettersReturnNonEmpty)
{
	int first_return = 0;

	std::string config = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_configuration_location(
				handle_, buf, size);
		},
		&first_return);
	EXPECT_FALSE(config.empty());
	EXPECT_EQ(first_return, static_cast<int>(config.size()) + 1);

	std::string temp = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_temp_file_path(
				handle_, buf, size);
		},
		nullptr);
	EXPECT_FALSE(temp.empty());
	EXPECT_TRUE(fs::is_directory(temp));

	std::string app = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_application_path(
				handle_, buf, size);
		},
		nullptr);
	EXPECT_FALSE(app.empty());

	std::string recovery = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_auto_recovery_root(
				handle_, buf, size);
		},
		nullptr);
	EXPECT_FALSE(recovery.empty());
}

TEST_F(FileFunctionsTest, GetFormattedExecutableForPlatform)
{
	std::string result = fetch_string(
		[&](char *buf, int size) {
			return oakcommon_filefunctions_get_formatted_executable_for_platform(
				handle_, "oak", buf, size);
		},
		nullptr);
#ifdef _WIN32
	EXPECT_EQ(result, "oak.exe");
#else
	EXPECT_EQ(result, "oak");
#endif
}
