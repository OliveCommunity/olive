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

#include "nodeparamviewitem.h"

#include <QCheckBox>
#include <QDebug>

#include "common/qtutils.h"
#include "dialog/speedduration/speeddurationdialog.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/node.h"
#include "pluginSupport/oliveplugininstance.h"

namespace olive
{

static NodeInput ResolveGroupInput(const NodeInput &input)
{
	OakEngineNode *node = reinterpret_cast<OakEngineNode *>(input.node());
	char input_id[256];
	int element = input.element();
	const QByteArray utf = input.input().toUtf8();
	memcpy(input_id, utf.constData(), qMin<int>(sizeof(input_id) - 1, utf.size()));
	input_id[sizeof(input_id) - 1] = '\0';
	if (oakengine_group_resolve_input(
			node, input_id, element,
			&node, input_id, sizeof(input_id), &element) != OAKENGINE_OK) {
		return input;
	}
	return NodeInput(reinterpret_cast<Node *>(node),
					 QString::fromUtf8(input_id), element);
}

const int NodeParamViewItemBody::k_key_control_column = 10;
const int NodeParamViewItemBody::k_array_insert_column = k_key_control_column - 1;
const int NodeParamViewItemBody::k_array_remove_column = k_array_insert_column - 1;
const int NodeParamViewItemBody::k_extra_button_column = k_key_control_column - 1;

const int NodeParamViewItemBody::k_optional_check_box = 0;
const int NodeParamViewItemBody::k_array_collapse_btn_column = 1;
const int NodeParamViewItemBody::k_label_column = 2;
const int NodeParamViewItemBody::k_widget_start_column = 3;
const int NodeParamViewItemBody::k_max_widget_column = k_array_remove_column;

#define super NodeParamViewItemBase

NodeParamViewItem::NodeParamViewItem(
	Node *node, NodeParamViewCheckBoxBehavior create_checkboxes,
	QWidget *parent)
	: super(parent)
	, body_(nullptr)
	, message_label_(nullptr)
	, message_clear_button_(nullptr)
	, message_container_(nullptr)
	, node_(node)
	, create_checkboxes_(create_checkboxes)
	, ctx_(nullptr)
	, time_target_(nullptr)
	, bridge_(new EngineEventBridge(this))
{
	node_->retranslate();

	// Create and add contents widget
	recreate_body();

	bridge_->subscribe(reinterpret_cast<void *>(node_),
					   OAKENGINE_EVENT_NODE_LABEL_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(node_),
					   OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(node_),
					   OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(node_),
					   OAKENGINE_EVENT_NODE_INPUT_FLAGS_CHANGED);

	connect(bridge_, &EngineEventBridge::node_label_changed, this,
			&NodeParamViewItem::retranslate);
	connect(bridge_, &EngineEventBridge::node_input_array_size_changed, this,
			[this](OakEngineNode *, const QString &input, int old_sz,
				   int new_size) {
				emit input_array_size_changed(input, old_sz, new_size);
			});
	connect(bridge_, &EngineEventBridge::node_message_count_changed, this,
			&NodeParamViewItem::update_message_panel);
	connect(bridge_, &EngineEventBridge::node_input_flags_changed, this,
			&NodeParamViewItem::recreate_body);

	setBackgroundRole(QPalette::Window);

	// Connect title bar enabled checkbox
	//title_bar()->SetEnabledCheckBoxVisible(true);
	//title_bar()->SetEnabledCheckBoxChecked(node_->IsEnabled());
	//connect(title_bar(), &NodeParamViewItemTitleBar::EnabledCheckBoxClicked, node_, &Node::SetEnabled);

	retranslate();
}

void NodeParamViewItem::retranslate()
{
	node_->retranslate();

	title_bar()->set_text(get_title_bar_text_from_node(node_));

	body_->retranslate();
}

void NodeParamViewItem::recreate_body()
{
	if (body_) {
		body_->setParent(nullptr);
		body_->deleteLater();
	}
	if (message_container_) {
		message_container_->setParent(nullptr);
		message_container_->deleteLater();
		message_container_ = nullptr;
		message_label_ = nullptr;
		message_clear_button_ = nullptr;
	}

	body_ = new NodeParamViewItemBody(node_, create_checkboxes_, this);
	connect(body_, &NodeParamViewItemBody::request_select_node, this,
			&NodeParamViewItem::request_select_node);
	connect(body_, &NodeParamViewItemBody::array_expanded_changed, this,
			&NodeParamViewItem::array_expanded_changed);
	connect(body_, &NodeParamViewItemBody::input_checked_changed, this,
			&NodeParamViewItem::input_checked_changed);
	connect(body_, &NodeParamViewItemBody::request_edit_text_in_viewer, this,
			&NodeParamViewItem::request_edit_text_in_viewer);
	body_->retranslate();
	body_->set_timebase(timebase_);
	body_->set_time_target(time_target_);

	message_container_ = new QWidget(this);
	QVBoxLayout *message_layout = new QVBoxLayout(message_container_);
	message_layout->setContentsMargins(0, 0, 0, 0);
	message_layout->setSpacing(4);

	QHBoxLayout *message_header = new QHBoxLayout();
	message_header->setContentsMargins(0, 0, 0, 0);
	message_header->addStretch();
	message_clear_button_ = new QPushButton(tr("Clear"), message_container_);
	message_clear_button_->setVisible(false);
	connect(message_clear_button_, &QPushButton::clicked, this,
			&NodeParamViewItem::clear_messages);
	message_header->addWidget(message_clear_button_);
	message_layout->addLayout(message_header);

	message_label_ = new QLabel(message_container_);
	message_label_->setWordWrap(true);
	message_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	message_label_->setStyleSheet(
		QStringLiteral("background: rgba(0, 0, 0, 0.06); padding: 6px;"));
	message_layout->addWidget(message_label_);
	message_layout->addWidget(body_);

	set_body(message_container_);
	update_message_panel();
}

void NodeParamViewItem::update_message_panel()
{
	if (!message_label_) {
		return;
	}

	auto *instance = node_->getPluginInstance();
	auto *olive_instance =
		dynamic_cast<plugin::OlivePluginInstance *>(instance);
	if (!olive_instance || olive_instance->persistent_message_count() == 0) {
		message_label_->setVisible(false);
		if (message_clear_button_) {
			message_clear_button_->setVisible(false);
		}
		return;
	}

	QStringList lines;
	for (const auto &msg : olive_instance->persistent_messages()) {
		QString prefix;
		switch (msg.type) {
		case plugin::ErrorType::error:
			prefix = QStringLiteral("Error");
			break;
		case plugin::ErrorType::warning:
			prefix = QStringLiteral("Warning");
			break;
		case plugin::ErrorType::message:
			prefix = QStringLiteral("Message");
			break;
		}
		lines.append(QStringLiteral("%1: %2").arg(prefix, msg.message));
	}

	message_label_->setText(lines.join('\n'));
	message_label_->setVisible(true);
	if (message_clear_button_) {
		message_clear_button_->setVisible(true);
	}
}

int NodeParamViewItem::get_element_y(const NodeInput &c) const
{
	if (is_expanded()) {
		return body_->get_element_y(c);
	} else {
		// Not expanded, put keyframes at the titlebar Y
		return mapToGlobal(title_bar()->rect().center()).y();
	}
}

void NodeParamViewItem::set_input_checked(const NodeInput &input, bool e)
{
	body_->set_input_checked(input, e);
}

void NodeParamViewItem::clear_messages()
{
	auto *instance = node_->getPluginInstance();
	auto *olive_instance =
		dynamic_cast<plugin::OlivePluginInstance *>(instance);
	if (!olive_instance) {
		return;
	}

	olive_instance->clearPersistentMessage();
}

NodeParamViewItemBody::NodeParamViewItemBody(
	Node *node, NodeParamViewCheckBoxBehavior create_checkboxes,
	QWidget *parent)
	: QWidget(parent)
	, node_(node)
	, time_target_(nullptr)
	, create_checkboxes_(create_checkboxes)
	, bridge_(new EngineEventBridge(this))
{
	QGridLayout *root_layout = new QGridLayout(this);

	int insert_row = 0;
	QString current_page;
	QString current_group;

	QVector<Node *> connected_signals;

	connect(bridge_, &EngineEventBridge::node_input_array_size_changed,
			this, &NodeParamViewItemBody::input_array_size_changed);
	connect(bridge_, &EngineEventBridge::node_input_connected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input, int element) {
				edge_changed(output,
					NodeInput(reinterpret_cast<Node *>(source), input,
							  element));
			});
	connect(bridge_, &EngineEventBridge::node_input_disconnected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input, int element) {
				edge_changed(output,
					NodeInput(reinterpret_cast<Node *>(source), input,
							  element));
			});

	// Create widgets all root level components
	foreach (QString input, node->inputs()) {
		Node *n = node;

		NodeInput resolved = ResolveGroupInput(NodeInput(n, input));
		if (!connected_signals.contains(resolved.node())) {
			bridge_->subscribe(reinterpret_cast<void *>(resolved.node()),
							   OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED);
			bridge_->subscribe(reinterpret_cast<void *>(resolved.node()),
							   OAKENGINE_EVENT_NODE_INPUT_CONNECTED);
			bridge_->subscribe(reinterpret_cast<void *>(resolved.node()),
							   OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED);

			connected_signals.append(resolved.node());
		}

		input_group_lookup_.insert({ resolved.node(), resolved.input() },
								   { n, input });

		if (!(n->get_input_flags(input) & k_input_flag_hidden)) {
			QString page_label =
				n->get_input_property(input, QStringLiteral("ui_page")).toString();
			QString group_label =
				n->get_input_property(input, QStringLiteral("ui_group"))
					.toString();
			if (!page_label.isEmpty() && page_label != current_page) {
				QLabel *page_title = new QLabel(page_label, this);
				QFont f = page_title->font();
				f.setBold(true);
				page_title->setFont(f);
				root_layout->addWidget(page_title, insert_row, 0, 1, 10);
				insert_row++;
				current_page = page_label;
				current_group.clear();
			}
			if (!group_label.isEmpty() && group_label != current_group) {
				QLabel *group_title = new QLabel(group_label, this);
				QFont f = group_title->font();
				f.setBold(true);
				group_title->setFont(f);
				root_layout->addWidget(group_title, insert_row, 0, 1, 10);
				insert_row++;
				current_group = group_label;
			}
			create_widgets(root_layout, n, input, -1, insert_row);

			insert_row++;

			if (n->input_is_array(input)) {
				// Insert here
				QWidget *array_widget = new QWidget(this);

				QGridLayout *array_layout = new QGridLayout(array_widget);
				array_layout->setContentsMargins(
					QtUtils::q_font_metrics_width(fontMetrics(),
											   QStringLiteral("    ")),
					0, 0, 0);

				root_layout->addWidget(array_widget, insert_row, 1, 1, 10);

				// Start with zero elements for efficiency. We will make the widgets for them if the user
				// requests the array UI to be expanded
				int arr_sz = 0;

				// Add one last add button for appending to the array
				NodeParamViewArrayButton *append_btn =
					new NodeParamViewArrayButton(NodeParamViewArrayButton::k_add,
												 this);
				connect(append_btn, &NodeParamViewArrayButton::clicked, this,
						&NodeParamViewItemBody::array_append_clicked);
				array_layout->addWidget(append_btn, arr_sz, k_array_insert_column);

				array_widget->setVisible(false);

				array_ui_.insert({ n, input },
								 { array_widget, arr_sz, append_btn });

				insert_row++;
			}
		}
	}
}

