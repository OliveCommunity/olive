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

#include "nodetableview.h"

#include <QCheckBox>
#include <QHeaderView>

#include "oakengine/traverse.h"
#include "oakengine/node.h"
#include "node/value.h"

namespace olive
{

NodeTableView::NodeTableView(QWidget *parent)
	: QTreeWidget(parent)
{
	setColumnCount(3);
	setHeaderLabels({ tr("Type"), tr("Source"), tr("R/X"), tr("G/Y"), tr("B/Z"),
					  tr("A/W") });
}

void NodeTableView::select_nodes(const QVector<Node *> &nodes)
{
	foreach (Node *n, nodes) {
		QTreeWidgetItem *top_item = new QTreeWidgetItem();
		top_item->setText(0, n->get_label_and_name());
		top_item->setFirstColumnSpanned(true);
		this->addTopLevelItem(top_item);
		top_level_item_map_.insert(n, top_item);
	}

	set_time(last_time_);
}

void NodeTableView::deselect_nodes(const QVector<Node *> &nodes)
{
	foreach (Node *n, nodes) {
		delete top_level_item_map_.take(n);
	}
}

void NodeTableView::set_time(const Rational &time)
{
	last_time_ = time;

	for (auto i = top_level_item_map_.constBegin();
		 i != top_level_item_map_.constEnd(); i++) {
		Node *node = i.key();
		QTreeWidgetItem *item = i.value();

		OakEngineTraverseDb *db = oakengine_traverse_generate_database(
			reinterpret_cast<OakEngineNode *>(node), time.numerator(),
			time.denominator(), time.numerator(), time.denominator());

		int input_count = oakengine_traverse_db_input_count(db);

		// Delete any children of this item that aren't in this database
		for (int j = 0; j < item->childCount(); j++) {
			QString child_id =
				item->child(j)->data(0, Qt::UserRole).toString();
			bool found = false;
			for (int k = 0; k < input_count; k++) {
				if (child_id ==
					QString::fromUtf8(
						oakengine_traverse_db_input_id(db, k))) {
					found = true;
					break;
				}
			}
			if (!found) {
				delete item->takeChild(j);
				j--;
			}
		}

		// Update all inputs
		for (int l = 0; l < input_count; l++) {
			const char *input_id_c = oakengine_traverse_db_input_id(db, l);
			QString input_id = QString::fromUtf8(input_id_c);

			if (!node->has_input_with_id(input_id)) {
				continue;
			}

			int row_count = oakengine_traverse_db_row_count(db, l);

			QTreeWidgetItem *input_item = nullptr;

			for (int j = 0; j < item->childCount(); j++) {
				QTreeWidgetItem *compare = item->child(j);

				if (compare->data(0, Qt::UserRole).toString() == input_id) {
					input_item = compare;
					break;
				}
			}

			if (!input_item) {
				input_item = new QTreeWidgetItem();
				input_item->setText(0, node->get_input_name(input_id));
				input_item->setData(0, Qt::UserRole, input_id);
				input_item->setFirstColumnSpanned(true);
				item->addChild(input_item);
			}

			// Create children if necessary
			while (input_item->childCount() < row_count) {
				input_item->addChild(new QTreeWidgetItem());
			}

			// Remove children if necessary
			while (input_item->childCount() > row_count) {
				delete input_item->takeChild(
					input_item->childCount() - 1);
			}

			for (int j = 0; j < row_count; j++) {
				int actual_row = row_count - 1 - j;
				QTreeWidgetItem *sub_item = input_item->child(j);

				int type =
					oakengine_traverse_row_type(db, l, actual_row);

				// Set data type name
				char name_buf[64];
				int len = oakengine_node_value_pretty_type_name(type, name_buf, sizeof(name_buf));
				if (len > 0 && len < sizeof(name_buf)) {
					name_buf[len] = '\0';
				} else {
					snprintf(name_buf, sizeof(name_buf), "Type %d", type);
				}
				sub_item->setText(0, QString::fromUtf8(name_buf));

				// Determine source
				OakEngineNode *source =
					oakengine_traverse_row_source(db, l, actual_row);
				QString source_name;
				if (source) {
					char label_buf[256];
					oakengine_node_get_label_and_name(
						source, label_buf, sizeof(label_buf));
					source_name = QString(label_buf);
				} else {
					source_name = tr("(unknown)");
				}
				sub_item->setText(1, source_name);

				switch (type) {
				case NodeValue::k_video_params:
				case NodeValue::k_audio_params:
					// These types have no string representation
					break;
				case NodeValue::k_texture: {
					for (int k = 0; k < 4; k++) {
						this->setItemWidget(sub_item, 2 + k,
											new QCheckBox());
					}
					break;
				}
				default: {
					int split_count =
						oakengine_traverse_row_split_count(db, l,
														   actual_row);
					for (int k = 0; k < split_count; k++) {
						const char *split_str =
							oakengine_traverse_row_split_string(
								db, l, actual_row, k);
						sub_item->setText(2 + k,
										  QString(split_str));
					}
				}
				}
			}
		}

		oakengine_traverse_db_free(db);
	}
}

}
