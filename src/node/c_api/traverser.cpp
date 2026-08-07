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

#include "nodehandle.h"
#include "valueconvert.h"

using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

namespace
{

/**
 * @brief Find the table named `key`, or NULL when absent.
 */
const olive::NodeValueTable *find_table(const olive::NodeValueDatabase *db,
										const char *key)
{
	for (auto it = db->cbegin(); it != db->cend(); ++it) {
		if (it->first == key) {
			return &it->second;
		}
	}
	return nullptr;
}

}

OakNodeTraverser oaknode_traverser_init(void)
{
	try {
		return make_handle<OakNodeTraverser>(
			new (std::nothrow) olive::NodeTraverser(), true,
			oaknode_c_api::delete_as<olive::NodeTraverser>);
	} catch (...) {
		return OakNodeTraverser{};
	}
}

void oaknode_traverser_free(OakNodeTraverser *traverser)
{
	try {
		oaknode_c_api::free_handle(traverser);
	} catch (...) {
	}
}

int oaknode_traverser_generate_database(OakNodeTraverser traverser,
										OakNodeNode node, int64_t in_num,
										int64_t in_den, int64_t out_num,
										int64_t out_den,
										OakNodeValueDatabase *out_db)
{
	olive::NodeTraverser *t = to_native<olive::NodeTraverser>(traverser);
	olive::Node *n = to_native<olive::Node>(node);
	if (!t || !n || !out_db) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::core::Rational in(static_cast<int>(in_num),
								 static_cast<int>(in_den));
		olive::core::Rational out(static_cast<int>(out_num),
								  static_cast<int>(out_den));

		olive::NodeValueDatabase *db =
			new (std::nothrow) olive::NodeValueDatabase();
		if (!db) {
			return OAKNODE_E_NOMEM;
		}

		*db = t->generate_database(n, olive::core::TimeRange(in, out));

		*out_db = make_handle<OakNodeValueDatabase>(
			db, true, oaknode_c_api::delete_as<olive::NodeValueDatabase>);
		if (!out_db->ctx) {
			return OAKNODE_E_NOMEM;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_traverser_database_free(OakNodeValueDatabase *db)
{
	try {
		oaknode_c_api::free_handle(db);
	} catch (...) {
	}
}

int oaknode_traverser_database_row_count(OakNodeValueDatabase db,
										 int *out_count)
{
	const olive::NodeValueDatabase *impl =
		to_native<olive::NodeValueDatabase>(db);
	if (!impl || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		int count = 0;
		for (auto it = impl->cbegin(); it != impl->cend(); ++it) {
			count++;
		}
		*out_count = count;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_row_key_at(OakNodeValueDatabase db,
										  int index, char *buf, int buf_size)
{
	const olive::NodeValueDatabase *impl =
		to_native<olive::NodeValueDatabase>(db);
	if (!impl) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (index < 0) {
			return OAKNODE_E_NOT_FOUND;
		}
		auto it = impl->cbegin();
		for (int i = 0; i < index && it != impl->cend(); i++, ++it) {
		}
		if (it == impl->cend()) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(it->first, buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_row_value_count(OakNodeValueDatabase db,
											   const char *key,
											   int *out_count)
{
	const olive::NodeValueDatabase *impl =
		to_native<olive::NodeValueDatabase>(db);
	if (!impl || !key || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(impl, key);
		if (!table) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_count = table->count();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_traverser_database_value_at(OakNodeValueDatabase db,
										const char *key, int index,
										oaknode_value *out)
{
	const olive::NodeValueDatabase *impl =
		to_native<olive::NodeValueDatabase>(db);
	if (!impl || !key || !out) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(impl, key);
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

int oaknode_traverser_database_value_string_at(OakNodeValueDatabase db,
											   const char *key, int index,
											   char *buf, int buf_size)
{
	const olive::NodeValueDatabase *impl =
		to_native<olive::NodeValueDatabase>(db);
	if (!impl || !key) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeValueTable *table = find_table(impl, key);
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
