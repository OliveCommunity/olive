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

#include "noise.h"

#include "sliderdisplaytype.h"

namespace olive
{

const std::string NoiseGeneratorNode::k_base_in = "base_in";
const std::string NoiseGeneratorNode::k_color_input = "color_in";
const std::string NoiseGeneratorNode::k_strength_input = "strength_in";

#define super Node

NoiseGeneratorNode::NoiseGeneratorNode()
{
	add_input(k_base_in, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_strength_input, NodeValue::k_float, 0.2);
	set_input_property(k_strength_input, "view", slider::k_percentage);
	set_input_property(k_strength_input, "min", 0);

	add_input(k_color_input, NodeValue::k_boolean, false);

	set_effect_input(k_base_in);
	set_flag(k_video_effect);
}

std::string NoiseGeneratorNode::name() const
{
	return "Noise";
}

std::string NoiseGeneratorNode::id() const
{
	return "org.olivevideoeditor.Olive.noise";
}

std::vector<Node::CategoryID> NoiseGeneratorNode::category() const
{
	return { k_category_generator };
}

std::string NoiseGeneratorNode::description() const
{
	return "Generates noise patterns";
}

void NoiseGeneratorNode::retranslate()
{
	super::retranslate();

	set_input_name(k_base_in, "Base");
	set_input_name(k_strength_input, "Strength");
	set_input_name(k_color_input, "Color");
}

ShaderCode NoiseGeneratorNode::get_shader_code(const ShaderRequest &request) const
{
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/noise.frag"));
}

void NoiseGeneratorNode::value(const NodeValueRow &value,
							   const NodeGlobals &globals,
							   NodeValueTable *table) const
{
	ShaderJob job(value);

	job.insert(value);
	job.insert("time_in",
			   NodeValue(NodeValue::k_float, globals.time().in().to_double(),
						 this));

	TexturePtr base = value.at(k_base_in).to_texture();

	table->push(NodeValue::k_texture,
				Texture::job(base ? base->params() : globals.vparams(), job),
				this);
}
}
