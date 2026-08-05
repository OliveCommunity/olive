/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "group.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "serializeddata.h"

namespace olive
{

#define super Node

NodeGroup::NodeGroup()
	: output_passthrough_(nullptr)
{
	set_flag(k_dont_show_in_create_menu);
}

std::string NodeGroup::name() const
{
	return "Group";
}

std::string NodeGroup::id() const
{
	return "org.olivevideoeditor.Olive.group";
}

std::vector<Node::CategoryID> NodeGroup::category() const
{
	return { k_category_unknown };
}

std::string NodeGroup::description() const
{
	return "A group of nodes that is represented as a single node.";
}

void NodeGroup::retranslate()
{
	super::retranslate();

	for (auto it = get_context_positions().cbegin();
		 it != get_context_positions().cend(); it++) {
		it->first->retranslate();
	}
}

bool NodeGroup::load_custom(XmlStreamReader *reader, SerializedData *data)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "inputpassthroughs") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "inputpassthrough") {
					SerializedData::GroupLink link;

					link.group = this;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == "node") {
							link.input_node = strtoull(
								reader->read_element_text().c_str(), nullptr, 10);
						} else if (reader->name() == "input") {
							link.input_id = reader->read_element_text();
						} else if (reader->name() == "element") {
							link.input_element = int(strtol(
								reader->read_element_text().c_str(), nullptr, 10));
						} else if (reader->name() == "id") {
							link.passthrough_id = reader->read_element_text();
						} else if (reader->name() == "name") {
							link.custom_name = reader->read_element_text();
						} else if (reader->name() == "flags") {
							link.custom_flags = InputFlags(strtoull(
								reader->read_element_text().c_str(), nullptr, 10));
						} else if (reader->name() == "type") {
							link.data_type = NodeValue::get_data_type_from_name(
								reader->read_element_text());
						} else if (reader->name() == "default") {
							link.default_val = NodeValue::string_to_value(
								link.data_type, reader->read_element_text(),
								false);
						} else if (reader->name() == "properties") {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == "property") {
									std::string key;
									std::string value;

									while (xml_read_next_start_element(reader)) {
										if (reader->name() == "key") {
											key = reader->read_element_text();
										} else if (reader->name() == "value") {
											value = reader->read_element_text();
										} else {
											reader->skip_current_element();
										}
									}

									if (!key.empty()) {
										link.custom_properties.insert({ key,
																		value });
									}
								} else {
									reader->skip_current_element();
								}
							}
						} else {
							reader->skip_current_element();
						}
					}

					data->group_input_links.push_back(link);
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "outputpassthrough") {
			data->group_output_links.insert(
				{ this, strtoull(reader->read_element_text().c_str(), nullptr,
								 10) });
		} else {
			reader->skip_current_element();
		}
	}

	return true;
}

void NodeGroup::save_custom(XmlStreamWriter *writer) const
{
	writer->write_start_element("inputpassthroughs");

	for (const NodeGroup::InputPassthrough &ip :
		 this->get_input_passthroughs()) {
		writer->write_start_element("inputpassthrough");

		// Reference to inner input
		writer->write_text_element(
			"node",
			std::to_string(reinterpret_cast<uintptr_t>(ip.second.node())));
		writer->write_text_element("input", ip.second.input());
		writer->write_text_element("element",
								 std::to_string(ip.second.element()));

		// ID of passthrough
		writer->write_text_element("id", ip.first);

		// Passthrough-specific details
		const std::string &input = ip.first;
		writer->write_text_element("name", this->Node::get_input_name(input));

		writer->write_text_element(
			"flags",
			std::to_string(
				(get_input_flags(input) & ~ip.second.get_flags()).value()));

		NodeValue::Type data_type = get_input_data_type(input);
		writer->write_text_element("type",
								 NodeValue::get_data_type_name(data_type));

		writer->write_text_element(
			"default",
			NodeValue::value_to_string(data_type, get_default_value(input), false));

		writer->write_start_element("properties");
		auto p = get_input_properties(input);
		for (auto it = p.cbegin(); it != p.cend(); it++) {
			writer->write_start_element("property");
			writer->write_text_element("key", it->first);
			writer->write_text_element("value", it->second.to_string());
			writer->write_end_element(); // property
		}
		writer->write_end_element(); // properties

		writer->write_end_element(); // input
	}

	writer->write_end_element(); // inputpassthroughs

	writer->write_text_element(
		"outputpassthrough",
		std::to_string(
			reinterpret_cast<uintptr_t>(this->get_output_passthrough())));
}

