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

#include "nodeparamviewitembase.h"

#include <QEvent>
#include <QPainter>

namespace olive
{

#define super QDockWidget

NodeParamViewItemBase::NodeParamViewItemBase(QWidget *parent)
	: super(parent)
	, highlighted_(false)
{
	// Create title bar widget
	title_bar_ = new NodeParamViewItemTitleBar(this);

	// Add title bar to widget
	this->setTitleBarWidget(title_bar_);

	// Connect title bar to this
	connect(title_bar_, &NodeParamViewItemTitleBar::expanded_state_changed, this,
			&NodeParamViewItemBase::set_expanded);
	connect(title_bar_, &NodeParamViewItemTitleBar::pin_toggled, this,
			&NodeParamViewItemBase::pin_toggled);
	connect(title_bar_, &NodeParamViewItemTitleBar::clicked, this,
			&NodeParamViewItemBase::clicked);

	// Use dummy QWidget to retain width when not expanded (QDockWidget seems to ignore the titlebar
	// size hints and will shrink as small as possible if the body is hidden)
	hidden_body_ = new QWidget(this);

	// Default to hidden body, this also seems to fix an issue with clicks being intermittent
	// on the titlebar
	setWidget(hidden_body_);

	setAutoFillBackground(true);

	setFocusPolicy(Qt::ClickFocus);
}

bool NodeParamViewItemBase::is_expanded() const
{
	return title_bar_->is_expanded();
}

QString NodeParamViewItemBase::get_title_bar_text_from_node(oak::Node n)
{
	return n.label_and_name();
}

void NodeParamViewItemBase::set_body(QWidget *body)
{
	body_ = body;
	body_->setParent(this);

	if (title_bar_->is_expanded()) {
		setWidget(body_);
	}
}

void NodeParamViewItemBase::paintEvent(QPaintEvent *event)
{
	super::paintEvent(event);

	// Draw border if focused
	if (highlighted_) {
		QPainter p(this);
		p.setBrush(Qt::NoBrush);
		p.setPen(palette().highlight().color());
		p.drawRect(rect().adjusted(0, 0, -1, -1));
	}
}

void NodeParamViewItemBase::set_expanded(bool e)
{
	setWidget(e ? body_ : hidden_body_);
	title_bar_->set_expanded(e);

	emit expanded_changed(e);
}

void NodeParamViewItemBase::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	}

	super::changeEvent(e);
}

void NodeParamViewItemBase::moveEvent(QMoveEvent *event)
{
	super::moveEvent(event);

	emit moved();
}

void NodeParamViewItemBase::mousePressEvent(QMouseEvent *e)
{
	super::mousePressEvent(e);

	emit clicked();
}

}
