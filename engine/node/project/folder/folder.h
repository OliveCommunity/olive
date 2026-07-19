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

#include "node/node.h"

namespace olive
{

/**
 * @brief The Folder class representing a directory in a project structure
 *
 * The Item base class already has support for children, but this functionality is disabled by default
 * (see CanHaveChildren() override). The Folder is a specific type that enables this functionality.
 */
class Folder : public Node {
	Q_OBJECT
public:
	Folder();

	NODE_DEFAULT_FUNCTIONS(Folder)

	virtual QString name() const override
	{
		return tr("Folder");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.folder");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_project };
	}

	virtual QString description() const override
	{
		return tr("Organize several items into a single collection.");
	}

	virtual QVariant data(const DataType &d) const override;

	virtual void retranslate() override;

	Node *get_child_with_name(const QString &s) const;
	bool child_exists_with_name(const QString &s) const
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

	const QVector<Node *> &children() const
	{
		return item_children_;
	}

	int index_of_child(Node *item) const
	{
		return item_children_.indexOf(item);
	}

	int index_of_child_in_array(Node *item) const;

	template <typename T> QVector<T *> list_children_of_type() const
	{
		QVector<T *> list;

		foreach (Node *node, item_children_) {
			T *cast_test = dynamic_cast<T *>(node);
			if (cast_test) {
				list.append(cast_test);
			}

			Folder *folder_test = dynamic_cast<Folder *>(node);
			if (folder_test) {
				list.append(folder_test->list_children_of_type<T>());
			}
		}

		return list;
	}

	static const QString k_child_input;

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

		virtual Project *get_relevant_project() const override
		{
			return folder_->project();
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

signals:
	void begin_insert_item(Node *n, int index);

	void end_insert_item();

	void begin_remove_item(Node *n, int index);

	void end_remove_item();

protected:
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;

private:
	template <typename T>
	static void list_outputs_of_type_internal(const Folder *n, QVector<T *> &list,
										  bool recursive)
	{
		foreach (const Node::OutputConnection &c, n->output_connections()) {
			Node *connected = c.second.node();

			T *cast_test = dynamic_cast<T *>(connected);

			if (cast_test) {
				// Avoid duplicates
				if (!list.contains(cast_test)) {
					list.append(cast_test);
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

	QVector<Node *> item_children_;
	QVector<int> item_element_index_;
};

class FolderAddChild : public UndoCommand {
public:
	FolderAddChild(Folder *folder, Node *child);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Folder *folder_;

	Node *child_;
};

}

#endif // OAK_FOLDER_H
