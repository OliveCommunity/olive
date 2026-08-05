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

#include "merge.h"

#include "filefunctions.h"
#include "traverser.h"

namespace olive
{

const std::string MergeNode::k_base_in = "base_in";
const std::string MergeNode::k_blend_in = "blend_in";

#define super Node

MergeNode::MergeNode()
{
	add_input(k_base_in, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_blend_in, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	set_flag(k_dont_show_in_param_view);
}

std::string MergeNode::name() const
{
	return "Merge";
}

std::string MergeNode::id() const
{
	return "org.olivevideoeditor.Olive.merge";
}

std::vector<Node::CategoryID> MergeNode::category() const
{
	return { k_category_math };
}

std::string MergeNode::description() const
{
	return "Merge two textures together.";
}

void MergeNode::retranslate()
{
	super::retranslate();

	set_input_name(k_base_in, "Base");

	set_input_name(k_blend_in, "Blend");
}

ShaderCode MergeNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/alphaover.frag"));
}

void MergeNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					  NodeValueTable *table) const
{
	TexturePtr base_tex = value.at(k_base_in).to_texture();
	TexturePtr blend_tex = value.at(k_blend_in).to_texture();

	if (base_tex || blend_tex) {
		if (!base_tex || (blend_tex && blend_tex->channel_count() <
										   VideoParams::k_rgba_channel_count)) {
			// We only have a blend texture or the blend texture is RGB only, no need to alpha over
			table->push(value.at(k_blend_in));
		} else if (!blend_tex) {
			// We only have a base texture, no need to alpha over
			table->push(value.at(k_base_in));
		} else {
			table->push(NodeValue::k_texture, base_tex->to_job(ShaderJob(value)),
						this);
		}
	}
}

}
