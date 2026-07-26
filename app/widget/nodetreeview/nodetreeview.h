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

#ifndef OAK_NODETREEVIEW_H
#define OAK_NODETREEVIEW_H

#include <QTreeWidget>

#include "node/node.h"
#include "oakengine/node.h"

namespace olive
{

class NodeTreeView : public QTreeWidget {
	Q_OBJECT
public:
	NodeTreeView(QWidget *parent = nullptr);

	bool is_node_enabled(Node *n) const;

	bool is_input_enabled(const NodeKeyframeTrackReference &ref) const;

	void set_check_boxes_enabled(bool e)
	{
		checkboxes_enabled_ = e;
	}

	void set_keyframe_track_color(const NodeKeyframeTrackReference &ref,
							   const QColor &color);

	void set_only_show_keyframable(bool e)
	{
		only_show_keyframable_ = e;
	}

	void set_show_keyframe_tracks_as_rows(bool e)
	{
		show_keyframe_tracks_as_rows_ = e;
	}

public:
	// Not a slot: signature uses the engine C++ type Node*, which must not be
	// exposed to MOC (it would pull Node::staticMetaObject across the ABI
	// boundary). All connections use new-style member-function syntax.
	void set_nodes(const QVector<Node *> &nodes);

signals:
	void node_enable_changed(OakEngineNode *n, bool e);

	void input_enable_changed(const NodeKeyframeTrackReference &ref, bool e);

	void input_selection_changed(const NodeKeyframeTrackReference &ref);

	void input_double_clicked(const NodeKeyframeTrackReference &ref);

protected:
	virtual void changeEvent(QEvent *e) override;

	virtual void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	void retranslate();

	NodeKeyframeTrackReference get_selected_input();

	QTreeWidgetItem *create_item(QTreeWidgetItem *parent,
								const NodeKeyframeTrackReference &ref);

	void create_items_for_tracks(QTreeWidgetItem *parent, const NodeInput &input,
							  int track_count);

	static bool use_rgba_over_xyzw(const NodeKeyframeTrackReference &ref);

	enum ItemType { k_item_type_node, k_item_type_input };

	static const int k_item_type = Qt::UserRole;
	static const int k_item_input_reference = Qt::UserRole + 1;
	static const int k_item_node_pointer = Qt::UserRole + 1;

	QVector<Node *> nodes_;

	QVector<Node *> disabled_nodes_;

	QVector<NodeKeyframeTrackReference> disabled_inputs_;

	QHash<NodeKeyframeTrackReference, QTreeWidgetItem *> item_map_;

	bool only_show_keyframable_;

	bool show_keyframe_tracks_as_rows_;

	QHash<NodeKeyframeTrackReference, QColor> keyframe_colors_;

	bool checkboxes_enabled_;

private slots:
	void item_check_state_changed(QTreeWidgetItem *item, int column);

	void selection_changed();
};

}

#endif // OAK_NODETREEVIEW_H
