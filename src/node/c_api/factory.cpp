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

#include "node/factory.h"

#include "factory.h"

#include "valueconvert.h"

namespace
{

inline OakNodeNode *from_node(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

}

int oaknode_factory_initialize(void)
{
	try {
		if (olive::NodeFactory::get_library().empty()) {
			olive::NodeFactory::initialize();
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_factory_destroy(void)
{
	try {
		olive::NodeFactory::destroy();
	} catch (...) {
	}
}

int oaknode_factory_id_count(int *out_count)
{
	if (!out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (olive::NodeFactory::get_library().empty()) {
			return OAKNODE_E_STATE;
		}
		*out_count = static_cast<int>(olive::NodeFactory::get_library().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_factory_id_at(int index, char *buf, int buf_size)
{
	try {
		const std::vector<olive::Node *> &library =
			olive::NodeFactory::get_library();
		if (library.empty()) {
			return OAKNODE_E_STATE;
		}
		if (index < 0 || index >= static_cast<int>(library.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(library[size_t(index)]->id(), buf,
										  buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_factory_name_from_id(const char *type_id, char *buf,
								 int buf_size)
{
	if (!type_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(
			olive::NodeFactory::get_name_from_id(type_id), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode *oaknode_factory_create_from_id(const char *type_id)
{
	if (!type_id) {
		return NULL;
	}

	try {
		olive::Node *node = olive::NodeFactory::create_from_id(type_id);
		if (node) {
			oaknode_c_api::alive_inc();
		}
		return from_node(node);
	} catch (...) {
		return NULL;
	}
}

int oaknode_factory_node_at(int index, OakNodeNode **out_node)
{
	if (!out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const std::vector<olive::Node *> &library =
			olive::NodeFactory::get_library();
		if (library.empty()) {
			return OAKNODE_E_STATE;
		}
		if (index < 0 || index >= static_cast<int>(library.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_node = from_node(library[size_t(index)]);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