void NodeParamViewItemBody::create_widgets(QGridLayout *layout, Node *node,
										  const QString &input, int element,
										  int row)
{
	NodeInput input_ref(node, input, element);

	InputUI ui_objects;

	// Store layout and row
	ui_objects.layout = layout;
	ui_objects.row = row;

	// Create optional checkbox if requested
	if (create_checkboxes_) {
		ui_objects.optional_checkbox = new QCheckBox(this);
		connect(ui_objects.optional_checkbox, &QCheckBox::clicked, this,
				&NodeParamViewItemBody::optional_check_box_clicked);
		layout->addWidget(ui_objects.optional_checkbox, row, k_optional_check_box);

		if (create_checkboxes_ == k_check_boxes_on_non_connected &&
			input_ref.is_connected()) {
			ui_objects.optional_checkbox->setVisible(false);
		}
	}

	// Add descriptor label
	ui_objects.main_label = new QLabel(this);

	// Create input label
	layout->addWidget(ui_objects.main_label, row, k_label_column);

	if (node->input_is_array(input)) {
		if (element == -1) {
			// Create a collapse toggle for expanding/collapsing the array
			CollapseButton *array_collapse_btn = new CollapseButton(this);

			// Default to collapsed
			array_collapse_btn->setChecked(false);

			// Add collapse button to layout
			layout->addWidget(array_collapse_btn, row, k_array_collapse_btn_column);

			// Connect signal to show/hide array params when toggled
			connect(array_collapse_btn, &CollapseButton::toggled, this,
					&NodeParamViewItemBody::array_collapse_btn_pressed);

			array_collapse_buttons_.insert({ node, input }, array_collapse_btn);

		} else {
			NodeParamViewArrayButton *insert_element_btn =
				new NodeParamViewArrayButton(NodeParamViewArrayButton::k_add,
											 this);
			NodeParamViewArrayButton *remove_element_btn =
				new NodeParamViewArrayButton(NodeParamViewArrayButton::k_remove,
											 this);

			layout->addWidget(insert_element_btn, row, k_array_insert_column);
			layout->addWidget(remove_element_btn, row, k_array_remove_column);

			ui_objects.array_insert_btn = insert_element_btn;
			ui_objects.array_remove_btn = remove_element_btn;

			connect(insert_element_btn, &NodeParamViewArrayButton::clicked,
					this, &NodeParamViewItemBody::array_insert_clicked);
			connect(remove_element_btn, &NodeParamViewArrayButton::clicked,
					this, &NodeParamViewItemBody::array_remove_clicked);
		}
	}

	// Create a widget/input bridge for this input
	ui_objects.widget_bridge =
		new NodeParamViewWidgetBridge(NodeInput(node, input, element), this);
	connect(ui_objects.widget_bridge,
			&NodeParamViewWidgetBridge::widgets_recreated, this,
			&NodeParamViewItemBody::replace_widgets);
	connect(ui_objects.widget_bridge,
			&NodeParamViewWidgetBridge::array_widget_double_clicked, this,
			&NodeParamViewItemBody::toggle_array_expanded);
	connect(ui_objects.widget_bridge,
			&NodeParamViewWidgetBridge::request_edit_text_in_viewer, this,
			&NodeParamViewItemBody::request_edit_text_in_viewer);

	// Place widgets into layout
	place_widgets_from_bridge(layout, ui_objects.widget_bridge, row);

	// In case this input is a group, resolve that actual input to use for connected labels
	NodeInput resolved = ResolveGroupInput(input_ref);

	if (node->is_input_connectable(input)) {
		// Create clickable label used when an input is connected
		ui_objects.connected_label =
			new NodeParamViewConnectedLabel(resolved, this);
		connect(ui_objects.connected_label,
				&NodeParamViewConnectedLabel::request_select_node, this,
				&NodeParamViewItemBody::request_select_node);
		layout->addWidget(ui_objects.connected_label, row, k_widget_start_column,
						  1, k_key_control_column - k_widget_start_column);
	}

	// Add keyframe control to this layout if parameter is keyframable
	if (node->is_input_keyframable(input)) {
		ui_objects.key_control = new NodeParamViewKeyframeControl(this);
		ui_objects.key_control->set_input(resolved);
		layout->addWidget(ui_objects.key_control, row, k_key_control_column);
	}

	input_ui_map_.insert(input_ref, ui_objects);

	if (node->is_input_connectable(input)) {
		update_ui_for_edge_connection(input_ref);
	}

	set_time_target_on_input_ui(ui_objects);
	set_timebase_on_input_ui(ui_objects);
}

