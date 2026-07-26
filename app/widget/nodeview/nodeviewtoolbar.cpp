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

#include "nodeviewtoolbar.h"

#include <QEvent>
#include <QHBoxLayout>

#include "ui/icons/icons.h"

namespace olive
{

#define super QWidget

NodeViewToolBar::NodeViewToolBar(QWidget *parent)
	: QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	add_node_btn_ = new QPushButton();
	connect(add_node_btn_, &QPushButton::clicked, this,
			&NodeViewToolBar::add_node_clicked);
	layout->addWidget(add_node_btn_);

	minimap_btn_ = new QPushButton();
	minimap_btn_->setCheckable(true);
	connect(minimap_btn_, &QPushButton::clicked, this,
			&NodeViewToolBar::mini_map_enabled_toggled);
	layout->addWidget(minimap_btn_);

	// Zoom controls per the UI design reference: [+] [-] [Fit]
	zoom_out_btn_ = new QPushButton();
	connect(zoom_out_btn_, &QPushButton::clicked, this,
			&NodeViewToolBar::zoom_out_clicked);
	layout->addWidget(zoom_out_btn_);

	zoom_in_btn_ = new QPushButton();
	connect(zoom_in_btn_, &QPushButton::clicked, this,
			&NodeViewToolBar::zoom_in_clicked);
	layout->addWidget(zoom_in_btn_);

	fit_btn_ = new QPushButton();
	connect(fit_btn_, &QPushButton::clicked, this,
			&NodeViewToolBar::fit_clicked);
	layout->addWidget(fit_btn_);

	layout->addStretch();

	retranslate();
	update_icons();
}

void NodeViewToolBar::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	} else if (e->type() == QEvent::StyleChange) {
		update_icons();
	}
	super::changeEvent(e);
}

void NodeViewToolBar::retranslate()
{
	add_node_btn_->setToolTip(tr("Add Node"));
	minimap_btn_->setToolTip(tr("Toggle Mini-Map"));
	zoom_in_btn_->setToolTip(tr("Zoom In"));
	zoom_out_btn_->setToolTip(tr("Zoom Out"));
	fit_btn_->setToolTip(tr("Fit to Content"));
	fit_btn_->setText(tr("Fit"));
}

void NodeViewToolBar::update_icons()
{
	add_node_btn_->setIcon(icon::add);
	minimap_btn_->setIcon(icon::mini_map);
	zoom_in_btn_->setIcon(icon::zoom_in);
	zoom_out_btn_->setIcon(icon::zoom_out);
}

}
