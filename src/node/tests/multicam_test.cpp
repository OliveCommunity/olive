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

#include "node/multicam.h"

#include <gtest/gtest.h>

#include "testnode.h"

#include "../src/input/multicam/multicamnode.h"

namespace
{

using oaknode_test::TestNode;
using oaknode_test::as_handle;

OakNodeNode multicam_handle(olive::MultiCamNode *node)
{
	return oaknode_c_api::make_handle<OakNodeNode>(
		node, false, &oaknode_c_api::delete_as<olive::Node>);
}

TEST(NodeMulticamTest, InputIdStrings)
{
	EXPECT_STREQ(oaknode_multicam_input_current(),
				 olive::MultiCamNode::k_current_input.c_str());
	EXPECT_STREQ(oaknode_multicam_input_sources(),
				 olive::MultiCamNode::k_sources_input.c_str());
	EXPECT_STREQ(oaknode_multicam_input_sequence(),
				 olive::MultiCamNode::k_sequence_input.c_str());
	EXPECT_STREQ(oaknode_multicam_input_sequence_type(),
				 olive::MultiCamNode::k_sequence_type_input.c_str());
	EXPECT_STREQ(oaknode_multicam_input_current(), "current_in");
	EXPECT_STREQ(oaknode_multicam_input_sources(), "sources_in");
	EXPECT_STREQ(oaknode_multicam_input_sequence(), "sequence_in");
	EXPECT_STREQ(oaknode_multicam_input_sequence_type(), "sequence_type_in");
}

TEST(NodeMulticamTest, SourceCount)
{
	olive::MultiCamNode node;
	OakNodeNode handle = multicam_handle(&node);

	// A fresh multicam has an empty sources array: 0 sources.
	int count = -1;
	EXPECT_EQ(oaknode_multicam_get_source_count(handle, &count), OAKNODE_OK);
	EXPECT_EQ(count, 0);

	// Growing the sources array grows the source count.
	oaknode_value v = oaknode_value();
	v.type = OAKNODE_VALUE_NONE;
	for (int i = 0; i < 3; i++) {
		EXPECT_EQ(oaknode_node_input_array_insert(handle, "sources_in", i),
				  OAKNODE_OK);
	}
	EXPECT_EQ(oaknode_multicam_get_source_count(handle, &count), OAKNODE_OK);
	EXPECT_EQ(count, 3);

	// Invalid arguments.
	EXPECT_EQ(oaknode_multicam_get_source_count(OakNodeNode{}, &count),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_get_source_count(handle, nullptr),
			  OAKNODE_E_INVALID);

	// A non-multicam node is invalid.
	TestNode other;
	EXPECT_EQ(oaknode_multicam_get_source_count(as_handle(&other), &count),
			  OAKNODE_E_INVALID);
}

TEST(NodeMulticamTest, RowsAndColumns)
{
	int rows = 0, cols = 0;

	// Edge cases: 0 and 1 sources both yield the 1x1 grid.
	EXPECT_EQ(oaknode_multicam_get_rows_and_columns(0, &rows, &cols),
			  OAKNODE_OK);
	EXPECT_EQ(rows, 1);
	EXPECT_EQ(cols, 1);

	EXPECT_EQ(oaknode_multicam_get_rows_and_columns(1, &rows, &cols),
			  OAKNODE_OK);
	EXPECT_EQ(rows, 1);
	EXPECT_EQ(cols, 1);

	// Non-square grids: the grid widens the smaller dimension first.
	struct GridCase {
		int sources;
		int rows;
		int cols;
	};
	const GridCase cases[] = {
		{ 2, 1, 2 }, { 3, 2, 2 }, { 4, 2, 2 }, { 5, 2, 3 },
		{ 6, 2, 3 }, { 7, 3, 3 }, { 8, 3, 3 }, { 9, 3, 3 },
		{ 10, 3, 4 },
	};
	for (const GridCase &c : cases) {
		EXPECT_EQ(oaknode_multicam_get_rows_and_columns(c.sources, &rows,
														&cols),
				  OAKNODE_OK);
		EXPECT_EQ(rows, c.rows);
		EXPECT_EQ(cols, c.cols);
	}

	// Invalid arguments.
	EXPECT_EQ(oaknode_multicam_get_rows_and_columns(-1, &rows, &cols),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_get_rows_and_columns(4, nullptr, &cols),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_get_rows_and_columns(4, &rows, nullptr),
			  OAKNODE_E_INVALID);
}

TEST(NodeMulticamTest, IndexToRowCols)
{
	int row = -1, col = -1;

	// Row-major layout in a 2x3 grid.
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 2, 3, &row, &col),
			  OAKNODE_OK);
	EXPECT_EQ(row, 0);
	EXPECT_EQ(col, 0);

	EXPECT_EQ(oaknode_multicam_index_to_row_cols(1, 2, 3, &row, &col),
			  OAKNODE_OK);
	EXPECT_EQ(row, 0);
	EXPECT_EQ(col, 1);

	EXPECT_EQ(oaknode_multicam_index_to_row_cols(3, 2, 3, &row, &col),
			  OAKNODE_OK);
	EXPECT_EQ(row, 1);
	EXPECT_EQ(col, 0);

	EXPECT_EQ(oaknode_multicam_index_to_row_cols(5, 2, 3, &row, &col),
			  OAKNODE_OK);
	EXPECT_EQ(row, 1);
	EXPECT_EQ(col, 2);

	// Single-cell grid.
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 1, 1, &row, &col),
			  OAKNODE_OK);
	EXPECT_EQ(row, 0);
	EXPECT_EQ(col, 0);

	// Invalid arguments.
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(-1, 2, 3, &row, &col),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 0, 3, &row, &col),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 2, 0, &row, &col),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 2, 3, nullptr, &col),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_index_to_row_cols(0, 2, 3, &row, nullptr),
			  OAKNODE_E_INVALID);
}