void NodeParamViewItemBody::set_time_target(ViewerOutput *target)
{
	time_target_ = target;

	foreach (const InputUI &ui_obj, input_ui_map_) {
		set_time_target_on_input_ui(ui_obj);
	}
}

void NodeParamViewItemBody::set_time_target_on_input_ui(const InputUI &ui_obj)
{
	// Only keyframable inputs have a key control widget
	if (ui_obj.key_control) {
		ui_obj.key_control->set_time_target(time_target_);
	}
	if (ui_obj.connected_label) {
		ui_obj.connected_label->set_viewer_node(time_target_);
	}
	ui_obj.widget_bridge->set_time_target(time_target_);
}

void NodeParamViewItemBody::retranslate()
{
	for (auto i = input_ui_map_.begin(); i != input_ui_map_.end(); i++) {
		const NodeInput &ic = i.key();

		if (ic.is_array() && ic.element() >= 0) {
			// Make the label the array index
			i.value().main_label->setText(tr("%1:").arg(
				ic.element() +
				ic.get_property(QStringLiteral("arraystart")).toInt()));
		} else {
			// Set to the input's name
			i.value().main_label->setText(tr("%1:").arg(ic.name()));
		}
	}
}

int NodeParamViewItemBody::get_element_y(NodeInput c) const
{
	if (c.is_array() && !array_ui_.value(c.input_pair()).widget->isVisible()) {
		// Array is collapsed, so we'll return the Y of its root
		c.set_element(-1);
	}

	//c = NodeGroup::ResolveInput(c);

	// Find its row in the parameters
	QLabel *lbl = input_ui_map_.value(c).main_label;

	// Find label's Y position
	QPoint lbl_center = lbl->rect().center();

	// Find global position
	lbl_center = lbl->mapToGlobal(lbl_center);

	// Return Y
	return lbl_center.y();
}

