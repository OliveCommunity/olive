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

#include "node/folder.h"
#include "node/node.h"
#include "node/project.h"

#include "../c_api/nodehandle.h"

namespace
{

/**
 * @brief Two-stage string getter helper: query, then fetch.
 */
std::string get_string(int (*fn)(OakNodeProject, char *, int),
					   OakNodeProject project)
{
	int required = fn(project, nullptr, 0);
	if (required <= 0) {
		return std::string();
	}
	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(fn(project, buf.data(), required), required);
	return std::string(buf.data());
}

/**
 * @brief Identity compare for value handles (each handle has its own
 * control block, so compare the wrapped objects).
 */
bool same_object(OakNodeNode a, OakNodeNode b)
{
	return oaknode_c_api::to_native<void>(a) == oaknode_c_api::to_native<void>(b);
}

} // namespace

TEST(NodeProject, InitAndFree)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	// No root folder before initialize()
	EXPECT_EQ(oaknode_project_root(project).ctx, nullptr);

	EXPECT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	EXPECT_NE(oaknode_project_root(project).ctx, nullptr);

	// Initializing twice is a state error
	EXPECT_EQ(oaknode_project_initialize(project), OAKNODE_E_STATE);

	oaknode_project_free(&project);
	EXPECT_EQ(project.ctx, nullptr);

	// NULL free is a no-op (must not crash)
	oaknode_project_free(nullptr);
}

