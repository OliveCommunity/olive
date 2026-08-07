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

#include <gtest/gtest.h>

#include "plugin/host.h"
#include "plugin/instance.h"

TEST(OakPluginHost, InitAndEnumerate)
{
	EXPECT_EQ(oakplugin_host_init(), OAKPLUGIN_OK);

	int count = oakplugin_host_plugin_count();
	EXPECT_GE(count, 0);

	char buf[256];
	EXPECT_EQ(oakplugin_host_plugin_id_at(-1, buf, sizeof(buf)),
			  OAKPLUGIN_E_NOT_FOUND);
	EXPECT_EQ(oakplugin_host_plugin_id_at(count + 100, buf, sizeof(buf)),
			  OAKPLUGIN_E_NOT_FOUND);
	EXPECT_EQ(oakplugin_host_plugin_label(nullptr, buf, sizeof(buf)),
			  OAKPLUGIN_E_INVALID);

	const char *dirs[] = { "/nonexistent/ofx/path" };
	EXPECT_EQ(oakplugin_host_scan(dirs, 1), OAKPLUGIN_OK);
	EXPECT_EQ(oakplugin_host_scan(nullptr, 1), OAKPLUGIN_E_INVALID);

	oakplugin_host_shutdown();
}

TEST(OakPluginHost, MessageHandler)
{
	static int calls = 0;
	calls = 0;

	oakplugin_host_set_message_handler(
		[](const char *type, const char *message, void *userdata) -> int {
			int *counter = static_cast<int *>(userdata);
			(*counter)++;
			EXPECT_STREQ(type, "Message");
			EXPECT_STRNE(message, "");
			return OAKPLUGIN_MESSAGE_ANSWER_NO;
		},
		&calls);

	// No plugin loaded here; just verify registration does not crash and
	// the default path works after clearing
	oakplugin_host_set_message_handler(nullptr, nullptr);
	SUCCEED();
}

TEST(OakPluginInstance, CreateUnknownIdFails)
{
	EXPECT_EQ(oakplugin_host_init(), OAKPLUGIN_OK);

	OakPluginInstance instance =
		oakplugin_instance_create("com.example.nonexistent.plugin");
	EXPECT_EQ(instance.ctx, nullptr);
	oakplugin_instance_free(&instance); // no-op on empty

	EXPECT_EQ(oakplugin_debug_alive_count(), 0);
}

TEST(OakPluginInstance, InvalidArgs)
{
	OakPluginInstance empty = {};

	oakplugin_instance_free(nullptr); // no-op
	oakplugin_instance_free(&empty); // no-op

	oaknode_value value = {};
	EXPECT_EQ(oakplugin_instance_set_param(empty, "p", &value),
			  OAKPLUGIN_E_INVALID);
	EXPECT_EQ(oakplugin_instance_get_param(empty, "p", &value),
			  OAKPLUGIN_E_INVALID);
	EXPECT_EQ(oakplugin_instance_set_param_string(empty, "p", "x"),
			  OAKPLUGIN_E_INVALID);
	EXPECT_EQ(oakplugin_instance_get_param_string(empty, "p", nullptr, 0),
			  OAKPLUGIN_E_INVALID);

	EXPECT_EQ(oakplugin_instance_render(empty, OakRenderTexture{},
										OakRenderTexture{}, 0.0),
			  OAKPLUGIN_E_INVALID);
	EXPECT_EQ(oakplugin_instance_set_progress_cb(empty, nullptr, nullptr),
			  OAKPLUGIN_E_INVALID);
	EXPECT_EQ(oakplugin_instance_cancel(empty), OAKPLUGIN_E_INVALID);
}
