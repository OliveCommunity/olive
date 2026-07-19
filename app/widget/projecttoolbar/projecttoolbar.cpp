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

#include "projecttoolbar.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QButtonGroup>

#include "ui/icons/icons.h"

namespace olive
{

ProjectToolbar::ProjectToolbar(QWidget *parent)
	: QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	new_button_ = new QPushButton();
	connect(new_button_, &QPushButton::clicked, this,
			&ProjectToolbar::new_clicked);
	layout->addWidget(new_button_);

	open_button_ = new QPushButton();
	connect(open_button_, &QPushButton::clicked, this,
			&ProjectToolbar::open_clicked);
	layout->addWidget(open_button_);

	save_button_ = new QPushButton();
	connect(save_button_, &QPushButton::clicked, this,
			&ProjectToolbar::save_clicked);
	layout->addWidget(save_button_);

	search_field_ = new QLineEdit();
	search_field_->setClearButtonEnabled(true);
	connect(search_field_, &QLineEdit::textChanged, this,
			&ProjectToolbar::search_changed);
	layout->addWidget(search_field_);

	tree_button_ = new QPushButton();
	tree_button_->setCheckable(true);
	connect(tree_button_, &QPushButton::clicked, this,
			&ProjectToolbar::view_button_clicked);
	layout->addWidget(tree_button_);

	list_button_ = new QPushButton();
	list_button_->setCheckable(true);
	connect(list_button_, &QPushButton::clicked, this,
			&ProjectToolbar::view_button_clicked);
	layout->addWidget(list_button_);

	icon_button_ = new QPushButton();
	icon_button_->setCheckable(true);
	connect(icon_button_, &QPushButton::clicked, this,
			&ProjectToolbar::view_button_clicked);
	layout->addWidget(icon_button_);

	// Group Tree/List/Icon view buttons into a button group for easy exclusive-buttons
	QButtonGroup *view_button_group = new QButtonGroup(this);
	view_button_group->setExclusive(true);
	view_button_group->addButton(tree_button_);
	view_button_group->addButton(list_button_);
	view_button_group->addButton(icon_button_);

	retranslate();
	update_icons();
}

void ProjectToolbar::set_view(ViewType type)
{
	switch (type) {
	case tree_view:
		tree_button_->setChecked(true);
		break;
	case icon_view:
		icon_button_->setChecked(true);
		break;
	case list_view:
		list_button_->setChecked(true);
		break;
	}
}

void ProjectToolbar::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	} else if (e->type() == QEvent::StyleChange) {
		update_icons();
	}
	QWidget::changeEvent(e);
}

void ProjectToolbar::retranslate()
{
	new_button_->setToolTip(tr("New..."));
	open_button_->setToolTip(tr("Open Project"));
	save_button_->setToolTip(tr("Save Project"));

	search_field_->setPlaceholderText(tr("Search media, markers, etc."));

	tree_button_->setToolTip(tr("Tree View"));
	list_button_->setToolTip(tr("List View"));
	icon_button_->setToolTip(tr("Icon View"));
}

void ProjectToolbar::update_icons()
{
	new_button_->setIcon(icon::New);
	open_button_->setIcon(icon::open);
	save_button_->setIcon(icon::save);
	tree_button_->setIcon(icon::tree_view);
	list_button_->setIcon(icon::list_view);
	icon_button_->setIcon(icon::icon_view);
}

void ProjectToolbar::view_button_clicked()
{
	// Determine which view button triggered this slot and emit a signal accordingly
	if (sender() == tree_button_) {
		emit view_changed(ProjectToolbar::tree_view);
	} else if (sender() == icon_button_) {
		emit view_changed(ProjectToolbar::icon_view);
	} else if (sender() == list_button_) {
		emit view_changed(ProjectToolbar::list_view);
	} else {
		// Assert that it was one of the above buttons
		abort();
	}
}

}
