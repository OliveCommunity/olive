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

#include "toolbar.h"

#include <QButtonGroup>
#include <QEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QVariant>

#include "ui/icons/icons.h"
#include "widget/menu/factorymenu.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"

namespace olive
{

#define super QWidget

Toolbar::Toolbar(QWidget *parent)
	: super(parent)
{
	layout_ = new FlowLayout(this);
	layout_->setContentsMargins(0, 0, 0, 0);

	// Create standard tool buttons
	btn_pointer_tool_ = create_tool_button(Tool::k_pointer);
	btn_trackselect_tool_ = create_tool_button(Tool::k_track_select);
	btn_edit_tool_ = create_tool_button(Tool::k_edit);
	btn_ripple_tool_ = create_tool_button(Tool::k_ripple);
	btn_rolling_tool_ = create_tool_button(Tool::k_rolling);
	btn_razor_tool_ = create_tool_button(Tool::k_razor);
	btn_slip_tool_ = create_tool_button(Tool::k_slip);
	btn_slide_tool_ = create_tool_button(Tool::k_slide);
	btn_hand_tool_ = create_tool_button(Tool::k_hand);
	btn_zoom_tool_ = create_tool_button(Tool::k_zoom);
	btn_record_ = create_tool_button(Tool::k_record);
	btn_transition_tool_ = create_tool_button(Tool::k_transition);
	btn_add_ = create_tool_button(Tool::k_add);

	// Create snapping button, which is not actually a tool, it's a toggle option
	btn_snapping_toggle_ = create_non_tool_button();
	connect(btn_snapping_toggle_, &QPushButton::clicked, this,
			&Toolbar::snapping_button_clicked);

	// Connect transition button to menu signal
	connect(btn_transition_tool_, &QPushButton::clicked, this,
			&Toolbar::transition_button_clicked);

	// Connect add button to menu signal
	connect(btn_add_, &QPushButton::clicked, this, &Toolbar::add_button_clicked);

	retranslate();
	update_icons();
}

void Toolbar::set_tool(const Tool::Item &tool)
{
	// For each tool, set the "checked" state to whether the button's tool is the current tool
	for (int i = 0; i < toolbar_btns_.size(); i++) {
		ToolbarButton *btn = toolbar_btns_.at(i);

		btn->setChecked(btn->tool() == tool);
	}
}

void Toolbar::set_snapping(const bool &snapping)
{
	// Set checked state of snapping toggle
	btn_snapping_toggle_->setChecked(snapping);
}

void Toolbar::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	} else if (e->type() == QEvent::StyleChange) {
		update_icons();
	}
	super::changeEvent(e);
}

void Toolbar::resizeEvent(QResizeEvent *e)
{
	super::resizeEvent(e);

	int min_height = toolbar_btns_.size() * toolbar_btns_.first()->height() +
					 (toolbar_btns_.size() - 1) * layout_->vertical_spacing();
	int new_height = e->size().height();
	int columns_required =
		min_height / new_height + (min_height % new_height != 0);
	setMinimumWidth(toolbar_btns_.first()->width() * columns_required +
					layout_->horizontal_spacing() * (columns_required - 1) + 1);
}

void Toolbar::retranslate()
{
	btn_pointer_tool_->setToolTip(tr("Pointer Tool"));
	btn_trackselect_tool_->setToolTip(tr("Track Select Tool"));
	btn_edit_tool_->setToolTip(tr("Edit Tool"));
	btn_ripple_tool_->setToolTip(tr("Ripple Tool"));
	btn_rolling_tool_->setToolTip(tr("Rolling Tool"));
	btn_razor_tool_->setToolTip(tr("Razor Tool"));
	btn_slip_tool_->setToolTip(tr("Slip Tool"));
	btn_slide_tool_->setToolTip(tr("Slide Tool"));
	btn_hand_tool_->setToolTip(tr("Hand Tool"));
	btn_zoom_tool_->setToolTip(tr("Zoom Tool"));
	btn_transition_tool_->setToolTip(tr("Transition Tool"));
	btn_record_->setToolTip(tr("Record Tool"));
	btn_add_->setToolTip(tr("Add Tool"));
	btn_snapping_toggle_->setToolTip(tr("Toggle Snapping"));
}

void Toolbar::update_icons()
{
	btn_pointer_tool_->setIcon(icon::tool_pointer);
	btn_trackselect_tool_->setIcon(icon::tool_track_select);
	btn_edit_tool_->setIcon(icon::tool_edit);
	btn_ripple_tool_->setIcon(icon::tool_ripple);
	btn_rolling_tool_->setIcon(icon::tool_rolling);
	btn_razor_tool_->setIcon(icon::tool_razor);
	btn_slip_tool_->setIcon(icon::tool_slip);
	btn_slide_tool_->setIcon(icon::tool_slide);
	btn_hand_tool_->setIcon(icon::tool_hand);
	btn_zoom_tool_->setIcon(icon::zoom_in);
	btn_record_->setIcon(icon::record);
	btn_transition_tool_->setIcon(icon::tool_transition);
	btn_add_->setIcon(icon::add);
	btn_snapping_toggle_->setIcon(icon::snapping);
}

ToolbarButton *Toolbar::create_tool_button(const Tool::Item &tool)
{
	// Create a ToolbarButton object
	ToolbarButton *b = new ToolbarButton(this, tool);

	// Add it to the layout
	layout_->addWidget(b);

	// Add it to the list for iterating through later
	toolbar_btns_.append(b);

	// Connect it to the tool button click handler
	connect(b, SIGNAL(clicked(bool)), this, SLOT(tool_button_clicked()));

	return b;
}

ToolbarButton *Toolbar::create_non_tool_button()
{
	// Create a ToolbarButton object
	ToolbarButton *b = new ToolbarButton(this, Tool::k_none);

	// Add it to the layout
	layout_->addWidget(b);

	return b;
}

void Toolbar::tool_button_clicked()
{
	// Get new tool from ToolbarButton object
	Tool::Item new_tool = static_cast<ToolbarButton *>(sender())->tool();

	// Set checked state of all tool buttons
	// NOTE: Not necessary if this is appropriately connected to Core
	//SetTool(new_tool);

	// Emit signal that the tool just changed
	emit tool_changed(new_tool);
}

void Toolbar::snapping_button_clicked(bool b)
{
	emit snapping_changed(b);
}

void Toolbar::add_button_clicked()
{
	Menu m(this);

	MenuShared::instance()->add_items_for_addable_objects_menu(&m);

	m.exec(QCursor::pos());
}

void Toolbar::transition_button_clicked()
{
	Menu *m = create_node_menu(this, false, oak::k_category_transition);

	connect(m, &QMenu::triggered, this, &Toolbar::transition_menu_item_triggered);

	m->exec(QCursor::pos());

	delete m;
}

void Toolbar::transition_menu_item_triggered(QAction *a)
{
	emit selected_transition_changed(get_node_id_from_menu_action(a));
}

}
