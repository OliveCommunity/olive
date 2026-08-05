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
#include "node/serializer.h"

namespace
{

/**
 * @brief Serialize `save_data` to an XML string using the two-stage getter.
 */
std::string save_to_string(OakNodeSerializerSaveData *save_data)
{
	int required = oaknode_serializer_save_to_xml(save_data, nullptr, 0);
	if (required <= 0) {
		return std::string();
	}
	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(oaknode_serializer_save_to_xml(save_data, buf.data(), required),
			  required);
	return std::string(buf.data());
}

} // namespace

// NOTE: this test must stay first in the file; it exercises the
// not-initialized state and leaves the serializers initialized behind it.
TEST(NodeSerializer, SaveWithoutInitializeFails)
{
	oaknode_serializer_shutdown();

	OakNodeProject *project = oaknode_project_init();
	ASSERT_NE(project, nullptr);

	OakNodeSerializerSaveData *save_data = oaknode_serializer_savedata_create(
		OAKNODE_SERIALIZER_LOAD_ONLY_NODES, project);
	ASSERT_NE(save_data, nullptr);
	EXPECT_EQ(oaknode_serializer_save_to_xml(save_data, nullptr, 0),
			  OAKNODE_E_STATE);

	int result = -1;
	EXPECT_EQ(oaknode_serializer_load_from_xml(project, "<olive/>",
											   OAKNODE_SERIALIZER_LOAD_ONLY_NODES,
											   &result, nullptr, nullptr, 0),
			  OAKNODE_E_STATE);

	oaknode_serializer_savedata_free(save_data);
	oaknode_project_free(project);
}

TEST(NodeSerializer, InitializeIsIdempotent)
{
	EXPECT_EQ(oaknode_serializer_initialize(), OAKNODE_OK);
	EXPECT_EQ(oaknode_serializer_initialize(), OAKNODE_OK);
}

TEST(NodeSerializer, NodeGraphCopyPasteRoundTrip)
{
	ASSERT_EQ(oaknode_serializer_initialize(), OAKNODE_OK);

	// Source project with a folder under the root
	OakNodeProject *source = oaknode_project_init();
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(oaknode_project_initialize(source), OAKNODE_OK);
	OakNodeFolder *root = oaknode_project_root(source);
	OakNodeFolder *folder = oaknode_folder_create(source);
	ASSERT_NE(root, nullptr);
	ASSERT_NE(folder, nullptr);
	ASSERT_EQ(oaknode_folder_add_child(
				  root, reinterpret_cast<OakNodeNode *>(folder)),
			  OAKNODE_OK);

	// "Copy": serialize the folder with a custom property
	OakNodeSerializerSaveData *save_data = oaknode_serializer_savedata_create(
		OAKNODE_SERIALIZER_LOAD_ONLY_NODES, source);
	ASSERT_NE(save_data, nullptr);
	OakNodeNode *nodes[] = { reinterpret_cast<OakNodeNode *>(folder) };
	EXPECT_EQ(oaknode_serializer_savedata_set_nodes(save_data, nodes, 1),
			  OAKNODE_OK);
	EXPECT_EQ(oaknode_serializer_savedata_set_property(
				  save_data, nodes[0], "nodeviewpos", "10;20"),
			  OAKNODE_OK);

	std::string xml = save_to_string(save_data);
	EXPECT_FALSE(xml.empty());
	EXPECT_NE(xml.find("<olive"), std::string::npos);
	oaknode_serializer_savedata_free(save_data);

	// "Paste": load the XML into a different project
	OakNodeProject *target = oaknode_project_init();
	ASSERT_NE(target, nullptr);

	int result = -1;
	OakNodeSerializerLoadData *load_data = nullptr;
	char details[256];
	EXPECT_EQ(oaknode_serializer_load_from_xml(
				  target, xml.c_str(), OAKNODE_SERIALIZER_LOAD_ONLY_NODES,
				  &result, &load_data, details, sizeof(details)),
			  OAKNODE_OK);
	EXPECT_EQ(result, OAKNODE_SERIALIZER_OK) << details;
	ASSERT_NE(load_data, nullptr);

	EXPECT_GE(oaknode_serializer_loaddata_node_count(load_data), 1);
	OakNodeNode *loaded = oaknode_serializer_loaddata_node_at(load_data, 0);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(oaknode_serializer_loaddata_node_at(load_data, -1), nullptr);
	EXPECT_EQ(oaknode_serializer_loaddata_node_at(
				  load_data,
				  oaknode_serializer_loaddata_node_count(load_data)),
			  nullptr);

	// The property rides along, remapped to the new node
	int required = oaknode_serializer_loaddata_get_property(
		load_data, loaded, "nodeviewpos", nullptr, 0);
	ASSERT_GT(required, 0);
	std::vector<char> prop_buf(static_cast<size_t>(required));
	EXPECT_EQ(oaknode_serializer_loaddata_get_property(load_data, loaded,
													   "nodeviewpos",
													   prop_buf.data(),
													   required),
			  required);
	EXPECT_STREQ(prop_buf.data(), "10;20");

	// Unknown keys / nodes are E_NOT_FOUND
	EXPECT_EQ(oaknode_serializer_loaddata_get_property(load_data, loaded,
													   "nosuchkey", nullptr,
													   0),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_serializer_loaddata_get_property(
				  load_data, nodes[0], "nodeviewpos", nullptr, 0),
			  OAKNODE_E_NOT_FOUND);

	// Adopt the loaded nodes into the target project so it owns them
	for (int i = 0; i < oaknode_serializer_loaddata_node_count(load_data);
		 i++) {
		EXPECT_EQ(oaknode_project_add_node(
					  target, oaknode_serializer_loaddata_node_at(load_data, i)),
				  OAKNODE_OK);
	}

	oaknode_serializer_loaddata_free(load_data);
	oaknode_serializer_loaddata_free(nullptr); // NULL free is a no-op
	// Detach the source hierarchy before teardown (see the folder tests'
	// detach_all note about Project::clear()).
	EXPECT_EQ(oaknode_folder_remove_child(
				  root, reinterpret_cast<OakNodeNode *>(folder)),
			  OAKNODE_OK);
	oaknode_project_free(target);
	oaknode_project_free(source);
}

