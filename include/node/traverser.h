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

#ifndef OAK_EDITOR_NODE_TRAVERSER_H
#define OAK_EDITOR_NODE_TRAVERSER_H

#include <stdint.h>

#include "node/error.h"
#include "node/node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file traverser.h
 * @brief C ABI for olive::NodeTraverser (src/node/src/traverser.h),
 * limited to database generation: generating the value database of a node
 * over a time range and enumerating its rows.
 *
 * The base NodeTraverser resolves no render jobs (textures/samples stay
 * dummy); only value-producing nodes are meaningful here.
 */

/**
 * @brief Opaque traverser handle (olive::NodeTraverser).
 */
typedef struct OakNodeTraverser OakNodeTraverser;

/**
 * @brief Opaque value-database handle (an owned copy of an
 * olive::NodeValueDatabase). Release with
 * oaknode_traverser_database_free().
 */
typedef struct OakNodeValueDatabase OakNodeValueDatabase;

/**
 * @brief Create a traverser.
 *
 * @return Traverser handle, or NULL on allocation failure.
 */
OakNodeTraverser *oaknode_traverser_init(void);

/**
 * @brief Destroy a traverser. NULL is a no-op.
 */
void oaknode_traverser_free(OakNodeTraverser *traverser);

/**
 * @brief Generate the value database of `node` over the time range
 * [`in_num`/`in_den`, `out_num`/`out_den`) seconds
 * (NodeTraverser::generate_database()).
 *
 * `out_db` receives an owned database handle.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_traverser_generate_database(OakNodeTraverser *traverser,
										OakNodeNode *node, int64_t in_num,
										int64_t in_den, int64_t out_num,
										int64_t out_den,
										OakNodeValueDatabase **out_db);

/**
 * @brief Destroy a database handle. NULL is a no-op.
 */
void oaknode_traverser_database_free(OakNodeValueDatabase *db);

/**
 * @brief Number of rows (input tables) in the database.
 */
int oaknode_traverser_database_row_count(const OakNodeValueDatabase *db,
										 int *out_count);

/**
 * @brief The input id (key) of the row at `index`. Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_traverser_database_row_key_at(const OakNodeValueDatabase *db,
										  int index, char *buf, int buf_size);

/**
 * @brief Number of values in the row named `key`.
 * OAKNODE_E_NOT_FOUND for an unknown key.
 */
int oaknode_traverser_database_row_value_count(const OakNodeValueDatabase *db,
											   const char *key,
											   int *out_count);

/**
 * @brief Read the value at `index` of row `key` mapped into `out`.
 * Values without a POD representation fail with OAKNODE_E_FAILED;
 * OAKNODE_E_NOT_FOUND for an unknown key or out-of-range index.
 */
int oaknode_traverser_database_value_at(const OakNodeValueDatabase *db,
										const char *key, int index,
										oaknode_value *out);

/**
 * @brief Read the value at `index` of row `key` as a string
 * (NodeValue::value_to_string()). Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an unknown key or out-of-range index.
 */
int oaknode_traverser_database_value_string_at(const OakNodeValueDatabase *db,
											   const char *key, int index,
											   char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_TRAVERSER_H