void NodeParamViewItemBody::edge_changed(OakEngineNode *output, const NodeInput &input)
{
	Q_UNUSED(output)

	const NodeInputPair &pair =
		input_group_lookup_.value({ input.node(), input.input() });
	NodeInput resolved(pair.node, pair.input, input.element());

	update_ui_for_edge_connection(resolved);
}

void NodeParamViewItemBody::update_ui_for_edge_connection(const NodeInput &input)
{
	// Show/hide bridge widgets
	if (input_ui_map_.contains(input)) {
		const InputUI &ui_objects = input_ui_map_[input];

		bool is_connected = ResolveGroupInput(input).is_connected();

		foreach (QWidget *w, ui_objects.widget_bridge->widgets()) {
			w->setVisible(!is_connected);
		}

		// Show/hide connection label
		ui_objects.connected_label->setVisible(is_connected);

		if (ui_objects.key_control) {
			ui_objects.key_control->setVisible(!is_connected);
		}

		// Show/hide optional checkbox if requested
		if (create_checkboxes_ == k_check_boxes_on_non_connected) {
			ui_objects.optional_checkbox->setVisible(!is_connected);
		}
	}
}

void NodeParamViewItemBody::place_widgets_from_bridge(
	QGridLayout *layout, NodeParamViewWidgetBridge *bridge, int row)
{
	// Add widgets for this parameter to the layout
	for (int i = 0; i < bridge->widgets().size(); i++) {
		QWidget *w = bridge->widgets().at(i);

		int col = i + k_widget_start_column;

		int colspan;
		if (i == bridge->widgets().size() - 1) {
			// Span this widget among remaining columns
			colspan = k_max_widget_column - col;
		} else {
			colspan = 1;
		}

		layout->addWidget(w, row, col, 1, colspan);
	}
}

