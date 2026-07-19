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

#include "node/serializeddata.h"

namespace olive
{

#define super Node

NodeGroup::NodeGroup()
	: output_passthrough_(nullptr)
{
	set_flag(k_dont_show_in_create_menu);
}

QString NodeGroup::name() const
{
	return tr("Group");
}

QString NodeGroup::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.group");
}

QVector<Node::CategoryID> NodeGroup::category() const
{
	return { k_category_unknown };
}

QString NodeGroup::description() const
{
	return tr("A group of nodes that is represented as a single node.");
}

void NodeGroup::retranslate()
{
	super::retranslate();

	for (auto it = get_context_positions().cbegin();
		 it != get_context_positions().cend(); it++) {
		it.key()->retranslate();
	}
}

bool NodeGroup::load_custom(QXmlStreamReader *reader, SerializedData *data)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("inputpassthroughs")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("inputpassthrough")) {
					SerializedData::GroupLink link;

					link.group = this;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("node")) {
							link.input_node =
								reader->readElementText().toULongLong();
						} else if (reader->name() == QStringLiteral("input")) {
							link.input_id = reader->readElementText();
						} else if (reader->name() ==
								   QStringLiteral("element")) {
							link.input_element =
								reader->readElementText().toInt();
						} else if (reader->name() == QStringLiteral("id")) {
							link.passthrough_id = reader->readElementText();
						} else if (reader->name() == QStringLiteral("name")) {
							link.custom_name = reader->readElementText();
						} else if (reader->name() == QStringLiteral("flags")) {
							link.custom_flags = InputFlags(
								reader->readElementText().toULongLong());
						} else if (reader->name() == QStringLiteral("type")) {
							link.data_type = NodeValue::get_data_type_from_name(
								reader->readElementText());
						} else if (reader->name() ==
								   QStringLiteral("default")) {
							link.default_val = NodeValue::string_to_value(
								link.data_type, reader->readElementText(),
								false);
						} else if (reader->name() ==
								   QStringLiteral("properties")) {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() ==
									QStringLiteral("property")) {
									QString key;
									QString value;

									while (xml_read_next_start_element(reader)) {
										if (reader->name() ==
											QStringLiteral("key")) {
											key = reader->readElementText();
										} else if (reader->name() ==
												   QStringLiteral("value")) {
											value = reader->readElementText();
										} else {
											reader->skipCurrentElement();
										}
									}

									if (!key.isEmpty()) {
										link.custom_properties.insert(key,
																	  value);
									}
								} else {
									reader->skipCurrentElement();
								}
							}
						} else {
							reader->skipCurrentElement();
						}
					}

					data->group_input_links.append(link);
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("outputpassthrough")) {
			data->group_output_links.insert(
				this, reader->readElementText().toULongLong());
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void NodeGroup::save_custom(QXmlStreamWriter *writer) const
{
	writer->writeStartElement(QStringLiteral("inputpassthroughs"));

	foreach (const NodeGroup::InputPassthrough &ip,
			 this->get_input_passthroughs()) {
		writer->writeStartElement(QStringLiteral("inputpassthrough"));

		// Reference to inner input
		writer->writeTextElement(
			QStringLiteral("node"),
			QString::number(reinterpret_cast<quintptr>(ip.second.node())));
		writer->writeTextElement(QStringLiteral("input"), ip.second.input());
		writer->writeTextElement(QStringLiteral("element"),
								 QString::number(ip.second.element()));

		// ID of passthrough
		writer->writeTextElement(QStringLiteral("id"), ip.first);

		// Passthrough-specific details
		const QString &input = ip.first;
		writer->writeTextElement(QStringLiteral("name"),
								 this->Node::get_input_name(input));

		writer->writeTextElement(
			QStringLiteral("flags"),
			QString::number(
				(get_input_flags(input) & ~ip.second.get_flags()).value()));

		NodeValue::Type data_type = get_input_data_type(input);
		writer->writeTextElement(QStringLiteral("type"),
								 NodeValue::get_data_type_name(data_type));

		writer->writeTextElement(
			QStringLiteral("default"),
			NodeValue::value_to_string(data_type, get_default_value(input), false));

		writer->writeStartElement(QStringLiteral("properties"));
		auto p = get_input_properties(input);
		for (auto it = p.cbegin(); it != p.cend(); it++) {
			writer->writeStartElement(QStringLiteral("property"));
			writer->writeTextElement(QStringLiteral("key"), it.key());
			writer->writeTextElement(QStringLiteral("value"),
									 it.value().toString());
			writer->writeEndElement(); // property
		}
		writer->writeEndElement(); // properties

		writer->writeEndElement(); // input
	}

	writer->writeEndElement(); // inputpassthroughs

	writer->writeTextElement(QStringLiteral("outputpassthrough"),
							 QString::number(reinterpret_cast<quintptr>(
								 this->get_output_passthrough())));
}

void NodeGroup::PostLoadEvent(SerializedData *data)
{
	super::PostLoadEvent(data);

	foreach (const SerializedData::GroupLink &l, data->group_input_links) {
		if (Node *input_node = data->node_ptrs.value(l.input_node)) {
			NodeInput resolved(input_node, l.input_id, l.input_element);

			l.group->add_input_passthrough(resolved, l.passthrough_id);

			l.group->set_input_flag(l.passthrough_id,
								  InputFlag(l.custom_flags.value()));

			if (!l.custom_name.isEmpty()) {
				l.group->set_input_name(l.passthrough_id, l.custom_name);
			}

			l.group->set_input_data_type(l.passthrough_id, l.data_type);

			l.group->set_default_value(l.passthrough_id, l.default_val);

			for (auto it = l.custom_properties.cbegin();
				 it != l.custom_properties.cend(); it++) {
				l.group->set_input_property(l.passthrough_id, it.key(),
										  it.value());
			}
		}
	}

	for (auto it = data->group_output_links.cbegin();
		 it != data->group_output_links.cend(); it++) {
		if (Node *output_node = data->node_ptrs.value(it.value())) {
			it.key()->set_output_passthrough(output_node);
		}
	}
}

QString NodeGroup::add_input_passthrough(const NodeInput &input,
									   const QString &force_id)
{
	Q_ASSERT(context_contains_node(input.node()));

	for (auto it = input_passthroughs_.cbegin();
		 it != input_passthroughs_.cend(); it++) {
		if (it->second == input) {
			// Already passing this input through
			return it->first;
		}
	}

	// Add input
	QString id;
	if (force_id.isEmpty()) {
		id = input.input();
		int i = 2;
		while (has_input_with_id(id)) {
			id = QStringLiteral("%1_%2").arg(input.input(), QString::number(i));
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

		Q_ASSERT(!already_exists);
	}

	add_input(id, input.get_data_type(), input.get_default_value(),
			 input.get_flags());

	input_passthroughs_.append({ id, input });

	emit input_passthrough_added(this, input);

	return id;
}

void NodeGroup::remove_input_passthrough(const NodeInput &input)
{
	for (auto it = input_passthroughs_.begin(); it != input_passthroughs_.end();
		 it++) {
		if (it->second == input) {
			remove_input(it->first);
			emit input_passthrough_removed(this, it->second);
			input_passthroughs_.erase(it);
			break;
		}
	}
}

void NodeGroup::set_output_passthrough(Node *node)
{
	Q_ASSERT(!node || context_contains_node(node));

	output_passthrough_ = node;

	emit output_passthrough_changed(this, output_passthrough_);
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

QString NodeGroup::get_input_name(const QString &id) const
{
	// If an override name was set, use that
	QString override = super::get_input_name(id);
	if (!override.isEmpty()) {
		return override;
	}

	// Call GetInputName of passed through node, which may be another group
	NodeInput pass = get_input_from_id(id);
	if (!pass.is_valid()) {
		return QString();
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
