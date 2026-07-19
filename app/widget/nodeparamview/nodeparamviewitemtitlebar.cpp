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

#include "nodeparamviewitemtitlebar.h"

#include <QHBoxLayout>
#include <QPainter>

#include "ui/icons/icons.h"

namespace olive
{

NodeParamViewItemTitleBar::NodeParamViewItemTitleBar(QWidget *parent)
	: QWidget(parent)
	, draw_border_(true)
{
	QHBoxLayout *layout = new QHBoxLayout(this);

	collapse_btn_ = new CollapseButton(this);
	connect(collapse_btn_, &QPushButton::clicked, this,
			&NodeParamViewItemTitleBar::expanded_state_changed);
	layout->addWidget(collapse_btn_);

	lbl_ = new QLabel(this);
	layout->addWidget(lbl_);

	// Place next buttons on the far side
	layout->addStretch();

	add_fx_btn_ = new QPushButton(this);
	add_fx_btn_->setIcon(icon::add_effect);
	add_fx_btn_->setFixedSize(add_fx_btn_->sizeHint().height(),
							  add_fx_btn_->sizeHint().height());
	add_fx_btn_->setVisible(false);
	layout->addWidget(add_fx_btn_);
	connect(add_fx_btn_, &QPushButton::clicked, this,
			&NodeParamViewItemTitleBar::add_effect_button_clicked);

	pin_btn_ = new QPushButton(QStringLiteral("P"), this);
	pin_btn_->setCheckable(true);
	pin_btn_->setFixedSize(pin_btn_->sizeHint().height(),
						   pin_btn_->sizeHint().height());
	pin_btn_->setVisible(false);
	layout->addWidget(pin_btn_);
	connect(pin_btn_, &QPushButton::clicked, this,
			&NodeParamViewItemTitleBar::pin_toggled);

	enabled_checkbox_ = new QCheckBox(this);
	enabled_checkbox_->setVisible(false);
	layout->addWidget(enabled_checkbox_);
	connect(enabled_checkbox_, &QCheckBox::clicked, this,
			&NodeParamViewItemTitleBar::enabled_check_box_clicked);
}

void NodeParamViewItemTitleBar::set_expanded(bool e)
{
	draw_border_ = e;
	collapse_btn_->setChecked(e);

	update();
}

void NodeParamViewItemTitleBar::paintEvent(QPaintEvent *event)
{
	QWidget::paintEvent(event);

	if (draw_border_) {
		QPainter p(this);

		// Draw bottom border using text color
		int bottom = height() - 1;
		p.setPen(palette().text().color());
		p.drawLine(0, bottom, width(), bottom);
	}
}

void NodeParamViewItemTitleBar::mousePressEvent(QMouseEvent *event)
{
	QWidget::mousePressEvent(event);

	emit clicked();
}

void NodeParamViewItemTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);

	collapse_btn_->click();
}

}
