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

#ifndef OAK_FOLDER_H
#define OAK_FOLDER_H

#include "node.h"

namespace olive
{

/**
 * @brief The Folder class representing a directory in a project structure
 *
 * The Item base class already has support for children, but this functionality is disabled by default
 * (see CanHaveChildren() override). The Folder is a specific type that enables this functionality.
 */
class Folder : public Node {
public:
	Folder();

	NODE_DEFAULT_FUNCTIONS(Folder)

	virtual std::string name() const override
	{
		return "Folder";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.folder";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_project };
	}

	virtual std::string description() const override
	{
		return "Organize several items into a single collection.";
	}

	virtual Variant data(const DataType &d) const override;

	virtual void retranslate() override;

	Node *get_child_with_name(const std::string &s) const;
	bool child_exists_with_name(const std::string &s) const
	{
		return get_child_with_name(s);
	}

	bool has_child_recursive(Node *child) const;

	int item_child_count() const
	{
		return item_children_.size();
	}

	Node *item_child(int i) const
	{
		return item_children_.at(i);
	}

	const std::vector<Node *> &children() const
	{
		return item_children_;
	}

	int index_of_child(Node *item) const
	{
		auto it = std::find(item_children_.begin(), item_children_.end(), item);
		return it == item_children_.end() ? -1 :
											int(it - item_children_.begin());
	}

	int index_of_child_in_array(Node *item) const;

	template <typename T> std::vector<T *> list_children_of_type() const
	{
		std::vector<T *> list;

		for (Node *node : item_children_) {
			T *cast_test = dynamic_cast<T *>(node);
			if (cast_test) {
				list.push_back(cast_test);
			}

			Folder *folder_test = dynamic_cast<Folder *>(node);
			if (folder_test) {
				std::vector<T *> sublist =
					folder_test->list_children_of_type<T>();
				list.insert(list.end(), sublist.begin(), sublist.end());
			}
		}

		return list;
	}

	static const std::string k_child_input;

	class RemoveElementCommand : public UndoCommand {
	public:
		RemoveElementCommand(Folder *folder, Node *child)
			: folder_(folder)
			, child_(child)
			, subcommand_(nullptr)
		{
		}

		virtual ~RemoveElementCommand() override
		{
			delete subcommand_;
		}

	protected:
		virtual void redo() override;

		virtual void undo() override
		{
			if (subcommand_) {
				subcommand_->undo_now();
			}
		}

	private:
		Folder *folder_;

		Node *child_;

		int remove_index_;

		MultiUndoCommand *subcommand_;
	};

protected:
	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output) override;

private:
	template <typename T>
	static void list_outputs_of_type_internal(const Folder *n,
										  std::vector<T *> &list, bool recursive)
	{
		for (const Node::OutputConnection &c : n->output_connections()) {
			Node *connected = c.second.node();

			T *cast_test = dynamic_cast<T *>(connected);

			if (cast_test) {
				// Avoid duplicates
				if (std::find(list.begin(), list.end(), cast_test) ==
					list.end()) {
					list.push_back(cast_test);
				}
			}

			if (recursive) {
				Folder *subfolder = dynamic_cast<Folder *>(connected);

				if (subfolder) {
					ListOutputsOfTypeInternal(subfolder, list, recursive);
				}
			}
		}
	}

	std::vector<Node *> item_children_;
	std::vector<int> item_element_index_;
};

class FolderAddChild : public UndoCommand {
public:
	FolderAddChild(Folder *folder, Node *child);

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Folder *folder_;

	Node *child_;
};

}

#endif // OAK_FOLDER_H
