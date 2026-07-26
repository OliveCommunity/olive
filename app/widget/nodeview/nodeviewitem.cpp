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

#include "nodeviewitem.h"

#include <QDebug>

#include "oakengine/node.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include "common/qtutils.h"
#include "common/configwrapper.h"
#include "core.h"
#include "node/value.h"
#include "pluginSupport/oliveplugininstance.h"
#include "nodeview.h"
#include "nodeviewscene.h"
#include "ui/colorcoding.h"
#include "ui/icons/icons.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

NodeViewItem::NodeViewItem(Node *node, const QString &input, int element,
						   Node *context, QGraphicsItem *parent)
	: QGraphicsRectItem(parent)
	, node_(node)
	, input_(input)
	, element_(element)
	, context_(context)
	, expanded_(false)
	, highlighted_(false)
	, flow_dir_(NodeViewCommon::k_invalid_direction)
	, arrow_click_(false)
	, label_as_output_(false)
{
	//
	// We use font metrics to set all the UI measurements for DPI-awareness
	//

	// Set border width
	node_border_width_ = default_item_border();

	// Set rect size to default
	set_rect_size();

	// Create connector
	input_connector_ = new NodeViewItemConnector(false, this);
	output_connector_ = new NodeViewItemConnector(true, this);

	bridge_ = new EngineEventBridge(this);
	bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_LABEL_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_COLOR_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED);
	connect(bridge_, &EngineEventBridge::node_label_changed, this,
			&NodeViewItem::node_appearance_changed);
	connect(bridge_, &EngineEventBridge::node_color_changed, this,
			&NodeViewItem::node_appearance_changed);
	connect(bridge_, &EngineEventBridge::node_message_count_changed, this,
			&NodeViewItem::node_appearance_changed);

	if (is_output_item()) {
		bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_INPUT_ADDED);
		bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_INPUT_REMOVED);
		connect(bridge_, &EngineEventBridge::node_input_added, this,
				&NodeViewItem::repopulate_inputs);
		connect(bridge_, &EngineEventBridge::node_input_removed, this,
				&NodeViewItem::repopulate_inputs);
		repopulate_inputs();

		// Set flags for this widget
		setFlag(QGraphicsItem::ItemSendsGeometryChanges);
		setFlag(QGraphicsItem::ItemIsMovable);
		setFlag(QGraphicsItem::ItemIsSelectable);

		if (context_) {
			set_node_position(context_->get_node_position_data_in_context(node_));
		}
	} else {
		output_connector_->setVisible(false);

		bridge_->subscribe(reinterpret_cast<void *>(node_), OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED);
		connect(bridge_, &EngineEventBridge::node_input_array_size_changed, this,
				[this](OakEngineNode *, const QString &input, int, int) {
					input_array_size_changed(input);
				});
	}

	// This should be set during runtime, but just in case here's a default fallback
	set_flow_direction(NodeViewCommon::k_left_to_right);
}

NodeViewItem::~NodeViewItem()
{
	Q_ASSERT(edges_.isEmpty());
}

Node::Position NodeViewItem::get_node_position_data() const
{
	return Node::Position(get_node_position(), is_expanded());
}

QPointF NodeViewItem::get_node_position() const
{
	return screen_to_node_point(pos(), flow_dir_);
}

void NodeViewItem::set_node_position(const QPointF &pos)
{
	cached_node_pos_ = pos;

	update_node_position();
}

void NodeViewItem::set_node_position(const Node::Position &pos)
{
	set_node_position(pos.position);
	set_expanded(pos.expanded);
}

QVector<NodeViewEdge *> NodeViewItem::get_all_edges_recursively() const
{
	QVector<NodeViewEdge *> list = edges_;

	foreach (NodeViewItem *item, children_) {
		list.append(item->get_all_edges_recursively());
	}

	return list;
}

int NodeViewItem::default_text_padding()
{
	return QFontMetrics(QFont()).height() / 4;
}

int NodeViewItem::default_item_height()
{
	return QFontMetrics(QFont()).height() + default_text_padding() * 2;
}

int NodeViewItem::default_item_width()
{
	return QtUtils::q_font_metrics_width(QFontMetrics(QFont()),
									  "HHHHHHHHHHHHHHHH");
	;
}

int NodeViewItem::default_item_border()
{
	return QFontMetrics(QFont()).height() / 12;
}

