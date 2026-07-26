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

#ifndef OAK_NODEVIEW_H
#define OAK_NODEVIEW_H

#include <QGraphicsView>
#include <QTimer>

#include "core.h"
#include "node/group/group.h"
#include "nodeviewedge.h"
#include "nodeviewcontext.h"
#include "nodeviewminimap.h"
#include "nodeviewscene.h"
#include "widget/handmovableview/handmovableview.h"
#include "widget/menu/menu.h"

namespace olive
{

/**
 * @brief A widget for viewing and editing node graphs
 *
 * This widget takes a NodeGraph object and constructs a QGraphicsScene representing its data, viewing and allowing
 * the user to make modifications to it.
 */
class NodeView : public HandMovableView {
	Q_OBJECT
public:
	NodeView(QWidget *parent = nullptr);

	virtual ~NodeView() override;

	void set_contexts(const QVector<Node *> &nodes);

	const QVector<Node *> &get_contexts() const
	{
		if (overlay_view_) {
			return overlay_view_->get_contexts();
		} else {
			return contexts_;
		}
	}

	bool is_group_overlay() const
	{
		return overlay_view_;
	}

	void close_contexts_belonging_to_project(Project *project);

	void clear_graph();

	/**
   * @brief Delete selected nodes from graph (user-friendly/undoable)
   */
	void delete_selected();

	void select_all();
	void deselect_all();

	void select(const QVector<Node::ContextPair> &nodes,
				bool center_view_on_item);

	void copy_selected(bool cut);
	void paste();

	void duplicate();

	void set_color_label(int index);

	void zoom_in();

	void zoom_out();

	const QVector<Node *> &get_current_contexts() const
	{
		return contexts_;
	}

public slots:
	void set_mini_map_enabled(bool e)
	{
		minimap_->setVisible(e);
	}

	void show_add_menu()
	{
		Menu *m = create_add_menu(nullptr);
		m->exec(QCursor::pos());
		delete m;
	}

	void center_on_items_bounding_rect();

	void center_on_node(OakEngineNode *n);

	void label_selected_nodes();

signals:
	void nodes_selected(const QVector<Node *> &nodes);

	void nodes_deselected(const QVector<Node *> &nodes);

	void node_selection_changed(const QVector<Node *> &nodes);
	void
	node_selection_changed_with_contexts(const QVector<Node::ContextPair> &nodes);

	void node_group_opened(NodeGroup *group);
	void node_group_closed();

	void esc_pressed();

protected:
	virtual void keyPressEvent(QKeyEvent *event) override;

	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;

	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dragMoveEvent(QDragMoveEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;
	virtual void dragLeaveEvent(QDragLeaveEvent *event) override;

	virtual void resizeEvent(QResizeEvent *event) override;

	virtual void zoom_into_cursor_position(QWheelEvent *event, double multiplier,
										const QPointF &cursor_pos) override;

	virtual bool event(QEvent *event) override;

	virtual bool eventFilter(QObject *object, QEvent *event) override;

	virtual void changeEvent(QEvent *e) override;

private:
	void detach_items_from_cursor(bool delete_nodes_too = true);

	void set_flow_direction(NodeViewCommon::FlowDirection dir);

	void move_attached_nodes_to_cursor(const QPoint &p);
	void process_moving_attached_nodes(const QPoint &pos);
	QVector<Node *> process_dropping_attached_nodes(MultiUndoCommand *command,
												 Node *select_context,
												 const QPoint &pos);
	Node *get_context_at_mouse_pos(const QPoint &p);

	void connect_selection_changed_signal();
	void disconnect_selection_changed_signal();

	void zoom_from_keyboard(double multiplier);

	void clear_create_edge_input_if_necessary();

	QPointF get_estimated_position_for_context(NodeViewItem *item,
										   Node *context) const;

	NodeViewItem *get_assumed_item_for_selected_node(Node *node);
	bool get_assumed_position_for_selected_node(Node *node, Node::Position *pos);

	Menu *create_add_menu(Menu *parent);

	void position_new_edge(const QPoint &pos);

	void add_context(Node *n);

	void remove_context(Node *n);

	bool is_item_attached_to_cursor(NodeViewItem *item) const;

	void expand_item(NodeViewItem *item);

	void collapse_item(NodeViewItem *item);

	void end_edge_drag(bool cancel = false);

	void post_paste(const QVector<Node *> &new_nodes,
				   const Node::PositionMap &map);

	void resize_overlay();

	NodeViewMiniMap *minimap_;

	NodeViewContext *get_context_item_from_node_item(NodeViewItem *item);

	struct AttachedItem {
		NodeViewItem *item;
		Node *node;
		QPointF original_pos;
	};

	void set_attached_items(const QVector<AttachedItem> &items);
	QVector<AttachedItem> attached_items_;

	NodeViewEdge *drop_edge_;
	NodeInput drop_input_;

	NodeViewEdge *create_edge_;
	NodeViewItem *create_edge_output_item_;
	NodeViewItem *create_edge_input_item_;
	NodeInput create_edge_input_;
	bool create_edge_already_exists_;
	bool create_edge_from_output_;

	QVector<NodeViewItem *> create_edge_expanded_items_;

	NodeViewScene scene_;

	QVector<Node *> selected_nodes_;

	QVector<Node *> contexts_;
	QVector<Node *> last_set_filter_nodes_;
	QMap<Node *, QPointF> context_offsets_;

	QMap<NodeViewItem *, QPointF> dragging_items_;

	NodeView *overlay_view_;

	double scale_;

	bool dont_emit_selection_signals_;

	QAction *show_in_param_editor_action_;

	static const double k_minimum_scale;

	static const int k_maximum_contexts;

private slots:
	/**
   * @brief Receiver for when the scene's selected items change
   */
	void update_selection_cache();

	/**
   * @brief Receiver for when the user right clicks (or otherwise requests a context menu)
   */
	void show_context_menu(const QPoint &pos);

	/**
   * @brief Receiver for when the user requests a new node from the add menu
   */
	void create_node_slot(QAction *action);

	/**
   * @brief Receiver for setting the direction from the context menu
   */
	void context_menu_set_direction(QAction *action);

	/**
   * @brief Opens the selected node in a Viewer
   */
	void open_selected_node_in_viewer();

	void update_scene_bounding_rect();

	void reposition_mini_map();

	void update_viewport_on_mini_map();

	void move_to_scene_point(const QPointF &pos);

	void node_removed_from_graph();

	void group_nodes();

	void ungroup_nodes();

	void show_node_properties();

	void show_selected_node_in_param_editor();

	void item_about_to_be_deleted(NodeViewItem *item);

	void close_overlay();
};

}

#endif // OAK_NODEVIEW_H
