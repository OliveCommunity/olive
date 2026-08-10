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

#ifndef OAK_EDITOR_NODE_FACTORY_H
#define OAK_EDITOR_NODE_FACTORY_H

#include "node/error.h"
#include "node/node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file factory.h
 * @brief C ABI for olive::NodeFactory (src/node/src/factory.h): the
 * internal node-type library.
 *
 * The library must be populated with oaknode_factory_initialize() before
 * any other call; oaknode_factory_destroy() releases it. The factory is
 * a process-wide singleton (static olive::NodeFactory), so there is no
 * OakNodeFactory handle type. Prototype nodes from
 * oaknode_factory_node_at() are owned by the library: read-only metadata
 * queries only, never add them to a graph.
 */

/**
 * @brief Populate the internal node library (NodeFactory::initialize()).
 * Idempotent: calling twice is a no-op.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_factory_initialize(void);

/**
 * @brief Release the internal node library (NodeFactory::destroy()).
 * Safe when not initialized.
 */
void oaknode_factory_destroy(void);

/**
 * @brief Number of registered node types (the library size).
 * OAKNODE_E_STATE when not initialized.
 */
int oaknode_factory_id_count(int *out_count);

/**
 * @brief The type id of the registered node at `index`. Two-stage
 * getter; OAKNODE_E_NOT_FOUND for an out-of-range index,
 * OAKNODE_E_STATE when not initialized.
 */
int oaknode_factory_id_at(int index, char *buf, int buf_size);

/**
 * @brief The display name of the node type `type_id`
 * (NodeFactory::get_name_from_id()). Two-stage getter; an unknown id
 * yields an empty string (required size 1).
 */
int oaknode_factory_name_from_id(const char *type_id, char *buf,
								 int buf_size);

/**
 * @brief Create a node of `type_id` WITHOUT adding it to any graph
 * (NodeFactory::create_from_id()). The caller owns the returned node
 * (reference count 1) and must release it with oaknode_node_free() while
 * it is still orphaned. ctx is NULL when the id is unknown or not
 * initialized.
 */
OakNodeNode oaknode_factory_create_from_id(const char *type_id);

/**
 * @brief Borrow the prototype node at `index` in the library (non-owning
 * handle written to `out_node`; release it with oaknode_node_free()).
 * OAKNODE_E_NOT_FOUND for an out-of-range index, OAKNODE_E_STATE when
 * not initialized.
 */
int oaknode_factory_node_at(int index, OakNodeNode *out_node);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_FACTORY_H