void NodeParamViewItemBody::input_array_size_changed_internal(Node *node,
														  const QString &input,
														  int size)
{
	NodeInputPair nip = { node, input };

	if (!array_ui_.contains(nip)) {
		return;
	}

	ArrayUI &array_ui = array_ui_[nip];

	if (size != array_ui.count) {
		QGridLayout *grid =
			static_cast<QGridLayout *>(array_ui.widget->layout());

		if (array_ui.count < size) {
			// Our UI count is smaller than the size, create more
			grid->addWidget(array_ui.append_btn, size, k_array_insert_column);

			for (int i = array_ui.count; i < size; i++) {
				create_widgets(grid, node, input, i, i);
			}
		} else {
			for (int i = array_ui.count - 1; i >= size; i--) {
				// Our UI count is larger than the size, delete
				InputUI input_ui = input_ui_map_.take({ node, input, i });
				delete input_ui.main_label;
				qDeleteAll(input_ui.widget_bridge->widgets());
				delete input_ui.widget_bridge;
				delete input_ui.connected_label;
				delete input_ui.key_control;
				delete input_ui.array_insert_btn;
				delete input_ui.array_remove_btn;
			}

			grid->addWidget(array_ui.append_btn, size, k_array_insert_column);
		}

		array_ui.count = size;

		retranslate();
	}
}

void NodeParamViewItemBody::array_collapse_btn_pressed(bool checked)
{
	const NodeInputPair &input =
		array_collapse_buttons_.key(static_cast<CollapseButton *>(sender()));

	array_ui_.value(input).widget->setVisible(checked);
	if (checked) {
		// Ensure widgets are created (the signal will be ignored if they are)
		NodeInput resolved =
			ResolveGroupInput(NodeInput(input.node, input.input));
		input_array_size_changed_internal(input.node, input.input,
									  resolved.get_array_size());
	}

	emit array_expanded_changed(checked);
}

