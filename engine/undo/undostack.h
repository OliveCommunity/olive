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

#ifndef OAK_UNDOSTACK_H
#define OAK_UNDOSTACK_H

#include <QAction>
#include <QAbstractItemModel>

#include "common/define.h"
#include "undo/undocommand.h"

namespace olive
{

class UndoStack : public QAbstractItemModel {
	Q_OBJECT
public:
	UndoStack();

	virtual ~UndoStack() override;

	void push(UndoCommand *command, const QString &name);

  /**
   * @brief Push a command that has already been executed (redo skipped).
   *
   * Used by the facade undo-group: child commands are added to the group
   * and executed eagerly, then the whole group is pushed with this method
   * so it is not redone again.  Empty commands are discarded.
   */
  void push_pre_executed(UndoCommand *command, const QString &name);

	void jump(size_t index);

	void clear();

	bool can_undo() const;

	bool can_redo() const
	{
		return !undone_commands_.empty();
	}

	void update_actions();

	QAction *GetUndoAction()
	{
		return undo_action_;
	}

	QAction *GetRedoAction()
	{
		return redo_action_;
	}

	virtual int
	columnCount(const QModelIndex &parent = QModelIndex()) const override;
	virtual QVariant data(const QModelIndex &index,
						  int role = Qt::DisplayRole) const override;
	virtual QModelIndex
	index(int row, int column,
		  const QModelIndex &parent = QModelIndex()) const override;
	virtual QModelIndex parent(const QModelIndex &index) const override;
	virtual int
	rowCount(const QModelIndex &parent = QModelIndex()) const override;
	virtual QVariant headerData(int section, Qt::Orientation orientation,
								int role = Qt::DisplayRole) const override;
	virtual bool
	hasChildren(const QModelIndex &parent = QModelIndex()) const override;

	// Facade accessors (oakengine/undo.h C ABI): row-based history queries.
	// Rows 0..done_count()-1 are done commands (commands_ in order), rows
	// done_count()..command_count()-1 are undone commands (undone_commands_
	// in order, most recently undone first).
	int command_count() const
	{
		return int(commands_.size() + undone_commands_.size());
	}

	int done_count() const
	{
		return int(commands_.size());
	}

	bool command_is_done(int row) const
	{
		return row >= 0 && row < done_count();
	}

	QString command_name(int row) const
	{
		if (row < 0 || row >= command_count()) {
			return QString();
		}
		if (row < done_count()) {
			auto it = commands_.begin();
			std::advance(it, row);
			return it->name;
		}
		auto it = undone_commands_.begin();
		std::advance(it, row - done_count());
		return it->name;
	}

signals:
	void index_changed(int i);

public slots:
	void undo();

	void redo();

private:
	static const int k_max_undo_commands;

	struct CommandEntry {
		UndoCommand *command;
		QString name;
	};

	std::list<CommandEntry> commands_;

	std::list<CommandEntry> undone_commands_;

	QAction *undo_action_;

	QAction *redo_action_;
};

}

#endif // OAK_UNDOSTACK_H