TEST(NodeMulticamTest, RowsColsToIndex)
{
	// Round-trip with index_to_row_cols over a 2x3 grid.
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, 0, 2, 3), 0);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, 2, 2, 3), 2);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(1, 0, 2, 3), 3);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(1, 2, 2, 3), 5);

	// Invalid arguments (negative or out-of-range cell, degenerate grid).
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(-1, 0, 2, 3),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, -1, 2, 3),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(2, 0, 2, 3),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, 3, 2, 3),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, 0, 0, 3),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_rows_cols_to_index(0, 0, 2, 0),
			  OAKNODE_E_INVALID);
}

TEST(NodeMulticamTest, CurrentSource)
{
	olive::MultiCamNode node;
	OakNodeNode handle = multicam_handle(&node);

	// The default "current_in" combo value is 0.
	int source = -1;
	EXPECT_EQ(oaknode_multicam_get_current_source(handle, &source),
			  OAKNODE_OK);
	EXPECT_EQ(source, 0);

	// Switching the combo changes the reported source.
	oaknode_value v = oaknode_value();
	v.type = OAKNODE_VALUE_COMBO;
	v.num = 2;
	EXPECT_EQ(oaknode_node_set_input(handle, "current_in", &v), OAKNODE_OK);
	EXPECT_EQ(oaknode_multicam_get_current_source(handle, &source),
			  OAKNODE_OK);
	EXPECT_EQ(source, 2);

	// Invalid arguments / non-multicam node.
	EXPECT_EQ(oaknode_multicam_get_current_source(OakNodeNode{}, &source),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_multicam_get_current_source(handle, nullptr),
			  OAKNODE_E_INVALID);
	TestNode other;
	EXPECT_EQ(oaknode_multicam_get_current_source(as_handle(&other), &source),
			  OAKNODE_E_INVALID);
}

} // namespace
