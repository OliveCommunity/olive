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

#include "nodevaluetree.h"

#include <QEvent>

#include "common/nodevaluehandle.h"
#include "oakengine/traverse.h"
#include "oakengine/node.h"
#include "node/value.h"

namespace olive
{

#define super QTreeWidget

NodeValueTree::NodeValueTree(QWidget *parent)
	: super(parent)
{
	setColumnWidth(0, 0);
	setColumnCount(4);

	QSizePolicy p = sizePolicy();
	p.setHorizontalStretch(1);
	setSizePolicy(p);

	static const int k_minimum_rows = 10;
	setMinimumHeight(fontMetrics().height() * k_minimum_rows);

	retranslate();
}

void NodeValueTree::set_node(const NodeInput &input, const Rational &time)
{
	clear();

	Node *connected_node = input.get_connected_output();

	OakEngineTraverseDb *table_db = oakengine_traverse_generate_table(
		reinterpret_cast<OakEngineNode *>(connected_node),
		time.numerator(), time.denominator(), time.numerator(),
		time.denominator());

	int db_index = 0;
	int row_count = oakengine_traverse_db_row_count(table_db, db_index);

	int index = oakengine_traverse_table_element_index_for_hint(
		reinterpret_cast<OakEngineNode *>(input.node()),
		input.input().toUtf8().constData(), input.element(), table_db);

	for (int i = 0; i < row_count; i++) {
		QTreeWidgetItem *item = new QTreeWidgetItem(this);

		int type = oakengine_traverse_row_type(table_db, db_index, i);
		OakEngineNode *source = oakengine_traverse_row_source(table_db, db_index,
															 i);
		const char *tag = oakengine_traverse_row_tag(table_db, db_index, i);

		Node::ValueHint hint({ static_cast<NodeValue::Type>(type) },
							 row_count - 1 - i, QString(tag));

		QRadioButton *radio = new QRadioButton(this);
		radio->setProperty("input", QVariant::fromValue(input));
		radio->setProperty("hint", QVariant::fromValue(hint));
		if (i == index) {
			radio->setChecked(true);
		}
		connect(radio, &QRadioButton::clicked, this,
				&NodeValueTree::radio_button_checked);

		setItemWidget(item, 0, radio);
		char name_buf[64];
		int len = oakengine_node_value_pretty_type_name(type, name_buf, sizeof(name_buf));
		if (len > 0 && len < sizeof(name_buf)) {
			name_buf[len] = '\0';
		} else {
			snprintf(name_buf, sizeof(name_buf), "Type %d", type);
		}
		item->setText(1, QString::fromUtf8(name_buf));

		const char *vs = oakengine_traverse_row_value_string(table_db, db_index, i);
		item->setText(2, vs ? QString(vs) : QString());

		if (source) {
			char label_buf[256];
			oakengine_node_get_label_and_name(source, label_buf,
											  sizeof(label_buf));
			item->setText(3, QString(label_buf));
		} else {
			item->setText(3, QString());
		}
	}

	oakengine_traverse_db_free(table_db);
}

void NodeValueTree::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		retranslate();
	}

	super::changeEvent(event);
}

void NodeValueTree::retranslate()
{
	setHeaderLabels({ QString(), tr("Type"), tr("Value"), tr("Source") });
}

void NodeValueTree::radio_button_checked(bool e)
{
	if (e) {
		QRadioButton *btn = static_cast<QRadioButton *>(sender());
		Node::ValueHint hint = btn->property("hint").value<Node::ValueHint>();
		NodeInput input = btn->property("input").value<NodeInput>();

		// Map the full hint through the facade: type (single, or -1 to keep
		// the input's declared type), index and tag must not be dropped.
		int c_type = -1;
		if (!hint.types().isEmpty()) {
			c_type = node_value_type_to_c(hint.types().first());
		}
		oakengine_node_set_value_hint(
			reinterpret_cast<OakEngineNode*>(input.node()),
			input.input().toUtf8().constData(), input.element(),
			c_type, hint.index(),
			hint.tag().isEmpty() ? nullptr : hint.tag().toUtf8().constData());
	}
}

}
