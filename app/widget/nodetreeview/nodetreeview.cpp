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

#include "nodetreeview.h"

#include <QEvent>

namespace olive
{

NodeTreeView::NodeTreeView(QWidget *parent)
	: QTreeWidget(parent)
	, only_show_keyframable_(false)
	, show_keyframe_tracks_as_rows_(false)
	, checkboxes_enabled_(false)
{
	connect(this, &NodeTreeView::itemChanged, this,
			&NodeTreeView::item_check_state_changed);
	connect(this, &NodeTreeView::itemSelectionChanged, this,
			&NodeTreeView::selection_changed);

	retranslate();
}

bool NodeTreeView::is_node_enabled(Node *n) const
{
	return !disabled_nodes_.contains(n);
}

bool NodeTreeView::is_input_enabled(const NodeKeyframeTrackReference &ref) const
{
	return !disabled_inputs_.contains(ref);
}

void NodeTreeView::set_keyframe_track_color(const NodeKeyframeTrackReference &ref,
										 const QColor &color)
{
	// Insert into hashmap
	keyframe_colors_.insert(ref, color);

	// If we currently have an item for this, set it
	QTreeWidgetItem *item = item_map_.value(ref);
	if (item) {
		item->setForeground(0, color);
	}
}

void NodeTreeView::set_nodes(const QVector<Node *> &nodes)
{
	nodes_ = nodes;

	this->clear();
	item_map_.clear();

	foreach (Node *n, nodes_) {
		QTreeWidgetItem *node_item = new QTreeWidgetItem();
		node_item->setText(0, n->name());
		if (checkboxes_enabled_) {
			node_item->setCheckState(
				0, disabled_nodes_.contains(n) ? Qt::Unchecked : Qt::Checked);
		}
		node_item->setData(0, k_item_type, k_item_type_node);
		node_item->setData(0, k_item_node_pointer, QtUtils::ptr_to_value(n));

		foreach (const QString &input, n->inputs()) {
			if (n->is_input_hidden(input) ||
				(only_show_keyframable_ && !n->is_input_keyframable(input))) {
				continue;
			}

			QTreeWidgetItem *input_item = nullptr;

			int arr_sz = n->input_array_size(input);
			for (int i = -1; i < arr_sz; i++) {
				NodeInput input_ref(n, input, i);
				const QVector<NodeKeyframeTrack> &key_tracks =
					n->get_keyframe_tracks(input_ref);

				int this_element_track;

				if (show_keyframe_tracks_as_rows_ &&
					(key_tracks.size() == 1 ||
					 (i == -1 && n->input_is_array(input)))) {
					this_element_track = 0;
				} else {
					this_element_track = -1;
				}

				QTreeWidgetItem *element_item;

				if (input_item) {
					element_item = create_item(
						input_item, NodeKeyframeTrackReference(
										input_ref, this_element_track));
				} else {
					input_item = create_item(node_item,
											NodeKeyframeTrackReference(
												input_ref, this_element_track));
					element_item = input_item;
				}

				if (show_keyframe_tracks_as_rows_ && key_tracks.size() > 1 &&
					(!n->input_is_array(input) || i >= 0)) {
					create_items_for_tracks(element_item, input_ref,
										 key_tracks.size());
				}
			}
		}

		// Add at the end to prevent unnecessary signalling while we're setting these objects up
		if (node_item->childCount() > 0) {
			this->addTopLevelItem(node_item);
		} else {
			delete node_item;
		}
	}

	expandAll();
}

void NodeTreeView::changeEvent(QEvent *e)
{
	QTreeWidget::changeEvent(e);

	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	}
}

void NodeTreeView::mouseDoubleClickEvent(QMouseEvent *e)
{
	QTreeWidget::mouseDoubleClickEvent(e);

	NodeKeyframeTrackReference ref = get_selected_input();

	if (ref.input().is_valid()) {
		emit input_double_clicked(ref);
	}
}

void NodeTreeView::retranslate()
{
	setHeaderLabel(tr("Nodes"));
}