void NodeParamViewItemBody::input_array_size_changed(OakEngineNode *source,
												  const QString &input,
												  int old_sz, int size)
{
	Q_UNUSED(old_sz)

	NodeInputPair nip =
		input_group_lookup_.value({ reinterpret_cast<Node *>(source), input });

	input_array_size_changed_internal(nip.node, nip.input, size);
}

void NodeParamViewItemBody::array_append_clicked()
{
	for (auto it = array_ui_.cbegin(); it != array_ui_.cend(); it++) {
		if (it.value().append_btn == sender()) {
			NodeInput real_input = ResolveGroupInput(
				NodeInput(it.key().node, it.key().input));
			// Through the liboakengine C ABI facade (one undoable command,
			// same as the old NodeArrayInsertCommand push).
			oakengine_node_array_insert_at(
				reinterpret_cast<OakEngineNode *>(real_input.node()),
				real_input.input().toUtf8().constData(),
				real_input.get_array_size());
			break;
		}
	}
}

void NodeParamViewItemBody::array_insert_clicked()
{
	for (auto it = input_ui_map_.cbegin(); it != input_ui_map_.cend(); it++) {
		if (it.value().array_insert_btn == sender()) {
			// Found our input and element
			NodeInput ic = ResolveGroupInput(it.key());
			// Through the liboakengine C ABI facade (one undoable command).
			oakengine_node_array_insert_at(
				reinterpret_cast<OakEngineNode *>(ic.node()),
				ic.input().toUtf8().constData(), ic.element());
			break;
		}
	}
}

void NodeParamViewItemBody::array_remove_clicked()
{
	for (auto it = input_ui_map_.cbegin(); it != input_ui_map_.cend(); it++) {
		if (it.value().array_remove_btn == sender()) {
			// Found our input and element
			NodeInput ic = ResolveGroupInput(it.key());
			// Through the liboakengine C ABI facade (one undoable command).
			oakengine_node_array_remove_at(
				reinterpret_cast<OakEngineNode *>(ic.node()),
				ic.input().toUtf8().constData(), ic.element());
			break;
		}
	}
}

void NodeParamViewItemBody::toggle_array_expanded()
{
	NodeParamViewWidgetBridge *bridge =
		static_cast<NodeParamViewWidgetBridge *>(sender());

	for (auto it = input_ui_map_.cbegin(); it != input_ui_map_.cend(); it++) {
		if (it.value().widget_bridge == bridge) {
			CollapseButton *b =
				array_collapse_buttons_.value(it.key().input_pair());
			b->setChecked(!b->isChecked());
			return;
		}
	}
}

void NodeParamViewItemBody::set_timebase(const Rational &timebase)
{
	timebase_ = timebase;

	foreach (const InputUI &ui_obj, input_ui_map_) {
		set_timebase_on_input_ui(ui_obj);
	}
}

void NodeParamViewItemBody::set_timebase_on_input_ui(const InputUI &ui_obj)
{
	ui_obj.widget_bridge->set_timebase(timebase_);
}

void NodeParamViewItemBody::set_input_checked(const NodeInput &input, bool e)
{
	if (input_ui_map_.contains(input)) {
		QCheckBox *cb = input_ui_map_.value(input).optional_checkbox;
		if (cb) {
			cb->setChecked(e);
		}
	}
}

void NodeParamViewItemBody::replace_widgets(const NodeInput &input)
{
	InputUI ui = input_ui_map_.value(input);
	place_widgets_from_bridge(ui.layout, ui.widget_bridge, ui.row);
}

void NodeParamViewItemBody::show_speed_duration_dialog_for_node()
{
	// We should only get there if the node is a clip, determined by the dynamic_cast in CreateWidgets
	SpeedDurationDialog sdd({ static_cast<ClipBlock *>(node_) }, timebase_,
							this);
	sdd.exec();
}

void NodeParamViewItemBody::optional_check_box_clicked(bool e)
{
	QCheckBox *cb = static_cast<QCheckBox *>(sender());

	for (auto it = input_ui_map_.cbegin(); it != input_ui_map_.cend(); it++) {
		if (it.value().optional_checkbox == cb) {
			emit input_checked_changed(it.key(), e);
			break;
		}
	}
}

NodeParamViewItemBody::InputUI::InputUI()
	: main_label(nullptr)
	, widget_bridge(nullptr)
	, connected_label(nullptr)
	, key_control(nullptr)
	, extra_btn(nullptr)
	, optional_checkbox(nullptr)
	, array_insert_btn(nullptr)
	, array_remove_btn(nullptr)
{
}

}
