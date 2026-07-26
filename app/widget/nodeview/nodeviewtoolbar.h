/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_NODEVIEWTOOLBAR_H
#define OAK_NODEVIEWTOOLBAR_H

#include <QPushButton>
#include <QWidget>

namespace olive
{

class NodeViewToolBar : public QWidget {
	Q_OBJECT
public:
	NodeViewToolBar(QWidget *parent = nullptr);

public slots:
	void set_mini_map_enabled(bool e)
	{
		minimap_btn_->setChecked(e);
	}

signals:
	void add_node_clicked();

	void mini_map_enabled_toggled(bool e);

	void zoom_in_clicked();

	void zoom_out_clicked();

	void fit_clicked();

protected:
	virtual void changeEvent(QEvent *e) override;

private:
	void retranslate();

	void update_icons();

	QPushButton *add_node_btn_;

	QPushButton *minimap_btn_;

	QPushButton *zoom_in_btn_;

	QPushButton *zoom_out_btn_;

	QPushButton *fit_btn_;
};

}

#endif // OAK_NODEVIEWTOOLBAR_H
