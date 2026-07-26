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

#ifndef OAK_NODEVIEWITEM_H
#define OAK_NODEVIEWITEM_H

#include <QFontMetrics>
#include <QGraphicsRectItem>
#include <QLinearGradient>
#include <QWidget>

#include "node/node.h"
#include "nodeviewcommon.h"
#include "nodeviewitemconnector.h"
#include "engineeventbridge.h"

namespace olive
{

class NodeViewItem;
class NodeViewEdge;

/**
 * @brief A visual widget representation of a Node object to be used in a NodeView
 *
 * This widget can be collapsed or expanded to show/hide the node's various parameters.
 *
 * To retrieve the NodeViewItem for a certain Node, use NodeView::NodeToUIObject().
 */
class NodeViewItem : public QObject, public QGraphicsRectItem {
	Q_OBJECT
public:
	NodeViewItem(Node *node, const QString &input, int element, Node *context,
				 QGraphicsItem *parent = nullptr);
	NodeViewItem(Node *node, Node *context, QGraphicsItem *parent = nullptr)
		: NodeViewItem(node, QString(), -1, context, parent)
	{
	}

	virtual ~NodeViewItem() override;

	Node::Position get_node_position_data() const;
	QPointF get_node_position() const;
	void set_node_position(const QPointF &pos);
	void set_node_position(const Node::Position &pos);

	QVector<NodeViewEdge *> get_all_edges_recursively() const;

	/**
   * @brief Get currently attached node
   */
	Node *get_node() const
	{
		return node_;
	}

	NodeInput get_input() const
	{
		return NodeInput(node_, input_, element_);
	}

	Node *get_context() const
	{
		return context_;
	}

	/**
   * @brief Get expanded state
   */
	bool is_expanded() const
	{
		return expanded_;
	}

	const QVector<NodeViewEdge *> &edges() const
	{
		return edges_;
	}

	/**
   * @brief Set expanded state
   */
	void set_expanded(bool e, bool hide_titlebar = false);
	void toggle_expanded();

	QPointF get_input_point() const;
	QPointF get_output_point() const;

	/**
   * @brief Sets the direction nodes are flowing
   */
	void set_flow_direction(NodeViewCommon::FlowDirection dir);

	NodeViewCommon::FlowDirection get_flow_direction() const
	{
		return flow_dir_;
	}

	static int default_text_padding();

	static int default_item_height();

	static int default_item_width();

	static int default_item_border();

	static QPointF node_to_screen_point(QPointF p,
									 NodeViewCommon::FlowDirection direction);
	static QPointF screen_to_node_point(QPointF p,
									 NodeViewCommon::FlowDirection direction);

	static qreal
	default_item_horizontal_padding(NodeViewCommon::FlowDirection dir);
	static qreal default_item_vertical_padding(NodeViewCommon::FlowDirection dir);
	qreal default_item_horizontal_padding() const;
	qreal default_item_vertical_padding() const;

	void add_edge(NodeViewEdge *edge);
	void remove_edge(NodeViewEdge *edge);

	bool is_labelled_as_output_of_context() const
	{
		return label_as_output_;
	}

	void set_label_as_output(bool e);

	void set_highlighted(bool e);

	NodeViewItem *get_item_for_input(NodeInput input);

	bool is_output_item() const
	{
		return input_.isEmpty();
	}

	void readjust_all_edges();

	void update_flow_direction_of_input_item(NodeViewItem *child);

	bool can_be_expanded() const;

protected:
	virtual void paint(QPainter *painter,
					   const QStyleOptionGraphicsItem *option,
					   QWidget *widget = nullptr) override;

	virtual void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

	virtual QVariant itemChange(QGraphicsItem::GraphicsItemChange change,
								const QVariant &value) override;

private:
	void update_context_rect();

	void draw_node_title(QPainter *painter, QString text, const QRectF &rect,
					   Qt::Alignment vertical_align, int icon_full_size);

	int draw_expand_arrow(QPainter *painter);

	/**
   * @brief Internal update function when logical position changes
   */
	void update_node_position();

	void update_input_connector_position();
	void update_output_connector_position();

	bool is_input_valid(const QString &input);

	void set_rect_size(int height_units = 1);

	void update_children_positions();

	int get_logical_height_with_children() const;

	/**
   * @brief Reference to attached Node
   */
	Node *node_;
	QString input_;
	int element_;

	Node *context_;

	/**
   * @brief Cached list of node inputs
   */
	QVector<NodeViewItem *> children_;

	/// Sizing variables to use when drawing
	int node_border_width_;

	/**
   * @brief Expanded state
   */
	bool expanded_;

	bool highlighted_;

	NodeViewCommon::FlowDirection flow_dir_;

	QVector<NodeViewEdge *> edges_;

	QPointF cached_node_pos_;

	QRect last_arrow_rect_;
	bool arrow_click_;

	NodeViewItemConnector *input_connector_;
	NodeViewItemConnector *output_connector_;

	bool has_connectable_inputs_;

	bool label_as_output_;

	EngineEventBridge *bridge_ = nullptr;

private slots:
	void node_appearance_changed();

	void repopulate_inputs();

	void input_array_size_changed(const QString &input);
};

}

#endif // OAK_NODEVIEWITEM_H
