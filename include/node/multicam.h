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

#ifndef OAK_EDITOR_NODE_MULTICAM_H
#define OAK_EDITOR_NODE_MULTICAM_H

#include <stdint.h>

#include "node/error.h"
#include "node/node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file multicam.h
 * @brief C ABI for olive::MultiCamNode (src/node/src/input/multicam/
 * multicamnode.h): multi-camera source switching and the source-grid
 * math used by the multicam viewer.
 *
 * The input-id getters return static strings (never freed) naming the
 * multicam node's inputs: current source (combo), sources (array),
 * sequence and sequence type. A node that is not a MultiCamNode (or a
 * NULL handle) fails the per-node queries with OAKNODE_E_INVALID.
 *
 * The grid helpers are static and pure: they only depend on their
 * arguments, not on a node.
 */

/**
 * @brief The input id string for the current camera ("current_in").
 */
const char *oaknode_multicam_input_current(void);

/**
 * @brief The input id string for the sources array ("sources_in").
 */
const char *oaknode_multicam_input_sources(void);

/**
 * @brief The input id string for the sequence ("sequence_in").
 */
const char *oaknode_multicam_input_sequence(void);

/**
 * @brief The input id string for the sequence type ("sequence_type_in").
 */
const char *oaknode_multicam_input_sequence_type(void);

/**
 * @brief Number of connected source cameras (MultiCamNode::
 * get_source_count(); the connected sequence's track count, or the
 * sources array size when no sequence is connected).
 *
 * OAKNODE_E_INVALID when `node` is not a multicam.
 */
int oaknode_multicam_get_source_count(OakNodeNode node, int *out_count);

/**
 * @brief Compute the grid (rows, cols) that holds `source_count` cells.
 *
 * Mirrors MultiCamNode::get_rows_and_columns(): the grid grows from
 * 1x1, widening the smaller dimension, until rows * cols >= source_count
 * (0 sources yields 1x1). OAKNODE_E_INVALID for a negative count or
 * NULL out pointers.
 */
int oaknode_multicam_get_rows_and_columns(int source_count, int *rows,
										   int *cols);

/**
 * @brief Convert a flat source index to (row, col) in a rows x cols grid
 * (row-major: col = index % cols, row = index / cols).
 *
 * OAKNODE_E_INVALID for a negative index, degenerate grid or NULL out
 * pointers.
 */
int oaknode_multicam_index_to_row_cols(int index, int rows, int cols,
									   int *out_row, int *out_col);

/**
 * @brief Convert (row, col) to a flat source index (col + row * cols).
 *
 * @return The flat index (>= 0), or OAKNODE_E_INVALID when the cell is
 *         out of range or the grid is degenerate.
 */
int oaknode_multicam_rows_cols_to_index(int row, int col, int rows,
										int cols);

/**
 * @brief The current source index (MultiCamNode::get_current_source(),
 * the "current_in" combo value).
 *
 * OAKNODE_E_INVALID when `node` is not a multicam.
 */
int oaknode_multicam_get_current_source(OakNodeNode node, int *out_source);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_MULTICAM_H