QPointF NodeViewItem::node_to_screen_point(QPointF p,
										NodeViewCommon::FlowDirection direction)
{
	switch (direction) {
	case NodeViewCommon::k_left_to_right:
		// NodeGraphs are always left-to-right internally, no need to translate
		break;
	case NodeViewCommon::k_right_to_left:
		// Invert X value
		p.setX(-p.x());
		break;
	case NodeViewCommon::k_top_to_bottom:
		// Swap X/Y
		p = QPointF(p.y(), p.x());
		break;
	case NodeViewCommon::k_bottom_to_top:
		// Swap X/Y and invert Y
		p = QPointF(p.y(), -p.x());
		break;
	case NodeViewCommon::k_invalid_direction:
		break;
	}

	// Multiply by item sizes for this direction
	p.setX(p.x() * default_item_horizontal_padding(direction));
	p.setY(p.y() * default_item_vertical_padding(direction));

	return p;
}

QPointF NodeViewItem::screen_to_node_point(QPointF p,
										NodeViewCommon::FlowDirection direction)
{
	// Divide by item sizes for this direction
	p.setX(p.x() / default_item_horizontal_padding(direction));
	p.setY(p.y() / default_item_vertical_padding(direction));

	switch (direction) {
	case NodeViewCommon::k_left_to_right:
		// NodeGraphs are always left-to-right internally, no need to translate
		break;
	case NodeViewCommon::k_right_to_left:
		// Invert X value
		p.setX(-p.x());
		break;
	case NodeViewCommon::k_top_to_bottom:
		// Swap X/Y
		p = QPointF(p.y(), p.x());
		break;
	case NodeViewCommon::k_bottom_to_top:
		// Swap X/Y and invert Y
		p = QPointF(-p.y(), p.x());
		break;
	case NodeViewCommon::k_invalid_direction:
		break;
	}

	return p;
}

qreal NodeViewItem::default_item_horizontal_padding(
	NodeViewCommon::FlowDirection dir)
{
	if (NodeViewCommon::get_flow_orientation(dir) == Qt::Horizontal) {
		return default_item_width() * 1.5;
	} else {
		return default_item_width() * 1.25;
	}
}

qreal NodeViewItem::default_item_vertical_padding(NodeViewCommon::FlowDirection dir)
{
	if (NodeViewCommon::get_flow_orientation(dir) == Qt::Horizontal) {
		return default_item_height() * 1.5;
	} else {
		return default_item_height() * 2.0;
	}
}

qreal NodeViewItem::default_item_horizontal_padding() const
{
	return default_item_horizontal_padding(flow_dir_);
}

qreal NodeViewItem::default_item_vertical_padding() const
{
	return default_item_vertical_padding(flow_dir_);
}

void NodeViewItem::add_edge(NodeViewEdge *edge)
{
	edges_.append(edge);
}

void NodeViewItem::remove_edge(NodeViewEdge *edge)
{
	edges_.removeOne(edge);
}

void NodeViewItem::set_expanded(bool e, bool hide_titlebar)
{
	if (!can_be_expanded() || (expanded_ == e)) {
		return;
	}

	expanded_ = e;

	if (context_) {
		context_->set_node_expanded_in_context(node_, e);
	}

	if (is_output_item()) {
		// We don't have to check has_connectable_inputs_ here because we did it at the top
		input_connector_->setVisible(!expanded_);
	}

	if (expanded_) {
		node_->retranslate();

		if (is_output_item()) {
			// Create items for each input of the node
			foreach (const QString &input, node_->inputs()) {
				if (is_input_valid(input)) {
					NodeViewItem *item =
						new NodeViewItem(node_, input, -1, context_, this);
					children_.append(item);
				}
			}

			QVector<NodeViewEdge *> edges = edges_;
			for (auto it = edges.cbegin(); it != edges.cend(); it++) {
				if ((*it)->to_item() == this) {
					(*it)->set_to_item(get_item_for_input((*it)->input()));
				}
			}
		} else {
			// Create items for each element of the input array
			int arr_sz = node_->input_array_size(input_);
			children_.resize(arr_sz);
			for (int i = 0; i < arr_sz; i++) {
				NodeViewItem *item =
					new NodeViewItem(node_, input_, i, context_, this);
				children_[i] = item;
			}

			QVector<NodeViewEdge *> edges = edges_;
			for (auto it = edges.cbegin(); it != edges.cend(); it++) {
				if ((*it)->to_item() == this) {
					(*it)->set_to_item(get_item_for_input((*it)->input()));
				}
			}
		}
	} else {
		foreach (NodeViewItem *child, children_) {
			QVector<NodeViewEdge *> child_edges = child->edges();
			foreach (NodeViewEdge *edge, child_edges) {
				edge->set_to_item(this);
			}
			delete child;
		}
		children_.clear();
	}

	update_children_positions();

	if (flow_dir_ == NodeViewCommon::k_top_to_bottom) {
		update_output_connector_position();
	}

	readjust_all_edges();

	update_context_rect();

	update();
}

