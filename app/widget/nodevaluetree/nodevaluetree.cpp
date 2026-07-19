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

#include "node/traverser.h"

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

	NodeTraverser traverser;

	Node *connected_node = input.get_connected_output();

	NodeValueTable table =
		traverser.generate_table(connected_node, TimeRange(time, time));

	int index = traverser.generate_row_value_element_index(
		input.node(), input.input(), input.element(), &table);

	for (int i = 0; i < table.count(); i++) {
		const NodeValue &value = table.at(i);
		QTreeWidgetItem *item = new QTreeWidgetItem(this);

		Node::ValueHint hint({ value.type() }, table.count() - 1 - i,
							 value.tag());

		QRadioButton *radio = new QRadioButton(this);
		radio->setProperty("input", QVariant::fromValue(input));
		radio->setProperty("hint", QVariant::fromValue(hint));
		if (i == index) {
			radio->setChecked(true);
		}
		connect(radio, &QRadioButton::clicked, this,
				&NodeValueTree::radio_button_checked);

		setItemWidget(item, 0, radio);
		item->setText(1, NodeValue::get_pretty_data_type_name(value.type()));
		item->setText(2, NodeValue::value_to_string(value, false));
		item->setText(3, value.source()->get_label_and_name());
	}
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

		input.node()->set_value_hint_for_input(input.input(), hint,
										   input.element());
	}
}

}