TEST(NodeProject, NullHandleErrors)
{
	EXPECT_EQ(oaknode_project_initialize(OakNodeProject{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_clear(OakNodeProject{}), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_root(OakNodeProject{}).ctx, nullptr);
	EXPECT_EQ(oaknode_project_name(OakNodeProject{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_filename(OakNodeProject{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_pretty_filename(OakNodeProject{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_set_filename(OakNodeProject{}, "/tmp/x.ove"),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_is_modified(OakNodeProject{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_set_modified(OakNodeProject{}, 1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_is_new(OakNodeProject{}), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_cache_path(OakNodeProject{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_get_cache_location_setting(OakNodeProject{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_set_cache_location_setting(OakNodeProject{}, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_get_custom_cache_path(OakNodeProject{}, nullptr,
													0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_set_custom_cache_path(OakNodeProject{}, "/tmp"),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_get_uuid(OakNodeProject{}, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_node_count(OakNodeProject{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_node_at(OakNodeProject{}, 0).ctx, nullptr);
}

TEST(NodeProject, NameAndFilename)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	// Untitled project
	EXPECT_EQ(get_string(oaknode_project_name, project), "(untitled)");
	EXPECT_EQ(get_string(oaknode_project_filename, project), "");
	EXPECT_EQ(get_string(oaknode_project_pretty_filename, project),
			  "(untitled)");

	EXPECT_EQ(oaknode_project_set_filename(project, "/tmp/work/my edit.ove"),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_project_name, project), "my edit");
	EXPECT_EQ(get_string(oaknode_project_filename, project),
			  "/tmp/work/my edit.ove");
	EXPECT_EQ(get_string(oaknode_project_pretty_filename, project),
			  "/tmp/work/my edit.ove");

	// NULL filename argument is invalid
	EXPECT_EQ(oaknode_project_set_filename(project, nullptr),
			  OAKNODE_E_INVALID);

	oaknode_project_free(&project);
}

TEST(NodeProject, ModifiedAndIsNew)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	EXPECT_EQ(oaknode_project_is_modified(project), 0);
	EXPECT_EQ(oaknode_project_is_new(project), 1);

	EXPECT_EQ(oaknode_project_set_modified(project, 1), OAKNODE_OK);
	EXPECT_EQ(oaknode_project_is_modified(project), 1);
	EXPECT_EQ(oaknode_project_is_new(project), 0);

	EXPECT_EQ(oaknode_project_set_modified(project, 0), OAKNODE_OK);
	EXPECT_EQ(oaknode_project_is_modified(project), 0);

	// A filename makes the project not-new even when unmodified
	EXPECT_EQ(oaknode_project_set_filename(project, "/tmp/a.ove"),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_project_is_new(project), 0);

	oaknode_project_free(&project);
}

TEST(NodeProject, CachePathSettings)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	// Default location setting
	EXPECT_EQ(oaknode_project_get_cache_location_setting(project), 0);
	EXPECT_EQ(get_string(oaknode_project_get_custom_cache_path, project), "");

	// Invalid setting values are rejected
	EXPECT_EQ(oaknode_project_set_cache_location_setting(project, -1),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_set_cache_location_setting(project, 3),
			  OAKNODE_E_INVALID);

	// Custom path setting is honored by cache_path()
	EXPECT_EQ(oaknode_project_set_custom_cache_path(project, "/tmp/oakcache"),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_project_set_cache_location_setting(project, 2),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_project_get_cache_location_setting(project), 2);
	EXPECT_EQ(get_string(oaknode_project_cache_path, project),
			  "/tmp/oakcache");
	EXPECT_EQ(get_string(oaknode_project_get_custom_cache_path, project),
			  "/tmp/oakcache");

	// NULL clears the custom path
	EXPECT_EQ(oaknode_project_set_custom_cache_path(project, nullptr),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_project_get_custom_cache_path, project), "");

	oaknode_project_free(&project);
}

TEST(NodeProject, Uuid)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);

	std::string uuid = get_string(oaknode_project_get_uuid, project);
	EXPECT_FALSE(uuid.empty());

	oaknode_project_free(&project);
}

TEST(NodeProject, AddRemoveNode)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);

	// The root folder is the only node after initialize()
	int base_count = oaknode_project_node_count(project);
	EXPECT_EQ(base_count, 1);

	// folder_create already adds the node to the graph
	OakNodeFolder folder = oaknode_folder_create(project);
	ASSERT_NE(folder.ctx, nullptr);
	EXPECT_EQ(oaknode_project_node_count(project), base_count + 1);

	// node_at returns the same node; out-of-range yields an empty handle
	OakNodeNode as_node = {};
	bool found = false;
	for (int i = 0; i < oaknode_project_node_count(project); i++) {
		OakNodeNode n = oaknode_project_node_at(project, i);
		ASSERT_NE(n.ctx, nullptr);
		if (same_object(n, oaknode_folder_as_node(folder))) {
			found = true;
		}
		if (i == 0) {
			as_node = n;
		}
	}
	EXPECT_TRUE(found);
	EXPECT_NE(as_node.ctx, nullptr);
	EXPECT_EQ(oaknode_project_node_at(project, -1).ctx, nullptr);
	EXPECT_EQ(oaknode_project_node_at(project,
									  oaknode_project_node_count(project))
				  .ctx,
			  nullptr);

	// Remove detaches without deleting
	OakNodeNode folder_node = oaknode_folder_as_node(folder);
	EXPECT_EQ(oaknode_project_remove_node(project, folder_node), OAKNODE_OK);
	EXPECT_EQ(oaknode_project_node_count(project), base_count);

	// Removing a node that is not in the graph is E_NOT_FOUND
	EXPECT_EQ(oaknode_project_remove_node(project, folder_node),
			  OAKNODE_E_NOT_FOUND);

	// Re-add takes ownership back so the project frees it
	EXPECT_EQ(oaknode_project_add_node(project, folder_node), OAKNODE_OK);
	EXPECT_EQ(oaknode_project_node_count(project), base_count + 1);

	// Empty handle args
	EXPECT_EQ(oaknode_project_add_node(project, OakNodeNode{}),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_project_remove_node(project, OakNodeNode{}),
			  OAKNODE_E_INVALID);

	oaknode_project_free(&project);
}

TEST(NodeProject, Clear)
{
	OakNodeProject project = oaknode_project_init();
	ASSERT_NE(project.ctx, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	ASSERT_NE(oaknode_folder_create(project).ctx, nullptr);
	EXPECT_GE(oaknode_project_node_count(project), 2);

	EXPECT_EQ(oaknode_project_clear(project), OAKNODE_OK);
	EXPECT_EQ(oaknode_project_node_count(project), 0);

	// clear() resets root_, so re-initializing must succeed
	EXPECT_EQ(oaknode_project_initialize(project), OAKNODE_OK);
	EXPECT_GE(oaknode_project_node_count(project), 1);

	oaknode_project_free(&project);
}
