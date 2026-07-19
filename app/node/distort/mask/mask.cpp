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

#include "mask.h"

#include "node/filter/blur/blur.h"

namespace olive
{

#define super PolygonGenerator

const QString MaskDistortNode::k_feather_input = QStringLiteral("feather_in");
const QString MaskDistortNode::k_invert_input = QStringLiteral("invert_in");

MaskDistortNode::MaskDistortNode()
{
	// Mask should always be (1.0, 1.0, 1.0) for multiply to work correctly
	set_input_flag(k_color_input, k_input_flag_hidden);

	add_input(k_invert_input, NodeValue::k_boolean, false);

	add_input(k_feather_input, NodeValue::k_float, 0.0);
	set_input_property(k_feather_input, QStringLiteral("min"), 0.0);
}

ShaderCode MaskDistortNode::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == QStringLiteral("mrg")) {
		return ShaderCode(FileFunctions::read_file_as_string(
			QStringLiteral(":/shaders/multiply.frag")));
	} else if (request.id == QStringLiteral("feather")) {
		return ShaderCode(FileFunctions::read_file_as_string(
			QStringLiteral(":/shaders/blur.frag")));
	} else if (request.id == QStringLiteral("invert")) {
		return ShaderCode(FileFunctions::read_file_as_string(
			QStringLiteral(":/shaders/invertrgba.frag")));
	} else {
		return super::get_shader_code(request);
	}
}

void MaskDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_base_input, tr("Texture"));
	set_input_name(k_invert_input, tr("Invert"));
	set_input_name(k_feather_input, tr("Feather"));
}

void MaskDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	TexturePtr texture = value[k_base_input].to_texture();

	VideoParams job_params = texture ? texture->params() : globals.vparams();
	NodeValue job(NodeValue::k_texture,
				  Texture::job(job_params, get_generate_job(value, job_params)),
				  this);

	if (value[k_invert_input].to_bool()) {
		ShaderJob invert;
		invert.set_shader_id(QStringLiteral("invert"));
		invert.insert(QStringLiteral("tex_in"), job);
		job.set_value(Texture::job(job_params, invert));
	}

	if (texture) {
		// Push as merge node
		ShaderJob merge;

		merge.set_shader_id(QStringLiteral("mrg"));
		merge.insert(QStringLiteral("tex_a"), value[k_base_input]);

		if (value[k_feather_input].to_double() > 0.0) {
			// Nest a blur shader in there too
			ShaderJob feather;

			feather.set_shader_id(QStringLiteral("feather"));
			feather.insert(BlurFilterNode::k_texture_input, job);
			feather.insert(BlurFilterNode::k_method_input,
						   NodeValue(NodeValue::k_int,
									 int(BlurFilterNode::k_gaussian), this));
			feather.insert(BlurFilterNode::k_horiz_input,
						   NodeValue(NodeValue::k_boolean, true, this));
			feather.insert(BlurFilterNode::k_vert_input,
						   NodeValue(NodeValue::k_boolean, true, this));
			feather.insert(BlurFilterNode::k_repeat_edge_pixels_input,
						   NodeValue(NodeValue::k_boolean, true, this));
			feather.insert(BlurFilterNode::k_radius_input,
						   NodeValue(NodeValue::k_float,
									 value[k_feather_input].to_double(), this));
			feather.set_iterations(2, BlurFilterNode::k_texture_input);
			feather.insert(QStringLiteral("resolution_in"),
						   NodeValue(NodeValue::k_vec2,
									 texture ? texture->virtual_resolution() :
											   globals.square_resolution(),
									 this));

			merge.insert(QStringLiteral("tex_b"),
						 NodeValue(NodeValue::k_texture,
								   Texture::job(job_params, feather), this));
		} else {
			merge.insert(QStringLiteral("tex_b"), job);
		}

		table->push(NodeValue::k_texture, Texture::job(job_params, merge), this);
	} else {
		table->push(job);
	}
}

}
