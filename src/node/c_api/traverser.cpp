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

#include <new>

#include "traverser.h"
#include "valuedatabase.h"

#include "valueconvert.h"

struct OakNodeValueDatabase {
	olive::NodeValueDatabase impl;
};

namespace
{

inline olive::NodeTraverser *to_traverser(OakNodeTraverser *traverser)
{
	return reinterpret_cast<olive::NodeTraverser *>(traverser);
}

inline olive::Node *to_node(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

/**
 * @brief Find the table named `key`, or NULL when absent.
 */
const olive::NodeValueTable *find_table(const OakNodeValueDatabase *db,
										const char *key)
{
	for (auto it = db->impl.cbegin(); it != db->impl.cend(); ++it) {
		if (it->first == key) {
			return &it->second;
		}
	}
	return nullptr;
}

}

OakNodeTraverser *oaknode_traverser_init(void)
{
	try {
		olive::NodeTraverser *traverser = new (std::nothrow) olive::NodeTraverser();
		if (traverser) {
			oaknode_c_api::alive_inc();
		}
		return reinterpret_cast<OakNodeTraverser *>(traverser);
	} catch (...) {
		return NULL;
	}
}

void oaknode_traverser_free(OakNodeTraverser *traverser)
{
	if (!traverser) {
		return;
	}

	try {
		delete to_traverser(traverser);
		oaknode_c_api::alive_dec();
	} catch (...) {
	}
}

int oaknode_traverser_generate_database(OakNodeTraverser *traverser,
										OakNodeNode *node, int64_t in_num,
										int64_t in_den, int64_t out_num,
										int64_t out_den,
										OakNodeValueDatabase **out_db)
{
	if (!traverser || !node || !out_db) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::core::Rational in(static_cast<int>(in_num),
								 static_cast<int>(in_den));
		olive::core::Rational out(static_cast<int>(out_num),
								  static_cast<int>(out_den));

		OakNodeValueDatabase *db = new (std::nothrow) OakNodeValueDatabase();
		if (!db) {
			return OAKNODE_E_NOMEM;
		}

		db->impl = to_traverser(traverser)->generate_database(
			to_node(node), olive::core::TimeRange(in, out));

		*out_db = db;
		oaknode_c_api::alive_inc();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_traverser_database_free(OakNodeValueDatabase *db)
{
	if (!db) {
		return;
	}

	delete db;
	oaknode_c_api::alive_dec();
}

int oaknode_traverser_database_row_count(const OakNodeValueDatabase *db,
										 int *out_count)
{
	if (!db || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		int count = 0;
		for (auto it = db->impl.cbegin(); it != db->impl.cend(); ++it) {
			count++;
		}
		*out_count = count;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_row_key_at(const OakNodeValueDatabase *db,
										  int index, char *buf, int buf_size)
{
	if (!db) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (index < 0) {
			return OAKNODE_E_NOT_FOUND;
		}
		auto it = db->impl.cbegin();
		for (int i = 0; i < index && it != db->impl.cend(); i++, ++it) {
		}
		if (it == db->impl.cend()) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(it->first, buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_row_value_count(const OakNodeValueDatabase *db,
											   const char *key,
											   int *out_count)
{
	if (!db || !key || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(db, key);
		if (!table) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_count = table->count();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_value_at(const OakNodeValueDatabase *db,
										const char *key, int index,
										oaknode_value *out)
{
	if (!db || !key || !out) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(db, key);
		if (!table || index < 0 || index >= table->count()) {
			return OAKNODE_E_NOT_FOUND;
		}

		const olive::NodeValue &value = table->at(index);
		return oaknode_c_api::value_from_variant(value.type(), value.data(),
												 out);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_value_string_at(const OakNodeValueDatabase *db,
											   const char *key, int index,
											   char *buf, int buf_size)
{
	if (!db || !key) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(db, key);
		if (!table || index < 0 || index >= table->count()) {
			return OAKNODE_E_NOT_FOUND;
		}

		const olive::NodeValue &value = table->at(index);
		return oaknode_c_api::copy_string(
			olive::NodeValue::value_to_string(value.type(), value.data(), false),
			buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
