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
#include "oakengine/events.h"
#include "oakengine/undo.h"

namespace olive
{

HistoryWidget::HistoryWidget(QWidget *parent)
	: QTreeView(parent)
{
	stack_ = Core::instance()->undo_stack();

	this->setModel(stack_);
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
