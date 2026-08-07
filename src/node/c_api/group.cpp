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

#include "node/group.h"

#include "group/group.h"

#include "nodehandle.h"
#include "valueconvert.h"

using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

OakNodeGroup oaknode_group_create(void)
{
	try {
		return make_handle<OakNodeGroup>(new (std::nothrow) olive::NodeGroup(),
										 true,
										 oaknode_c_api::delete_as<olive::NodeGroup>);
	} catch (...) {
		return OakNodeGroup{};
	}
}

OakNodeGroup oaknode_group_cast(OakNodeNode node)
{
	olive::Node *native = to_native<olive::Node>(node);
	if (!native) {
		return OakNodeGroup{};
	}

	try {
		return make_handle<OakNodeGroup>(
			dynamic_cast<olive::NodeGroup *>(native), false, nullptr);
	} catch (...) {
		return OakNodeGroup{};
	}
}

void oaknode_group_free(OakNodeGroup *group)
{
	try {
		oaknode_c_api::free_handle(group);
	} catch (...) {
	}
}

int oaknode_group_add_input_passthrough(OakNodeGroup group,
										OakNodeNode node,
										const char *input_id, int element,
										char *buf, int buf_size)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	olive::Node *n = to_native<olive::Node>(node);
	if (!g || !n || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		std::string id = g->add_input_passthrough(
			olive::NodeInput(n, input_id, element));
		return oaknode_c_api::copy_string(id, buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_add_input_passthrough_undoable(OakNodeGroup group,
												 OakNodeNode node,
												 const char *input_id,
												 int element,
												 OakUndoCommand *out_command)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	olive::Node *n = to_native<olive::Node>(node);
	if (!g || !n || !input_id || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeGroupAddInputPassthrough(
				g, olive::NodeInput(n, input_id, element)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_remove_input_passthrough(OakNodeGroup group,
										   OakNodeNode node,
										   const char *input_id, int element)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	olive::Node *n = to_native<olive::Node>(node);
	if (!g || !n || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput input(n, input_id, element);
		if (!g->contains_input_passthrough(input)) {
			return OAKNODE_E_NOT_FOUND;
		}
		g->remove_input_passthrough(input);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_count(OakNodeGroup group, int *out_count)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count = static_cast<int>(g->get_input_passthroughs().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_id_at(OakNodeGroup group, int index,
									char *buf, int buf_size)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeGroup::InputPassthroughs &passthroughs =
			g->get_input_passthroughs();
		if (index < 0 || index >= static_cast<int>(passthroughs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(passthroughs[size_t(index)].first, buf,
										  buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_input_at(OakNodeGroup group, int index,
									   OakNodeNode *out_node, char *buf,
									   int buf_size, int *out_element)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeGroup::InputPassthroughs &passthroughs =
			g->get_input_passthroughs();
		if (index < 0 || index >= static_cast<int>(passthroughs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}

		const olive::NodeInput &input = passthroughs[size_t(index)].second;
		if (out_node) {
			*out_node = make_handle<OakNodeNode>(input.node(), false, nullptr);
		}
		if (out_element) {
			*out_element = input.element();
		}
		return oaknode_c_api::copy_string(input.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_get_output_passthrough(OakNodeGroup group,
										 OakNodeNode *out_node)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_node =
			make_handle<OakNodeNode>(g->get_output_passthrough(), false, nullptr);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_set_output_passthrough(OakNodeGroup group,
										 OakNodeNode node)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g) {
		return OAKNODE_E_INVALID;
	}

	try {
		g->set_output_passthrough(to_native<olive::Node>(node));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_set_output_passthrough_undoable(
	OakNodeGroup group, OakNodeNode node, OakUndoCommand *out_command)
{
	olive::NodeGroup *g = to_native<olive::NodeGroup>(group);
	if (!g || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeGroupSetOutputPassthrough(
				g, to_native<olive::Node>(node)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_resolve_input(OakNodeNode node, const char *input_id,
								int element, OakNodeNode *out_node,
								char *buf, int buf_size, int *out_element)
{
	olive::Node *n = to_native<olive::Node>(node);
	if (!n || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput resolved = olive::NodeGroup::resolve_input(
			olive::NodeInput(n, input_id, element));
		if (!resolved.is_valid()) {
			return OAKNODE_E_NOT_FOUND;
		}

		if (out_node) {
			*out_node = make_handle<OakNodeNode>(resolved.node(), false, nullptr);
		}
		if (out_element) {
			*out_element = resolved.element();
		}
		return oaknode_c_api::copy_string(resolved.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
