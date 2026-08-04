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

#include "historywidget.h"

#include "core.h"
#include "oakengine/undo.h"

namespace olive
{

HistoryModel::HistoryModel(QObject *parent)
	: QAbstractItemModel(parent)
{
	// Refresh on Core's app-internal undo signal (issue 7 of the EventBridge
	// elimination plan) instead of a raw C event subscription; the connection
	// auto-disconnects with `this`.
	connect(Core::instance(), &Core::undo_index_changed, this, [this](int) {
		beginResetModel();
		endResetModel();
	});
}

QModelIndex HistoryModel::index(int row, int column,
								const QModelIndex &parent) const
{
	Q_UNUSED(parent)
	return createIndex(row, column, nullptr);
}

QModelIndex HistoryModel::parent(const QModelIndex &index) const
{
	Q_UNUSED(index)
	return QModelIndex();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid()) {
		return 0;
	}
	return static_cast<int>(oakengine_undo_count());
}

int HistoryModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid()) {
		return 0;
	}
	return 2;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::DisplayRole) {
		switch (index.column()) {
		case 0:
			return index.row() + 1;
		case 1: {
			char buf[1024];
			buf[0] = '\0';
			oakengine_undo_command_text(index.row(), buf, sizeof(buf));
			const QString name = QString::fromUtf8(buf);
			return name.isEmpty() ? tr("Command") : name;
		}
		}
	} else if (role == Qt::ForegroundRole) {
		// Rows at/after the current stack index are undone commands
		if (index.row() >= oakengine_undo_index()) {
			return QVariant(QColor(Qt::gray));
		}
	}

	return QVariant();
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation,
								  int role) const
{
	Q_UNUSED(orientation)
	if (role == Qt::DisplayRole) {
		switch (section) {
		case 0:
			return QStringLiteral("Number");
		case 1:
			return QStringLiteral("Action");
		}
	}

	return QVariant();
}

HistoryWidget::HistoryWidget(QWidget *parent)
	: QTreeView(parent)
{
	model_ = new HistoryModel(this);

	this->setModel(model_);
	this->setRootIsDecorated(false);
	connect(Core::instance(), &Core::undo_index_changed, this,
			&HistoryWidget::index_changed);
	connect(this->selectionModel(), &QItemSelectionModel::currentRowChanged,
			this, &HistoryWidget::current_row_changed);
}

void HistoryWidget::index_changed(int i)
{
	this->selectionModel()->select(this->model()->index(i - 1, 0),
								   QItemSelectionModel::ClearAndSelect |
									   QItemSelectionModel::Rows);
}

void HistoryWidget::current_row_changed(const QModelIndex &current,
									  const QModelIndex &previous)
{
	size_t jump_to = (current.row() + 1);
	oakengine_undo_jump(static_cast<int64_t>(jump_to));
}

}