void NodeViewItem::toggle_expanded()
{
	set_expanded(!is_expanded());
}

void NodeViewItem::paint(QPainter *painter,
						 const QStyleOptionGraphicsItem *option, QWidget *)
{
	// Use main window palette since the palette passed in `widget` is the NodeView palette which
	// has been slightly modified
	QPalette app_pal = Core::instance()->main_window()->palette();

	// We only draw a single unit's worth
	QRectF single_unit_rect = rect();
	single_unit_rect.setHeight(default_item_height());

	if (is_output_item()) {
		// Set output item colors
		painter->setPen(Qt::black);
		painter->setBrush(
			node_->brush(single_unit_rect.top(), single_unit_rect.bottom()));
	} else {
		// Set input item colors
		painter->setPen(Qt::NoPen);
		painter->setBrush(element_ == -1 ? app_pal.color(QPalette::Window) :
										   app_pal.color(QPalette::Base));
	}

	painter->drawRect(single_unit_rect);

	// Draw highlight if applicable
	if (highlighted_) {
		QColor highlight_col = app_pal.color(QPalette::Text);
		highlight_col.setAlpha(64);
		painter->setBrush(highlight_col);
		painter->drawRect(rect());
	}

	// Determine what text to draw and whether to draw an arrow
	QString node_label, node_name;

	if (is_output_item()) {
		if (label_as_output_) {
			node_name = QCoreApplication::translate("NodeViewItem", "Output");
		} else {
			node_label = node_->get_label();
			node_name = node_->short_name();
		}
	} else {
		if (element_ == -1) {
			node_name = node_->get_input_name(input_);
		} else {
			node_name = QString::number(
				element_ +
				node_->get_input_property(input_, QStringLiteral("arraystart"))
					.toInt());
		}
	}

	// Draw arrow if necessary
	int arrow_size = can_be_expanded() ? draw_expand_arrow(painter) : 0;

	if (is_output_item()) {
		// Determine the text color (automatically calculate from node background color)
		painter->setPen(ColorCoding::get_ui_selector_color(node_->color()));
	} else {
		// Just use text item
		painter->setPen(app_pal.text().color());
	}

	if (node_label.isEmpty()) {
		// Draw name only
		draw_node_title(painter, node_name, single_unit_rect, Qt::AlignVCenter,
					  arrow_size);
	} else {
		int text_pad = default_text_padding() / 2;
		QRectF safe_label_bounds =
			single_unit_rect.adjusted(text_pad, text_pad, -text_pad, -text_pad);
		QFont f;
		qreal font_sz = f.pointSizeF();

		// Draw label as larger/upper text
		f.setPointSizeF(font_sz * 0.8);
		painter->setFont(f);
		draw_node_title(painter, node_label, safe_label_bounds, Qt::AlignTop,
					  arrow_size);

		// Draw node name as smaller/lower text
		f.setPointSizeF(font_sz * 0.6);
		painter->setFont(f);
		draw_node_title(painter, node_name, safe_label_bounds, Qt::AlignBottom,
					  arrow_size);
	}

	if (is_output_item()) {
		auto *instance = node_->getPluginInstance();
		auto *olive_instance =
			dynamic_cast<plugin::OlivePluginInstance *>(instance);
		int message_count =
			olive_instance ? olive_instance->persistent_message_count() : 0;

		if (message_count > 0) {
			QString badge_text = QString::number(message_count);
			QFont badge_font = painter->font();
			badge_font.setPointSizeF(badge_font.pointSizeF() * 0.7);
			painter->setFont(badge_font);

			QFontMetrics badge_metrics(badge_font);
			int text_width = badge_metrics.horizontalAdvance(badge_text);
			int text_height = badge_metrics.height();
			int pad = text_height / 3;
			int badge_width = qMax(text_width + pad * 2, text_height + pad);
			int badge_height = text_height + pad;

			QRectF badge_rect(single_unit_rect.right() - badge_width - 4,
							  single_unit_rect.top() + 4, badge_width,
							  badge_height);

			painter->setPen(Qt::NoPen);
			painter->setBrush(QColor(220, 50, 47));
			painter->drawRoundedRect(badge_rect, badge_height / 2,
									 badge_height / 2);

			painter->setPen(Qt::white);
			painter->drawText(badge_rect, Qt::AlignCenter, badge_text);
		}
	}

	// Draw final border (output only)
	if (is_output_item()) {
		QPen border_pen;
		border_pen.setWidth(node_border_width_);

		if (option->state & QStyle::State_Selected) {
			border_pen.setColor(app_pal.color(QPalette::Highlight));
		} else {
			border_pen.setColor(Qt::black);
		}

		painter->setPen(border_pen);
		painter->setBrush(Qt::NoBrush);

		painter->drawRect(rect());
	}
}

void NodeViewItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	if (last_arrow_rect_.contains(event->pos().toPoint())) {
		arrow_click_ = true;
		toggle_expanded();
		return;
	}

	event->setModifiers(
		QtUtils::flip_control_and_shift_modifiers(event->modifiers()));

	QGraphicsRectItem::mousePressEvent(event);
}

void NodeViewItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (arrow_click_) {
		return;
	}

	event->setModifiers(
		QtUtils::flip_control_and_shift_modifiers(event->modifiers()));

	QGraphicsRectItem::mouseMoveEvent(event);
}

void NodeViewItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (arrow_click_) {
		arrow_click_ = false;
		return;
	}

	event->setModifiers(
		QtUtils::flip_control_and_shift_modifiers(event->modifiers()));

	QGraphicsRectItem::mouseReleaseEvent(event);
}

QVariant NodeViewItem::itemChange(QGraphicsItem::GraphicsItemChange change,
								  const QVariant &value)
{
	if (node_) {
		if (change == ItemPositionHasChanged) {
			readjust_all_edges();

			update_context_rect();
		} else if (change == ItemSelectedHasChanged) {
			if (value.toBool()) {
				qDebug() << "Selected node:" << node_;
			}
		}
	}

	return QGraphicsItem::itemChange(change, value);
}

void NodeViewItem::readjust_all_edges()
{
	foreach (NodeViewEdge *edge, edges_) {
		if (NodeViewItem *to_item = edge->to_item()) {
			static_cast<NodeViewItem *>(to_item->parentItem())
				->update_flow_direction_of_input_item(to_item);
		}

		edge->adjust();
	}
	foreach (NodeViewItem *child, children_) {
		child->readjust_all_edges();
	}
}

void NodeViewItem::update_context_rect()
{
	QGraphicsItem *item = parentItem();

	while (item) {
		if (NodeViewContext *ctx = dynamic_cast<NodeViewContext *>(item)) {
			ctx->update_rect();
			break;
		}

		item = item->parentItem();
	}
}

void NodeViewItem::draw_node_title(QPainter *painter, QString text,
								 const QRectF &rect,
								 Qt::Alignment vertical_align,
								 int icon_full_size)
{
	QFontMetrics fm = painter->fontMetrics();

	// Calculate how much space we have for text
	int item_width = this->rect().width();
	int max_text_width = item_width - default_text_padding() * 2 - icon_full_size;
	int label_width = QtUtils::q_font_metrics_width(fm, text);

	// Concatenate text if necessary (adds a "..." to the end and removes characters until the
	// string fits in the bounds)
	if (label_width > max_text_width) {
		QString concatenated;

		do {
			text.chop(1);
			concatenated =
				QCoreApplication::translate("NodeViewItem", "%1...").arg(text);
		} while ((label_width = QtUtils::q_font_metrics_width(fm, concatenated)) >
				 max_text_width);

		text = concatenated;
	}

	// Determine X position (favors horizontal centering unless it'll overrun the arrow)
	QRectF text_rect = rect;
	Qt::Alignment text_align = Qt::AlignHCenter | vertical_align;
	int likely_x = item_width / 2 - label_width / 2;
	if (likely_x < icon_full_size) {
		text_rect.adjust(icon_full_size, 0, 0, 0);
		text_align = Qt::AlignLeft | vertical_align;
	}

	// Draw the text in a rect (the rect is sized around text already in the constructor)
	painter->drawText(text_rect, text_align, text);
}

