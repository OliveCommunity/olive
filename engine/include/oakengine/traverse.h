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

#ifndef OAKENGINE_TRAVERSE_H
#define OAKENGINE_TRAVERSE_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "node.h"
#include "videoparams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file traverse.h
 * @brief C ABI for synchronous node-graph value evaluation
 * (olive::NodeTraverser)
 *
 * The engine evaluates a node's value at a given time by traversing its
 * input graph (olive::NodeTraverser). The application uses this in three
 * places: the node table view (per-input value databases), the node value
 * tree (one output table + the element a downstream input's value hint
 * selects) and the viewer display gizmos (a transform between two nodes
 * and the gizmo node's input row at drag start). This family exposes
 * those paths without leaking NodeValueTable/NodeValueRow C++ types.
 *
 * All evaluation is SYNCHRONOUS on the calling thread and CPU-only in
 * this family (textures are resolved as engine-side dummy textures, which
 * is exactly what the table/tree views need -- they only read metadata).
 * Call from the GUI thread.
 *
 * OakEngineTraverseDb is an OWNED result object; free it with
 * oakengine_traverse_db_free(). Strings it returns point into the object
 * and are valid until freed. Times are Rational seconds as int64
 * numerator/denominator pairs, like the rest of the facade.
 */

typedef struct OakEngineTraverseDb OakEngineTraverseDb;

/**
 * @brief Evaluate every input of `node` over [in_num/in_den,
 * out_num/out_den] seconds and return the per-input value database
 * (NodeTraverser::generate_database()). NULL on invalid arguments.
 */
OAKENGINE_API OakEngineTraverseDb *oakengine_traverse_generate_database(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den);

/**
 * @brief Evaluate the single output table of `node`
 * (NodeTraverser::generate_table()). Returned as a database with exactly
 * one entry whose input id is an empty string.
 */
OAKENGINE_API OakEngineTraverseDb *oakengine_traverse_generate_table(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den);

/** @brief Free a database returned by this family. NULL is a no-op. */
OAKENGINE_API void oakengine_traverse_db_free(OakEngineTraverseDb *db);

/** @brief Number of input entries (generate_database: one per input id
 * that produced a table; generate_table: exactly 1). */
OAKENGINE_API int oakengine_traverse_db_input_count(
	const OakEngineTraverseDb *db);

/** @brief Input id of entry `input_index` (valid until db is freed). */
OAKENGINE_API const char *oakengine_traverse_db_input_id(
	const OakEngineTraverseDb *db, int input_index);

/** @brief Row count of the table at `input_index`
 * (NodeValueTable::count()). */
OAKENGINE_API int oakengine_traverse_db_row_count(
	const OakEngineTraverseDb *db, int input_index);

/**
 * @brief Row accessors. `row` is 0-based in table order (the views
 * reverse it themselves where needed). Strings are valid until db is
 * freed.
 *
 * - type: oak_node_value_type of the value.
 * - source: borrowed node that produced the value, or NULL.
 * - tag: the value's tag (may be empty, never NULL).
 * - value_string: NodeValue::value_to_string(value, false).
 * - split_count / split_string: NodeValue::to_split_value() count and
 *   NodeValue::value_to_string(type, split[k], true) per element.
 */
OAKENGINE_API int oakengine_traverse_row_type(const OakEngineTraverseDb *db,
											  int input_index, int row);
OAKENGINE_API OakEngineNode *oakengine_traverse_row_source(
	const OakEngineTraverseDb *db, int input_index, int row);
OAKENGINE_API const char *oakengine_traverse_row_tag(
	const OakEngineTraverseDb *db, int input_index, int row);
OAKENGINE_API const char *oakengine_traverse_row_value_string(
	const OakEngineTraverseDb *db, int input_index, int row);
OAKENGINE_API int oakengine_traverse_row_split_count(
	const OakEngineTraverseDb *db, int input_index, int row);
OAKENGINE_API const char *oakengine_traverse_row_split_string(
	const OakEngineTraverseDb *db, int input_index, int row, int split);

/**
 * @brief The table element selected by `hint_node`'s value hint for input
 * `input_id`@`element` against a table produced by
 * oakengine_traverse_generate_table() (pass its db; must contain exactly
 * one entry) -- NodeTraverser::generate_row_value_element_index().
 * Returns -1 when no element matches.
 */
OAKENGINE_API int oakengine_traverse_table_element_index_for_hint(
	OakEngineNode *hint_node, const char *input_id, int element,
	const OakEngineTraverseDb *table_db);

/**
 * @brief Fill a caller-allocated olive::NodeValueRow with `node`'s input
 * values over the given range (NodeTraverser::generate_row()) -- the
 * viewer display gizmo drag-start path. `cache_video_params` (may be NULL
 * for engine defaults) and `sample_rate`/`channel_layout` seed the
 * traverser's cache params (NodeTraverser::set_cache_video_params /
 * set_cache_audio_params).
 *
 * Transition bridge (same pattern as
 * replaced by internal ColorTransformJob API): `row_out` is opaque to C
 * consumers; the application passes a pointer to its own
 * olive::NodeValueRow (a QHash typedef, no engine symbols) which the
 * engine fills in place.
 */
OAKENGINE_API int oakengine_traverse_generate_row(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den, const oak_video_params *cache_video_params,
	int sample_rate, uint64_t channel_layout, void *row_out);

/**
 * @brief Accumulate the transform from `start` to `end` over the given
 * range (NodeTraverser::transform()) and return it as the 6 affine
 * coefficients of a QTransform (m11, m12, m21, m22, dx, dy), suitable for
 * `QTransform(m11, m12, m21, m22, dx, dy)`. `cache_video_params` may be
 * NULL for engine defaults.
 */
OAKENGINE_API int oakengine_traverse_transform(
	OakEngineNode *start, OakEngineNode *end, int64_t in_num, int64_t in_den,
	int64_t out_num, int64_t out_den, const oak_video_params *cache_video_params,
	double out_m[6]);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_TRAVERSE_H */
