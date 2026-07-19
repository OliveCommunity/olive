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

#include "nodeparamviewtextedit.h"

#include <QHBoxLayout>

#include "dialog/text/text.h"
#include "ui/icons/icons.h"

namespace olive
{

NodeParamViewTextEdit::NodeParamViewTextEdit(QWidget *parent)
	: QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	line_edit_ = new QPlainTextEdit();
	line_edit_->setUndoRedoEnabled(true);
	connect(line_edit_, &QPlainTextEdit::textChanged, this,
			&NodeParamViewTextEdit::inner_widget_text_changed);
	layout->addWidget(line_edit_);

	edit_btn_ = new QPushButton();
	edit_btn_->setIcon(icon::tool_edit);
	edit_btn_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
	layout->addWidget(edit_btn_);
	connect(edit_btn_, &QPushButton::clicked, this,
			&NodeParamViewTextEdit::show_text_dialog);

	edit_in_viewer_btn_ = new QPushButton(tr("Edit In Viewer"));
	edit_in_viewer_btn_->setIcon(icon::pencil);
	layout->addWidget(edit_in_viewer_btn_);
	connect(edit_in_viewer_btn_, &QPushButton::clicked, this,
			&NodeParamViewTextEdit::request_edit_in_viewer);

	set_edit_in_viewer_only_mode(false);
}

void NodeParamViewTextEdit::set_edit_in_viewer_only_mode(bool on)
{
	line_edit_->setVisible(!on);
	edit_btn_->setVisible(!on);
	edit_in_viewer_btn_->setVisible(on);
}

void NodeParamViewTextEdit::show_text_dialog()
{
	TextDialog d(this->text(), this);
	if (d.exec() == QDialog::Accepted) {
		QString s = d.text();

		line_edit_->setPlainText(s);
		emit text_edited(s);
	}
}

void NodeParamViewTextEdit::inner_widget_text_changed()
{
	emit text_edited(this->text());
}

}
