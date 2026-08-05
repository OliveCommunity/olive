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

#include "despill.h"

#include "project.h"

namespace olive
{

const std::string DespillNode::k_texture_input = "tex_in";
const std::string DespillNode::k_color_input = "color_in";
const std::string DespillNode::k_method_input = "method_in";
const std::string DespillNode::k_preserve_luminance_input =
	"preserve_luminance_input";

#define super Node

DespillNode::DespillNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_color_input, NodeValue::k_combo, 0);

	add_input(k_method_input, NodeValue::k_combo, 0);

	add_input(k_preserve_luminance_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

std::string DespillNode::name() const
{
	return "Despill";
}

std::string DespillNode::id() const
{
	return "org.olivevideoeditor.Olive.despill";
}

std::vector<Node::CategoryID> DespillNode::category() const
{
	return { k_category_keying };
}

std::string DespillNode::description() const
{
	return "Selection of simple despill operations";
}

void DespillNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");

	set_input_name(k_color_input, "Key Color");
	set_combo_box_strings(k_color_input, { "Green", "Blue" });

	set_input_name(k_method_input, "Method");
	set_combo_box_strings(k_method_input, { "Average", "Double Red Average",
									   "Double Average", "Limit" });

	set_input_name(k_preserve_luminance_input, "Preserve Luminance");
}

ShaderCode DespillNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/despill.frag"));
}

void DespillNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						NodeValueTable *table) const
{
	ShaderJob job;
	job.insert(value);

	// Set luma coefficients
	double luma_coeffs[3] = { 0.0f, 0.0f, 0.0f };
	if (project() && project()->color_manager()) {
		project()->color_manager()->get_default_luma_coefs(luma_coeffs);
	} else {
		luma_coeffs[0] = 0.2126;
		luma_coeffs[1] = 0.7152;
		luma_coeffs[2] = 0.0722;
	}
	job.insert("luma_coeffs",
			   NodeValue(NodeValue::k_vec3,
						 Vector3D(luma_coeffs[0], luma_coeffs[1], luma_coeffs[2])));

	// If there's no texture, no need to run an operation
	if (TexturePtr tex = job.get(k_texture_input).to_texture()) {
		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

} // namespace olive