void NodeGroup::PostLoadEvent(SerializedData *data)
{
	super::PostLoadEvent(data);

	for (const SerializedData::GroupLink &l : data->group_input_links) {
		auto node_it = data->node_ptrs.find(l.input_node);
		if (node_it != data->node_ptrs.end()) {
			Node *input_node = node_it->second;
			NodeInput resolved(input_node, l.input_id, l.input_element);

			l.group->add_input_passthrough(resolved, l.passthrough_id);

			l.group->set_input_flag(l.passthrough_id,
								  InputFlag(l.custom_flags.value()));

			if (!l.custom_name.empty()) {
				l.group->set_input_name(l.passthrough_id, l.custom_name);
			}

			l.group->set_input_data_type(l.passthrough_id, l.data_type);

			l.group->set_default_value(l.passthrough_id, l.default_val);

			for (auto it = l.custom_properties.cbegin();
				 it != l.custom_properties.cend(); it++) {
				l.group->set_input_property(l.passthrough_id, it->first,
										  it->second);
			}
		}
	}

	for (auto it = data->group_output_links.cbegin();
		 it != data->group_output_links.cend(); it++) {
		auto node_it = data->node_ptrs.find(it->second);
		if (node_it != data->node_ptrs.end()) {
			it->first->set_output_passthrough(node_it->second);
		}
	}
}

std::string NodeGroup::add_input_passthrough(const NodeInput &input,
									   const std::string &force_id)
{
	assert(context_contains_node(input.node()));

	for (auto it = input_passthroughs_.cbegin();
		 it != input_passthroughs_.cend(); it++) {
		if (it->second == input) {
			// Already passing this input through
			return it->first;
		}
	}

	// Add input
	std::string id;
	if (force_id.empty()) {
		id = input.input();
		int i = 2;
		while (has_input_with_id(id)) {
			id = input.input() + "_" + std::to_string(i);
			i++;
		}
	} else {
		id = force_id;

		bool already_exists = false;
		for (auto it = input_passthroughs_.cbegin();
			 it != input_passthroughs_.cend(); it++) {
			if (it->first == id) {
				already_exists = true;
				break;
			}
		}

		assert(!already_exists);
	}

	add_input(id, input.get_data_type(), input.get_default_value(),
			 input.get_flags());

	input_passthroughs_.push_back({ id, input });

	return id;
}

void NodeGroup::remove_input_passthrough(const NodeInput &input)
{
	for (auto it = input_passthroughs_.begin(); it != input_passthroughs_.end();
		 it++) {
		if (it->second == input) {
			remove_input(it->first);
			input_passthroughs_.erase(it);
			break;
		}
	}
}

void NodeGroup::set_output_passthrough(Node *node)
{
	assert(!node || context_contains_node(node));

	output_passthrough_ = node;
}

bool NodeGroup::contains_input_passthrough(const NodeInput &input) const
{
	for (auto it = input_passthroughs_.cbegin();
		 it != input_passthroughs_.cend(); it++) {
		if (it->second == input) {
			return true;
		}
	}

	return false;
}

std::string NodeGroup::get_input_name(const std::string &id) const
{
	// If an override name was set, use that
	std::string override = super::get_input_name(id);
	if (!override.empty()) {
		return override;
	}

	// Call GetInputName of passed through node, which may be another group
	NodeInput pass = get_input_from_id(id);
	if (!pass.is_valid()) {
		return std::string();
	}
	return pass.node()->get_input_name(pass.input());
}

NodeInput NodeGroup::resolve_input(NodeInput input)
{
	while (get_inner(&input)) {
	}

	return input;
}

bool NodeGroup::get_inner(NodeInput *input)
{
	if (NodeGroup *g = dynamic_cast<NodeGroup *>(input->node())) {
		const NodeInput &passthrough = g->get_input_from_id(input->input());
		if (!passthrough.is_valid()) {
			return false;
		}

		input->set_node(passthrough.node());
		input->set_input(passthrough.input());
		return true;
	} else {
		return false;
	}
}

void NodeGroupAddInputPassthrough::redo()
{
	if (!group_->contains_input_passthrough(input_)) {
		group_->add_input_passthrough(input_, force_id_);
		actually_added_ = true;
	} else {
		actually_added_ = false;
	}
}

void NodeGroupAddInputPassthrough::undo()
{
	if (actually_added_) {
		group_->remove_input_passthrough(input_);
	}
}

void NodeGroupSetOutputPassthrough::redo()
{
	old_output_ = group_->get_output_passthrough();
	group_->set_output_passthrough(new_output_);
}

void NodeGroupSetOutputPassthrough::undo()
{
	group_->set_output_passthrough(old_output_);
}

}
