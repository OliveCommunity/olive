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
#include "common/colortransform.h"
#include "node/colormanager.h"
#include "render/color.h"

#include "color/colormanager/colormanager.h"

namespace olive
{

const std::string DisplayTransformNode::k_display_input = "display_in";
const std::string DisplayTransformNode::k_view_input = "view_in";
const std::string DisplayTransformNode::k_direction_input = "dir_in";

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

std::string DisplayTransformNode::name() const
{
	return "Display Transform";
}

std::string DisplayTransformNode::id() const
{
	return "org.olivevideoeditor.Olive.displaytransform";
}

std::vector<Node::CategoryID> DisplayTransformNode::category() const
{
	return { k_category_color };
}

std::string DisplayTransformNode::description() const
{
	return "Converts an image to or from a display color space.";
}

void DisplayTransformNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_display_input, "Display");
	set_input_name(k_view_input, "View");
	set_input_name(k_direction_input, "Direction");
	set_combo_box_strings(k_direction_input, { "Forward", "Inverse" });
}

void DisplayTransformNode::InputValueChangedEvent(const std::string &input,
												  int element)
{
	(void) element;
	if (input == k_display_input || input == k_direction_input ||
		input == k_view_input) {
		if (input == k_display_input) {
			update_views();
		}
		generate_processor();
	}
}

std::string DisplayTransformNode::get_display() const
{
	if (manager()) {
		int index = get_standard_value(k_display_input).to_int();
		if (index < int(manager()->list_available_displays().size())) {
			return manager()->list_available_displays().at(index);
		}
	}
	return std::string();
}

std::string DisplayTransformNode::get_view() const
{
	if (manager()) {
		std::string display = get_display();
		if (!display.empty()) {
			int index = get_standard_value(k_view_input).to_int();
			StringList views = manager()->list_available_views(display);
			if (index < int(views.size())) {
				return views.at(index);
			}
		}
	}
	return std::string();
}

ColorProcessor::Direction DisplayTransformNode::get_direction() const
{
	return static_cast<ColorProcessor::Direction>(
		get_standard_value(k_direction_input).to_int());
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
		OakNodeColorManager mgr =
			oaknode_colormanager_wrap_borrowed(manager());
		OakColorTransform transform = oakcommon_colortransform_init_display(
			get_display().c_str(), get_view().c_str(), "");

		char ref_space[256];
		int needed = oaknode_colormanager_get_reference_color_space(
			mgr, ref_space, sizeof(ref_space));
		if (needed > 0 && needed <= int(sizeof(ref_space))) {
			set_processor(oakrender_color_processor_create_transform(
				mgr, ref_space, transform,
				get_direction() == ColorProcessor::k_normal ?
					OAKRENDER_COLOR_DIRECTION_NORMAL :
					OAKRENDER_COLOR_DIRECTION_INVERSE));
		}

		oakcommon_colortransform_free(&transform);
		mgr.release(mgr.ctx);
	}
}

}
