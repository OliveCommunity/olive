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

#include "valueconvert.h"

namespace
{

inline olive::NodeGroup *to_group(OakNodeGroup *group)
{
	return reinterpret_cast<olive::NodeGroup *>(group);
}

inline const olive::NodeGroup *to_group(const OakNodeGroup *group)
{
	return reinterpret_cast<const olive::NodeGroup *>(group);
}

inline olive::Node *to_node(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

inline OakNodeNode *from_node(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

}

OakNodeGroup *oaknode_group_create(void)
{
	try {
		olive::NodeGroup *group = new (std::nothrow) olive::NodeGroup();
		if (group) {
			oaknode_c_api::alive_inc();
		}
		return reinterpret_cast<OakNodeGroup *>(group);
	} catch (...) {
		return NULL;
	}
}

OakNodeGroup *oaknode_group_cast(OakNodeNode *node)
{
	if (!node) {
		return NULL;
	}

	try {
		return reinterpret_cast<OakNodeGroup *>(
			dynamic_cast<olive::NodeGroup *>(to_node(node)));
	} catch (...) {
		return NULL;
	}
}

void oaknode_group_free(OakNodeGroup *group)
{
	if (!group) {
		return;
	}

	try {
		delete to_group(group);
		oaknode_c_api::alive_dec();
	} catch (...) {
	}
}

int oaknode_group_add_input_passthrough(OakNodeGroup *group,
										OakNodeNode *node,
										const char *input_id, int element,
										char *buf, int buf_size)
{
	if (!group || !node || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		std::string id = to_group(group)->add_input_passthrough(
			olive::NodeInput(to_node(node), input_id, element));
		return oaknode_c_api::copy_string(id, buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_add_input_passthrough_undoable(OakNodeGroup *group,
												 OakNodeNode *node,
												 const char *input_id,
												 int element,
												 OakUndoCommand *out_command)
{
	if (!group || !node || !input_id || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeGroupAddInputPassthrough(
				to_group(group),
				olive::NodeInput(to_node(node), input_id, element)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_remove_input_passthrough(OakNodeGroup *group,
										   OakNodeNode *node,
										   const char *input_id, int element)
{
	if (!group || !node || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput input(to_node(node), input_id, element);
		if (!to_group(group)->contains_input_passthrough(input)) {
			return OAKNODE_E_NOT_FOUND;
		}
		to_group(group)->remove_input_passthrough(input);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_count(const OakNodeGroup *group, int *out_count)
{
	if (!group || !out_count) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_count =
			static_cast<int>(to_group(group)->get_input_passthroughs().size());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_id_at(const OakNodeGroup *group, int index,
									char *buf, int buf_size)
{
	if (!group) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeGroup::InputPassthroughs &passthroughs =
			to_group(group)->get_input_passthroughs();
		if (index < 0 || index >= static_cast<int>(passthroughs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}
		return oaknode_c_api::copy_string(passthroughs[size_t(index)].first, buf,
										  buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_passthrough_input_at(const OakNodeGroup *group, int index,
									   OakNodeNode **out_node, char *buf,
									   int buf_size, int *out_element)
{
	if (!group) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeGroup::InputPassthroughs &passthroughs =
			to_group(group)->get_input_passthroughs();
		if (index < 0 || index >= static_cast<int>(passthroughs.size())) {
			return OAKNODE_E_NOT_FOUND;
		}

		const olive::NodeInput &input = passthroughs[size_t(index)].second;
		if (out_node) {
			*out_node = from_node(input.node());
		}
		if (out_element) {
			*out_element = input.element();
		}
		return oaknode_c_api::copy_string(input.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_get_output_passthrough(const OakNodeGroup *group,
										 OakNodeNode **out_node)
{
	if (!group || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_node = from_node(to_group(group)->get_output_passthrough());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_set_output_passthrough(OakNodeGroup *group,
										 OakNodeNode *node)
{
	if (!group) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_group(group)->set_output_passthrough(to_node(node));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_set_output_passthrough_undoable(
	OakNodeGroup *group, OakNodeNode *node, OakUndoCommand *out_command)
{
	if (!group || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeGroupSetOutputPassthrough(to_group(group),
													 to_node(node)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_group_resolve_input(OakNodeNode *node, const char *input_id,
								int element, OakNodeNode **out_node,
								char *buf, int buf_size, int *out_element)
{
	if (!node || !input_id) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeInput resolved = olive::NodeGroup::resolve_input(
			olive::NodeInput(to_node(node), input_id, element));
		if (!resolved.is_valid()) {
			return OAKNODE_E_NOT_FOUND;
		}

		if (out_node) {
			*out_node = from_node(resolved.node());
		}
		if (out_element) {
			*out_element = resolved.element();
		}
		return oaknode_c_api::copy_string(resolved.input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
