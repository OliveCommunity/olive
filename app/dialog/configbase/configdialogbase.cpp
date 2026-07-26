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

#include "configdialogbase.h"

#include <QDialogButtonBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "core.h"

#include "oakengine/undo.h"
namespace olive
{

ConfigDialogBase::ConfigDialogBase(QWidget *parent)
	: QDialog(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	QSplitter *splitter = new QSplitter();
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter);

	list_widget_ = new QListWidget();

	preference_pane_stack_ = new QStackedWidget(this);

	splitter->addWidget(list_widget_);
	splitter->addWidget(preference_pane_stack_);

	QDialogButtonBox *button_box = new QDialogButtonBox(this);
	button_box->setOrientation(Qt::Horizontal);
	button_box->setStandardButtons(QDialogButtonBox::Cancel |
								   QDialogButtonBox::Ok);

	layout->addWidget(button_box);

	connect(button_box, &QDialogButtonBox::accepted, this,
			&ConfigDialogBase::accept);
	connect(button_box, &QDialogButtonBox::rejected, this,
			&ConfigDialogBase::reject);

	connect(list_widget_, &QListWidget::currentRowChanged,
			preference_pane_stack_, &QStackedWidget::setCurrentIndex);
}

void ConfigDialogBase::accept()
{
	foreach (ConfigDialogBaseTab *tab, tabs_) {
		if (!tab->validate()) {
			return;
		}
	}

	void *command = oakengine_undo_command_create_multi();

	foreach (ConfigDialogBaseTab *tab, tabs_) {
		tab->accept(command);
	}

	oakengine_undo_push(command, tr("Set Configuration").toUtf8().constData());

	AcceptEvent();

	QDialog::accept();
}

void ConfigDialogBase::add_tab(ConfigDialogBaseTab *tab, const QString &title)
{
	list_widget_->addItem(title);
	preference_pane_stack_->addWidget(tab);

	tabs_.append(tab);
}

void ConfigDialogBase::set_current_tab(int index)
{
	if (index >= 0 && index < list_widget_->count()) {
		list_widget_->setCurrentRow(index);
	}
}

}
