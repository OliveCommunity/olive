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

#include "node/factory.h"
#include "node/group.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "testnode.h"

namespace
{

class NodeFactoryTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ASSERT_EQ(oaknode_factory_initialize(), OAKNODE_OK);
	}

	void TearDown() override
	{
		oaknode_factory_destroy();
	}
};

TEST_F(NodeFactoryTest, EnumerateLibrary)
{
	int count = 0;
	EXPECT_EQ(oaknode_factory_id_count(&count), OAKNODE_OK);
	EXPECT_GT(count, 0);

	// Every id is non-empty and name_from_id maps back to a name.
	for (int i = 0; i < count; i++) {
		int required = oaknode_factory_id_at(i, nullptr, 0);
		ASSERT_GT(required, 1) << i;
		std::vector<char> id(static_cast<size_t>(required));
		ASSERT_EQ(oaknode_factory_id_at(i, id.data(), required), required);

		int name_required =
			oaknode_factory_name_from_id(id.data(), nullptr, 0);
		EXPECT_GT(name_required, 1) << id.data();
	}

	EXPECT_EQ(oaknode_factory_id_at(count, nullptr, 0), OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_factory_id_at(-1, nullptr, 0), OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_factory_id_count(nullptr), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_factory_name_from_id(nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);

	// Unknown ids yield an empty name (required size 1).
	EXPECT_EQ(oaknode_factory_name_from_id("org.oak.DoesNotExist", nullptr, 0),
			  1);
}

TEST_F(NodeFactoryTest, NodeAt)
{
	int count = 0;
	ASSERT_EQ(oaknode_factory_id_count(&count), OAKNODE_OK);
	ASSERT_GT(count, 0);

	OakNodeNode *prototype = nullptr;
	EXPECT_EQ(oaknode_factory_node_at(0, &prototype), OAKNODE_OK);
	ASSERT_NE(prototype, nullptr);

	// The prototype's id matches id_at(0).
	int required = oaknode_factory_id_at(0, nullptr, 0);
	ASSERT_GT(required, 1);
	std::vector<char> id(static_cast<size_t>(required));
	ASSERT_EQ(oaknode_factory_id_at(0, id.data(), required), required);

	char buf[256];
	ASSERT_GT(oaknode_node_get_id(prototype, buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, id.data());

	EXPECT_EQ(oaknode_factory_node_at(count, &prototype), OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_factory_node_at(0, nullptr), OAKNODE_E_INVALID);
}

TEST_F(NodeFactoryTest, CreateFromId)
{
	// The group node is part of family A and always registered.
	int alive_before = oaknode_debug_alive_count();

	int name_required =
		oaknode_factory_name_from_id("org.olivevideoeditor.Olive.group",
									 nullptr, 0);
	ASSERT_GT(name_required, 1);
	std::vector<char> name(static_cast<size_t>(name_required));
	EXPECT_EQ(oaknode_factory_name_from_id("org.olivevideoeditor.Olive.group",
										   name.data(), name_required),
			  name_required);

	OakNodeNode *node =
		oaknode_factory_create_from_id("org.olivevideoeditor.Olive.group");
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	char buf[256];
	ASSERT_GT(oaknode_node_get_id(node, buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "org.olivevideoeditor.Olive.group");

	// The created instance is a group.
	EXPECT_NE(oaknode_group_cast(node), nullptr);

	oaknode_node_free(node);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	EXPECT_EQ(oaknode_factory_create_from_id("org.oak.DoesNotExist"), nullptr);
	EXPECT_EQ(oaknode_factory_create_from_id(nullptr), nullptr);
}

TEST(NodeFactoryUninitializedTest, StateErrors)
{
	// Ensure the library is empty regardless of test execution order.
	oaknode_factory_destroy();

	int count = 0;
	EXPECT_EQ(oaknode_factory_id_count(&count), OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_factory_id_at(0, nullptr, 0), OAKNODE_E_STATE);
	OakNodeNode *node = nullptr;
	EXPECT_EQ(oaknode_factory_node_at(0, &node), OAKNODE_E_STATE);

	// Re-initialize for any subsequent test run.
	EXPECT_EQ(oaknode_factory_initialize(), OAKNODE_OK);
	oaknode_factory_destroy();
}

}
