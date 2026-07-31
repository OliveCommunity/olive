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

#include "oakengine/node.h"
#include "oakutil/qtutils.h"
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

bool NodeTreeView::is_node_enabled(const oak::Node &n) const
{
	return !disabled_nodes_.contains(n);
}

bool NodeTreeView::is_input_enabled(const oak::KeyframeTrackRef &ref) const
{
	return !disabled_inputs_.contains(ref);
}

void NodeTreeView::set_keyframe_track_color(const oak::KeyframeTrackRef &ref,
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

void NodeTreeView::set_nodes(const QVector<oak::Node> &nodes)
{
	nodes_ = nodes;

	this->clear();
	item_map_.clear();

	foreach (const oak::Node &n, nodes_) {
		QTreeWidgetItem *node_item = new QTreeWidgetItem();
		node_item->setText(0, n.name());
		if (checkboxes_enabled_) {
			node_item->setCheckState(
				0, disabled_nodes_.contains(n) ? Qt::Unchecked : Qt::Checked);
		}
		node_item->setData(0, k_item_type, k_item_type_node);
		node_item->setData(0, k_item_node_pointer, QtUtils::ptr_to_value(n.handle()));

		const int input_count = n.input_count();
		for (int idx = 0; idx < input_count; idx++) {
			const QString input_id = n.input_id(idx);

			oak::Input probe(n.handle(), input_id);
			if (probe.is_hidden() ||
				(only_show_keyframable_ && !probe.is_keyframable())) {
				continue;
			}

			QTreeWidgetItem *input_item = nullptr;

			const int arr_sz = probe.array_size();
			for (int i = -1; i < arr_sz; i++) {
				oak::Input input_ref(n.handle(), input_id, i);
				const int track_count = input_ref.keyframe_track_count();

				int this_element_track;

				if (show_keyframe_tracks_as_rows_ &&
					(track_count == 1 ||
					 (i == -1 && probe.is_array()))) {
					this_element_track = 0;
				} else {
					this_element_track = -1;
				}

				QTreeWidgetItem *element_item;

				if (input_item) {
					element_item = create_item(
						input_item, oak::KeyframeTrackRef(
										input_ref, this_element_track));
				} else {
					input_item = create_item(node_item,
											oak::KeyframeTrackRef(
												input_ref, this_element_track));
					element_item = input_item;
				}

				if (show_keyframe_tracks_as_rows_ && track_count > 1 &&
					(!probe.is_array() || i >= 0)) {
					create_items_for_tracks(element_item, input_ref,
										 track_count);
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

	oak::KeyframeTrackRef ref = get_selected_input();

	if (ref.input().is_valid()) {
		emit input_double_clicked(ref);
	}
}

void NodeTreeView::retranslate()
{
	setHeaderLabel(tr("Nodes"));
}

oak::KeyframeTrackRef NodeTreeView::get_selected_input()
{
	QList<QTreeWidgetItem *> sel = selectedItems();

	oak::KeyframeTrackRef selected_ref;

	if (!sel.isEmpty()) {
		QTreeWidgetItem *item = sel.first();

		if (item->data(0, k_item_type).toInt() == k_item_type_input) {
			selected_ref = item->data(0, k_item_input_reference)
							   .value<oak::KeyframeTrackRef>();
		} else {
			selected_ref = oak::KeyframeTrackRef(oak::Input(
				QtUtils::value_to_ptr<OakEngineNode>(item->data(0, k_item_node_pointer)),
				QString()));
		}
	}

	return selected_ref;
}

QTreeWidgetItem *NodeTreeView::create_item(QTreeWidgetItem *parent,
										  const oak::KeyframeTrackRef &ref)
{
	QTreeWidgetItem *input_item = new QTreeWidgetItem(parent);

	QString item_name;
	if (ref.track() == -1 ||
		ref.input().keyframe_track_count() == 1 ||
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
										const oak::Input &input, int track_count)
{
	for (int j = 0; j < track_count; j++) {
		create_item(parent, oak::KeyframeTrackRef(input, j));
	}
}

bool NodeTreeView::use_rgba_over_xyzw(const oak::KeyframeTrackRef &ref)
{
	return ref.input().c_type() == OAK_NODE_VALUE_COLOR;
}

void NodeTreeView::item_check_state_changed(QTreeWidgetItem *item, int column)
{
	Q_UNUSED(column)

	switch (item->data(0, k_item_type).toInt()) {
	case k_item_type_node: {
		oak::Node n(QtUtils::value_to_ptr<OakEngineNode>(item->data(0, k_item_node_pointer)));

		if (item->checkState(0) == Qt::Checked) {
			if (disabled_nodes_.contains(n)) {
				disabled_nodes_.removeOne(n);
				emit node_enable_changed(n.handle(), true);
			}
		} else if (!disabled_nodes_.contains(n)) {
			disabled_nodes_.append(n);
			emit node_enable_changed(n.handle(), false);
		}
		break;
	}
	case k_item_type_input: {
		oak::KeyframeTrackRef i = item->data(0, k_item_input_reference)
									   .value<oak::KeyframeTrackRef>();

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
