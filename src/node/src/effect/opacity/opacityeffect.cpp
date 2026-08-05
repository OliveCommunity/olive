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

#include "opacityeffect.h"

#include <algorithm>
#include <cmath>

#include "math/math/math.h"
#include "sliderdisplaytype.h"

#include "filefunctions.h"

namespace olive
{

#define super Node

const std::string OpacityEffect::k_texture_input = "tex_in";
const std::string OpacityEffect::k_value_input = "opacity_in";

OpacityEffect::OpacityEffect()
	: math_(new MathNode())
{
	math_->set_operation(MathNode::k_op_multiply);

	set_node_position_in_context(math_.get(), PointF(0, 0));

	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_value_input, NodeValue::k_float, 1.0);
	set_input_property(k_value_input, "view",
					 slider::k_percentage);
	set_input_property(k_value_input, "min", 0.0);
	set_input_property(k_value_input, "max", 1.0);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

void OpacityEffect::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Texture");
	set_input_name(k_value_input, "Opacity");
}

ShaderCode OpacityEffect::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == "rgbmult") {
		return ShaderCode(
			FileFunctions::read_file_as_string(":/shaders/opacity_rgb.frag"));
	} else {
		return ShaderCode(
			FileFunctions::read_file_as_string(":/shaders/opacity.frag"));
	}
}

void OpacityEffect::value(const NodeValueRow &value, const NodeGlobals &globals,
						  NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		if (TexturePtr opacity_tex = value.at(k_value_input).to_texture()) {
			ShaderJob job(value);
			job.set_shader_id("rgbmult");
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else if (std::abs(value.at(k_value_input).to_double() - 1.0) *
					   1000000000000.0 >
				   std::min(std::abs(value.at(k_value_input).to_double()),
							1.0)) {
			table->push(NodeValue::k_texture, tex->to_job(ShaderJob(value)),
						this);
		} else {
			// 1.0 float is a no-op, so just push the texture
			table->push(value.at(k_texture_input));
		}
	}
}

}
