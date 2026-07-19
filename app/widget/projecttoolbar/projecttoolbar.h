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

#ifndef OAK_PROJECTTOOLBAR_H
#define OAK_PROJECTTOOLBAR_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>

#include "common/define.h"

namespace olive
{

/**
 * @brief The ProjectToolbar class
 *
 * A toolbar consisting of project functions (new/open/save), edit functions (undo/redo), a search field, and a
 * project view selector (tree/icon/list).
 *
 * This object's signals can be connected to various functions in the application for better user experience.
 */
class ProjectToolbar : public QWidget {
	Q_OBJECT
public:
	ProjectToolbar(QWidget *parent);

	enum ViewType { tree_view, list_view, icon_view };

public slots:
	void set_view(ViewType type);

protected:
	void changeEvent(QEvent *) override;

signals:
	void new_clicked();
	void open_clicked();
	void save_clicked();

	void search_changed(const QString &);

	void view_changed(ViewType type);

private:
	void retranslate();
	void update_icons();

	QPushButton *new_button_;
	QPushButton *open_button_;
	QPushButton *save_button_;

	QLineEdit *search_field_;

	QPushButton *tree_button_;
	QPushButton *list_button_;
	QPushButton *icon_button_;

private slots:
	void view_button_clicked();
};

}

#endif // OAK_PROJECTTOOLBAR_H
