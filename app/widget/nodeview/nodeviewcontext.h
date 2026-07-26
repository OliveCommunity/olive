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

#ifndef OAK_NODEVIEWCONTEXT_H
#define OAK_NODEVIEWCONTEXT_H

#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QHash>

#include "engineeventbridge.h"
#include "node/node.h"
#include "nodeviewcommon.h"
#include "nodeviewedge.h"

namespace olive
{

class NodeViewContext : public QObject, public QGraphicsRectItem {
	Q_OBJECT
public:
	NodeViewContext(Node *context, QGraphicsItem *item = nullptr);

	virtual ~NodeViewContext() override;

	Node *get_context() const
	{
		return context_;
	}

	void update_rect();

	void set_flow_direction(NodeViewCommon::FlowDirection dir);

	void set_curved_edges(bool e);

	void get_selected_for_deletion(QVector<Node *> &nodes,
								  QVector<Node *> &contexts,
								  QVector<NodeViewEdge *> &edges) const;

	void select(const QVector<Node *> &nodes);

	QVector<NodeViewItem *> get_selected_items() const;

	QPointF map_scene_pos_to_node_pos_in_context(const QPointF &pos) const;

	NodeViewItem *get_item_from_map(Node *node) const
	{
		return item_map_.value(node);
	}

	virtual void paint(QPainter *painter,
					   const QStyleOptionGraphicsItem *option,
					   QWidget *widget = nullptr) override;

public:
	// Not slots: signatures use the engine C++ type Node*, which must not be
	// exposed to MOC (it would pull Node::staticMetaObject across the ABI
	// boundary). They are invoked from lambdas / directly, never as connect()
	// targets.
	void add_child(Node *node);

	void set_child_position(Node *node, const QPointF &pos);

	void remove_child(Node *node);

	void child_input_connected(Node *output, const NodeInput &input);

	bool child_input_disconnected(Node *output, const NodeInput &input);

signals:
	void item_about_to_be_deleted(NodeViewItem *item);

protected:
	virtual QVariant itemChange(QGraphicsItem::GraphicsItemChange change,
								const QVariant &value) override;

	virtual void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
	void add_node_internal(Node *node, NodeViewItem *item);

	void add_edge_internal(Node *output, const NodeInput &input,
						 NodeViewItem *from, NodeViewItem *to);

	Node *context_;

	QString lbl_;

	NodeViewCommon::FlowDirection flow_dir_;

	bool curved_edges_;

	int last_titlebar_height_;

	QMap<Node *, NodeViewItem *> item_map_;

	QVector<NodeViewEdge *> edges_;

	EngineEventBridge *bridge_ = nullptr;

	QHash<Node *, QVector<int64_t>> node_subs_;

private:
	// Ordinary member functions (NOT slots): signatures use Node*, which must
	// not be exposed to MOC. Invoked from lambdas only.
	void group_added_node(Node *node, Node *group);

	void group_removed_node(Node *node, Node *group);
};

}

#endif // OAK_NODEVIEWCONTEXT_H
