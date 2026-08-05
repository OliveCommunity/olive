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

#include "stroke.h"

#include "filefunctions.h"
#include "sliderdisplaytype.h"

namespace olive
{

const std::string StrokeFilterNode::k_texture_input = "tex_in";
const std::string StrokeFilterNode::k_color_input = "color_in";
const std::string StrokeFilterNode::k_radius_input = "radius_in";
const std::string StrokeFilterNode::k_opacity_input = "opacity_in";
const std::string StrokeFilterNode::k_inner_input = "inner_in";

#define super Node

StrokeFilterNode::StrokeFilterNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_color_input, NodeValue::k_color,
			 Variant::from_value(Color(1.0f, 1.0f, 1.0f, 1.0f)));

	add_input(k_radius_input, NodeValue::k_float, 10.0);
	set_input_property(k_radius_input, "min", 0.0);

	add_input(k_opacity_input, NodeValue::k_float, 1.0f);
	set_input_property(k_opacity_input, "view", slider::k_percentage);
	set_input_property(k_opacity_input, "min", 0.0f);
	set_input_property(k_opacity_input, "max", 1.0f);

	add_input(k_inner_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

std::string StrokeFilterNode::name() const
{
	return "Stroke";
}

std::string StrokeFilterNode::id() const
{
	return "org.olivevideoeditor.Olive.stroke";
}

std::vector<Node::CategoryID> StrokeFilterNode::category() const
{
	return { k_category_filter };
}

std::string StrokeFilterNode::description() const
{
	return "Creates a stroke outline around an image.";
}

void StrokeFilterNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_color_input, "Color");
	set_input_name(k_radius_input, "Radius");
	set_input_name(k_opacity_input, "Opacity");
	set_input_name(k_inner_input, "Inner");
}

void StrokeFilterNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		if (value.at(k_radius_input).to_double() > 0.0 &&
			value.at(k_opacity_input).to_double() > 0.0) {
			ShaderJob job(value);
			job.insert("resolution_in",
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			table->push(value.at(k_texture_input));
		}
	}
}

ShaderCode StrokeFilterNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/stroke.frag"));
}

}
