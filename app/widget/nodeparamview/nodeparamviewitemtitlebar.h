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

#ifndef OAK_NODEPARAMVIEWITEMTITLEBAR_H
#define OAK_NODEPARAMVIEWITEMTITLEBAR_H

#include <QCheckBox>
#include <QLabel>
#include <QWidget>

#include "widget/collapsebutton/collapsebutton.h"

namespace olive
{

class NodeParamViewItemTitleBar : public QWidget {
	Q_OBJECT
public:
	NodeParamViewItemTitleBar(QWidget *parent = nullptr);

	bool is_expanded() const
	{
		return collapse_btn_->isChecked();
	}

public slots:
	void set_expanded(bool e);

	void set_text(const QString &s)
	{
		lbl_->setText(s);
		lbl_->setToolTip(s);
		lbl_->setMinimumWidth(1);
	}

	void set_pin_button_visible(bool e)
	{
		pin_btn_->setVisible(e);
	}

	void set_add_effect_button_visible(bool e)
	{
		add_fx_btn_->setVisible(e);
	}

	void set_enabled_check_box_visible(bool e)
	{
		enabled_checkbox_->setVisible(e);
	}

	void set_enabled_check_box_checked(bool e)
	{
		enabled_checkbox_->setChecked(e);
	}

signals:
	void expanded_state_changed(bool e);

	void pin_toggled(bool e);

	void add_effect_button_clicked();

	void enabled_check_box_clicked(bool e);

	void clicked();

protected:
	virtual void paintEvent(QPaintEvent *event) override;

	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	bool draw_border_;

	QLabel *lbl_;

	CollapseButton *collapse_btn_;

	QPushButton *pin_btn_;

	QPushButton *add_fx_btn_;

	QCheckBox *enabled_checkbox_;
};

}

#endif // OAK_NODEPARAMVIEWITEMTITLEBAR_H
