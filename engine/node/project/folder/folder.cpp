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

#include "folder.h"

#include "common/xmlutils.h"
#include "node/nodeundo.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"

namespace olive
{

#define super Node

const QString Folder::k_child_input = QStringLiteral("child_in");

Folder::Folder()
{
	set_flag(k_is_item);

	add_input(k_child_input, NodeValue::k_none,
			 InputFlags(k_input_flag_array | k_input_flag_not_keyframable));
}

QVariant Folder::data(const DataType &d) const
{
	if (d == icon) {
		return QStringLiteral("folder");
	}

	return super::data(d);
}

void Folder::retranslate()
{
	super::retranslate();

	set_input_name(k_child_input, tr("Children"));
}

Node *get_child_with_name_internal(const Folder *n, const QString &s)
{
	for (int i = 0; i < n->item_child_count(); i++) {
		Node *child = n->item_child(i);

		if (child->get_label() == s) {
			return child;
		} else if (Folder *subfolder = dynamic_cast<Folder *>(child)) {
			if (Node *n2 = get_child_with_name_internal(subfolder, s)) {
				return n2;
			}
		}
	}

	return nullptr;
}

Node *Folder::get_child_with_name(const QString &s) const
{
	return get_child_with_name_internal(this, s);
}

bool Folder::has_child_recursive(Node *child) const
{
	for (Node *i : item_children_) {
		if (i == child) {
			return true;
		} else if (Folder *f = dynamic_cast<Folder *>(i)) {
			if (f->has_child_recursive(child)) {
				return true;
			}
		}
	}

	return false;
}

int Folder::index_of_child_in_array(Node *item) const
{
	int index_of_item = item_children_.indexOf(item);

	if (index_of_item == -1) {
		return -1;
	}

	return item_element_index_.at(index_of_item);
}

void Folder::InputConnectedEvent(const QString &input, int element,
								 Node *output)
{
	if (input == k_child_input && element != -1) {
		Node *item = output;

		// The insert index is always our "count" because we only support appending in our internal
		// model. For sorting/organizing, a QSortFilterProxyModel is used instead.
		emit begin_insert_item(item, item_child_count());
		item_children_.append(item);
		item_element_index_.append(element);
		item->set_folder(this);
		emit end_insert_item();
	}
}

void Folder::InputDisconnectedEvent(const QString &input, int element,
									Node *output)
{
	if (input == k_child_input && element != -1) {
		Node *item = output;

		int child_index = item_children_.indexOf(item);
		emit begin_remove_item(item, child_index);
		item_children_.removeAt(child_index);
		item_element_index_.removeAt(child_index);
		item->set_folder(nullptr);
		emit end_remove_item();
	}
}

FolderAddChild::FolderAddChild(Folder *folder, Node *child)
	: folder_(folder)
	, child_(child)
{
}

Project *FolderAddChild::get_relevant_project() const
{
	return folder_->project();
}

void FolderAddChild::redo()
{
	int array_index = folder_->input_array_size(Folder::k_child_input);
	folder_->input_array_append(Folder::k_child_input);
	Node::connect_edge(child_,
					  NodeInput(folder_, Folder::k_child_input, array_index));
}

void FolderAddChild::undo()
{
	Node::disconnect_edge(
		child_, NodeInput(folder_, Folder::k_child_input,
						  folder_->input_array_size(Folder::k_child_input) - 1));
	folder_->input_array_remove_last(Folder::k_child_input);
}

void Folder::RemoveElementCommand::redo()
{
	if (!subcommand_) {
		remove_index_ = folder_->index_of_child_in_array(child_);
		if (remove_index_ != -1) {
			NodeInput connected_input(folder_, Folder::k_child_input,
									  remove_index_);
			subcommand_ = new MultiUndoCommand();
			subcommand_->add_child(new NodeEdgeRemoveCommand(
				folder_->get_connected_output(connected_input), connected_input));
			subcommand_->add_child(new NodeArrayRemoveCommand(
				folder_, Folder::k_child_input, remove_index_));
		}
	}

	if (subcommand_) {
		subcommand_->redo_now();
	}
}

}
