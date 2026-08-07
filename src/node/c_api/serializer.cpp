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

#include "nodehandle.h"

namespace
{

bool g_initialized = false;

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

OakNodeSerializerSaveData oaknode_serializer_savedata_create(
	int load_type, OakNodeProject project)
{
	if (!is_valid_load_type(load_type)) {
		return OakNodeSerializerSaveData{};
	}

	try {
		return oaknode_c_api::make_handle<OakNodeSerializerSaveData>(
			new olive::ProjectSerializer::SaveData(
				static_cast<olive::ProjectSerializer::LoadType>(load_type),
				oaknode_c_api::to_native<olive::Project>(project)),
			true,
			&oaknode_c_api::delete_as<olive::ProjectSerializer::SaveData>);
	} catch (...) {
		return OakNodeSerializerSaveData{};
	}
}

void oaknode_serializer_savedata_free(OakNodeSerializerSaveData *save_data)
{
	oaknode_c_api::free_handle(save_data);
}

int oaknode_serializer_savedata_set_nodes(
	OakNodeSerializerSaveData save_data, const OakNodeNode *nodes, int count)
{
	olive::ProjectSerializer::SaveData *sd =
		oaknode_c_api::to_native<olive::ProjectSerializer::SaveData>(save_data);
	if (!sd || !nodes || count < 0) {
		return OAKNODE_E_INVALID;
	}

	try {
		std::vector<olive::Node *> cpp_nodes;
		cpp_nodes.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; i++) {
			olive::Node *node = oaknode_c_api::to_native<olive::Node>(nodes[i]);
			if (!node) {
				return OAKNODE_E_INVALID;
			}
			cpp_nodes.push_back(node);
		}

		sd->set_only_serialize_nodes(cpp_nodes);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_savedata_set_property(
	OakNodeSerializerSaveData save_data, OakNodeNode node, const char *key,
	const char *value)
{
	olive::ProjectSerializer::SaveData *sd =
		oaknode_c_api::to_native<olive::ProjectSerializer::SaveData>(save_data);
	olive::Node *native_node = oaknode_c_api::to_native<olive::Node>(node);
	if (!sd || !native_node || !key || !value) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::ProjectSerializer::SerializedProperties properties =
			sd->get_properties();
		properties[native_node][key] = value;
		sd->set_properties(properties);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_save_to_xml(OakNodeSerializerSaveData save_data,
								   char *buf, int buf_size)
{
	olive::ProjectSerializer::SaveData *sd =
		oaknode_c_api::to_native<olive::ProjectSerializer::SaveData>(save_data);
	if (!sd) {
		return OAKNODE_E_INVALID;
	}
	if (!g_initialized) {
		return OAKNODE_E_STATE;
	}

	try {
		olive::XmlStreamWriter writer;
		olive::ProjectSerializer::Result result =
			olive::ProjectSerializer::save(&writer, *sd);
		if (result != olive::ProjectSerializer::k_success) {
			return OAKNODE_E_FAILED;
		}
		return copy_string(writer.output(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_load_from_xml(OakNodeProject project, const char *xml,
									 int load_type, int *out_result,
									 OakNodeSerializerLoadData *out_load_data,
									 char *details_buf, int details_buf_size)
{
	if (!xml || !out_result || !is_valid_load_type(load_type)) {
		return OAKNODE_E_INVALID;
	}
	if (!g_initialized) {
		return OAKNODE_E_STATE;
	}

	if (out_load_data) {
		*out_load_data = OakNodeSerializerLoadData{};
	}

	try {
		olive::XmlStreamReader reader(xml);
		olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
			oaknode_c_api::to_native<olive::Project>(project), &reader,
			static_cast<olive::ProjectSerializer::LoadType>(load_type));

		*out_result = static_cast<int>(result.code());

		if (details_buf && details_buf_size > 0) {
			copy_string(result.get_details(), details_buf, details_buf_size);
		}

		if (result == olive::ProjectSerializer::k_success && out_load_data) {
			auto *load_data =
				new (std::nothrow) olive::ProjectSerializer::LoadData();
			if (!load_data) {
				return OAKNODE_E_NOMEM;
			}
			*load_data = result.get_load_data();
			*out_load_data =
				oaknode_c_api::make_handle<OakNodeSerializerLoadData>(
					load_data, true,
					&oaknode_c_api::delete_as<
						olive::ProjectSerializer::LoadData>);
			if (!out_load_data->ctx) {
				return OAKNODE_E_NOMEM;
			}
		}

		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_serializer_loaddata_free(OakNodeSerializerLoadData *load_data)
{
	oaknode_c_api::free_handle(load_data);
}

int oaknode_serializer_loaddata_node_count(
	OakNodeSerializerLoadData load_data)
{
	olive::ProjectSerializer::LoadData *ld =
		oaknode_c_api::to_native<olive::ProjectSerializer::LoadData>(load_data);
	if (!ld) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(ld->nodes.size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode oaknode_serializer_loaddata_node_at(
	OakNodeSerializerLoadData load_data, int index)
{
	olive::ProjectSerializer::LoadData *ld =
		oaknode_c_api::to_native<olive::ProjectSerializer::LoadData>(load_data);
	if (!ld || index < 0 ||
		static_cast<size_t>(index) >= ld->nodes.size()) {
		return OakNodeNode{};
	}

	try {
		// Borrowed: the node is owned by the caller only in the sense of
		// the documented adoption contract; see oaknode_project_add_node().
		return oaknode_c_api::make_handle<OakNodeNode>(
			ld->nodes[static_cast<size_t>(index)], false, nullptr);
	} catch (...) {
		return OakNodeNode{};
	}
}

int oaknode_serializer_loaddata_get_property(
	OakNodeSerializerLoadData load_data, OakNodeNode node, const char *key,
	char *buf, int buf_size)
{
	olive::ProjectSerializer::LoadData *ld =
		oaknode_c_api::to_native<olive::ProjectSerializer::LoadData>(load_data);
	olive::Node *native_node = oaknode_c_api::to_native<olive::Node>(node);
	if (!ld || !native_node || !key) {
		return OAKNODE_E_INVALID;
	}

	try {
		auto node_it = ld->properties.find(native_node);
		if (node_it == ld->properties.end()) {
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
	OakNodeSerializerLoadData load_data)
{
	olive::ProjectSerializer::LoadData *ld =
		oaknode_c_api::to_native<olive::ProjectSerializer::LoadData>(load_data);
	if (!ld) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(ld->promised_connections.size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_loaddata_connection_at(
	OakNodeSerializerLoadData load_data, int index,
	OakNodeNode *out_output_node, OakNodeNode *out_input_node,
	char *input_id_buf, int input_id_buf_size, int *out_element)
{
	olive::ProjectSerializer::LoadData *ld =
		oaknode_c_api::to_native<olive::ProjectSerializer::LoadData>(load_data);
	if (!ld || !out_output_node || !out_input_node || !out_element) {
		return OAKNODE_E_INVALID;
	}
	if (index < 0 ||
		static_cast<size_t>(index) >= ld->promised_connections.size()) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		const olive::Node::OutputConnection &connection =
			ld->promised_connections[static_cast<size_t>(index)];
		*out_output_node = oaknode_c_api::make_handle<OakNodeNode>(
			connection.first, false, nullptr);
		*out_input_node = oaknode_c_api::make_handle<OakNodeNode>(
			connection.second.node(), false, nullptr);
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

namespace
{

int report_serializer_result(const olive::ProjectSerializer::Result &result,
							 int *out_code, char *details, int details_size)
{
	if (out_code) {
		*out_code = int(result.code());
	}

	int needed = 0;
	if (details || details_size > 0) {
		std::string d = result.get_details();
		needed = int(d.size()) + 1;
		if (details && details_size >= needed) {
			memcpy(details, d.c_str(), needed);
		}
	}

	if (result.code() == olive::ProjectSerializer::k_success) {
		return OAKNODE_OK;
	}
	return (details && details_size > 0) ? needed : OAKNODE_E_FAILED;
}

} // namespace

int oaknode_serializer_save_to_file(OakNodeProject project,
		const char *filename, int use_compression, int *out_code,
		char *details, int details_size)
{
	olive::Project *native = oaknode_c_api::to_native<olive::Project>(project);
	if (!native || !filename) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_serializer_initialize();

		olive::ProjectSerializer::SaveData data(
			olive::ProjectSerializer::k_project, native, filename);

		olive::ProjectSerializer::Result result =
			olive::ProjectSerializer::save(data, use_compression != 0);

		return report_serializer_result(result, out_code, details,
										details_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_serializer_load_from_file(OakNodeProject project,
		const char *filename, int *out_code, char *details,
		int details_size)
{
	olive::Project *native = oaknode_c_api::to_native<olive::Project>(project);
	if (!native || !filename) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_serializer_initialize();

		olive::ProjectSerializer::Result result =
			olive::ProjectSerializer::load(native, filename,
										   olive::ProjectSerializer::k_project);

		return report_serializer_result(result, out_code, details,
										details_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