int NodeViewItem::draw_expand_arrow(QPainter *painter)
{
	// Draw right or down arrow based on expanded state
	int icon_size = painter->fontMetrics().height() / 2;
	int icon_padding = default_item_height() / 2 - icon_size / 2;
	int icon_full_size = icon_size + icon_padding * 2;

	painter->setRenderHint(QPainter::SmoothPixmapTransform);

	const QIcon &expand_icon = is_expanded() ? icon::tri_down : icon::tri_right;
	int icon_size_scaled = icon_size * painter->transform().m11();

	last_arrow_rect_ = QRect(this->rect().x() + icon_padding,
							 this->rect().y() + icon_padding, icon_size,
							 icon_size);

	painter->drawPixmap(
		last_arrow_rect_,
		expand_icon.pixmap(QSize(icon_size_scaled, icon_size_scaled)));

	return icon_full_size;
}

void NodeViewItem::set_label_as_output(bool e)
{
	label_as_output_ = e;
	output_connector_->setVisible(!e);
	update();
}

QPointF NodeViewItem::get_input_point() const
{
	return input_connector_->scenePos();
}

QPointF NodeViewItem::get_output_point() const
{
	QPointF p = output_connector_->scenePos();
	QRectF r = output_connector_->polygon().boundingRect();

	switch (flow_dir_) {
	case NodeViewCommon::k_left_to_right:
	default:
		p.setX(p.x() + r.width());
		break;
	case NodeViewCommon::k_right_to_left:
		p.setX(p.x() - r.width());
		break;
	case NodeViewCommon::k_top_to_bottom:
		p.setY(p.y() + r.height());
		break;
	case NodeViewCommon::k_bottom_to_top:
		p.setY(p.y() - r.height());
		break;
	}

	return p;
}

void NodeViewItem::set_flow_direction(NodeViewCommon::FlowDirection dir)
{
	if (flow_dir_ != dir) {
		flow_dir_ = dir;

		input_connector_->set_flow_direction(dir);
		output_connector_->set_flow_direction(dir);

		update_input_connector_position();
		update_output_connector_position();

		if (is_output_item()) {
			update_node_position();
		}

		readjust_all_edges();
	}
}

void NodeViewItem::update_node_position()
{
	setPos(node_to_screen_point(cached_node_pos_, flow_dir_));
}

void NodeViewItem::update_input_connector_position()
{
	QRectF output_rect = input_connector_->polygon().boundingRect();

	NodeViewCommon::FlowDirection using_flow_dir = flow_dir_;

	if (is_expanded() && !NodeViewCommon::is_flow_horizontal(flow_dir_)) {
		if (edges_.isEmpty() || edges_.first()->from_item()->x() < this->x()) {
			using_flow_dir = NodeViewCommon::k_left_to_right;
		} else {
			using_flow_dir = NodeViewCommon::k_right_to_left;
		}
	}

	// Input connector flow directions change conditionally
	switch (using_flow_dir) {
	case NodeViewCommon::k_left_to_right:
		input_connector_->setPos(rect().left() - output_rect.width(), 0);
		break;
	case NodeViewCommon::k_right_to_left:
		input_connector_->setPos(rect().right() + output_rect.width(), 0);
		break;
	case NodeViewCommon::k_top_to_bottom:
		input_connector_->setPos(rect().center().x(),
								 rect().top() - output_rect.height());
		break;
	case NodeViewCommon::k_bottom_to_top:
		input_connector_->setPos(rect().center().x(),
								 rect().bottom() + output_rect.height());
		break;
	case NodeViewCommon::k_invalid_direction:
		break;
	}
}

void NodeViewItem::update_output_connector_position()
{
	switch (flow_dir_) {
	case NodeViewCommon::k_left_to_right:
		output_connector_->setPos(rect().right(), 0);
		break;
	case NodeViewCommon::k_right_to_left:
		output_connector_->setPos(rect().left(), 0);
		break;
	case NodeViewCommon::k_top_to_bottom:
		output_connector_->setPos(rect().center().x(), rect().bottom());
		break;
	case NodeViewCommon::k_bottom_to_top:
		output_connector_->setPos(rect().center().x(), rect().top());
		break;
	case NodeViewCommon::k_invalid_direction:
		break;
	}
}

