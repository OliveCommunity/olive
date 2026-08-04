/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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

#ifndef OAK_HISTORYWIDGET_H
#define OAK_HISTORYWIDGET_H

#include <QAbstractItemModel>
#include <QTreeView>

#include <cstdint>

namespace olive
{

/**
 * @brief App-side undo history model over the engine C ABI.
 *
 * Replaces the direct use of the engine's UndoStack as a Qt item model.
 * Semantics mirror engine/undo/undostack.cpp: two columns (Number, Action),
 * rows are all commands on the stack (done first, then undone), undone rows
 * are shown gray. Refreshes itself on Core::undo_index_changed.
 */
class HistoryModel : public QAbstractItemModel {
	Q_OBJECT
public:
	explicit HistoryModel(QObject *parent = nullptr);

	QModelIndex index(int row, int column,
					  const QModelIndex &parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex &index) const override;
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index,
				  int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
						int role = Qt::DisplayRole) const override;
};

class HistoryWidget : public QTreeView {
	Q_OBJECT
public:
	HistoryWidget(QWidget *parent = nullptr);

private:
	HistoryModel *model_;

	size_t current_row_;

private slots:
	void index_changed(int i);

	void current_row_changed(const QModelIndex &current,
						   const QModelIndex &previous);
};

}

#endif // OAK_HISTORYWIDGET_H