TEST(NodeSerializer, LoadInvalidXmlReportsSerializerError)
{
	ASSERT_EQ(oaknode_serializer_initialize(), OAKNODE_OK);

	OakNodeProject *project = oaknode_project_init();
	ASSERT_NE(project, nullptr);

	int result = -1;
	OakNodeSerializerLoadData *load_data = nullptr;
	char details[256] = { 0 };
	EXPECT_EQ(oaknode_serializer_load_from_xml(
				  project, "this is not xml",
				  OAKNODE_SERIALIZER_LOAD_ONLY_NODES, &result, &load_data,
				  details, sizeof(details)),
			  OAKNODE_OK);
	// Not an oak document: the format version cannot be determined
	EXPECT_EQ(result, OAKNODE_SERIALIZER_UNKNOWN_VERSION);
	EXPECT_EQ(load_data, nullptr);

	oaknode_project_free(project);
}

TEST(NodeSerializer, NullAndInvalidArgs)
{
	ASSERT_EQ(oaknode_serializer_initialize(), OAKNODE_OK);

	int result = -1;

	EXPECT_EQ(oaknode_serializer_savedata_create(-1, nullptr), nullptr);
	EXPECT_EQ(oaknode_serializer_savedata_create(99, nullptr), nullptr);

	EXPECT_EQ(oaknode_serializer_savedata_set_nodes(nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_savedata_set_property(nullptr, nullptr,
													   nullptr, nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_save_to_xml(nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);

	EXPECT_EQ(oaknode_serializer_load_from_xml(nullptr, nullptr, 1, &result,
											   nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_load_from_xml(nullptr, "<olive/>", 99,
											   &result, nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_load_from_xml(nullptr, "<olive/>", 1,
											   nullptr, nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);

	EXPECT_EQ(oaknode_serializer_loaddata_node_count(nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_loaddata_node_at(nullptr, 0), nullptr);
	EXPECT_EQ(oaknode_serializer_loaddata_get_property(nullptr, nullptr,
													   nullptr, nullptr, 0),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_loaddata_connection_count(nullptr),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_serializer_loaddata_connection_at(
				  nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr),
			  OAKNODE_E_INVALID);
}

TEST(NodeSerializer, Shutdown)
{
	// Keep the global state clean for any families sharing this binary.
	oaknode_serializer_shutdown();
}
