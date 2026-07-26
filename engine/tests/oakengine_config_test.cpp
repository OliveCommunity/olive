/***

  Oak - Non-Linear Video Editor
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

// Pure C ABI tests for the liboakengine configuration facade
// (oakengine/config.h). Runs headless; no GPU required.

#include <assert.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>

#include "oakengine/config.h"
#include "oakengine/init.h"

static int g_error_calls = 0;
static char g_last_title[256];
static char g_last_message[256];

static void error_cb(const char *title, const char *message, void *userdata)
{
	(void) userdata;
	g_error_calls++;
	strncpy(g_last_title, title, sizeof(g_last_title) - 1);
	g_last_title[sizeof(g_last_title) - 1] = '\0';
	strncpy(g_last_message, message, sizeof(g_last_message) - 1);
	g_last_message[sizeof(g_last_message) - 1] = '\0';
}

static void test_string_round_trip(void)
{
	char buf[256];

	// Missing key returns 0 (empty string).
	EXPECT_TRUE(oakengine_config_get_string("oak_test_string_key", buf,
									   sizeof(buf)) == 0);

	EXPECT_TRUE(oakengine_config_set_string("oak_test_string_key",
									   "hello world") == OAKENGINE_OK);
	int len = oakengine_config_get_string("oak_test_string_key", buf,
									  sizeof(buf));
	EXPECT_TRUE(len == int(strlen("hello world")));
	EXPECT_TRUE(strcmp(buf, "hello world") == 0);

	// Query length with NULL buffer.
	EXPECT_TRUE(oakengine_config_get_string("oak_test_string_key", NULL, 0) == len);

	// NULL key is rejected.
	EXPECT_TRUE(oakengine_config_get_string(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_config_set_string(NULL, "x") == OAKENGINE_E_INVALID);
}

static void test_int_round_trip(void)
{
	EXPECT_TRUE(oakengine_config_get_int("oak_test_int_key", 42) == 42);

	EXPECT_TRUE(oakengine_config_set_int("oak_test_int_key", 12345) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_config_get_int("oak_test_int_key", 0) == 12345);

	EXPECT_TRUE(oakengine_config_set_int("oak_test_int_key", -7) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_config_get_int("oak_test_int_key", 0) == -7);

	// NULL key returns default.
	EXPECT_TRUE(oakengine_config_get_int(NULL, 99) == 99);
	EXPECT_TRUE(oakengine_config_set_int(NULL, 1) == OAKENGINE_E_INVALID);
}

static void test_error_handler(void)
{
	g_error_calls = 0;
	EXPECT_TRUE(oakengine_config_set_error_handler(error_cb, NULL) ==
		   OAKENGINE_OK);

	EXPECT_TRUE(oakengine_config_report_error("Test Title",
									 "Test Message") == OAKENGINE_OK);
	EXPECT_TRUE(g_error_calls == 1);
	EXPECT_TRUE(strcmp(g_last_title, "Test Title") == 0);
	EXPECT_TRUE(strcmp(g_last_message, "Test Message") == 0);

	// Clearing the handler does not crash.
	EXPECT_TRUE(oakengine_config_set_error_handler(NULL, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_config_report_error("Ignored", "Ignored") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(g_error_calls == 1);
}

TEST(OakEngineConfig, Main)
{
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_string_round_trip();
	test_int_round_trip();
	test_error_handler();

	EXPECT_TRUE(oakengine_config_save() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_config_load() == OAKENGINE_OK);

	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);
}
