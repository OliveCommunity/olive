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

#ifndef OAK_NODEPARAMVIEWITEMBASE_H
#define OAK_NODEPARAMVIEWITEMBASE_H

#include <QDockWidget>

#include "nodeparamviewitemtitlebar.h"
#include "oakutil/oaknode.h"

namespace olive
{

class NodeParamViewItemBase : public QDockWidget {
	Q_OBJECT
public:
	NodeParamViewItemBase(QWidget *parent = nullptr);

	void set_highlighted(bool e)
	{
		highlighted_ = e;

		update();
	}

	bool is_highlighted() const
	{
		return highlighted_;
	}

	bool is_expanded() const;

	static QString get_title_bar_text_from_node(oak::Node n);

public slots:
	void set_expanded(bool e);

	void toggle_expanded()
	{
		set_expanded(!is_expanded());
	}

signals:
	void pin_toggled(bool e);

	void expanded_changed(bool e);

	void moved();

	void clicked();

protected:
	void set_body(QWidget *body);

	virtual void paintEvent(QPaintEvent *event) override;

	NodeParamViewItemTitleBar *title_bar() const
	{
		return title_bar_;
	}

	virtual void changeEvent(QEvent *e) override;

	virtual void moveEvent(QMoveEvent *event) override;

	virtual void mousePressEvent(QMouseEvent *e) override;

protected slots:
	virtual void retranslate()
	{
	}

private:
	NodeParamViewItemTitleBar *title_bar_;

	QWidget *body_;

	QWidget *hidden_body_;

	bool highlighted_;
};

}

#endif // OAK_NODEPARAMVIEWITEMBASE_H
