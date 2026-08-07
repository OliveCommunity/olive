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
 * @brief Reference-counted handle to a traverser (olive::NodeTraverser).
 *
 * Semantics are shared_ptr-like: oaknode_traverser_init() returns a
 * handle with count 1, addref(ctx) takes another reference, release(ctx)
 * drops one and the library destroys the object when the count reaches
 * zero.
 */
typedef struct OakNodeTraverser {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeTraverser;

/**
 * @brief Reference-counted handle to an owned copy of an
 * olive::NodeValueDatabase. Same reference-counting rules as
 * OakNodeTraverser; release with oaknode_traverser_database_free().
 */
typedef struct OakNodeValueDatabase {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeValueDatabase;

/**
 * @brief Create a traverser.
 *
 * @return Traverser handle with count 1; ctx is NULL on allocation
 *         failure.
 */
OakNodeTraverser oaknode_traverser_init(void);

/**
 * @brief Release one reference to a traverser handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * traverser when the count reaches zero. NULL handle or NULL ctx is a
 * no-op; clears `traverser->ctx` after releasing.
 */
void oaknode_traverser_free(OakNodeTraverser *traverser);

/**
 * @brief Generate the value database of `node` over the time range
 * [`in_num`/`in_den`, `out_num`/`out_den`) seconds
 * (NodeTraverser::generate_database()).
 *
 * `out_db` receives an owned database handle with count 1.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_traverser_generate_database(OakNodeTraverser traverser,
										OakNodeNode node, int64_t in_num,
										int64_t in_den, int64_t out_num,
										int64_t out_den,
										OakNodeValueDatabase *out_db);

/**
 * @brief Release one reference to a database handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * database when the count reaches zero. NULL handle or NULL ctx is a
 * no-op; clears `db->ctx` after releasing.
 */
void oaknode_traverser_database_free(OakNodeValueDatabase *db);

/**
 * @brief Number of rows (input tables) in the database.
 */
int oaknode_traverser_database_row_count(OakNodeValueDatabase db,
										 int *out_count);

/**
 * @brief The input id (key) of the row at `index`. Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_traverser_database_row_key_at(OakNodeValueDatabase db,
										  int index, char *buf, int buf_size);

/**
 * @brief Number of values in the row named `key`.
 * OAKNODE_E_NOT_FOUND for an unknown key.
 */
int oaknode_traverser_database_row_value_count(OakNodeValueDatabase db,
											   const char *key,
											   int *out_count);

/**
 * @brief Read the value at `index` of row `key` mapped into `out`.
 * Values without a POD representation fail with OAKNODE_E_FAILED;
 * OAKNODE_E_NOT_FOUND for an unknown key or out-of-range index.
 */
int oaknode_traverser_database_value_at(OakNodeValueDatabase db,
										const char *key, int index,
										oaknode_value *out);

/**
 * @brief Read the value at `index` of row `key` as a string
 * (NodeValue::value_to_string()). Two-stage getter;
 * OAKNODE_E_NOT_FOUND for an unknown key or out-of-range index.
 */
int oaknode_traverser_database_value_string_at(OakNodeValueDatabase db,
											   const char *key, int index,
											   char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_TRAVERSER_H
