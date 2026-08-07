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

#include "node/node.h"

#include "common/videoparams.h"
#include "olive/core/oakcore/audioparams.h"

#include <atomic>
#include <string>

#include "nodeundo.h"

#include "project.h"

#include "output/viewer/viewer.h"

#include "project/footage/footage.h"

#include "valueconvert.h"

namespace
{

inline olive::Node *to_node(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

inline const olive::Node *to_node(const OakNodeNode *node)
{
	return reinterpret_cast<const olive::Node *>(node);
}

inline OakNodeNode *from_node(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

std::atomic<int> g_alive_count(0);

/**
 * @brief Validate that `input_id` names an existing input of `node`.
 */
bool has_input(const olive::Node *node, const char *input_id)
{
	return node && input_id && node->has_input_with_id(input_id);
}

/**
 * @brief Map a POD value for setting on `input_id`, validating that the
 * POD type matches the input's declared type.
 */
int variant_for_input(olive::Node *node, const char *input_id,
					  const oaknode_value *v, olive::Variant *out)
{
	if (!v) {
		return OAKNODE_E_INVALID;
	}

	olive::NodeValue::Type type = node->get_input_data_type(input_id);

	if (oaknode_c_api::value_type_is_string(type) ||
		v->type != oaknode_c_api::value_type_to_oak(type)) {
		return OAKNODE_E_INVALID;
	}

	if (!oaknode_c_api::variant_from_value(v, out)) {
		return OAKNODE_E_INVALID;
	}

	return OAKNODE_OK;
}

}

namespace oaknode_c_api
{

void alive_inc()
{
	g_alive_count.fetch_add(1, std::memory_order_relaxed);
}

void alive_dec()
{
	g_alive_count.fetch_sub(1, std::memory_order_relaxed);
}

}

int oaknode_debug_alive_count(void)
{
	return g_alive_count.load(std::memory_order_relaxed);
}

/* ---- Metadata --------------------------------------------------------- */

int oaknode_node_get_id(const OakNodeNode *node, char *buf, int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(to_node(node)->id(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_name(const OakNodeNode *node, char *buf, int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(to_node(node)->name(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_label(const OakNodeNode *node, char *buf, int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(to_node(node)->get_label(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_label(OakNodeNode *node, const char *label)
{
	if (!node || !label) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_node(node)->set_label(label);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_label_undoable(OakNodeNode *node, const char *label,
									OakUndoCommand *out_command)
{
	if (!node || !label || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeRenameCommand(to_node(node), label));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_override_color(const OakNodeNode *node, int *out_value)
{
	if (!node || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value = to_node(node)->get_override_color();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_override_color(OakNodeNode *node, int index)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_node(node)->set_override_color(index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_override_color_undoable(OakNodeNode *node, int index,
											 OakUndoCommand *out_command)
{
	if (!node || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeOverrideColorCommand(to_node(node), index));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_is_enabled(const OakNodeNode *node, int *out_value)
{
	if (!node || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value = to_node(node)
						 ->get_standard_value(olive::Node::k_enabled_input)
						 .to_bool()
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_enabled(OakNodeNode *node, int enabled)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_node(node)->set_standard_value(olive::Node::k_enabled_input,
										  olive::Variant(enabled != 0));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_enabled_undoable(OakNodeNode *node, int enabled,
									  OakUndoCommand *out_command)
{
	if (!node || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput ref(to_node(node), olive::Node::k_enabled_input);
		olive::SplitValue split =
			olive::NodeValue::split_normal_value_into_track_values(
				olive::NodeValue::k_boolean, olive::Variant(enabled != 0));
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetSplitStandardValueCommand(ref, split));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Input introspection ------------------------------------------------ */

int oaknode_node_input_count(const OakNodeNode *node, int *out_count)
{
	if (!node || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(to_node(node)->inputs().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_id(const OakNodeNode *node, int index, char *buf,
						  int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const std::vector<std::string> &inputs = to_node(node)->inputs();
		if (index < 0 || index >= static_cast<int>(inputs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(inputs[size_t(index)], buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_get_type(const OakNodeNode *node, const char *input_id,
								int *out_type)
{
	if (!node || !out_type) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_type = oaknode_c_api::value_type_to_oak(
			to_node(node)->get_input_data_type(input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_is_connected(const OakNodeNode *node,
									const char *input_id, int *out_value)
{
	if (!node || !out_value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_value = to_node(node)->is_input_connected(input_id) ? 1 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_is_connectable(const OakNodeNode *node,
									  const char *input_id, int *out_value)
{
	if (!node || !out_value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_value = to_node(node)->is_input_connectable(input_id) ? 1 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_input_name(const OakNodeNode *node, const char *input_id,
								char *buf, int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		return oaknode_c_api::copy_string(to_node(node)->get_input_name(input_id),
										  buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_get_connected_node(const OakNodeNode *node,
										  const char *input_id,
										  OakNodeNode **out_node)
{
	if (!node || !out_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_node = from_node(to_node(node)->get_connected_output(input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Parameter access ----------------------------------------------------- */

int oaknode_node_get_input(const OakNodeNode *node, const char *input_id,
						   oaknode_value *out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::NodeValue::Type type = to_node(node)->get_input_data_type(input_id);
		return oaknode_c_api::value_from_variant(
			type, to_node(node)->get_standard_value(input_id), out);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input(OakNodeNode *node, const char *input_id,
						   const oaknode_value *v)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Variant variant;
		int rc = variant_for_input(to_node(node), input_id, v, &variant);
		if (rc != OAKNODE_OK) {
			return rc;
		}

		to_node(node)->set_standard_value(input_id, variant);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_undoable(OakNodeNode *node, const char *input_id,
									const oaknode_value *v,
									OakUndoCommand *out_command)
{
	if (!node || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Variant variant;
		int rc = variant_for_input(to_node(node), input_id, v, &variant);
		if (rc != OAKNODE_OK) {
			return rc;
		}

		olive::NodeValue::Type type = to_node(node)->get_input_data_type(input_id);
		olive::NodeInput ref(to_node(node), input_id);
		olive::SplitValue split =
			olive::NodeValue::split_normal_value_into_track_values(type, variant);

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetSplitStandardValueCommand(ref, split));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_input_string(const OakNodeNode *node,
								  const char *input_id, char *buf,
								  int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				to_node(node)->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}
		return oaknode_c_api::copy_string(
			to_node(node)->get_standard_value(input_id).to_string(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_string(OakNodeNode *node, const char *input_id,
								  const char *value)
{
	if (!node || !value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				to_node(node)->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}
		to_node(node)->set_standard_value(input_id, olive::Variant(value));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_string_undoable(OakNodeNode *node,
										   const char *input_id,
										   const char *value,
										   OakUndoCommand *out_command)
{
	if (!node || !value || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				to_node(node)->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}

		olive::NodeInput ref(to_node(node), input_id);
		olive::SplitValue split =
			olive::NodeValue::split_normal_value_into_track_values(
				to_node(node)->get_input_data_type(input_id),
				olive::Variant(value));

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetSplitStandardValueCommand(ref, split));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Graph editing -------------------------------------------------------- */

int oaknode_node_connect(OakNodeNode *output_node, OakNodeNode *input_node,
						 const char *input_id)
{
	if (!output_node || !input_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!to_node(input_node)->is_input_connectable(input_id)) {
			return OAKNODE_E_INVALID;
		}
		if (to_node(input_node)->is_input_connected(input_id) ||
			to_node(input_node)->parent() != to_node(output_node)->parent()) {
			return OAKNODE_E_STATE;
		}

		olive::Node::connect_edge(to_node(output_node),
								  olive::NodeInput(to_node(input_node), input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_connect_undoable(OakNodeNode *output_node,
								  OakNodeNode *input_node,
								  const char *input_id,
								  OakUndoCommand *out_command)
{
	if (!output_node || !input_node || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!to_node(input_node)->is_input_connectable(input_id)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeEdgeAddCommand(
				to_node(output_node),
				olive::NodeInput(to_node(input_node), input_id)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect(OakNodeNode *input_node, const char *input_id)
{
	if (!input_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output = to_node(input_node)->get_connected_output(input_id);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::disconnect_edge(
			output, olive::NodeInput(to_node(input_node), input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect_undoable(OakNodeNode *input_node,
									 const char *input_id,
									 OakUndoCommand *out_command)
{
	if (!input_node || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output = to_node(input_node)->get_connected_output(input_id);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeEdgeRemoveCommand(
				output, olive::NodeInput(to_node(input_node), input_id)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_count(const OakNodeNode *node,
										 int *out_count)
{
	if (!node || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(to_node(node)->output_connections().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_node_at(const OakNodeNode *node, int index,
										   OakNodeNode **out_node)
{
	if (!node || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			to_node(node)->output_connections();
		if (index < 0 || index >= static_cast<int>(connections.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_node = from_node(connections[size_t(index)].second.node());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_input_id_at(const OakNodeNode *node,
											   int index, char *buf,
											   int buf_size)
{
	if (!node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			to_node(node)->output_connections();
		if (index < 0 || index >= static_cast<int>(connections.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(
			connections[size_t(index)].second.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_element_at(const OakNodeNode *node,
											  int index, int *out_element)
{
	if (!node || !out_element) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			to_node(node)->output_connections();
		if (index < 0 || index >= static_cast<int>(connections.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_element = connections[size_t(index)].second.element();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Links --------------------------------------------------------------- */

int oaknode_node_link(OakNodeNode *a, OakNodeNode *b, int *out_linked)
{
	if (!a || !b) {
		return OAKNODE_E_INVALID;
	}

	try {
		bool linked = olive::Node::link(to_node(a), to_node(b));
		if (out_linked) {
			*out_linked = linked ? 1 : 0;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_unlink(OakNodeNode *a, OakNodeNode *b, int *out_unlinked)
{
	if (!a || !b) {
		return OAKNODE_E_INVALID;
	}

	try {
		bool unlinked = olive::Node::unlink(to_node(a), to_node(b));
		if (out_unlinked) {
			*out_unlinked = unlinked ? 1 : 0;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_undoable(OakNodeNode *a, OakNodeNode *b, int link,
							   OakUndoCommand *out_command)
{
	if (!a || !b || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeLinkCommand(to_node(a), to_node(b), link != 0));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_are_linked(const OakNodeNode *a, const OakNodeNode *b,
							int *out_value)
{
	if (!a || !b || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value = olive::Node::are_linked(
							 const_cast<olive::Node *>(to_node(a)),
							 const_cast<olive::Node *>(to_node(b)))
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_count(const OakNodeNode *node, int *out_count)
{
	if (!node || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(to_node(node)->links().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_at(const OakNodeNode *node, int index,
						 OakNodeNode **out_node)
{
	if (!node || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const std::vector<olive::Node *> &links = to_node(node)->links();
		if (index < 0 || index >= static_cast<int>(links.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_node = from_node(links[size_t(index)]);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Context positions ---------------------------------------------------- */

int oaknode_node_context_count(const OakNodeNode *node, int *out_count)
{
	if (!node || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(to_node(node)->get_context_positions().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_context_node_at(const OakNodeNode *node, int index,
								 OakNodeNode **out_node)
{
	if (!node || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::PositionMap &positions =
			to_node(node)->get_context_positions();
		if (index < 0 || index >= static_cast<int>(positions.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		auto it = positions.cbegin();
		std::advance(it, index);
		*out_node = from_node(it->first);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_context_position(const OakNodeNode *node,
									  OakNodeNode *context, double *out_x,
									  double *out_y, int *out_expanded)
{
	if (!node || !context) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node *self = to_node(node);
		if (!self->context_contains_node(to_node(context))) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::Position position =
			const_cast<olive::Node *>(self)->get_node_position_data_in_context(
				to_node(context));
		if (out_x) {
			*out_x = position.position.x();
		}
		if (out_y) {
			*out_y = position.position.y();
		}
		if (out_expanded) {
			*out_expanded = position.expanded ? 1 : 0;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_context_position(OakNodeNode *node, OakNodeNode *context,
									  double x, double y, int expanded)
{
	if (!node || !context) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node::Position position(olive::PointF(x, y), expanded != 0);
		to_node(node)->set_node_position_in_context(to_node(context), position);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_context_position_undoable(OakNodeNode *node,
											   OakNodeNode *context, double x,
											   double y, int expanded,
											   OakUndoCommand *out_command)
{
	if (!node || !context || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node::Position position(olive::PointF(x, y), expanded != 0);
		// NOTE: NodeSetPositionCommand's redo() calls
		// context_->set_node_position_in_context(node_, pos_), and the
		// position map lives on the positioned node keyed by the context,
		// so the command's (node, context) arguments are swapped relative
		// to this function's signature.
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeSetPositionCommand(to_node(context), to_node(node),
											  position));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_remove_from_context(OakNodeNode *node, OakNodeNode *context)
{
	if (!node || !context) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (!to_node(node)->remove_node_from_context(to_node(context))) {
			return OAKNODE_E_NOT_FOUND;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Lifetime --------------------------------------------------------------- */

OakNodeNode *oaknode_node_create_copy(const OakNodeNode *node)
{
	if (!node) {
		return NULL;
	}

	try {
		olive::Node *copy = to_node(node)->copy();
		if (copy) {
			oaknode_c_api::alive_inc();
		}
		return from_node(copy);
	} catch (...) {
		return NULL;
	}
}

void oaknode_node_free(OakNodeNode *node)
{
	if (!node) {
		return;
	}

	try {
		delete to_node(node);
		oaknode_c_api::alive_dec();
	} catch (...) {
	}
}

OakNodeNode *oaknode_node_copy_in_graph(OakNodeNode *node,
										OakUndoCommand *out_command)
{
	if (!node || !out_command) {
		return NULL;
	}

	try {
		auto *command = new olive::MultiUndoCommand();
		olive::Node *copy =
			olive::Node::copy_node_in_graph(to_node(node), command);
		if (!copy) {
			delete command;
			return NULL;
		}
		*out_command = oaknode_c_api::wrap_command(command);
		if (!out_command->ctx) {
			delete command;
			return NULL;
		}
		oaknode_c_api::alive_inc();
		return from_node(copy);
	} catch (...) {
		return NULL;
	}
}

OakUndoCommand oaknode_command_create_remove_node(OakNodeNode *node)
{
	if (!node) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeRemoveWithExclusiveDependenciesAndDisconnect(
				to_node(node)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oaknode_node_get_project(const OakNodeNode *node, OakNodeProject **out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeProject *>(to_node(node)->project());
	return OAKNODE_OK;
}

int oaknode_node_input_array_insert(OakNodeNode *node, const char *input_id,
									int index)
{
	if (!node || !input_id) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		to_node(node)->input_array_insert(input_id, index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_array_remove(OakNodeNode *node, const char *input_id,
									int index)
{
	if (!node || !input_id) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		to_node(node)->input_array_remove(input_id, index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_connect_element(OakNodeNode *output_node,
								 OakNodeNode *input_node,
								 const char *input_id, int element)
{
	if (!output_node || !input_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node::connect_edge(
			to_node(output_node),
			olive::NodeInput(to_node(input_node), input_id, element));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect_element(OakNodeNode *input_node,
									const char *input_id, int element)
{
	if (!input_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(to_node(input_node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output = to_node(input_node)->get_connected_output(
			input_id, element);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::disconnect_edge(
			output, olive::NodeInput(to_node(input_node), input_id, element));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakUndoCommand oaknode_command_create_add_node(OakNodeProject *graph,
												OakNodeNode *node)
{
	if (!graph || !node) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeAddCommand(
				reinterpret_cast<olive::Project *>(graph), to_node(node)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaknode_command_create_set_position_recursive(
	OakNodeNode *node, OakNodeNode *context, double x, double y)
{
	if (!node || !context) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeSetPositionAndDependenciesRecursivelyCommand(
				to_node(node), to_node(context),
				olive::Node::Position(olive::PointF(x, y))));
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oaknode_node_get_markers(const OakNodeNode *node,
							 OakNodeMarkerList **out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}

	const olive::Node *n = to_node(node);
	if (auto *v = dynamic_cast<const olive::ViewerOutput *>(n)) {
		*out = reinterpret_cast<OakNodeMarkerList *>(v->get_markers());
	} else {
		*out = NULL;
	}
	return OAKNODE_OK;
}

int oaknode_node_get_work_area(const OakNodeNode *node,
							   OakNodeWorkArea **out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}

	const olive::Node *n = to_node(node);
	if (auto *v = dynamic_cast<const olive::ViewerOutput *>(n)) {
		*out = reinterpret_cast<OakNodeWorkArea *>(v->get_work_area());
	} else {
		*out = NULL;
	}
	return OAKNODE_OK;
}

int oaknode_node_get_video_frame_cache(const OakNodeNode *node,
									   OakNodeFrameCache **out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeFrameCache *>(
		to_node(node)->video_frame_cache());
	return OAKNODE_OK;
}

int oaknode_node_copy_inputs(OakNodeNode *dst, const OakNodeNode *src,
							 int include_connections)
{
	if (!dst || !src) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node::copy_inputs(to_node(src), to_node(dst),
								 include_connections != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_value_hint_track(OakNodeNode *node,
									  const char *input_id,
									  int track_type, int track_index)
{
	if (!node || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_node(node)->set_value_hint_for_input(
			input_id,
			olive::Node::ValueHint({ olive::NodeValue::k_texture },
								   olive::Track::Reference(
									   static_cast<olive::Track::Type>(
										   track_type),
									   track_index)
									   .to_string()));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_viewer_set_video_params(OakNodeNode *viewer,
									const OakVideoParams *params)
{
	if (!viewer || !params || !params->ctx) {
		return OAKNODE_E_INVALID;
	}

	const olive::VideoParams *native =
		oakcommon_videoparams_get_native(*params);
	if (!native) {
		return OAKNODE_E_INVALID;
	}

	olive::ViewerOutput *v =
		dynamic_cast<olive::ViewerOutput *>(to_node(viewer));
	if (!v) {
		return OAKNODE_E_INVALID;
	}

	try {
		v->set_video_params(*native);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_viewer_set_audio_params(OakNodeNode *viewer,
									const OakAudioParams *params)
{
	if (!viewer || !params) {
		return OAKNODE_E_INVALID;
	}

	olive::ViewerOutput *v =
		dynamic_cast<olive::ViewerOutput *>(to_node(viewer));
	if (!v) {
		return OAKNODE_E_INVALID;
	}

	try {
		v->set_audio_params(olive::AudioParams::from_handle(
			oakcore_audioparams_copy(params)));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_find_input_footage(const OakNodeNode *node,
									OakNodeFootage **out)
{
	if (!node || !out) {
		return OAKNODE_E_INVALID;
	}

	*out = NULL;
	try {
		std::vector<olive::Footage *> found =
			to_node(node)->find_input_nodes<olive::Footage>();
		if (!found.empty()) {
			*out = reinterpret_cast<OakNodeFootage *>(found.front());
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
