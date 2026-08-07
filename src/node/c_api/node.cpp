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
#include "node/footage.h"
#include "node/project.h"
#include "olive/core/oakcore/audioparams.h"
#include "timeline/marker.h"
#include "timeline/workarea.h"

#include <atomic>
#include <string>

#include "nodehandle.h"
#include "nodeundo.h"

#include "project.h"

#include "output/viewer/viewer.h"

#include "project/footage/footage.h"

#include "valueconvert.h"

namespace
{

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

int oaknode_node_get_id(OakNodeNode node, char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(
			oaknode_c_api::to_native<olive::Node>(node)->id(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_name(OakNodeNode node, char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(
			oaknode_c_api::to_native<olive::Node>(node)->name(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_label(OakNodeNode node, char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(
			oaknode_c_api::to_native<olive::Node>(node)->get_label(), buf,
			buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_label(OakNodeNode node, const char *label)
{
	if (!node.ctx || !label) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->set_label(label);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_label_undoable(OakNodeNode node, const char *label,
									OakUndoCommand *out_command)
{
	if (!node.ctx || !label || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeRenameCommand(
				oaknode_c_api::to_native<olive::Node>(node), label));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_override_color(OakNodeNode node, int *out_value)
{
	if (!node.ctx || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value =
			oaknode_c_api::to_native<olive::Node>(node)->get_override_color();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_override_color(OakNodeNode node, int index)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->set_override_color(index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_override_color_undoable(OakNodeNode node, int index,
											 OakUndoCommand *out_command)
{
	if (!node.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeOverrideColorCommand(
				oaknode_c_api::to_native<olive::Node>(node), index));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_is_enabled(OakNodeNode node, int *out_value)
{
	if (!node.ctx || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value = oaknode_c_api::to_native<olive::Node>(node)
						 ->get_standard_value(olive::Node::k_enabled_input)
						 .to_bool()
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_enabled(OakNodeNode node, int enabled)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->set_standard_value(
			olive::Node::k_enabled_input, olive::Variant(enabled != 0));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_enabled_undoable(OakNodeNode node, int enabled,
									  OakUndoCommand *out_command)
{
	if (!node.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput ref(oaknode_c_api::to_native<olive::Node>(node),
							 olive::Node::k_enabled_input);
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

int oaknode_node_input_count(OakNodeNode node, int *out_count)
{
	if (!node.ctx || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(
			oaknode_c_api::to_native<olive::Node>(node)->inputs().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_id(OakNodeNode node, int index, char *buf,
						  int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		const std::vector<std::string> &inputs =
			oaknode_c_api::to_native<olive::Node>(node)->inputs();
		if (index < 0 || index >= static_cast<int>(inputs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(inputs[size_t(index)], buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_get_type(OakNodeNode node, const char *input_id,
								int *out_type)
{
	if (!node.ctx || !out_type) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_type = oaknode_c_api::value_type_to_oak(
			oaknode_c_api::to_native<olive::Node>(node)->get_input_data_type(
				input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_is_connected(OakNodeNode node, const char *input_id,
									int *out_value)
{
	if (!node.ctx || !out_value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_value = oaknode_c_api::to_native<olive::Node>(node)
						 ->is_input_connected(input_id)
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_is_connectable(OakNodeNode node, const char *input_id,
									  int *out_value)
{
	if (!node.ctx || !out_value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_value = oaknode_c_api::to_native<olive::Node>(node)
						 ->is_input_connectable(input_id)
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_input_name(OakNodeNode node, const char *input_id,
								char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		return oaknode_c_api::copy_string(
			oaknode_c_api::to_native<olive::Node>(node)->get_input_name(
				input_id),
			buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_get_connected_node(OakNodeNode node,
										  const char *input_id,
										  OakNodeNode *out_node)
{
	if (!node.ctx || !out_node) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		*out_node = oaknode_c_api::make_handle<OakNodeNode>(
			oaknode_c_api::to_native<olive::Node>(node)->get_connected_output(
				input_id),
			false, &oaknode_c_api::delete_as<olive::Node>);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Parameter access ----------------------------------------------------- */

int oaknode_node_get_input(OakNodeNode node, const char *input_id,
						   oaknode_value *out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::NodeValue::Type type =
			oaknode_c_api::to_native<olive::Node>(node)->get_input_data_type(
				input_id);
		return oaknode_c_api::value_from_variant(
			type,
			oaknode_c_api::to_native<olive::Node>(node)->get_standard_value(
				input_id),
			out);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input(OakNodeNode node, const char *input_id,
						   const oaknode_value *v)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Variant variant;
		int rc = variant_for_input(oaknode_c_api::to_native<olive::Node>(node),
								   input_id, v, &variant);
		if (rc != OAKNODE_OK) {
			return rc;
		}

		oaknode_c_api::to_native<olive::Node>(node)->set_standard_value(
			input_id, variant);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_undoable(OakNodeNode node, const char *input_id,
									const oaknode_value *v,
									OakUndoCommand *out_command)
{
	if (!node.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Variant variant;
		int rc = variant_for_input(oaknode_c_api::to_native<olive::Node>(node),
								   input_id, v, &variant);
		if (rc != OAKNODE_OK) {
			return rc;
		}

		olive::NodeValue::Type type =
			oaknode_c_api::to_native<olive::Node>(node)->get_input_data_type(
				input_id);
		olive::NodeInput ref(oaknode_c_api::to_native<olive::Node>(node),
							 input_id);
		olive::SplitValue split =
			olive::NodeValue::split_normal_value_into_track_values(type,
																   variant);

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

int oaknode_node_get_input_string(OakNodeNode node, const char *input_id,
								  char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				oaknode_c_api::to_native<olive::Node>(node)
					->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}
		return oaknode_c_api::copy_string(
			oaknode_c_api::to_native<olive::Node>(node)
				->get_standard_value(input_id)
				.to_string(),
			buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_string(OakNodeNode node, const char *input_id,
								  const char *value)
{
	if (!node.ctx || !value) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				oaknode_c_api::to_native<olive::Node>(node)
					->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}
		oaknode_c_api::to_native<olive::Node>(node)->set_standard_value(
			input_id, olive::Variant(value));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_string_undoable(OakNodeNode node,
										   const char *input_id,
										   const char *value,
										   OakUndoCommand *out_command)
{
	if (!node.ctx || !value || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::value_type_is_string(
				oaknode_c_api::to_native<olive::Node>(node)
					->get_input_data_type(input_id))) {
			return OAKNODE_E_INVALID;
		}

		olive::NodeInput ref(oaknode_c_api::to_native<olive::Node>(node),
							 input_id);
		olive::SplitValue split =
			olive::NodeValue::split_normal_value_into_track_values(
				oaknode_c_api::to_native<olive::Node>(node)
					->get_input_data_type(input_id),
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

int oaknode_node_connect(OakNodeNode output_node, OakNodeNode input_node,
						 const char *input_id)
{
	if (!output_node.ctx || !input_node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::to_native<olive::Node>(input_node)
				 ->is_input_connectable(input_id)) {
			return OAKNODE_E_INVALID;
		}
		if (oaknode_c_api::to_native<olive::Node>(input_node)
				->is_input_connected(input_id) ||
			oaknode_c_api::to_native<olive::Node>(input_node)->parent() !=
				oaknode_c_api::to_native<olive::Node>(output_node)->parent()) {
			return OAKNODE_E_STATE;
		}

		olive::Node::connect_edge(
			oaknode_c_api::to_native<olive::Node>(output_node),
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(input_node),
							 input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_connect_undoable(OakNodeNode output_node,
								  OakNodeNode input_node,
								  const char *input_id,
								  OakUndoCommand *out_command)
{
	if (!output_node.ctx || !input_node.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		if (!oaknode_c_api::to_native<olive::Node>(input_node)
				 ->is_input_connectable(input_id)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeEdgeAddCommand(
				oaknode_c_api::to_native<olive::Node>(output_node),
				olive::NodeInput(
					oaknode_c_api::to_native<olive::Node>(input_node),
					input_id)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect(OakNodeNode input_node, const char *input_id)
{
	if (!input_node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output =
			oaknode_c_api::to_native<olive::Node>(input_node)
				->get_connected_output(input_id);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::disconnect_edge(
			output,
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(input_node),
							 input_id));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect_undoable(OakNodeNode input_node,
									 const char *input_id,
									 OakUndoCommand *out_command)
{
	if (!input_node.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output =
			oaknode_c_api::to_native<olive::Node>(input_node)
				->get_connected_output(input_id);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeEdgeRemoveCommand(
				output,
				olive::NodeInput(
					oaknode_c_api::to_native<olive::Node>(input_node),
					input_id)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_count(OakNodeNode node, int *out_count)
{
	if (!node.ctx || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(
			oaknode_c_api::to_native<olive::Node>(node)
				->output_connections()
				.size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_node_at(OakNodeNode node, int index,
										   OakNodeNode *out_node)
{
	if (!node.ctx || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			oaknode_c_api::to_native<olive::Node>(node)->output_connections();
		if (index < 0 || index >= static_cast<int>(connections.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_node = oaknode_c_api::make_handle<OakNodeNode>(
			connections[size_t(index)].second.node(), false,
			&oaknode_c_api::delete_as<olive::Node>);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_input_id_at(OakNodeNode node, int index,
											   char *buf, int buf_size)
{
	if (!node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			oaknode_c_api::to_native<olive::Node>(node)->output_connections();
		if (index < 0 || index >= static_cast<int>(connections.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(
			connections[size_t(index)].second.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_output_connection_element_at(OakNodeNode node, int index,
											  int *out_element)
{
	if (!node.ctx || !out_element) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::OutputConnections &connections =
			oaknode_c_api::to_native<olive::Node>(node)->output_connections();
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

int oaknode_node_link(OakNodeNode a, OakNodeNode b, int *out_linked)
{
	if (!a.ctx || !b.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		bool linked = olive::Node::link(
			oaknode_c_api::to_native<olive::Node>(a),
			oaknode_c_api::to_native<olive::Node>(b));
		if (out_linked) {
			*out_linked = linked ? 1 : 0;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_unlink(OakNodeNode a, OakNodeNode b, int *out_unlinked)
{
	if (!a.ctx || !b.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		bool unlinked = olive::Node::unlink(
			oaknode_c_api::to_native<olive::Node>(a),
			oaknode_c_api::to_native<olive::Node>(b));
		if (out_unlinked) {
			*out_unlinked = unlinked ? 1 : 0;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_undoable(OakNodeNode a, OakNodeNode b, int link,
							   OakUndoCommand *out_command)
{
	if (!a.ctx || !b.ctx || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeLinkCommand(
				oaknode_c_api::to_native<olive::Node>(a),
				oaknode_c_api::to_native<olive::Node>(b), link != 0));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_are_linked(OakNodeNode a, OakNodeNode b, int *out_value)
{
	if (!a.ctx || !b.ctx || !out_value) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_value = olive::Node::are_linked(
						 oaknode_c_api::to_native<olive::Node>(a),
						 oaknode_c_api::to_native<olive::Node>(b))
						 ? 1
						 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_count(OakNodeNode node, int *out_count)
{
	if (!node.ctx || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(
			oaknode_c_api::to_native<olive::Node>(node)->links().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_link_at(OakNodeNode node, int index, OakNodeNode *out_node)
{
	if (!node.ctx || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const std::vector<olive::Node *> &links =
			oaknode_c_api::to_native<olive::Node>(node)->links();
		if (index < 0 || index >= static_cast<int>(links.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out_node = oaknode_c_api::make_handle<OakNodeNode>(
			links[size_t(index)], false,
			&oaknode_c_api::delete_as<olive::Node>);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Context positions ---------------------------------------------------- */

int oaknode_node_context_count(OakNodeNode node, int *out_count)
{
	if (!node.ctx || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(
			oaknode_c_api::to_native<olive::Node>(node)
				->get_context_positions()
				.size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_context_node_at(OakNodeNode node, int index,
								 OakNodeNode *out_node)
{
	if (!node.ctx || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Node::PositionMap &positions =
			oaknode_c_api::to_native<olive::Node>(node)
				->get_context_positions();
		if (index < 0 || index >= static_cast<int>(positions.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		auto it = positions.cbegin();
		std::advance(it, index);
		*out_node = oaknode_c_api::make_handle<OakNodeNode>(
			it->first, false, &oaknode_c_api::delete_as<olive::Node>);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_context_position(OakNodeNode node, OakNodeNode context,
									  double *out_x, double *out_y,
									  int *out_expanded)
{
	if (!node.ctx || !context.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node *self = oaknode_c_api::to_native<olive::Node>(node);
		if (!self->context_contains_node(
				oaknode_c_api::to_native<olive::Node>(context))) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::Position position =
			self->get_node_position_data_in_context(
				oaknode_c_api::to_native<olive::Node>(context));
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

int oaknode_node_set_context_position(OakNodeNode node, OakNodeNode context,
									  double x, double y, int expanded)
{
	if (!node.ctx || !context.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node::Position position(olive::PointF(x, y), expanded != 0);
		oaknode_c_api::to_native<olive::Node>(node)
			->set_node_position_in_context(
				oaknode_c_api::to_native<olive::Node>(context), position);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_context_position_undoable(OakNodeNode node,
											   OakNodeNode context, double x,
											   double y, int expanded,
											   OakUndoCommand *out_command)
{
	if (!node.ctx || !context.ctx || !out_command) {
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
			new olive::NodeSetPositionCommand(
				oaknode_c_api::to_native<olive::Node>(context),
				oaknode_c_api::to_native<olive::Node>(node), position));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_remove_from_context(OakNodeNode node, OakNodeNode context)
{
	if (!node.ctx || !context.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (!oaknode_c_api::to_native<olive::Node>(node)
				 ->remove_node_from_context(
					 oaknode_c_api::to_native<olive::Node>(context))) {
			return OAKNODE_E_NOT_FOUND;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

/* ---- Lifetime --------------------------------------------------------------- */

OakNodeNode oaknode_node_create_copy(OakNodeNode node)
{
	if (!node.ctx) {
		return OakNodeNode{};
	}

	try {
		return oaknode_c_api::make_handle<OakNodeNode>(
			oaknode_c_api::to_native<olive::Node>(node)->copy(), true,
			&oaknode_c_api::delete_as<olive::Node>);
	} catch (...) {
		return OakNodeNode{};
	}
}

void oaknode_node_free(OakNodeNode *node)
{
	oaknode_c_api::free_handle(node);
}

OakNodeNode oaknode_node_copy_in_graph(OakNodeNode node,
									   OakUndoCommand *out_command)
{
	if (!node.ctx || !out_command) {
		return OakNodeNode{};
	}

	try {
		auto *command = new olive::MultiUndoCommand();
		olive::Node *copy = olive::Node::copy_node_in_graph(
			oaknode_c_api::to_native<olive::Node>(node), command);
		if (!copy) {
			delete command;
			return OakNodeNode{};
		}
		*out_command = oaknode_c_api::wrap_command(command);
		if (!out_command->ctx) {
			delete command;
			return OakNodeNode{};
		}
		return oaknode_c_api::make_handle<OakNodeNode>(
			copy, true, &oaknode_c_api::delete_as<olive::Node>);
	} catch (...) {
		return OakNodeNode{};
	}
}

OakUndoCommand oaknode_command_create_remove_node(OakNodeNode node)
{
	if (!node.ctx) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeRemoveWithExclusiveDependenciesAndDisconnect(
				oaknode_c_api::to_native<olive::Node>(node)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oaknode_node_get_project(OakNodeNode node, OakNodeProject *out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = oaknode_c_api::make_handle<OakNodeProject>(
		oaknode_c_api::to_native<olive::Node>(node)->project(), false,
		&oaknode_c_api::delete_as<olive::Project>);
	return OAKNODE_OK;
}

int oaknode_node_input_array_insert(OakNodeNode node, const char *input_id,
									int index)
{
	if (!node.ctx || !input_id) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->input_array_insert(
			input_id, index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_input_array_remove(OakNodeNode node, const char *input_id,
									int index)
{
	if (!node.ctx || !input_id) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->input_array_remove(
			input_id, index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_connect_element(OakNodeNode output_node,
								 OakNodeNode input_node,
								 const char *input_id, int element)
{
	if (!output_node.ctx || !input_node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node::connect_edge(
			oaknode_c_api::to_native<olive::Node>(output_node),
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(input_node),
							 input_id, element));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_disconnect_element(OakNodeNode input_node,
									const char *input_id, int element)
{
	if (!input_node.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(input_node),
				   input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *output =
			oaknode_c_api::to_native<olive::Node>(input_node)
				->get_connected_output(input_id, element);
		if (!output) {
			return OAKNODE_E_NOT_FOUND;
		}

		olive::Node::disconnect_edge(
			output,
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(input_node),
							 input_id, element));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakUndoCommand oaknode_command_create_add_node(OakNodeProject graph,
											   OakNodeNode node)
{
	if (!graph.ctx || !node.ctx) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeAddCommand(
				oaknode_c_api::to_native<olive::Project>(graph),
				oaknode_c_api::to_native<olive::Node>(node)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaknode_command_create_set_position_recursive(
	OakNodeNode node, OakNodeNode context, double x, double y)
{
	if (!node.ctx || !context.ctx) {
		return OakUndoCommand{};
	}

	try {
		return oaknode_c_api::wrap_command(
			new olive::NodeSetPositionAndDependenciesRecursivelyCommand(
				oaknode_c_api::to_native<olive::Node>(node),
				oaknode_c_api::to_native<olive::Node>(context),
				olive::Node::Position(olive::PointF(x, y))));
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oaknode_node_get_markers(OakNodeNode node,
							 struct OakTimelineMarkerList *out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}

	const olive::Node *n = oaknode_c_api::to_native<olive::Node>(node);
	if (auto *v = dynamic_cast<const olive::ViewerOutput *>(n)) {
		*out = v->markers_handle();
		if (out->ctx) {
			out->addref(out->ctx);
		}
	} else {
		*out = OakTimelineMarkerList{};
	}
	return OAKNODE_OK;
}

int oaknode_node_get_work_area(OakNodeNode node,
							   struct OakTimelineWorkArea *out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}

	const olive::Node *n = oaknode_c_api::to_native<olive::Node>(node);
	if (auto *v = dynamic_cast<const olive::ViewerOutput *>(n)) {
		*out = v->workarea_handle();
		if (out->ctx) {
			out->addref(out->ctx);
		}
	} else {
		*out = OakTimelineWorkArea{};
	}
	return OAKNODE_OK;
}

int oaknode_node_get_video_frame_cache(OakNodeNode node,
									   OakNodeFrameCache **out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}
	*out = reinterpret_cast<OakNodeFrameCache *>(
		oaknode_c_api::to_native<olive::Node>(node)->video_frame_cache());
	return OAKNODE_OK;
}

int oaknode_node_copy_inputs(OakNodeNode dst, OakNodeNode src,
							 int include_connections)
{
	if (!dst.ctx || !src.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Node::copy_inputs(oaknode_c_api::to_native<olive::Node>(src),
								 oaknode_c_api::to_native<olive::Node>(dst),
								 include_connections != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_value_hint_track(OakNodeNode node, const char *input_id,
									  int track_type, int track_index)
{
	if (!node.ctx || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		oaknode_c_api::to_native<olive::Node>(node)->set_value_hint_for_input(
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

int oaknode_viewer_set_video_params(OakNodeNode viewer,
									const OakVideoParams *params)
{
	if (!viewer.ctx || !params || !params->ctx) {
		return OAKNODE_E_INVALID;
	}

	const olive::VideoParams *native =
		oakcommon_videoparams_get_native(*params);
	if (!native) {
		return OAKNODE_E_INVALID;
	}

	olive::ViewerOutput *v = dynamic_cast<olive::ViewerOutput *>(
		oaknode_c_api::to_native<olive::Node>(viewer));
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

int oaknode_viewer_set_audio_params(OakNodeNode viewer,
									const OakAudioParams *params)
{
	if (!viewer.ctx || !params) {
		return OAKNODE_E_INVALID;
	}

	olive::ViewerOutput *v = dynamic_cast<olive::ViewerOutput *>(
		oaknode_c_api::to_native<olive::Node>(viewer));
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

int oaknode_node_find_input_footage(OakNodeNode node, OakNodeFootage *out)
{
	if (!node.ctx || !out) {
		return OAKNODE_E_INVALID;
	}

	*out = OakNodeFootage{};
	try {
		std::vector<olive::Footage *> found =
			oaknode_c_api::to_native<olive::Node>(node)
				->find_input_nodes<olive::Footage>();
		if (!found.empty()) {
			*out = oaknode_c_api::make_handle<OakNodeFootage>(
				found.front(), false,
				&oaknode_c_api::delete_as<olive::Footage>);
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_get_input_at_time(OakNodeNode node,
								   const char *input_id, int64_t time_num,
								   int64_t time_den, oaknode_value *out)
{
	if (!node.ctx || !input_id || !out) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Node *n = oaknode_c_api::to_native<olive::Node>(node);
		olive::Variant v = n->get_value_at_time(
			input_id,
			olive::core::Rational(int(time_num), int(time_den)));

		olive::NodeValue::Type type = n->get_input_data_type(input_id);
		return oaknode_c_api::value_from_variant(type, v, out);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_node_set_input_at_time_undoable(OakNodeNode node,
		const char *input_id, int64_t time_num, int64_t time_den,
		const oaknode_value *v, int track, OakUndoCommand *out_command)
{
	if (!node.ctx || !input_id || !v || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	try {
		olive::Variant value;
		if (!oaknode_c_api::variant_from_value(v, &value)) {
			return OAKNODE_E_INVALID;
		}

		auto *multi = new olive::MultiUndoCommand();
		olive::Node::set_value_at_time(
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(node),
							 input_id),
			olive::core::Rational(int(time_num), int(time_den)), value, track,
			multi, true);

		*out_command = oaknode_c_api::wrap_command(multi);
		return out_command->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

uintptr_t oaknode_node_identity(OakNodeNode node)
{
	return reinterpret_cast<uintptr_t>(
		oaknode_c_api::to_native<olive::Node>(node));
}

int oaknode_node_set_input_at_time_into(OakNodeNode node,
		const char *input_id, int64_t time_num, int64_t time_den,
		const oaknode_value *v, int track, OakUndoCommand multi_command)
{
	if (!node.ctx || !input_id || !v || !multi_command.ctx) {
		return OAKNODE_E_INVALID;
	}
	if (!has_input(oaknode_c_api::to_native<olive::Node>(node), input_id)) {
		return OAKNODE_E_NOT_FOUND;
	}

	olive::UndoCommand *base = oakundo_capi::to_command(multi_command);
	auto *multi = dynamic_cast<olive::MultiUndoCommand *>(base);
	if (!multi) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Variant value;
		if (!oaknode_c_api::variant_from_value(v, &value)) {
			return OAKNODE_E_INVALID;
		}
		olive::Node::set_value_at_time(
			olive::NodeInput(oaknode_c_api::to_native<olive::Node>(node),
							 input_id),
			olive::core::Rational(int(time_num), int(time_den)), value, track,
			multi, true);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
