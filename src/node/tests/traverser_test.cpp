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

#include "node/traverser.h"

#include <gtest/gtest.h>

#include <cstring>

#include "testnode.h"

namespace
{

using oaknode_test::TestNode;
using oaknode_test::as_handle;

TEST(NodeTraverserTest, InitFree)
{
	int alive_before = oaknode_debug_alive_count();

	OakNodeTraverser traverser = oaknode_traverser_init();
	ASSERT_NE(traverser.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	oaknode_traverser_free(&traverser);
	EXPECT_EQ(traverser.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);

	oaknode_traverser_free(nullptr); // no crash
}

TEST(NodeTraverserTest, GenerateDatabaseAndEnumerate)
{
	TestNode node;
	oaknode_value in = oaknode_value();
	in.type = OAKNODE_VALUE_FLOAT;
	in.f[0] = 4.5;
	ASSERT_EQ(oaknode_node_set_input(as_handle(&node), "float_in", &in),
			  OAKNODE_OK);

	OakNodeTraverser traverser = oaknode_traverser_init();
	ASSERT_NE(traverser.ctx, nullptr);

	int alive_before = oaknode_debug_alive_count();

	OakNodeValueDatabase db = {};
	EXPECT_EQ(oaknode_traverser_generate_database(traverser, as_handle(&node),
												  0, 1, 1, 1, &db),
			  OAKNODE_OK);
	ASSERT_NE(db.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before + 1);

	int rows = 0;
	EXPECT_EQ(oaknode_traverser_database_row_count(db, &rows), OAKNODE_OK);
	EXPECT_GT(rows, 0);

	// Find the float_in row and read its value back.
	char key[64];
	int found = 0;
	for (int i = 0; i < rows; i++) {
		ASSERT_GT(oaknode_traverser_database_row_key_at(db, i, key,
														sizeof(key)),
				  1);
		if (std::strcmp(key, "float_in") == 0) {
			found = 1;

			int values = 0;
			EXPECT_EQ(oaknode_traverser_database_row_value_count(db, key,
																 &values),
					  OAKNODE_OK);
			ASSERT_GE(values, 1);

			oaknode_value out;
			EXPECT_EQ(oaknode_traverser_database_value_at(db, key, 0, &out),
					  OAKNODE_OK);
			EXPECT_EQ(out.type, OAKNODE_VALUE_FLOAT);
			EXPECT_DOUBLE_EQ(out.f[0], 4.5);

			char text[64];
			EXPECT_GT(oaknode_traverser_database_value_string_at(
						  db, key, 0, text, sizeof(text)),
					  1);

			EXPECT_EQ(oaknode_traverser_database_value_at(db, key, values,
														  &out),
					  OAKNODE_E_NOT_FOUND);
		}
	}
	EXPECT_EQ(found, 1);

	// Error paths.
	EXPECT_EQ(oaknode_traverser_database_row_key_at(db, rows, key,
													sizeof(key)),
			  OAKNODE_E_NOT_FOUND);
	int values = 0;
	EXPECT_EQ(oaknode_traverser_database_row_value_count(db, "nope", &values),
			  OAKNODE_E_NOT_FOUND);
	oaknode_value out;
	EXPECT_EQ(oaknode_traverser_database_value_at(db, "nope", 0, &out),
			  OAKNODE_E_NOT_FOUND);
	EXPECT_EQ(oaknode_traverser_database_value_at(OakNodeValueDatabase{},
												  "float_in", 0, &out),
			  OAKNODE_E_INVALID);

	oaknode_traverser_database_free(&db);
	EXPECT_EQ(db.ctx, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), alive_before);
	oaknode_traverser_database_free(nullptr); // no crash

	oaknode_traverser_free(&traverser);
}

TEST(NodeTraverserTest, InvalidArguments)
{
	OakNodeValueDatabase db = {};
	EXPECT_EQ(oaknode_traverser_generate_database(OakNodeTraverser{},
												  OakNodeNode{}, 0, 1, 1, 1,
												  &db),
			  OAKNODE_E_INVALID);

	OakNodeTraverser traverser = oaknode_traverser_init();
	ASSERT_NE(traverser.ctx, nullptr);
	EXPECT_EQ(oaknode_traverser_generate_database(traverser, OakNodeNode{}, 0,
												  1, 1, 1, &db),
			  OAKNODE_E_INVALID);

	TestNode node;
	EXPECT_EQ(oaknode_traverser_generate_database(traverser, as_handle(&node),
												  0, 1, 1, 1, nullptr),
			  OAKNODE_E_INVALID);

	EXPECT_EQ(oaknode_traverser_database_row_count(OakNodeValueDatabase{},
												   nullptr),
			  OAKNODE_E_INVALID);

	oaknode_traverser_free(&traverser);
}

}
