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

#ifndef OAK_NODEPARAMVIEWTEXTEDIT_H
#define OAK_NODEPARAMVIEWTEXTEDIT_H

#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

#include "oakutil/define.h"

namespace olive
{

class NodeParamViewTextEdit : public QWidget {
	Q_OBJECT
public:
	NodeParamViewTextEdit(QWidget *parent = nullptr);

	QString text() const
	{
		return line_edit_->toPlainText();
	}

	void set_edit_in_viewer_only_mode(bool on);

public slots:
	void setText(const QString &s)
	{
		line_edit_->blockSignals(true);
		line_edit_->setPlainText(s);
		line_edit_->blockSignals(false);
	}

	void setTextPreservingCursor(const QString &s)
	{
		// Save cursor position
		int cursor_pos = line_edit_->textCursor().position();

		// Set text
		this->setText(s);

		// Get new text cursor
		QTextCursor c = line_edit_->textCursor();
		c.setPosition(cursor_pos);
		line_edit_->setTextCursor(c);
	}

signals:
	void text_edited(const QString &);

	void request_edit_in_viewer();

private:
	QPlainTextEdit *line_edit_;

	QPushButton *edit_btn_;

	QPushButton *edit_in_viewer_btn_;

private slots:
	void show_text_dialog();

	void inner_widget_text_changed();
};

}

#endif // OAK_NODEPARAMVIEWTEXTEDIT_H
