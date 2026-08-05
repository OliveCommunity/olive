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

#include "mosaicfilternode.h"

#include "filefunctions.h"

namespace olive
{

const std::string MosaicFilterNode::k_texture_input = "tex_in";
const std::string MosaicFilterNode::k_horiz_input = "horiz_in";
const std::string MosaicFilterNode::k_vert_input = "vert_in";

#define super Node

MosaicFilterNode::MosaicFilterNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_horiz_input, NodeValue::k_float, 32.0);
	set_input_property(k_horiz_input, "min", 1.0);

	add_input(k_vert_input, NodeValue::k_float, 18.0);
	set_input_property(k_vert_input, "min", 1.0);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

void MosaicFilterNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Texture");
	set_input_name(k_horiz_input, "Horizontal");
	set_input_name(k_vert_input, "Vertical");
}

void MosaicFilterNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	if (TexturePtr texture = value.at(k_texture_input).to_texture()) {
		if (texture && (value.at(k_horiz_input).to_int() != texture->width() ||
						value.at(k_vert_input).to_int() != texture->height())) {
			ShaderJob job(value);

			// Mipmapping makes this look weird, so we just use bilinear for finding the color of each block
			job.set_interpolation(k_texture_input, Texture::k_linear);

			table->push(NodeValue::k_texture, texture->to_job(job), this);
		} else {
			table->push(value.at(k_texture_input));
		}
	}
}

ShaderCode MosaicFilterNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/mosaic.frag"));
}

}
