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

#include "flipdistortnode.h"

namespace olive
{

const std::string FlipDistortNode::k_texture_input = "tex_in";
const std::string FlipDistortNode::k_horizontal_input = "horiz_in";
const std::string FlipDistortNode::k_vertical_input = "vert_in";

#define super Node

FlipDistortNode::FlipDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_horizontal_input, NodeValue::k_boolean, false);

	add_input(k_vertical_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

std::string FlipDistortNode::name() const
{
	return "Flip";
}

std::string FlipDistortNode::id() const
{
	return "org.olivevideoeditor.Olive.flip";
}

std::vector<Node::CategoryID> FlipDistortNode::category() const
{
	return { k_category_distort };
}

std::string FlipDistortNode::description() const
{
	return "Flips an image horizontally or vertically";
}

void FlipDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_horizontal_input, "Horizontal");
	set_input_name(k_vertical_input, "Vertical");
}

ShaderCode FlipDistortNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/flip.frag"));
}

void FlipDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		if (value.at(k_horizontal_input).to_bool() ||
			value.at(k_vertical_input).to_bool()) {
			table->push(NodeValue::k_texture, tex->to_job(ShaderJob(value)),
						this);
		} else {
			// If we're not flipping or flopping just push the texture
			table->push(value.at(k_texture_input));
		}
	}
}

}
