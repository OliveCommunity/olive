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

#include "oakengine/events.h"
#include "oakengine/undo.h"

namespace olive
{

HistoryModel::HistoryModel(QObject *parent)
	: QAbstractItemModel(parent)
{
	sub_ = oakengine_event_subscribe(
		oakengine_undo_handle(), OAKENGINE_EVENT_UNDO_INDEX_CHANGED,
		[](const oakengine_event *event, void *userdata) {
			Q_UNUSED(event)
			auto *self = static_cast<HistoryModel *>(userdata);
			self->beginResetModel();
			self->endResetModel();
		},
		this);
}

HistoryModel::~HistoryModel()
{
	if (sub_ > 0) {
		oakengine_event_unsubscribe(sub_);
	}
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
	undo_sub_ = oakengine_event_subscribe(
		oakengine_undo_handle(), OAKENGINE_EVENT_UNDO_INDEX_CHANGED,
		[](const oakengine_event *event, void *userdata) {
			auto *self = static_cast<HistoryWidget *>(userdata);
			self->index_changed(static_cast<int>(event->a));
		},
		this);
	connect(this->selectionModel(), &QItemSelectionModel::currentRowChanged,
			this, &HistoryWidget::current_row_changed);
}

HistoryWidget::~HistoryWidget()
{
	// Raw subscription carries `this` as userdata; cancel it or the engine
	// calls back into a dead widget (the undo stack outlives us).
	if (undo_sub_ > 0) {
		oakengine_event_unsubscribe(undo_sub_);
	}
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
