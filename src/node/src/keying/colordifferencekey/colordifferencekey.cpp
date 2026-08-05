/***
  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team
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

#include "colordifferencekey.h"

namespace olive
{

const std::string ColorDifferenceKeyNode::k_texture_input = "tex_in";
const std::string ColorDifferenceKeyNode::k_garbage_matte_input = "garbage_in";
const std::string ColorDifferenceKeyNode::k_core_matte_input = "core_in";
const std::string ColorDifferenceKeyNode::k_color_input = "color_in";
const std::string ColorDifferenceKeyNode::k_shadows_input = "shadows_in";
const std::string ColorDifferenceKeyNode::k_highlights_input = "highlights_in";
const std::string ColorDifferenceKeyNode::k_mask_only_input = "mask_only_in";

#define super Node

ColorDifferenceKeyNode::ColorDifferenceKeyNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_garbage_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_core_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_color_input, NodeValue::k_combo, 0);

	add_input(k_highlights_input, NodeValue::k_float, 1.0f);
	set_input_property(k_highlights_input, "min", 0.0);
	set_input_property(k_highlights_input, "base", 0.01);

	add_input(k_shadows_input, NodeValue::k_float, 1.0f);
	set_input_property(k_shadows_input, "min", 0.0);
	set_input_property(k_shadows_input, "base", 0.01);

	add_input(k_mask_only_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

std::string ColorDifferenceKeyNode::name() const
{
	return "Color Difference Key";
}

std::string ColorDifferenceKeyNode::id() const
{
	return "org.olivevideoeditor.Olive.colordifferencekey";
}

std::vector<Node::CategoryID> ColorDifferenceKeyNode::category() const
{
	return { k_category_keying };
}

std::string ColorDifferenceKeyNode::description() const
{
	return "A simple color key based on the distance of one color from other colors.";
}

void ColorDifferenceKeyNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_garbage_matte_input, "Garbage Matte");
	set_input_name(k_core_matte_input, "Core Matte");
	set_input_name(k_color_input, "Key Color");
	set_combo_box_strings(k_color_input, { "Green", "Blue" });
	set_input_name(k_shadows_input, "Shadows");
	set_input_name(k_highlights_input, "Highlights");
	set_input_name(k_mask_only_input, "Show Mask Only");
}

ShaderCode
ColorDifferenceKeyNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/colordifferencekey.frag"));
}

void ColorDifferenceKeyNode::value(const NodeValueRow &value,
								   const NodeGlobals &globals,
								   NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		ShaderJob job;
		job.insert(value);
		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

} // namespace olive