NodeKeyframeTrackReference NodeTreeView::get_selected_input()
{
	QList<QTreeWidgetItem *> sel = selectedItems();

	NodeKeyframeTrackReference selected_ref;

	if (!sel.isEmpty()) {
		QTreeWidgetItem *item = sel.first();

		if (item->data(0, k_item_type).toInt() == k_item_type_input) {
			selected_ref = item->data(0, k_item_input_reference)
							   .value<NodeKeyframeTrackReference>();
		} else {
			selected_ref = NodeKeyframeTrackReference(NodeInput(
				QtUtils::value_to_ptr<Node>(item->data(0, k_item_node_pointer)),
				QString()));
		}
	}

	return selected_ref;
}

QTreeWidgetItem *NodeTreeView::create_item(QTreeWidgetItem *parent,
										  const NodeKeyframeTrackReference &ref)
{
	QTreeWidgetItem *input_item = new QTreeWidgetItem(parent);

	QString item_name;
	if (ref.track() == -1 ||
		NodeValue::get_number_of_keyframe_tracks(ref.input().get_data_type()) ==
			1 ||
		(ref.input().is_array() && ref.input().element() == -1)) {
		if (ref.input().element() == -1) {
			item_name = ref.input().name();
		} else {
			item_name = QString::number(ref.input().element());
		}
	} else {
		switch (ref.track()) {
		case 0:
			item_name = use_rgba_over_xyzw(ref) ? tr("R") : tr("X");
			break;
		case 1:
			item_name = use_rgba_over_xyzw(ref) ? tr("G") : tr("Y");
			break;
		case 2:
			item_name = use_rgba_over_xyzw(ref) ? tr("B") : tr("Z");
			break;
		case 3:
			item_name = use_rgba_over_xyzw(ref) ? tr("A") : tr("W");
			break;
		default:
			item_name = QString::number(ref.track());
		}
	}
	input_item->setText(0, item_name);

	if (checkboxes_enabled_) {
		input_item->setCheckState(
			0, disabled_inputs_.contains(ref) ? Qt::Unchecked : Qt::Checked);
	}
	input_item->setData(0, k_item_type, k_item_type_input);
	input_item->setData(0, k_item_input_reference, QVariant::fromValue(ref));

	if (keyframe_colors_.contains(ref)) {
		input_item->setForeground(0, keyframe_colors_.value(ref));
	}

	item_map_.insert(ref, input_item);

	return input_item;
}

void NodeTreeView::create_items_for_tracks(QTreeWidgetItem *parent,
										const NodeInput &input, int track_count)
{
	for (int j = 0; j < track_count; j++) {
		create_item(parent, NodeKeyframeTrackReference(input, j));
	}
}

bool NodeTreeView::use_rgba_over_xyzw(const NodeKeyframeTrackReference &ref)
{
	return ref.input().get_data_type() == NodeValue::k_color;
}

void NodeTreeView::item_check_state_changed(QTreeWidgetItem *item, int column)
{
	Q_UNUSED(column)

	switch (item->data(0, k_item_type).toInt()) {
	case k_item_type_node: {
		Node *n = QtUtils::value_to_ptr<Node>(item->data(0, k_item_node_pointer));

		if (item->checkState(0) == Qt::Checked) {
			if (disabled_nodes_.contains(n)) {
				disabled_nodes_.removeOne(n);
				emit node_enable_changed(n, true);
			}
		} else if (!disabled_nodes_.contains(n)) {
			disabled_nodes_.append(n);
			emit node_enable_changed(n, false);
		}
		break;
	}
	case k_item_type_input: {
		NodeKeyframeTrackReference i = item->data(0, k_item_input_reference)
										   .value<NodeKeyframeTrackReference>();

		if (item->checkState(0) == Qt::Checked) {
			if (disabled_inputs_.contains(i)) {
				disabled_inputs_.removeOne(i);
				emit input_enable_changed(i, true);
			}
		} else if (!disabled_inputs_.contains(i)) {
			disabled_inputs_.append(i);
			emit input_enable_changed(i, false);
		}
		break;
	}
	}
}

void NodeTreeView::selection_changed()
{
	emit input_selection_changed(get_selected_input());
}

}
