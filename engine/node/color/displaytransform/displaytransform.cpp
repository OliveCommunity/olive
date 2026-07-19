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

#include "displaytransform.h"

#include "node/color/colormanager/colormanager.h"

namespace olive
{

const QString DisplayTransformNode::k_display_input =
	QStringLiteral("display_in");
const QString DisplayTransformNode::k_view_input = QStringLiteral("view_in");
const QString DisplayTransformNode::k_direction_input = QStringLiteral("dir_in");

#define super OCIOBaseNode

DisplayTransformNode::DisplayTransformNode()
{
	add_input(k_display_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));

	add_input(k_view_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));

	add_input(k_direction_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));
}

QString DisplayTransformNode::name() const
{
	return tr("Display Transform");
}

QString DisplayTransformNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.displaytransform");
}

QVector<Node::CategoryID> DisplayTransformNode::category() const
{
	return { k_category_color };
}

QString DisplayTransformNode::description() const
{
	return tr("Converts an image to or from a display color space.");
}

void DisplayTransformNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_display_input, tr("Display"));
	set_input_name(k_view_input, tr("View"));
	set_input_name(k_direction_input, tr("Direction"));
	set_combo_box_strings(k_direction_input, { tr("Forward"), tr("Inverse") });
}

void DisplayTransformNode::InputValueChangedEvent(const QString &input,
												  int element)
{
	Q_UNUSED(element);
	if (input == k_display_input || input == k_direction_input ||
		input == k_view_input) {
		if (input == k_display_input) {
			update_views();
		}
		generate_processor();
	}
}

QString DisplayTransformNode::get_display() const
{
	if (manager()) {
		int index = get_standard_value(k_display_input).toInt();
		if (index < manager()->list_available_displays().size()) {
			return manager()->list_available_displays().at(index);
		}
	}
	return QString();
}

QString DisplayTransformNode::get_view() const
{
	if (manager()) {
		QString display = get_display();
		if (!display.isEmpty()) {
			int index = get_standard_value(k_view_input).toInt();
			QStringList views = manager()->list_available_views(display);
			if (index < views.size()) {
				return views.at(index);
			}
		}
	}
	return QString();
}

ColorProcessor::Direction DisplayTransformNode::get_direction() const
{
	return static_cast<ColorProcessor::Direction>(
		get_standard_value(k_direction_input).toInt());
	;
}

void DisplayTransformNode::update_displays()
{
	if (manager()) {
		set_combo_box_strings(k_display_input, manager()->list_available_displays());
	}
}

void DisplayTransformNode::update_views()
{
	if (manager()) {
		set_combo_box_strings(k_view_input,
						   manager()->list_available_views(get_display()));
	}
}

void DisplayTransformNode::config_changed()
{
	update_displays();
	update_views();
	generate_processor();
}

void DisplayTransformNode::generate_processor()
{
	if (manager()) {
		ColorTransform transform(get_display(), get_view(), QString());
		set_processor(ColorProcessor::create(
			manager(), manager()->get_reference_color_space(), transform,
			get_direction()));
	}
}

}
