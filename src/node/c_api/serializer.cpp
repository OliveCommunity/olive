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

#include "node/serializer.h"

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "../src/factory.h"
#include "../src/project.h"
#include "../src/project/serializer/serializer.h"
#include "xmlutils.h"

struct OakNodeSerializerSaveData {
	olive::ProjectSerializer::SaveData impl;

	OakNodeSerializerSaveData(olive::ProjectSerializer::LoadType type,
							  olive::Project *project)
		: impl(type, project)
	{
	}
};

struct OakNodeSerializerLoadData {
	olive::ProjectSerializer::LoadData impl;
};

namespace
{

bool g_initialized = false;

olive::Project *to_cpp(OakNodeProject *project)
{
	return reinterpret_cast<olive::Project *>(project);
}

olive::Node *to_cpp(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

OakNodeNode *to_c(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

bool is_valid_load_type(int load_type)
{
	return load_type >= static_cast<int>(olive::ProjectSerializer::k_project) &&
		   load_type <=
			   static_cast<int>(olive::ProjectSerializer::k_only_keyframes);
}

/**
 * @brief Shared two-stage string getter.
 *
 * Returns the required buffer size in bytes (including the terminating
 * NUL) as a non-negative value.
 */
int copy_string(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;

	if (buf && buf_size > 0) {
		size_t copy_len = value.size();
		if (copy_len > static_cast<size_t>(buf_size) - 1) {
			copy_len = static_cast<size_t>(buf_size) - 1;
		}
		memcpy(buf, value.data(), copy_len);
		buf[copy_len] = '\0';
	}

	return required;
}

} // namespace

int oaknode_serializer_initialize(void)
{
	if (g_initialized) {
		return OAKNODE_OK;
	}

	try {
		// The loaders instantiate nodes by id through the factory.
		olive::NodeFactory::initialize();
		olive::ProjectSerializer::initialize();
		g_initialized = true;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_serializer_shutdown(void)
{
	if (!g_initialized) {
		return;
	}

	try {
		olive::ProjectSerializer::destroy();
		olive::NodeFactory::destroy();
	} catch (...) {
	}
	g_initialized = false;
}

OakNodeSerializerSaveData *oaknode_serializer_savedata_create(
	int load_type, OakNodeProject *project)
{
	if (!is_valid_load_type(load_type)) {
		return NULL;
	}

	try {
		return new (std::nothrow) OakNodeSerializerSaveData(
			static_cast<olive::ProjectSerializer::LoadType>(load_type),
			to_cpp(project));
	} catch (...) {
		return NULL;
	}
}

void oaknode_serializer_savedata_free(OakNodeSerializerSaveData *save_data)
{
	delete save_data;
}

int oaknode_serializer_savedata_set_nodes(
	OakNodeSerializerSaveData *save_data, OakNodeNode *const *nodes, int count)
{
	if (!save_data || !nodes || count < 0) {
		return OAKNODE_E_INVALID;
	}

	try {
		std::vector<olive::Node *> cpp_nodes;
		cpp_nodes.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; i++) {
			if (!nodes[i]) {
				return OAKNODE_E_INVALID;
			}
			cpp_nodes.push_back(to_cpp(nodes[i]));
		}

		save_data->impl.set_only_serialize_nodes(cpp_nodes);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_savedata_set_property(
	OakNodeSerializerSaveData *save_data, OakNodeNode *node, const char *key,
	const char *value)
{
	if (!save_data || !node || !key || !value) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::ProjectSerializer::SerializedProperties properties =
			save_data->impl.get_properties();
		properties[to_cpp(node)][key] = value;
		save_data->impl.set_properties(properties);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_save_to_xml(OakNodeSerializerSaveData *save_data,
								   char *buf, int buf_size)
{
	if (!save_data) {
		return OAKNODE_E_INVALID;
	}
	if (!g_initialized) {
		return OAKNODE_E_STATE;
	}

	try {
		olive::XmlStreamWriter writer;
		olive::ProjectSerializer::Result result =
			olive::ProjectSerializer::save(&writer, save_data->impl);
		if (result != olive::ProjectSerializer::k_success) {
			return OAKNODE_E_FAILED;
		}
		return copy_string(writer.output(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_load_from_xml(OakNodeProject *project, const char *xml,
									 int load_type, int *out_result,
									 OakNodeSerializerLoadData **out_load_data,
									 char *details_buf, int details_buf_size)
{
	if (!xml || !out_result || !is_valid_load_type(load_type)) {
		return OAKNODE_E_INVALID;
	}
	if (!g_initialized) {
		return OAKNODE_E_STATE;
	}

	if (out_load_data) {
		*out_load_data = NULL;
	}

	try {
		olive::XmlStreamReader reader(xml);
		olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
			to_cpp(project), &reader,
			static_cast<olive::ProjectSerializer::LoadType>(load_type));

		*out_result = static_cast<int>(result.code());

		if (details_buf && details_buf_size > 0) {
			copy_string(result.get_details(), details_buf, details_buf_size);
		}

		if (result == olive::ProjectSerializer::k_success && out_load_data) {
			auto *load_data = new (std::nothrow) OakNodeSerializerLoadData();
			if (!load_data) {
				return OAKNODE_E_NOMEM;
			}
			load_data->impl = result.get_load_data();
			*out_load_data = load_data;
		}

		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_serializer_loaddata_free(OakNodeSerializerLoadData *load_data)
{
	delete load_data;
}

int oaknode_serializer_loaddata_node_count(
	const OakNodeSerializerLoadData *load_data)
{
	if (!load_data) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(load_data->impl.nodes.size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode *oaknode_serializer_loaddata_node_at(
	const OakNodeSerializerLoadData *load_data, int index)
{
	if (!load_data || index < 0 ||
		static_cast<size_t>(index) >= load_data->impl.nodes.size()) {
		return NULL;
	}

	try {
		return to_c(load_data->impl.nodes[static_cast<size_t>(index)]);
	} catch (...) {
		return NULL;
	}
}

int oaknode_serializer_loaddata_get_property(
	const OakNodeSerializerLoadData *load_data, OakNodeNode *node,
	const char *key, char *buf, int buf_size)
{
	if (!load_data || !node || !key) {
		return OAKNODE_E_INVALID;
	}

	try {
		auto node_it = load_data->impl.properties.find(to_cpp(node));
		if (node_it == load_data->impl.properties.end()) {
			return OAKNODE_E_NOT_FOUND;
		}
		auto key_it = node_it->second.find(key);
		if (key_it == node_it->second.end()) {
			return OAKNODE_E_NOT_FOUND;
		}
		return copy_string(key_it->second, buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_loaddata_connection_count(
	const OakNodeSerializerLoadData *load_data)
{
	if (!load_data) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(load_data->impl.promised_connections.size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_loaddata_connection_at(
	const OakNodeSerializerLoadData *load_data, int index,
	OakNodeNode **out_output_node, OakNodeNode **out_input_node,
	char *input_id_buf, int input_id_buf_size, int *out_element)
{
	if (!load_data || !out_output_node || !out_input_node || !out_element) {
		return OAKNODE_E_INVALID;
	}
	if (index < 0 || static_cast<size_t>(index) >=
						 load_data->impl.promised_connections.size()) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		const olive::Node::OutputConnection &connection =
			load_data->impl.promised_connections[static_cast<size_t>(index)];
		*out_output_node = to_c(connection.first);
		*out_input_node = to_c(connection.second.node());
		if (input_id_buf && input_id_buf_size > 0) {
			copy_string(connection.second.input(), input_id_buf,
						input_id_buf_size);
		}
		*out_element = connection.second.element();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