bool NodeViewItem::is_input_valid(const QString &input)
{
	if (!node_->is_input_connectable(input) || node_->is_input_hidden(input)) {
		return false;
	}
	// For OFX plugin nodes, only show texture inputs in the node graph
	// to avoid excessively tall nodes with dozens of scalar parameters.
	// Scalar parameters are still visible in the parameter panel.
	if (node_->getPluginInstance() != nullptr &&
		node_->get_input_data_type(input) != NodeValue::k_texture) {
		return false;
	}
	return true;
}

void NodeViewItem::set_rect_size(int height_units)
{
	// Set rect
	int widget_width = default_item_width();
	int widget_height = default_item_height();

	setRect(QRectF(-widget_width / 2, -widget_height / 2, widget_width,
				   widget_height * height_units));
}

bool NodeViewItem::can_be_expanded() const
{
	if (is_output_item()) {
		return has_connectable_inputs_;
	} else {
		return node_->get_input_flags(input_) & k_input_flag_array &&
			   element_ == -1 && !node_->is_input_connected(input_);
	}
}

void NodeViewItem::update_children_positions()
{
	int y = 1;
	int h = default_item_height();

	foreach (NodeViewItem *c, children_) {
		c->setPos(QPointF(0, y * h));

		y += c->get_logical_height_with_children();
	}

	set_rect_size(y);

	if (NodeViewItem *p = dynamic_cast<NodeViewItem *>(parentItem())) {
		p->update_children_positions();
	}
}

int NodeViewItem::get_logical_height_with_children() const
{
	int h = 1;

	foreach (NodeViewItem *c, children_) {
		h += c->get_logical_height_with_children();
	}

	return h;
}

void NodeViewItem::update_flow_direction_of_input_item(NodeViewItem *child)
{
	if (!child->is_output_item()) {
		if (NodeViewCommon::is_flow_vertical(flow_dir_)) {
			if (!child->edges().isEmpty() &&
				child->edges().first()->from_item()->scenePos().x() >
					child->scenePos().x()) {
				child->set_flow_direction(NodeViewCommon::k_right_to_left);
			} else {
				child->set_flow_direction(NodeViewCommon::k_left_to_right);
			}
		} else {
			child->set_flow_direction(flow_dir_);
		}
	}
}

void NodeViewItem::repopulate_inputs()
{
	if (is_output_item()) {
		has_connectable_inputs_ = false;

		foreach (const QString &input, node_->inputs()) {
			if (is_input_valid(input)) {
				has_connectable_inputs_ = true;
				break;
			}
		}

		input_connector_->setVisible(has_connectable_inputs_);
	}

	if (is_expanded() && (is_output_item() || element_ == -1)) {
		// Create or remove inputs when necessary
		// NOTE: This is not the most efficient thing in the world, but it does work
		set_expanded(false);
		set_expanded(true);
	}
}

void NodeViewItem::input_array_size_changed(const QString &input)
{
	if (input == input_) {
		repopulate_inputs();
	}
}

void NodeViewItem::node_appearance_changed()
{
	update();
}

void NodeViewItem::set_highlighted(bool e)
{
	highlighted_ = e;
	update();
}

NodeViewItem *NodeViewItem::get_item_for_input(NodeInput input)
{
	if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(node_))) {
		if (input.node() != node_) {
			// Translate input to group input
			char id[256];
			if (oakengine_group_get_id_of_passthrough(
					reinterpret_cast<OakEngineNode *>(node_),
					reinterpret_cast<OakEngineNode *>(input.node()),
					input.input().toUtf8().constData(), input.element(),
					id, sizeof(id)) > 0) {
				input.set_node(node_);
				input.set_input(QString::fromUtf8(id));
			}
		}
	}

	if (is_expanded()) {
		if (input_.isEmpty()) {
			// Look for the input in our children
			foreach (NodeViewItem *i, children_) {
				if (i->input_ == input.input()) {
					return i->get_item_for_input(input);
				}
			}
		} else {
			// Look for element in our children
			if (input.element() >= 0 && input.element() < children_.size()) {
				return children_.at(input.element())->get_item_for_input(input);
			}
		}
	}

	// Fallback to this object
	return this;
}

}
