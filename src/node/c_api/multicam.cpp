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

#include "node/node.h"

#include "nodehandle.h"

#include "input/multicam/multicamnode.h"

using oaknode_c_api::to_native;

const char *oaknode_multicam_input_current(void)
{
	return olive::MultiCamNode::k_current_input.c_str();
}

const char *oaknode_multicam_input_sources(void)
{
	return olive::MultiCamNode::k_sources_input.c_str();
}

const char *oaknode_multicam_input_sequence(void)
{
	return olive::MultiCamNode::k_sequence_input.c_str();
}

const char *oaknode_multicam_input_sequence_type(void)
{
	return olive::MultiCamNode::k_sequence_type_input.c_str();
}

int oaknode_multicam_get_source_count(OakNodeNode node, int *out_count)
{
	olive::Node *n = to_native<olive::Node>(node);
	auto *m = dynamic_cast<olive::MultiCamNode *>(n);
	if (!m || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = m->get_source_count();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_multicam_get_rows_and_columns(int source_count, int *rows,
										  int *cols)
{
	if (source_count < 0 || !rows || !cols) {
		return OAKNODE_E_INVALID;
	}

	olive::MultiCamNode::get_rows_and_columns(source_count, rows, cols);
	return OAKNODE_OK;
}

int oaknode_multicam_index_to_row_cols(int index, int rows, int cols,
									   int *out_row, int *out_col)
{
	if (index < 0 || rows < 1 || cols < 1 || !out_row || !out_col) {
		return OAKNODE_E_INVALID;
	}

	olive::MultiCamNode::index_to_row_cols(index, rows, cols, out_row,
										   out_col);
	return OAKNODE_OK;
}

int oaknode_multicam_rows_cols_to_index(int row, int col, int rows, int cols)
{
	if (row < 0 || col < 0 || rows < 1 || cols < 1 || row >= rows ||
		col >= cols) {
		return OAKNODE_E_INVALID;
	}

	return olive::MultiCamNode::rows_cols_to_index(row, col, rows, cols);
}

int oaknode_multicam_get_current_source(OakNodeNode node, int *out_source)
{
	olive::Node *n = to_native<olive::Node>(node);
	auto *m = dynamic_cast<olive::MultiCamNode *>(n);
	if (!m || !out_source) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_source = m->get_current_source();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
