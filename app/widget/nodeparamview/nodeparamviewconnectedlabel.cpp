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

#include "playback/playbackcontroller.h"

#include <QHBoxLayout>

#include "oakutil/qtutils.h"
#include "core.h"
#include "oakengine/node.h"
#include "widget/collapsebutton/collapsebutton.h"
#include "widget/menu/menu.h"

#include "oakengine/viewer.h"
#include "oakengine/events.h"

namespace olive
{

NodeParamViewConnectedLabel::NodeParamViewConnectedLabel(const oak::Input &input,
														 QWidget *parent)
	: QWidget(parent)
	, input_(input)
	, connected_node_()
	, viewer_(nullptr)
	, bridge_(new EngineEventBridge(this))
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
		input_connected(input_.connected_node(), input_);
	} else {
		input_disconnected(oak::Node(), input_);
	}

	bridge_->subscribe(reinterpret_cast<void *>(input_.node_handle()),
					   OAKENGINE_EVENT_NODE_INPUT_CONNECTED);
	bridge_->subscribe(reinterpret_cast<void *>(input_.node_handle()),
					   OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED);
	connect(bridge_, &EngineEventBridge::node_input_connected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input_id, int element) {
				input_connected(output,
					oak::Input(source, input_id, element));
			});
	connect(bridge_, &EngineEventBridge::node_input_disconnected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input_id, int element) {
				input_disconnected(output,
					oak::Input(source, input_id, element));
			});

	// Creating the tree is expensive, hold off until the user specifically requests it
	value_tree_ = nullptr;
	connect(collapse_btn, &CollapseButton::toggled, this,
			&NodeParamViewConnectedLabel::set_value_tree_visible);
}

NodeParamViewConnectedLabel::~NodeParamViewConnectedLabel()
{
	// Drop the PlaybackController connection (Qt would auto-disconnect
	// anyway, but keep the symmetric teardown).
	set_viewer_node(nullptr);
}

void NodeParamViewConnectedLabel::set_viewer_node(OakEngineNode *viewer)
{
	disconnect(viewer_conn_);

	viewer_ = viewer;

	if (viewer_) {
		viewer_conn_ = connect(
			PlaybackController::instance(),
			&PlaybackController::playhead_changed, this,
			[this](OakEngineNode *n, const core::Rational &) {
				if (n == viewer_) {
					update_value_tree();
				}
			});
		update_value_tree();
	}
}

void NodeParamViewConnectedLabel::create_tree()
{
	// Set up table area
	value_tree_ = new NodeValueTree(this);
	layout()->addWidget(value_tree_);
}

void NodeParamViewConnectedLabel::input_connected(oak::Node output,
												 const oak::Input &input)
{
	if (input_ != input) {
		return;
	}

	connected_node_ = output;

	update_label();
}

void NodeParamViewConnectedLabel::input_disconnected(oak::Node output,
													const oak::Input &input)
{
	if (input_ != input) {
		return;
	}

	Q_UNUSED(output)

	connected_node_ = oak::Node();

	update_label();
}

void NodeParamViewConnectedLabel::show_label_context_menu()
{
	Menu m(this);

	QAction *disconnect_action = m.addAction(tr("Disconnect"));
	connect(disconnect_action, &QAction::triggered, this, [this]() {
		// Through the liboakengine C ABI facade (one undoable command,
		// array element included, same as the old NodeEdgeRemoveCommand).
		input_.disconnect();
	});

	m.exec(QCursor::pos());
}

void NodeParamViewConnectedLabel::connection_clicked()
{
	if (!connected_node_.is_null()) {
		emit request_select_node(connected_node_.handle());
	}
}

void NodeParamViewConnectedLabel::update_label()
{
	QString s;

	if (!connected_node_.is_null()) {
		s = connected_node_.name();
	} else {
		s = tr("Nothing");
	}

	connected_to_lbl_->setText(s);
}

void NodeParamViewConnectedLabel::update_value_tree()
{
	if (value_tree_ && viewer_ && value_tree_->isVisible()) {
		int64_t pn, pd;
		oakengine_viewer_get_playhead(
			reinterpret_cast<OakEngineNode *>(viewer_), &pn, &pd);
		value_tree_->set_node(input_, Rational(pn, pd));
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
