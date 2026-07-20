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

#include "nodeparamviewconnectedlabel.h"

#include <QHBoxLayout>

#include "common/qtutils.h"
#include "core.h"
#include "node/node.h"
#include "oakengine/node.h"
#include "widget/collapsebutton/collapsebutton.h"
#include "widget/menu/menu.h"

namespace olive
{

NodeParamViewConnectedLabel::NodeParamViewConnectedLabel(const NodeInput &input,
														 QWidget *parent)
	: QWidget(parent)
	, input_(input)
	, connected_node_(nullptr)
	, viewer_(nullptr)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	QSizePolicy p = sizePolicy();
	p.setHorizontalStretch(1);
	p.setHorizontalPolicy(QSizePolicy::Expanding);
	setSizePolicy(p);

	// Set up label area
	QHBoxLayout *label_layout = new QHBoxLayout();
	label_layout->setSpacing(
		QtUtils::q_font_metrics_width(fontMetrics(), QStringLiteral(" ")));
	label_layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(label_layout);

	CollapseButton *collapse_btn = new CollapseButton(this);
	collapse_btn->setChecked(false);
	label_layout->addWidget(collapse_btn);

	label_layout->addWidget(new QLabel(tr("Connected to"), this));

	connected_to_lbl_ = new ClickableLabel(this);
	connected_to_lbl_->setCursor(Qt::PointingHandCursor);
	connected_to_lbl_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(connected_to_lbl_, &ClickableLabel::mouse_clicked, this,
			&NodeParamViewConnectedLabel::connection_clicked);
	connect(connected_to_lbl_, &ClickableLabel::customContextMenuRequested,
			this, &NodeParamViewConnectedLabel::show_label_context_menu);
	label_layout->addWidget(connected_to_lbl_);

	label_layout->addStretch();

	// Set up "link" font
	QFont link_font = connected_to_lbl_->font();
	link_font.setUnderline(true);
	connected_to_lbl_->setForegroundRole(QPalette::Link);
	connected_to_lbl_->setFont(link_font);

	if (input_.is_connected()) {
		input_connected(input_.get_connected_output(), input_);
	} else {
		input_disconnected(nullptr, input_);
	}

	connect(input_.node(), &Node::input_connected, this,
			&NodeParamViewConnectedLabel::input_connected);
	connect(input_.node(), &Node::input_disconnected, this,
			&NodeParamViewConnectedLabel::input_disconnected);

	// Creating the tree is expensive, hold off until the user specifically requests it
	value_tree_ = nullptr;
	connect(collapse_btn, &CollapseButton::toggled, this,
			&NodeParamViewConnectedLabel::set_value_tree_visible);
}

void NodeParamViewConnectedLabel::set_viewer_node(ViewerOutput *viewer)
{
	if (viewer_) {
		disconnect(viewer_, &ViewerOutput::playhead_changed, this,
				   &NodeParamViewConnectedLabel::update_value_tree);
	}

	viewer_ = viewer;

	if (viewer_) {
		connect(viewer_, &ViewerOutput::playhead_changed, this,
				&NodeParamViewConnectedLabel::update_value_tree);
		update_value_tree();
	}
}

void NodeParamViewConnectedLabel::create_tree()
{
	// Set up table area
	value_tree_ = new NodeValueTree(this);
	layout()->addWidget(value_tree_);
}

void NodeParamViewConnectedLabel::input_connected(Node *output,
												 const NodeInput &input)
{
	if (input_ != input) {
		return;
	}

	connected_node_ = output;

	update_label();
}

void NodeParamViewConnectedLabel::input_disconnected(Node *output,
													const NodeInput &input)
{
	if (input_ != input) {
		return;
	}

	Q_UNUSED(output)

	connected_node_ = nullptr;

	update_label();
}

void NodeParamViewConnectedLabel::show_label_context_menu()
{
	Menu m(this);

	QAction *disconnect_action = m.addAction(tr("Disconnect"));
	connect(disconnect_action, &QAction::triggered, this, [this]() {
		// Through the liboakengine C ABI facade (one undoable command,
		// array element included, same as the old NodeEdgeRemoveCommand).
		oakengine_node_disconnect_ex(
			reinterpret_cast<OakEngineNode *>(input_.node()),
			input_.input().toUtf8().constData(), input_.element());
	});

	m.exec(QCursor::pos());
}

void NodeParamViewConnectedLabel::connection_clicked()
{
	if (connected_node_) {
		emit request_select_node(connected_node_);
	}
}

void NodeParamViewConnectedLabel::update_label()
{
	QString s;

	if (connected_node_) {
		s = connected_node_->name();
	} else {
		s = tr("Nothing");
	}

	connected_to_lbl_->setText(s);
}

void NodeParamViewConnectedLabel::update_value_tree()
{
	if (value_tree_ && viewer_ && value_tree_->isVisible()) {
		value_tree_->set_node(input_, viewer_->get_playhead());
	}
}

void NodeParamViewConnectedLabel::set_value_tree_visible(bool e)
{
	if (value_tree_) {
		value_tree_->setVisible(e);
	}

	if (e) {
		if (!value_tree_) {
			create_tree();
			value_tree_->setVisible(true);
		}

		update_value_tree();
	}
}

}
